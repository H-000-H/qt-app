#pragma once
#include <QByteArray>
#include <QByteArrayView>
#include <QDataStream>
#include <QDebug>
#include <QMetaType>
#include <QString>
#include <QTimer>
#include <QtGlobal>
#include <array>
#include <memory>
#include <optional>

namespace app
{
    /**
     * @brief 所有网络协议都有的连接参数
     * @details 只放"任何协议都绕不开"的字段，HTTP/UDP 也能直接用
     */
    struct NetConfig
    {
        QString host = "";      //服务端地址
        quint16 port = 1883;    //服务端端口，其它协议在自己的 Config 里覆盖默认值
    };

    /**
     * @brief 面向连接、掉线需要重连的协议共用配置（TCP / MQTT / 以后的 WebSocket）
     * @details UDP、HTTP 这种无连接或短连接协议不继承这一层
     */
    struct StreamConfig : NetConfig
    {
        static constexpr quint8 kDefaultMaxRetry = 6;

        std::optional<quint8> max_retry;     //重连次数上限；不填用 kDefaultMaxRetry，填 0 表示不重连
        bool   auto_reconnect     = true;   //掉线后是否自动重连
        bool   reconnect_infinite = false;  //无限重连，退避封顶 30s
        int    reconnect_interval = 0;      //固定重连间隔(ms)，0 表示指数退避
    };

    /**
     * @brief 帧功能位图：Config::frame_flags 按位组合，决定头部里有哪些字段
     * @details 只表示"开不开"，字段具体排在哪由 head_len / len_size 决定
     */
    enum class FrameFlag : quint8
    {
        None    = 0x00,        //纯长度头
        Magic   = 0x01 << 0,   //头部带 magic，用于校验和错位重同步
        Version = 0x01 << 1,   //头部带协议版本
        Type    = 0x01 << 2,   //头部带消息类型
        Crc     = 0x01 << 3,   //头部带校验字段
        Tag     = 0x01 << 4,   //头部带自定义标签，内容不校验只随帧携带
    };

    /** @brief 位图组合，方便写 FrameFlag::Magic | FrameFlag::Version */
    constexpr FrameFlag operator|(FrameFlag a, FrameFlag b) noexcept
    {
        return static_cast<FrameFlag>(static_cast<quint8>(a) | static_cast<quint8>(b));
    }

    /** @brief 判断某个功能位是否打开 */
    constexpr bool has_flag(FrameFlag flags, FrameFlag bit) noexcept
    {
        return (static_cast<quint8>(flags) & static_cast<quint8>(bit)) != 0;
    }

    /** @brief 校验收字段的算法，宽度由算法决定，避免 size 与算法配不一致 */
    enum class CrcKind : quint8
    {
        None  = 0,
        Sum8  = 1,   //1 字节累加和
        Crc16 = 2,   //2 字节 CRC-16/CCITT
        Crc32 = 3,   //4 字节 CRC-32
    };

    /** @brief 校验工具：宽度、计算、比对 */
    struct Crc
    {
        /** @brief 该算法占几个字节 */
        static quint8 width(CrcKind kind)
        {
            switch (kind)
            {
            case CrcKind::Sum8:  return 1;
            case CrcKind::Crc16: return 2;
            case CrcKind::Crc32: return 4;
            default:             return 0;
            }
        }

        /** @brief 计算校验值，覆盖传入的全部字节 */
        static quint32 compute(CrcKind kind, const QByteArray &data)
        {
            switch (kind)
            {
            case CrcKind::Sum8:
            {
                quint32 sum = 0;
                for (const char c : data)
                {
                    sum = (sum + static_cast<quint8>(c)) & 0xFF;
                }
                return sum;
            }
            case CrcKind::Crc16:
                return qChecksum(QByteArrayView(data), Qt::ChecksumIso3309);
            case CrcKind::Crc32:
            {
                static const std::array<quint32, 256> table = []
                {
                    std::array<quint32, 256> t{};
                    for (quint32 i = 0; i < 256; ++i)
                    {
                        quint32 c = i;
                        for (int k = 0; k < 8; ++k)
                        {
                            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                        }
                        t[i] = c;
                    }
                    return t;
                }();

                quint32 c = 0xFFFFFFFFu;
                for (const char ch : data)
                {
                    c = table[(c ^ static_cast<quint8>(ch)) & 0xFF] ^ (c >> 8);
                }
                return c ^ 0xFFFFFFFFu;
            }
            default:
                return 0;
            }
        }

