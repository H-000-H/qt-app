#pragma once
#include "net-base.hh"
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QObject>
#include <QTimer>
#include <QString>
#include <memory>
#include <qarraydata.h>
#include <qcontainerfwd.h>
#include <qobject.h>
#include <qstringview.h>
#include <qtcpsocket.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
namespace app
{
    constexpr qint32 k1MByte = (1<<20);
    /**
     * @brief TCP 客户端
     * @details 帧布局：字段顺序固定为 magic → ver → tag → type → crc → len，未开启的位不占空间
     * @code
     *     +-------+-----+-----+------+-----+-----+  +------------+
     *     | magic | ver | tag | type | crc | len |  | body       |
     *     | 2B    | 3B  | 4B  | 1B   | 2B  | 4B  |  | len 字节   |
     *     +-------+-----+-----+------+-----+-----+  +------------+
     *   len_offset = head_len - len_size，即长度字段固定在头部最后
     *   上面是全部字段都打开的样子，head_len = 2+3+4+1+2+4 = 16
     * @endcode
     * @note type 是每帧变化的业务字段，不像 version/tag 有固定期望值，所以没有 strict 校验
     */
    class TcpClient : public QObject, public NetBase
    {
        Q_OBJECT
    public:
        explicit TcpClient(QObject *parent = nullptr);
        ~TcpClient();

        struct Config : StreamConfig
        {
            QString local_host              = "0.0.0.0";  //auto_allocate=false 时绑定的本地地址
            quint16 local_port              = 0;           //0 表示由系统分配
            bool    auto_allocate           = true;   //true 由系统分配本地地址和端口，false 用 local_host/local_port
            int     connect_timeout         = 0;

            qint8   head_length              = 4;    /* 头部总长，含 magic 与长度字段 */
            quint8  len_size                = 4;    /* 长度字段字节数 1~4 */
            QByteArray magic                ;       /* 帧头魔术字，FrameFlag::Magic 打开时才生效 */
            FrameFlag frame_flags           = FrameFlag::None; /* 帧功能位图，见 FrameFlag */
            QByteArray version              ;       /* 版本原始字节，如 {1,0,1}，长度即占用的字节数 */
            QByteArray tag                  ;       /* 自定义标签原始字节，FrameFlag::Tag 时随头部携带 */
            bool    version_strict          = false;/* 版本不匹配时：true 断开重连，false 只告警 */
            bool    tag_strict              = false;/* 标签不匹配时：true 断开重连，false 只告警 */
            CrcKind crc_kind                = CrcKind::None; /* 校验算法，占几字节由算法决定 */
            quint8  max_bad_frames          = 3;    /* 连续多少帧异常后强制断开重连 */
            quint16 bad_frame_window_ms     = 10000;/* 坏帧计数的时间窗，超过这么久没有新坏帧就重新计数；0 = 只按连续计数 */
            bool    big_endian              = true;
            quint32 max_package_size        = 4*k1MByte;

            bool       enable_heart        = false;   //是否启用心跳
            int        heart_interval_ms   = 30000;   //心跳发送间隔，30s 量级；10ms 会把链路刷满
            QByteArray heart_package       ;          //心跳包内容，打开开关时必须非空
            int        heart_timeout_ms    = 90000;   //超过这么久没收到任何数据就判定链路已死，通常取 3 个发送周期

            bool    local_cache_enable      = false;   //断线时只保留最后一帧，重连后补发一次
            quint32 max_write_buffer        = 4*k1MByte; //本地发送缓冲背压阈值，超过就拒发
        };
        /**
         * @brief 启动tcp的api 默认不加入构造函数必须主动调用才会启动
         * @param[in] 本次tcp配置所需参数
         */
        bool start(const Config &cfg);
        /**
         * @brief 发送一帧完整数据
         * @param[in] frame 已组好帧头和校验的完整帧，不接受半帧
         * @return true 表示已交给本地发送缓冲，不代表对端收到；false 表示这次没发出去
         * @details 断线时按 cfg_.local_cache_enable 决定是缓存最后一帧还是直接丢
         */
        bool send(const QByteArray &frame);
        /**
         * @brief 把断线期间缓存的那一帧补发出去，发完即清
         * @details 由上层在合适的时机调用：同步鉴权就在 connected 槽里调，
         *          异步鉴权要等到收到鉴权成功的信号后再调
         */
        void flush_local_cache();

        /**
         * @brief 运行时改目标地址，只影响下一次连接
         * @param[in] host 新地址，不能为空
         * @param[in] port 新端口，不能为 0
         * @return false 表示参数非法，原地址不变
         * @details 不会打断当前连接：已连上时改的是"下次连哪"，
         *          在 disconnected() 槽里调用正好能让紧接着的重连用上新地址
         */
        bool set_endpoint(const QString &host, quint16 port);

        /**
         * @brief 运行时暂停或恢复心跳
         * @param[in] paused true 暂停：不发心跳，也不做超时判死；false 恢复
         * @details 配置在 start() 后冻结，运行中只能靠这个接口切换。
         *          暂停期间链路再久没数据也不会 abort；恢复时重新计时，不会立刻判死
         */
        void set_heartbeat_paused(bool paused);

