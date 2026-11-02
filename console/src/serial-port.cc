#include "serial-port.hh"
#include <qdebug.h>
#include <utility>

namespace app
{
    Serial::Serial(QObject *parent):QObject(parent)
    {
        port_ = std::make_unique<QSerialPort>(this);
        QObject::connect(port_.get(), &QSerialPort::errorOccurred, this, &Serial::on_err);
    }

    Serial::~Serial() = default;

    void Serial::on_err(QSerialPort::SerialPortError error)
    {
        /*写完会自动发这个信号所以在第一行直接屏蔽 */
        if(error == QSerialPort::NoError)
            return;

        qWarning().noquote() << "[serial]" << cfg_.port_name
                             << "error" << error << port_->errorString();

        if(error == QSerialPort::ResourceError)
        {
            //设备被拔掉或句柄失效，这个端口对象已经不能再用，自己决定是否重新 open
            port_->close();
            qWarning().noquote() << "[serial]" << cfg_.port_name << "gone, closed";
            emit port_lost();      //是resource错误导致的掉线
            emit disconnected();
        }
    }

    bool Serial::write(const QByteArray &buf)
    {
        if(!port_->isOpen())
        {
            qWarning().noquote() << "[serial] not open, drop" << buf.size() << "bytes";
            return false;
        }
        if(buf.isEmpty())
            return true;

        //write() 是往 QSerialPort 的写缓冲里排队，返回实际收下的字节数；满了会小于 buf.size()
        const qint64 n = port_->write(buf);
        if(n != buf.size())
        {
            qWarning().noquote() << "[serial] short write" << n << "/" << buf.size()
                                 << port_->errorString();
            return false;  
        }
        if(open_pending() == true)
        {
            if(cfg_.writen_pending_max > 0
               && writen_pending_.size() >=
               static_cast<std::size_t>(cfg_.writen_pending_max))
                writen_pending_.pop();

            writen_pending_.push(buf);
        }
        return true;
    }

    bool Serial::read(QByteArray &buf ,qint64 &length)
    {
        if(!port_->isOpen())
        {
            qWarning().noquote() << "[serial] not open, read failed";
            length = 0;
            return false;
        }

        //readAll() 取走缓冲区里已经到达的全部字节，没数据时是空数组，不算错误
        buf = port_->readAll();
        length = buf.size();

        if(length == 0)
            return true;

        if(open_pending() == true)
        {
            if(cfg_.recv_pending_max > 0
               && recv_pending_.size() >= 
               static_cast<std::size_t>(cfg_.recv_pending_max))
                recv_pending_.pop();

            recv_pending_.push(buf);
        }
        return true;
    }
    
    constexpr bool Serial::open_pending() const
    {
        return cfg_.open_pending;
    }

    bool Serial::open(Config &cfg)
    {
        if(port_->isOpen())
        {
            qWarning().noquote() 
                << "[serial] already open" << port_->portName();
            return false;
        }
        if(cfg.port_name.isEmpty())
        {
            qWarning().noquote() << "[serial] empty port name";
            return false;
        }

        cfg_ = cfg;

        //新会话不保留上一轮的积压
        while(!recv_pending_.empty())
            recv_pending_.pop();
        while(!writen_pending_.empty())
            writen_pending_.pop();

        port_->setPortName(cfg_.port_name);
        if(!port_->open(QIODevice::ReadWrite))
        {
            qWarning().noquote()    << "[serial] open failed" 
                                    << cfg_.port_name
                                    << port_->errorString();
            return false;
        }

        if(!port_->setBaudRate(cfg_.baud)
           || !port_->setParity(cfg_.parity)
           || !port_->setDataBits(cfg_.data_bits)
           || !port_->setStopBits(cfg_.stop_bits)
           || !port_->setFlowControl(cfg_.flow_control))
        {
            qWarning().noquote() << "[serial] apply config failed" << port_->errorString();
            port_->close();
            return false;
        }

        qInfo().noquote() << "[serial] opened" << cfg_.port_name
                          << "baud" << cfg_.baud
                          << "data" << cfg_.data_bits
                          << "parity" << cfg_.parity
                          << "stop" << cfg_.stop_bits;
        emit connected();
        return true;
    }

    bool Serial::stop()
    {
        if(!port_->isOpen())
        {
            qInfo().noquote() << "[serial] already closed" << cfg_.port_name;
            return true;
        }

        //先把写缓冲里的数据发完再关，flush 会阻塞到清空，串口慢时留意
        if(!port_->flush())
            qWarning().noquote() << "[serial] flush failed" << port_->errorString();

        port_->clear(QSerialPort::Input);//丢掉没读完的接收数据，避免下次打开读到旧内容
        port_->close();

        //停止后不保留积压，下一次 open() 从空队列开始
        while(!recv_pending_.empty())
            recv_pending_.pop();
        while(!writen_pending_.empty())
            writen_pending_.pop();

        qInfo().noquote() << "[serial] closed" << cfg_.port_name;
        emit disconnected();
        return true;
    }

    bool Serial::set_baud_rate(qint32 baud)
    {
        cfg_.baud = baud;

        if(!port_->isOpen())
        {
            qInfo().noquote() << "[serial] baud set to" << baud << "(apply on next open)";
            return true;//下次 open() 一起设
        }

        if(!port_->setBaudRate(cfg_.baud, cfg_.direction))
        {
            qWarning().noquote() << "[serial] set baud failed" << baud << port_->errorString();
            return false;
        }

        qInfo().noquote() << "[serial] baud changed to" << baud;
        return true;
    }

    std::size_t Serial::recv_size() const
    {
        return recv_pending_.size();
    }

    const QByteArray &Serial::peek_recv() const
    {
        static const QByteArray kEmpty;
        if(recv_pending_.empty())
            return kEmpty;
        return recv_pending_.front();
    }

    QByteArray Serial::take_recv()
    {
        if(recv_pending_.empty())
            return QByteArray();
        auto out = std::move(recv_pending_.front());//转移缓冲区
        recv_pending_.pop();
        return out;
    }

    std::size_t Serial::writen_size() const
    {
        return writen_pending_.size();
    }

    const QByteArray &Serial::peek_writen() const
    {
        static const QByteArray kEmpty;
        if(writen_pending_.empty())
            return kEmpty;
        return writen_pending_.front();
    }

    QByteArray Serial::take_writen()
    {
        if(writen_pending_.empty())
            return QByteArray();
        auto out = std::move(writen_pending_.front());//转移缓冲区，不拷贝
        writen_pending_.pop();
        return out;
    }
}
