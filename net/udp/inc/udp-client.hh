/**
 * @brief udp基础功能追求快用来传递视频和音频的无所谓错误只要别太离谱而且
 */
#pragma once
#include "net-base.hh"
#include <memory>
#include <QHostAddress>
#include <QUdpSocket>
#include <qcontainerfwd.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qudpsocket.h>
#include <queue>
namespace app
{
    class UdpClient:public QObject , public NetBase
    {
        Q_OBJECT
    public:
        /** @brief 建议的单报文大小，超过会走 IP 分片，丢一片整包作废 */
        static constexpr int kDefaultDatagramWarnSize = 1200;

        struct Config : public StreamConfig
        {
            QString local_host              = "0.0.0.0";
            quint16 local_port              = 0;
            bool    auto_allocate           = true;   //true 由系统分配本地地址和端口，false 用 local_host/local_port
            int     connect_timeout         = 5000;
            int     max_recv_number_of_msg  = 500;//接收条数不是bit数量因为udp的包分发和tcp这种不一样
            int     datagram_warn_size      = kDefaultDatagramWarnSize;//超过这个值只告警不拦截，0 表示关闭告警
            int     recv_buffer_size        = 0;   //内核接收缓冲字节数(SO_RCVBUF)，0 表示用系统默认
            QUdpSocket::BindMode bind_mode  = QUdpSocket::BindFlag::DefaultForPlatform;
        };

        UdpClient(QObject *parent = nullptr);
        ~UdpClient();

        bool start(const Config &config);
        bool stop();
        bool stop_and_wait(int timeout_ms);

        /**
         * @brief 发送一个数据报
         * @param[in] payload 数据报内容，一次一条，不自动分片
         * @return true 表示已交给内核；false 表示未连接、过长或写失败
         * @details 单个报文上限 kMaxDatagramSize(65507 字节，IPv4)，超过 MTU 的分片要上层自己做
         */
        bool send(const QByteArray &payload);

        /** @brief 调试用：当前积压的报文条数，不消费 */
        std::size_t pending_size() const;
        /** @brief 调试用：看一眼队首报文，不消费 */
        const QByteArray &peek_pending() const;
        /** @brief 调试用：取出队首报文，队列为空时返回空数组 */
        QByteArray take_pending();
    signals:
        void connected();
        void disconnected();
        void disconnected_permanently(CloseReason reason);
        void err_occurred(const QString msg);
    private slots:
        void on_state_changed(QAbstractSocket::SocketState state);
        void on_err(QAbstractSocket::SocketError err);
        void on_read();
    private:
        /** @brief 把 socket 的状态、错误、可读信号接到本类的槽上 */
        void wire_client();
        /** @brief 连接建立后设置内核接收缓冲，只需生效一次 */
        void apply_recv_buffer();

        std::queue<QByteArray>        pending_;
        std::unique_ptr<QUdpSocket>   client_;
        std::unique_ptr<QHostAddress> host_;
        Config                        cfg_;
        bool                          started_             = false;
        bool                          stop_down_           = false;
        bool                          recv_buffer_applied_ = false;
    protected:
        /** @brief 重连定时器到点：重新发起连接（基类共用调度调用） */
        void do_reconnect() override;
        /** @brief 建链超时：等不到 Connected/Bound 就 abort，回到 Unconnected 走统一重连 */
        void on_timeout() override;
        /** @brief 共用调度日志的协议前缀 */
        const char *log_tag() const override { return "udp"; }
    };
}