        /**
         * @brief 结束tcp 关闭tcp申请的资源
         */
        bool stop();
        /**
         * @brief 主动断开并等待真正断开完成，只在退出流程里调用
         * @param[in] timeout_ms 最长等待毫秒数
         * @return true 表示已经断开
         * @details 内部跑局部事件循环，FIN 才有机会发出去；析构函数里不能调
         */
        bool stop_and_wait(int timeout_ms = 3000);
        signals:
        void domain_name_resolution();
        void connected();
        /**
         * @brief 刚掉线，之后可能重连成功，也可能走到 disconnected_permanently
         */
        void disconnected();
        /**
         * @brief 彻底断开：主动 stop 或不会再有下一次重连
         * @param[in] reason 断开原因，上层据此决定是提示用户还是走兜底逻辑
         * @details 每次停止只发射一次，之后不会再自动重连
         */
        void disconnected_permanently(CloseReason reason);
        void err_occurred(const QString &err_info);
        /**
         * @brief 收到一条完整帧，只要包体
         * @param[in] body 包体原始字节，不保证是 UTF-8
         * @details 不关心帧头（type/tag 等）时连这个就够了；和 frame_received 二选一
         */
        void body_received(const QByteArray &body);
        /**
         * @brief 收到一条完整帧，帧头和包体都给
         * @param[in] hdr 帧头：body_len 与 ext（magic/ver/type/tag/crc 的原样字节）
         * @param[in] body 包体原始字节，不保证是 UTF-8
         * @details 网络层不解释 ext 里的字段含义，上层按自己的布局取 type/tag 等信息；
         *          和 body_received 二选一，两个都连会把同一帧处理两遍
         */
        void frame_received(const FrameHeader &hdr,const QByteArray &body);
        private slots:
        void on_read();
        void on_err(QAbstractSocket::SocketError err);
        void on_state_changed(QAbstractSocket::SocketState state);
    private:
        void wire_client();
        /** @brief 记一次异常帧：窗口过期就先清零，返回本次累计次数 */
        quint8 count_bad_frame();
        /** @brief 心跳定时器到点：先判链路是否已死，空闲时再补一个心跳 */
        void heartbeat_tick();
        /** @brief 心跳定时器周期：发送间隔与超时 1/3 中的较小值 */
        int heartbeat_tick_ms() const;
        Config cfg_;
        QByteArray  pending_;
        quint8 bad_frame_count_ = 0;   //时间窗内的异常帧计数，正常帧或窗口过期就清零
        QElapsedTimer bad_frame_timer_; //最近一次异常帧的时刻，用于判断窗口是否过期

        //帧格式在 start() 里一次算好，运行时不再读 cfg_
        QByteArray  magic_;            //生效的魔术字，关闭时为空
        qsizetype   magic_len_ = 0;    //魔术字字节数
        qsizetype   head_len_  = 0;    //头部总长（含 magic）
        qsizetype   len_offset_ = 0;   //长度字段在头部内的偏移
        qsizetype   len_size_   = 0;   //长度字段字节数 1~4，来自 Config::len_size
        bool        big_endian_ = true;//长度字段字节序
        quint32     max_package_ = 0;  //单帧包体上限
        quint8      max_bad_frames_ = 0;//异常帧次数上限
        quint16     bad_frame_window_ms_ = 0;//异常帧计数时间窗
        QByteArray  version_;          //期望的版本字节，未开启时为空
        qsizetype   version_len_ = 0;  //版本字段字节数
        qsizetype   version_offset_ = 0;//版本字段在头部内的偏移
        bool        version_strict_ = false;//版本不匹配时是否直接断开
        QByteArray  tag_;              //期望的标签字节，未开启时为空
        qsizetype   tag_len_ = 0;      //标签字段字节数
        qsizetype   tag_offset_ = 0;   //标签字段在头部内的偏移
        bool        tag_strict_ = false;//标签不匹配时是否直接断开
        bool        has_type_ = false; //是否带消息类型字段
        qsizetype   type_offset_ = 0;  //类型字段在头部内的偏移
        CrcKind     crc_kind_   = CrcKind::None;//生效的校验算法
        qsizetype   crc_width_  = 0;    //校验字段字节数
        qsizetype   crc_offset_ = 0;    //校验字段在帧内的偏移
        bool started_ = false;
        std::unique_ptr<QTcpSocket> client_;
        std::unique_ptr<QHostAddress> host_;
        QByteArray local_cache_frame_;     //发送方向只缓存最后一帧，不做队列
        bool has_local_cache_frame_ = false;
        bool stop_down_             = false;
        std::unique_ptr<QTimer> stop_timer_;      //stop() 之后的挥手超时兜底
        std::unique_ptr<QTimer> heart_timer_;     //心跳发送与超时判定共用
        QElapsedTimer last_tx_;                   //最近一次成功写出的时刻
        QElapsedTimer last_rx_;                   //最近一次收到合法帧的时刻
        QByteArray heart_package_;                //冻结后的心跳帧（完整帧）
        int  heart_interval_ms_ = 0;              //心跳发送间隔
        int  heart_timeout_ms_  = 0;              //多久没收到数据判死
        bool heart_enabled_     = false;
        bool heart_paused_      = false;          //运行时暂停：不发心跳也不判死
    protected:
        /** @brief 重连定时器到点：重新发起连接（基类共用调度调用） */
        void do_reconnect() override;
        /** @brief 建链超时：卡在解析/握手阶段时强制断开，回到 Unconnected 走统一重连 */
        void on_timeout() override;
        /** @brief 共用调度日志的协议前缀 */
        const char *log_tag() const override { return "tcp"; }
    };
}