        /** @brief 按字节序读出帧里保存的校验值，宽度 1~4 */
        static quint32 read_value(const char *p, quint8 n, bool big_endian)
        {
            quint32 v = 0;
            if (big_endian)
            {
                for (quint8 i = 0; i < n; ++i)
                {
                    v = (v << 8) | static_cast<quint8>(p[i]);
                }
            }
            else
            {
                for (quint8 i = n; i > 0; --i)
                {
                    v = (v << 8) | static_cast<quint8>(p[i - 1]);
                }
            }
            return v;
        }

        /**
         * @brief 校验一帧：覆盖除校验字段本身以外的所有字节
         * @param[in] frame 完整帧（头 + 包体）
         * @param[in] crc_offset 校验字段在帧内的偏移
         */
        static bool verify(CrcKind kind, const QByteArray &frame, qsizetype crc_offset, bool big_endian)
        {
            const quint8 n = width(kind);
            if (n == 0 || crc_offset < 0 || crc_offset + n > frame.size())
                return false;

            const quint32 stored = read_value(frame.constData() + crc_offset, n, big_endian);
            QByteArray data = frame.left(crc_offset);
            data += frame.mid(crc_offset + n);
            return compute(kind, data) == stored;
        }
    };

    /**
     * @brief 帧头：长度字段定长，其余字段一律当变长扩展区
     * @details 各协议把头部字节原样交给 parse()，变长部分留在 ext 里上层自己解析
     *
     * @code
     * 情形一：不带 magic，head_len = N
     *   +----------+----------------------------------+
     *   | len:N    | body                             |
     *   +----------+----------------------------------+
     *   len_offset = 0   len_size = N   ext 为空
     *
     * 情形二：只有 magic + len，head_len = M + N
     *   +-----------+----------+-----------------------+
     *   | magic:M   | len:N    | body                  |
     *   +-----------+----------+-----------------------+
     *   len_offset = M   len_size = N   ext = magic 的 M 字节
     *
     * 情形三：头部含多个字段，head_len = 12（magic 2 + ver 3 + type 1 + crc 2 + len 4）
     *     +-------+-----+------+-----+-----+  +------------+
     *     | magic | ver | type | crc | len |  | body       |
     *     | 2B    | 3B  | 1B   | 2B  | 4B  |  | len 字节   |
     *     +-------+-----+------+-----+-----+  +------------+
     *   len_offset = head_len - len_size（长度字段压在头部最后）
     *   ext = magic + ver + type + crc（除 len 外的全部头部字节）
     * 情形三：头部含多个字段，head_len = 16
     *   +-------+-----+------+-----+-----+-----+  +------------+
     *   | magic | ver | type | crc | len |     |  | body       |
     *   | 2B    | 3B  | 1B   | 2B  | 4B  | pad |  | len 字节   |
     *   +-------+-----+------+-----+-----+-----+  +------------+
     *   len_offset = head_len - len_size（长度字段压在头部最后）
     *   ext = magic + ver + type + crc（除 len 外的全部头部字节）
     * @endcode
     */
    struct FrameHeader
    {
        quint32    body_len = 0;   //包体字节数
        QByteArray ext;            //头部剩余字节（magic/type/seq/crc 等，协议自定义）

