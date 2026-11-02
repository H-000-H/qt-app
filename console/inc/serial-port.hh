#pragma once
#include <memory>
#include <qobject.h>
#include <qstringview.h>
#include <qtmetamacros.h>
#include <qserialport.h>
#include <qstring.h>
#include <qtypes.h>
#include <queue>
namespace app
{

    class Serial : public QObject
    {
        Q_OBJECT
    public:
        explicit Serial(QObject *parent = nullptr);
        ~Serial();
        /**@brief 默认用最常用的配置 115200 波特率 无奇偶校验 8bit data位 1 bit停止位 */
        struct Config
        {
            /** @brief 串口设备名，如 /dev/ttyUSB0 或 COM3 */
            QString port_name                       = QString();
            /** @brief 波特率，标准枚举支持太少，这里用整数，921600 这种值也能填不然就和at773一样1500000的奇怪波特率就完蛋了 */
            qint32  baud                            = QSerialPort::Baud115200;
            QSerialPort::Parity   parity            = QSerialPort::Parity::NoParity;
            QSerialPort::DataBits data_bits         = QSerialPort::DataBits::Data8;
            QSerialPort::StopBits stop_bits         = QSerialPort::StopBits::OneStop;
            QSerialPort::FlowControl flow_control   = QSerialPort::FlowControl::NoFlowControl;
            QSerialPort::Direction direction        = QSerialPort::Direction::AllDirections;
            bool    open_pending                    = false;
            /** @brief 接收队列上限，0 表示不限 */
            qint16  recv_pending_max                = 32;
            /** @brief 发送队列上限，0 表示不限 */
            qint16  writen_pending_max              = 16;
        };
        bool write(const QByteArray &buf);
        bool read(QByteArray &buf ,qint64 &length);
        bool open(Config &cfg);
        bool stop();

        /**
         * @brief 改波特率
         * @param[in] baud 新的波特率
         * @return false 表示端口已打开且设置失败
         * @details 未打开时只记到 cfg_，下次 open() 生效；已打开时立即生效
         */
        bool set_baud_rate(qint32 baud);

        /** @brief 查看用：接收队列积压条数，不消费 */
        std::size_t recv_size() const;
        /** @brief 查看用：队首接收报文，不消费；队列变动后引用失效 */
        const QByteArray &peek_recv() const;
        /** @brief 取出队首接收报文，队列空时返回空数组 */
        QByteArray take_recv();

        /** @brief 查看用：发送队列积压条数，不消费 */
        std::size_t writen_size() const;
        /** @brief 查看用：队首发送报文，不消费；队列变动后引用失效 */
        const QByteArray &peek_writen() const;
        /** @brief 取出队首发送报文，队列空时返回空数组 */
        QByteArray take_writen();
    signals:
        /** @brief 端口已打开，可以收发 */
        void connected();
        /** @brief 端口已关闭：主动 stop()，或设备掉线(ResourceError) */
        void disconnected();
        /** @brief 设备掉线(ResourceError)，先于 disconnected() 发出，用于触发重开 */
        void port_lost();

    private slots:
        void on_err(QSerialPort::SerialPortError error);
    private:
        constexpr bool open_pending() const;

        Config cfg_ ;
        /**@brief 连接的客户端*/
        std::unique_ptr<QSerialPort> port_;

        /**@brief最近的接收队列 */
        std::queue<QByteArray> recv_pending_;

        /**@brief最近的发送队列 */
        std::queue<QByteArray> writen_pending_;
    };
}
