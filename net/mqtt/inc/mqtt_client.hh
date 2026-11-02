#pragma once
#include "net-base.hh"
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMqttClient>
#include <QObject>
#include <QTimer>
#include <QString>
#include <QDateTime>
#include <memory>
namespace app
{
    class MqttClient : public QObject, public NetBase
    {
        Q_OBJECT
    public:
        struct Config : StreamConfig
        {
            /** @brief 空则用uuid自动生成 */
            QString clientId        = "";
            /** @brief 可为空(看情况) */
            QString username        = "";
            QString password        = "";
            /** @brief 前缀主题 */
            QString topic_prefix    = "h-000-h";
            quint16 keep_alive      = 30;
            QByteArray will_msg     = "{\"status\":\"off\"}";
            /** @brief 最终主题 = topic_prefix + "/" + 该值 */
            QString    will_subtopic = "will";
            /** @brief 断线期间最多缓存多少个不同主题，0 表示不缓存直接丢 */
            quint16   max_pending   = 16;
            bool    clean_session   = true;
            bool    auto_keep_alive = true;
            bool    will_retain     = true;
        };

        explicit  MqttClient(QObject*parent = nullptr);
        ~MqttClient() override;

        bool start(const Config &cfg);
        void stop();
        /**
         * @brief 主动断开并等待真正断开完成，只在退出流程里调用
         * @param[in] timeout_ms 最长等待毫秒数
         * @return true 表示已经断开
         * @details 内部跑局部事件循环，DISCONNECT 报文才有机会发出去；
         *          析构函数里不能调 —— 那时事件循环已经不再转了
         */
        bool stop_and_wait(int timeout_ms = 3000);
        bool is_connected() const;
        /** @brief 累计自动补发成功的消息条数，进程生命周期内单调递增 */
        quint64 resent_total() const;

        /* 命名约定：xxx_sub_yyy = 子主题，自动拼 topic_prefix
         *           xxx_topic_yyy = 完整主题，原样发出去
         */

        /**
         * @brief 发布一条 json 消息（子主题，自动拼 topic_prefix）
         * @param[in] subtopic 子主题，最终主题 = topic_prefix + "/" + subtopic
         * @param[in] json_obj 待序列化的对象
         * @param[in] qos 服务质量 0/1/2
         * @param[in] retain 是否保留消息
         * @param[in] style 序列化格式，上线用 Compact（省带宽），调试可传 Indented
         * @return 未连接或发布失败返回 false
         */
        bool publish_sub_json( const QString &subtopic,
                               const QJsonObject &json_obj,
                               quint8 qos = 1,
                               bool retain = false,
                               QJsonDocument::JsonFormat style = QJsonDocument::Compact);

        /**
         * @brief 发布一条 json 消息（完整主题，不拼 topic_prefix）
         * @param[in] topic 完整主题名字
         * @param[in] json_obj 待序列化的对象
         * @param[in] qos 服务质量 0/1/2
         * @param[in] retain 是否保留消息
         * @param[in] style 序列化格式
         * @return 未连接或发布失败返回 false
         */
        bool publish_topic_json( const QString &topic,
                                 const QJsonObject &json_obj,
                                 quint8 qos = 1,
                                 bool retain = false,
                                 QJsonDocument::JsonFormat style = QJsonDocument::Compact);

        /**
         * @brief 发布原始数据（子主题，自动拼 topic_prefix）
         * @param[in] subtopic 子主题，最终主题 = topic_prefix + "/" + subtopic
         * @param[in] payload 原始字节
         * @param[in] qos 服务质量 0/1/2
         * @param[in] retain 是否保留消息
         * @return 未连接或发布失败返回 false
         * @code
         * mqtt.publish_sub_data("cmd", "hello");                          //普通字符串
         * mqtt.publish_sub_data("cmd", QString("中文").toUtf8());         //UTF-8
         * mqtt.publish_sub_data("cmd", QByteArray::fromHex("0102FF"));    //十六进制
         * @endcode
         */
        bool publish_sub_data(const QString &subtopic,const QByteArray &payload,quint8 qos = 1,bool retain = false);

        /**
         * @brief 发布原始数据（完整主题，不拼 topic_prefix）
         * @param[in] topic 完整主题名字
         * @param[in] payload 原始字节
         * @param[in] qos 服务质量 0/1/2
         * @param[in] retain 是否保留消息
         * @return 未连接或发布失败返回 false
         */
        bool publish_topic_data(const QString &topic,const QByteArray &payload,quint8 qos = 1,bool retain = false);