        //以下由协议层解析后填好，未开启对应 FrameFlag 时为空/0
        QByteArray magic;          //帧头魔术字
        QByteArray version;        //协议版本原始字节
        QByteArray tag;            //自定义标签原始字节
        quint8     type = 0;       //消息类型

        /**
         * @brief 解析长度字段，并把头部除长度外的其余字节收进 ext
         * @param[in] buf 至少包含完整头部的数据
         * @param[in] head_len 头部总长
         * @param[in] len_offset 长度字段在头部内的偏移
         * @param[in] len_size 长度字段字节数，1~4 任意宽度（3 字节也支持）
         * @param[in] big_endian 字节序
         * @return false 表示数据不足或参数非法
         */
        static bool parse(const QByteArray &buf, qsizetype head_len, qsizetype len_offset,
                          qsizetype len_size, bool big_endian, FrameHeader &out)
        {
            if (head_len <= 0 || len_offset < 0 || len_size < 1 || len_size > 4)
                return false;
            if (len_offset + len_size > head_len || buf.size() < head_len)
                return false;

            QDataStream ds(buf.left(head_len));
            if (len_offset > 0)
                ds.skipRawData(static_cast<int>(len_offset));

            //按字节数原样取出，再按字节序拼，能覆盖 1~4 的所有宽度
            char raw[4] = {0};
            if (ds.readRawData(raw, static_cast<int>(len_size)) != len_size)
                return false;

            quint32 v = 0;
            if (big_endian)
            {
                for (qsizetype i = 0; i < len_size; ++i)
                {
                    v = (v << 8) | static_cast<quint8>(raw[i]);
                }
            }
            else
            {
                for (qsizetype i = len_size - 1; i >= 0; --i)
                {
                    v = (v << 8) | static_cast<quint8>(raw[i]);
                }
            }

            out.body_len = v;
            out.ext = buf.left(len_offset);
            out.ext += buf.mid(len_offset + len_size, head_len - len_offset - len_size);
            return true;
        }
    };

    /**
     * @brief 网络客户端基类：只放"重连状态 + 定时器 + 调度"这类与协议无关的东西
     */
    class NetBase
    {
    public:
        /** @brief 未显式配置超时时的默认值(ms) */
        static constexpr int kDefaultTimeoutMs = 5000;

        virtual ~NetBase() = default;

        /**
         * @brief 彻底断开的原因
         */
        enum class CloseReason
        {
            UserStop,       ///< 主动调用 stop()
            NetworkLost,    ///< 掉线且不会再重连（未开自动重连或次数耗尽）
        };

        /**
         * @brief 计算下一次重连延迟
         * @param[in] retry 已经重连过几次
         * @param[in] fixed_interval_ms 固定间隔；>0 时直接用它，否则走指数退避
         * @return 延迟毫秒数，指数退避为 1s/2s/4s/8s/16s/30s(max)
         */
        static int backoff_delay(int retry, int fixed_interval_ms)
        {
            if (fixed_interval_ms > 0)
                return fixed_interval_ms;
            return qMin(30000, 1000 << qMin(retry, 5));
        }

        /**
         * @brief 重连次数是否已用尽
         * @param[in] infinite 为 true 时永远不判上限
         */
        static bool give_up(int retry, std::optional<quint8> max_retry, bool infinite)
        {
            if (infinite) return false;
            return retry >= static_cast<int>(max_retry.value_or(StreamConfig::kDefaultMaxRetry));
        }

        /** @brief 日志用的 host:port，避免每个类各写一遍拼接 */
        static QString endpoint(const QString &host, quint16 port)
        {
            return QStringLiteral("%1:%2").arg(host).arg(port);
        }

    protected:
        /** @brief 重连定时器到点后重新发起连接，由派生类实现 */
        virtual void do_reconnect() = 0;
        /** @brief 日志前缀，共用调度里用来区分协议 */
        virtual const char *log_tag() const = 0;
        /**
         * @brief 超时到点后怎么中断，由派生类实现
         * @details 一般是 abort / disconnectFromHost；中断后走各自的状态机回到 Unconnected，
         *          再由 schedule_reconnect 用同一份 retry_ 预算重连，不另设超时计数器
         */
        virtual void on_timeout() = 0;

