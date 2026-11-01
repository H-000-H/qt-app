#include "udp-client.hh"
#include "net-error.hh"
#include <cstddef>
#include <memory>
#include <qabstractsocket.h>
#include <utility>
#include <qdatetime.h>
#include <qeventloop.h>
#include <qnetworkdatagram.h>
#include <qobject.h>
#include <QDateTime>
#include <qtimer.h>
#include <qudpsocket.h>
namespace app
{

    /** @brief IPv4 下单个 UDP 报文的最大负载，再大内核和协议都是直接拒绝的 */
    constexpr qsizetype kMaxDatagramSize = 65507;

    UdpClient::UdpClient(QObject *parent):QObject(parent)
    {
        client_             = std::make_unique<QUdpSocket>(this);
        host_               = std::make_unique<QHostAddress>();
        init_reconnect_timer(this);//重连定时器由基类创建，到点回调 do_reconnect()
        init_timeout_timer(this);//建链超时定时器由基类创建，到点回调 on_timeout()
        wire_client();
    }

    UdpClient::~UdpClient()
    {
        if(!client_)
            return;
        QObject::disconnect(client_.get(),nullptr,this,nullptr);
        if(client_->state() != QAbstractSocket::UnconnectedState)
            client_->abort();
    }

    bool UdpClient::start(const Config &config)
    {
        if(started_)
        {
            qWarning().noquote() << "[udp] already started, do not start again";
            return false;
        }

        cfg_ = config;
        stop_down_ = false;
        recv_buffer_applied_ = false;

        if(cfg_.host.isEmpty() || cfg_.port == 0)
        {
            qWarning().noquote() << "[udp] invalid endpoint" << cfg_.host << cfg_.port;
            return false;
        }

        while(!pending_.empty())
            pending_.pop();//新会话不保留上一轮积压

        if(cfg_.auto_allocate == false)
        {
            //指定本地地址：解析失败直接放弃
            if(!host_->setAddress(cfg_.local_host))
            {
                qWarning().noquote() << "[udp] invalid local_host" << cfg_.local_host;
                return false;
            }
            if(!client_->bind(*host_, cfg_.local_port, cfg_.bind_mode))
            {
                qWarning().noquote()    << "[udp] bind failed"
                                        << cfg_.local_host
                                        << cfg_.local_port
                                        << client_->errorString();
                return false;
            }
            qInfo().noquote()   << "[udp] bound"<< cfg_.local_host
                                << "local port"<< client_->localPort();
        }

        reset_retry();
        //先置位：connectToHost 可能同步失败并立刻回到 Unconnected，
        started_ = true;
        client_->connectToHost(cfg_.host, cfg_.port);
        start_timeout(cfg_.connect_timeout);
        return true;
    }

    bool UdpClient::send(const QByteArray &payload)
    {
        if(payload.isEmpty())
        {
            qWarning().noquote() << "[udp] empty payload, ignore";
            return false;
        }

        if(client_->state() != QAbstractSocket::ConnectedState)
        {
            qWarning().noquote() << "[udp] not connected, drop datagram, size =" << payload.size();
            return false;
        }

        //单个 UDP 报文上限(IPv4)，再大内核直接拒绝
        if(payload.size() > kMaxDatagramSize)
        {
            qWarning().noquote() << "[udp] datagram too large" << payload.size();
            return false;
        }

        //超过告警值不拦截：只是提醒会走 IP 分片，丢一片整包就废
        if(cfg_.datagram_warn_size > 0 && payload.size() > cfg_.datagram_warn_size)
        {
            qWarning().noquote() << "[udp] datagram" << payload.size()
                                 << "bytes exceeds warn size" << cfg_.datagram_warn_size
                                 << "(IP will fragment)";
        }

        if(client_->write(payload) != payload.size())
        {
            qWarning().noquote() << "[udp] write failed" << client_->errorString();
            return false;
        }
        return true;
    }

    void UdpClient::apply_recv_buffer()
    {
        if(recv_buffer_applied_ || cfg_.recv_buffer_size <= 0)
            return;

        //SO_RCVBUF 只能在 socket 创建之后设置：bind 之前设会被忽略，这里在 Bound/Connected 后调
        client_->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                                 cfg_.recv_buffer_size);
        const int actual = client_->socketOption(QAbstractSocket::ReceiveBufferSizeSocketOption).toInt();
        recv_buffer_applied_ = true;