        /**
         * @brief 订阅子主题 名字 = prefix/subtopic
         * @param[in] subtopic 子主题名字
         * @param[in] qos 服务质量 0/1/2
         * @details 订阅关系会被记录，自动重连成功后按原 qos 重新订阅
         */
        bool subscribe_sub(const QString &subtopic,quint8 qos = 1);

        /**
         * @brief 订阅完整主题 名字自己定
         * @param[in] topic 主题名字
         * @param[in] qos 服务质量 0/1/2
         * @details 订阅关系会被记录，自动重连成功后按原 qos 重新订阅
         */
        bool subscribe_topic(const QString &topic,quint8 qos = 1);

    signals:
        /** @brief 已连上 broker，首次连接成功和自动重连成功都会发射 */
        void connected();
        /**
         * @brief 刚掉线，可用于在重试前做备份之类工作
         * @details 之后可能自动重连成功，也可能走到 disconnected_permanently
         */
        void disconnected();
        /**
         * @brief 彻底断开：主动 stop 或重连次数耗尽，不会再自动重连
         * @param[in] reason 断开原因，上层据此决定是提示用户还是重连兜底
         * @details 上层在这里做落盘、报警、退出重连循环等收尾工作；每次停止只发射一次
         */
        void disconnected_permanently(CloseReason reason);
        /**
         * @brief 收到一条订阅主题的消息
         * @param[in] topic 完整主题名，未去掉 topic_prefix
         * @param[in] payload 原始负载，不保证是 UTF-8 文本
         */
        void message_received(const QString &topic,const QByteArray &payload);
        /**
         * @brief 发生了一条错误，msg 可直接展示给用户
         * @param[in] msg 已格式化的错误描述，含时间戳与错误名
         */
        void error_occurred(const QString &msg);

    private slots:
        void on_state_changed(QMqttClient::ClientState state);
        void on_message(const QByteArray &payload,const QMqttTopicName &topic);
        void on_err(QMqttClient::ClientError err);
    private:
        /** @brief 收尾：只发射一次 disconnected_permanently，超时保护与正常断开共用 */
        void finalize_stop(CloseReason reason);
        /** @brief 把 client_ 的信号接到本类的槽上，构造与重建后各调用一次 */
        void wire_client();
        /** @brief 丢弃并重建 client_：旧对象交由 deleteLater 销毁，避免在它自己的信号里直接析构 */
        void delete_client_later();
        /** @brief 重连成功后按 subscriptions_ 记录的 qos 重新订阅 */
        void resubscribe_all();
        /** @brief 连上后把 pending_ 里缓存的消息按原 qos/retain 依次发出 */
        void flush_pending();
        QString full_topic(const QString &sub) const;
        /** @brief 真正发消息的地方，两个 publish_* 共用 */
        bool publish_bytes(const QString &topic,const QByteArray &payload,quint8 qos,bool retain);
        /** @brief 真正订阅的地方，两个 subscribe_* 共用 */
        bool do_subscribe(const QString &topic,quint8 qos);
        /** @brief 断线期间缓存的一条待发消息 */
        struct Pending
        {
            QString    topic;
            QByteArray payload;
            quint8     qos       = 1;
            bool       retain    = false;
            /** @brief 入队时间，补发时用它算延迟 */
            QDateTime  queued_at;
        };
        std::unique_ptr<QMqttClient> client_ = nullptr;
        Config      cfg_;
        std::unique_ptr<QTimer>   stop_timer_      = nullptr;
        /** @brief 主题 -> qos，重连后重放 */
        QHash<QString, quint8>    subscriptions_;
        /** @brief 断线待发：主题 -> 最新一条，同主题覆盖 */
        QHash<QString, Pending>   pending_;
        /** @brief 累计自动补发成功条数 */
        quint64     resent_total_ = 0;
        bool        started_    = false;
        bool        stopping_   = false;
        bool        stop_done_  = false;
    protected:
        /** @brief 重连定时器到点：重新发起连接（基类共用调度调用） */
        void do_reconnect() override;
        /** @brief 建链超时：卡在 Connecting 时放弃本次握手，回到 Disconnected 走统一重连 */
        void on_timeout() override;
        /** @brief 共用调度日志的协议前缀 */
        const char *log_tag() const override { return "mqtt"; }
    };
}