        /**
         * @brief 创建通用超时定时器并接到 on_timeout()
         * @param[in] owner 派生类对象本身，只作为信号上下文
         * @details 建链超时、握手超时这类"等不到状态迁移就强制中断"的场景共用；
         *          定时器由本类独占持有，不设 parent，避免和 QObject 父子关系重复接管
         */
        void init_timeout_timer(QObject *owner)
        {
            timeout_timer_ = std::make_unique<QTimer>();
            timeout_timer_->setSingleShot(true);
            QObject::connect(timeout_timer_.get(), &QTimer::timeout, owner, [this]()
            {
                qWarning().noquote() << QStringLiteral("[%1] timeout after %2 ms")
                                            .arg(log_tag()).arg(timeout_timer_->interval());
                on_timeout();
            });
        }

        /**
         * @brief 启动超时保护
         * @param[in] ms 超时毫秒数，<=0 时用 kDefaultTimeoutMs
         */
        void start_timeout(int ms)
        {
            if (timeout_timer_)
                timeout_timer_->start(ms > 0 ? ms : kDefaultTimeoutMs);
        }
        /** @brief 状态已经迁移（连上/断开），掐掉超时保护 */
        void stop_timeout()
        {
            if (timeout_timer_)
                timeout_timer_->stop();
        }

        /**
         * @brief 创建重连定时器并接到 do_reconnect()
         * @param[in] owner 派生类对象本身，只作为信号上下文
         * @details 构造期调用；定时器由本类独占持有，不设 parent
         */
        void init_reconnect_timer(QObject *owner)
        {
            reconnect_timer_ = std::make_unique<QTimer>();
            reconnect_timer_->setSingleShot(true);
            QObject::connect(reconnect_timer_.get(), &QTimer::timeout, owner, [this]()
            {
                do_reconnect();
            });
        }

        /**
         * @brief 排下一次重连：先判次数，再算间隔并启动定时器
         * @param[in] max_retry 次数上限
         * @param[in] infinite 是否无限重连
         * @param[in] interval 固定间隔(ms)，0 表示指数退避
         */
        void schedule_reconnect(std::optional<quint8> max_retry, bool infinite, int interval)
        {
            const int limit = static_cast<int>(max_retry.value_or(StreamConfig::kDefaultMaxRetry));

            if (give_up(retry_, max_retry, infinite))
            {
                qWarning().noquote() << QStringLiteral("[%1] reconnect give up, %2/%3 used")
                                            .arg(log_tag())
                                            .arg(retry_)
                                            .arg(infinite ? QStringLiteral("inf") : QString::number(limit));
                return;
            }

            const int delay = backoff_delay(retry_, interval);
            retry_++;
            qWarning().noquote() << QStringLiteral("[%1] retry %2/%3 in %4 ms")
                                        .arg(log_tag())
                                        .arg(retry_)
                                        .arg(infinite ? QStringLiteral("inf") : QString::number(limit))
                                        .arg(delay);
            reconnect_timer_->start(delay);
        }

        /** @brief 连接成功后清零重连计数 */
        void reset_retry() { retry_ = 0; }
        /** @brief 主动 stop 时掐掉待触发的重连 */
        void cancel_reconnect() { if (reconnect_timer_) reconnect_timer_->stop(); }

        qint8   retry_ = 0;                          //当前已重连次数，连上后清零
        std::unique_ptr<QTimer> reconnect_timer_;    //重连调度定时器
        std::unique_ptr<QTimer> timeout_timer_;      //建链/握手超时定时器
    };
}

Q_DECLARE_METATYPE(app::FrameHeader)
Q_DECLARE_METATYPE(app::NetBase::CloseReason)