        //实际值由内核决定（会翻倍并受 rmem_max 限制），这里只记录，不判断是否被截断
        qInfo().noquote() << "[udp] recv buffer requested" << cfg_.recv_buffer_size
                          << "actual" << actual;
    }

    std::size_t UdpClient::pending_size() const
    {
        return pending_.size();
    }

    const QByteArray &UdpClient::peek_pending() const
    {
        static const QByteArray kEmpty;
        if(pending_.empty())
            return kEmpty;
        return pending_.front();
    }

    QByteArray UdpClient::take_pending()
    {
        if(pending_.empty())
            return QByteArray();
        auto out = std::move(pending_.front());//转移缓冲区，不拷贝
        pending_.pop();
        return out;
    }

    void UdpClient::wire_client()
    {
        QObject::connect(client_.get(),
                        &QUdpSocket::stateChanged,
                        this,
                        &UdpClient::on_state_changed);

        QObject::connect(client_.get(),
                        &QUdpSocket::errorOccurred,
                        this,
                        &UdpClient::on_err);
        QObject::connect(client_.get(),
                        &QUdpSocket::readyRead,
                        this,
                        &UdpClient::on_read);
    }

    bool UdpClient::stop()
    {
        if(stop_down_ == true)
        {
            qInfo().noquote() << "[udp] already stopped";
            return true;
        }

        stop_down_ = true;
        started_   = false;//先置位，掉线回调里就不会再排重连
        cancel_reconnect();
        stop_timeout();//可能还在建链，防止建链超时和挥手超时打
        //清理pending_ started清理这里也清理以防万一
        while (!pending_.empty())
            pending_.pop();
        if(client_->state() == QAbstractSocket::UnconnectedState)
        {
            emit disconnected_permanently(CloseReason::UserStop);//本来就没连，状态机不会再来一次
            return true;
        }
        client_->disconnectFromHost();
        return true;
    }

    bool UdpClient::stop_and_wait(int timeout_ms)
    {
        if(client_->state() == QAbstractSocket::SocketState::UnconnectedState)
        {
            stop();//可能还排着一次重连，走 stop 取消掉
            return true;
        }

        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(this, &UdpClient::disconnected, &loop, &QEventLoop::quit);
        stop();
        if(client_->state() == QAbstractSocket::SocketState::UnconnectedState)
            return true;//disconnectFromHost 可能同步就完成了
        guard.start(timeout_ms);
        loop.exec();
        return client_->state() == QAbstractSocket::SocketState::UnconnectedState;
    }

    void UdpClient::on_err(QAbstractSocket::SocketError err)
    {
        if (err == QAbstractSocket::RemoteHostClosedError)
            return;
        const QString msg = QStringLiteral("[udp] %1 err_reason :%2 (%3)")
                                .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                                .arg(detail::socket_error_name(err))
                                .arg(client_->errorString());
        qWarning().noquote() << msg;

        emit err_occurred(msg);
    }
    void UdpClient::do_reconnect()
    {
        qInfo().noquote() << "[udp] reconnecting...";
        //abort 后重连会创建新的 socket 引擎，SO_RCVBUF 会回到默认值，允许重新设置
        recv_buffer_applied_ = false;
        client_->connectToHost(cfg_.host, cfg_.port);
        start_timeout(cfg_.connect_timeout);
    }

    void UdpClient::on_timeout()
    {
        //UDP 没有握手，卡在解析/连接就直接 abort，回到 Unconnected 走统一重连
        client_->abort();
    }

    void UdpClient::on_state_changed(QAbstractSocket::SocketState state)
    {
        switch (state)
        {
        case QAbstractSocket::SocketState::UnconnectedState:
        {
            stop_timeout();
            qInfo().noquote() << QStringLiteral("[udp] close %1")
                                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
            emit disconnected();

            if(stop_down_)//主动 stop，不再重连
            {
                emit disconnected_permanently(CloseReason::UserStop);
                break;
            }
            if(!started_) break;//没在运行，不重连也不报终态

            //没开自动重连，或重连次数已用尽：不会再有下一次
            if(!cfg_.auto_reconnect
               || NetBase::give_up(retry_, cfg_.max_retry, cfg_.reconnect_infinite))
            {
                started_ = false;//彻底停了，允许上层重新 start()
                emit disconnected_permanently(CloseReason::NetworkLost);
                break;
            }
            schedule_reconnect(cfg_.max_retry, cfg_.reconnect_infinite, cfg_.reconnect_interval);
            break;
        }
        case QAbstractSocket::SocketState::HostLookupState:
        {
            qInfo().noquote() << QStringLiteral("[udp] host lookup ... %1")
                                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
            break;
        }
        case QAbstractSocket::SocketState::BoundState:
        {
            //local_port 填 0 时真实端口只有这里能拿到纯粹的接收端走这里
            qInfo().noquote() << QStringLiteral("[udp] bound %1:%2")
                                    .arg(client_->localAddress().toString())
                                    .arg(client_->localPort());
            apply_recv_buffer();
            break;
        }
        case QAbstractSocket::SocketState::ConnectingState:
        {
            qInfo().noquote() << QStringLiteral("[udp] connecting ... %1")
                                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
            break;
        }
        case QAbstractSocket::SocketState::ConnectedState:
        {
            reset_retry();
            stop_timeout();
            apply_recv_buffer();
            emit connected();
            break;
        }
        case QAbstractSocket::SocketState::ClosingState:
        {
            qInfo().noquote() << QStringLiteral("[udp] closing ... %1")
                                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
            break;
        }
        default:
            break;
        }
    }

    void UdpClient::on_read()
    {
        //一次 readyRead 可能对应多个数据报，须循环取空；
        while(client_->hasPendingDatagrams())
        {
            const QNetworkDatagram datagram = client_->receiveDatagram();
            if(!datagram.isValid())
            {
                qWarning().noquote() << "[udp] receive failed" << client_->errorString();
                break;
            }

            //空报文没有内容可解，qDebug 级别记录后丢弃（编译时用 QT_NO_DEBUG_OUTPUT 关掉）
            if(datagram.data().isEmpty())
            {
                qDebug().noquote() << "[udp] empty datagram from"
                                   << datagram.senderAddress().toString();
                continue;
            }

            //队列满就丢最旧的，保证新数据能进来
            if(cfg_.max_recv_number_of_msg > 0
               && pending_.size() >= static_cast<size_t>(cfg_.max_recv_number_of_msg))
            {
                qWarning().noquote() << "[udp] recv queue full, drop oldest";
                pending_.pop();
            }
            pending_.push(datagram.data());
        }
    }
};
