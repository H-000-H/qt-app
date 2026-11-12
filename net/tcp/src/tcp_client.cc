#include "tcp_client.hh"
#include "net-error.hh"
#include <memory>
#include <qabstracteventdispatcher.h>
#include <qabstractsocket.h>
#include <qcontainerfwd.h>
#include <qendian.h>
#include <qeventloop.h>
#include <qlogging.h>
#include <qminmax.h>
#include <qobject.h>
#include <qtcpsocket.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
namespace app
{
    constexpr int kCloseTimeoutMs = 5000;

    TcpClient::TcpClient(QObject *parent):QObject(parent)
    {
        client_             = std::make_unique<QTcpSocket>(this);
        stop_timer_         = std::make_unique<QTimer>();
        stop_timer_->setSingleShot(true);
        host_               = std::make_unique<QHostAddress>();//不带qobject
        init_reconnect_timer(this);//重连定时器由基类创建，到点回调 do_reconnect()
        init_timeout_timer(this);//建链超时定时器由基类创建，到点回调 on_timeout()
        wire_client();

        //关闭超时兜底：disconnectFromHost 迟迟等不到 UnconnectedState 就暴力断
        QObject::connect(   stop_timer_.get(),
                            &QTimer::timeout,
                            this,
                            [this]()
                            {
                                qWarning().noquote() << "[tcp] close timeout, abort";
                                client_->abort();
                            });

        //心跳：同一个定时器既负责发心跳，也负责判链路是否已经死了
        heart_timer_ = std::make_unique<QTimer>(this);
        heart_timer_->setTimerType(Qt::CoarseTimer);
        QObject::connect(heart_timer_.get(), &QTimer::timeout, this, &TcpClient::heartbeat_tick);
    }

    TcpClient::~TcpClient()
    {
        if(!client_)
            return;

        //析构阶段事件循环不再转，disconnectFromHost 的挥手发不出去，
        //这里直接 abort：同步关闭
        QObject::disconnect(client_.get(), nullptr, this, nullptr);
        if(client_->state() != QAbstractSocket::UnconnectedState)
            client_->abort();
    }

    void TcpClient::wire_client()
    {
        QObject::connect(   client_.get(),
                            &QTcpSocket::stateChanged,
                            this,
                            &TcpClient::on_state_changed);

        QObject::connect(   client_.get(),
                            &QTcpSocket::readyRead,
                            this,
                            &TcpClient::on_read);

        QObject::connect(   client_.get(),
                            &QTcpSocket::errorOccurred,
                            this,
                            &TcpClient::on_err);
    }

    bool TcpClient::start(const Config &cfg)
    {
        if(started_ == true)
        {
            qWarning().noquote()<<"[tcp] already started, do not start again";
            return false;
        }
        cfg_ = cfg;
        local_cache_frame_.clear();//新tcp不补发上一轮缓存的帧
        has_local_cache_frame_ = false;
        pending_.clear();
        bad_frame_count_ = 0;
        bad_frame_timer_.invalidate();
        heart_timer_->stop();
        last_tx_.invalidate();
        last_rx_.invalidate();
        heart_paused_ = false;//重新 start() 时回到默认，自动重连不清，保留上层的暂停意图
        stop_down_ = false;

        //帧格式校验：长度字段 1~4 字节，头部空间要够放所有已开启的字段
        if(cfg_.len_size < 1 || cfg_.len_size > 4)
        {
            qWarning().noquote() << "[tcp] len_size 非法：" << cfg_.len_size << "（只支持 1~4）";
            return false;
        }
        if(has_flag(cfg_.frame_flags, FrameFlag::Magic) && cfg_.magic.isEmpty())
        {
            qWarning().noquote() << "[tcp] FrameFlag::Magic 已打开但 magic 为空";
            return false;
        }
        if(has_flag(cfg_.frame_flags, FrameFlag::Version) && cfg_.version.isEmpty())
        {
            qWarning().noquote() << "[tcp] FrameFlag::Version 已打开但 version 为空";
            return false;
        }
        if(has_flag(cfg_.frame_flags, FrameFlag::Tag) && cfg_.tag.isEmpty())
        {
            qWarning().noquote() << "[tcp] FrameFlag::Tag 已打开但 tag 为空";
            return false;
        }
        if(cfg_.enable_heart && cfg_.heart_package.isEmpty())
        {
            qWarning().noquote() << "[tcp] enable_heart 已打开但 heart_package 为空";
            return false;
        }
        if(cfg_.enable_heart
           && (cfg_.heart_interval_ms <= 0 || cfg_.heart_timeout_ms <= cfg_.heart_interval_ms))
        {
            qWarning().noquote() << "[tcp] 心跳参数非法：interval =" << cfg_.heart_interval_ms
                                 << "timeout =" << cfg_.heart_timeout_ms
                                 << "（要求 timeout > interval > 0）";
            return false;
        }
        if(has_flag(cfg_.frame_flags, FrameFlag::Crc) && Crc::width(cfg_.crc_kind) == 0)
        {
            qWarning().noquote() << "[tcp] FrameFlag::Crc 已打开但 crc_kind 为 None";
            return false;
        }
        constexpr quint8 kAllFlags = static_cast<quint8>(FrameFlag::Magic) | static_cast<quint8>(FrameFlag::Version)
                                   | static_cast<quint8>(FrameFlag::Type) | static_cast<quint8>(FrameFlag::Crc)
                                   | static_cast<quint8>(FrameFlag::Tag);
        if((static_cast<quint8>(cfg_.frame_flags) & static_cast<quint8>(~kAllFlags)) != 0)
        {
            qWarning().noquote() << "[tcp] frame_flags 含未知位：" << static_cast<int>(cfg_.frame_flags);
            return false;
        }

        //各字段按自身实际长度累加，version 可以是 {1,0,1} 这种多字节，crc 也不再固定 1 字节
        const qsizetype magic_len = has_flag(cfg_.frame_flags, FrameFlag::Magic)   ? cfg_.magic.size()   : 0;
        const qsizetype ver_len   = has_flag(cfg_.frame_flags, FrameFlag::Version) ? cfg_.version.size() : 0;
        const qsizetype crc_len   = has_flag(cfg_.frame_flags, FrameFlag::Crc)     ? Crc::width(cfg_.crc_kind) : 0;
        const qsizetype tag_len   = has_flag(cfg_.frame_flags, FrameFlag::Tag)     ? cfg_.tag.size()     : 0;

        qsizetype need = magic_len + cfg_.len_size + ver_len + crc_len + tag_len;
        if(has_flag(cfg_.frame_flags, FrameFlag::Type))
            need += 1;

        if(cfg_.head_length < need)
        {
            qWarning().noquote() << "[tcp] head_length 不足：" << cfg_.head_length
                                 << "，至少需要" << need << "（magic + len + 已开启字段）";
            return false;
        }

        //帧格式一次性算好并冻结，on_read 里不再碰 cfg_
        magic_          = has_flag(cfg_.frame_flags, FrameFlag::Magic) ? cfg_.magic : QByteArray();
        magic_len_      = magic_.size();
        head_len_       = cfg_.head_length;
        //头部顺序：magic | version | tag | type | crc | len，未开启的位不占空间
        len_offset_     = cfg_.head_length - cfg_.len_size;
        len_size_       = cfg_.len_size;
        big_endian_     = cfg_.big_endian;
        max_package_    = cfg_.max_package_size;
        max_bad_frames_ = cfg_.max_bad_frames;
        bad_frame_window_ms_ = cfg_.bad_frame_window_ms;
        version_        = has_flag(cfg_.frame_flags, FrameFlag::Version) ? cfg_.version : QByteArray();
        version_len_    = version_.size();
        version_offset_ = magic_len_;
        version_strict_ = cfg_.version_strict;
        tag_            = has_flag(cfg_.frame_flags, FrameFlag::Tag) ? cfg_.tag : QByteArray();
        tag_len_        = tag_.size();
        tag_offset_     = magic_len_ + version_len_;
        tag_strict_     = cfg_.tag_strict;
        has_type_       = has_flag(cfg_.frame_flags, FrameFlag::Type);
        type_offset_    = tag_offset_ + tag_len_;
        crc_kind_       = has_flag(cfg_.frame_flags, FrameFlag::Crc) ? cfg_.crc_kind : CrcKind::None;
        crc_width_      = Crc::width(crc_kind_);
        heart_enabled_     = cfg_.enable_heart;
        heart_package_     = cfg_.heart_package;
        heart_interval_ms_ = cfg_.heart_interval_ms;
        heart_timeout_ms_  = cfg_.heart_timeout_ms;
        crc_offset_     = cfg_.head_length - cfg_.len_size - crc_width_;
        if(cfg_.auto_allocate == false)
        {
            //指定本地地址：解析失败直接放弃
            if(!host_->setAddress(cfg_.local_host))
            {
                qWarning().noquote() << "[tcp] invalid local_host" << cfg_.local_host;
                return false;
            }
            if(!client_->bind(*host_, cfg_.local_port))
            {
                qWarning().noquote()    << "[tcp] bind failed"
                                        << cfg_.local_host
                                        << cfg_.local_port
                                        << client_->errorString();
                return false;
            }
            qInfo().noquote()   << "[tcp] bound"<< cfg_.local_host
                                << "local port"<< client_->localPort();
        }

        reset_retry();
        //先置位：connectToHost 可能同步失败并立刻回到 Unconnected，
        //那时状态机需要知道"还在运行"才会继续重连或发终态
        started_ = true;
        client_->connectToHost(cfg_.host, cfg_.port);
        //建链超时交给基类：connect_timeout > 0 用它，否则用基类默认值
        start_timeout(cfg_.connect_timeout);
        return true;
    }

    bool TcpClient::stop()
    {
        if(stop_down_ == true)
        {
            qInfo().noquote() << "[tcp] already stopped";
            return true;
        }

        stop_down_ = true;
        started_   = false;//先置位，掉线回调里就不会再排重连
        cancel_reconnect();
        stop_timeout();//可能还在建链，别让建链超时和挥手超时打架
        heart_timer_->stop();
        pending_.clear();
        bad_frame_count_ = 0;
        bad_frame_timer_.invalidate();

        if(client_->state() == QAbstractSocket::UnconnectedState)
        {
            emit disconnected_permanently(CloseReason::UserStop);//本来就没连，状态机不会再来一次
            return true;
        }

        stop_timer_->start(kCloseTimeoutMs);//挥手超时兜底：到点直接 abort
        client_->disconnectFromHost();
        return true;
    }
    bool TcpClient::stop_and_wait(int timeout_ms)
    {
        if(client_->state() == QAbstractSocket::SocketState::UnconnectedState)
        {
            stop();//可能还排着一次重连，必须走 stop 取消掉
            return true;
        }

        //先接管信号再调 stop()，避免已经断开那种情况信号在 connect 之前就发出来
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(this, &TcpClient::disconnected, &loop, &QEventLoop::quit);

        stop();
        if(client_->state() == QAbstractSocket::SocketState::UnconnectedState) 
            return true;//disconnectFromHost 可能同步就完成了

        guard.start(timeout_ms);
        loop.exec();
        return client_->state() == QAbstractSocket::SocketState::UnconnectedState;
    }

    void TcpClient::on_state_changed(QAbstractSocket::SocketState state)
    {
        switch (state)
        {
        case QAbstractSocket::SocketState::UnconnectedState:
        {
            stop_timer_->stop();
            stop_timeout();//建链失败落到这里，超时定时器必须一起停
            heart_timer_->stop();
            qInfo().noquote() << "[tcp] close";
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
            qInfo().noquote() << "[tcp] host lookup ...";
            emit domain_name_resolution();//余名解析传递到这里如果解析失败可以在内部加逻辑
            break;
        }

        case QAbstractSocket::SocketState::ConnectingState:
        {
            //这个就是纯粹的过度态只写一句话
            qInfo().noquote() << "[tcp] connecting ...";
            break;
        }

        case QAbstractSocket::SocketState::ConnectedState:
        {
            qInfo().noquote() << QStringLiteral("[tcp] connected, local %1:%2")
                                    .arg(client_->localAddress().toString())
                                    .arg(client_->localPort());
            reset_retry();//连上就清零，下次掉线重新按次数算
            stop_timeout();//已连上，建链超时不能再留着

            if(heart_enabled_ && !heart_paused_)
            {
                last_tx_.start();
                last_rx_.start();
                heart_timer_->start(heartbeat_tick_ms());
            }

            emit connected();//emit 之后不再做任何事，补发/鉴权逻辑交给上层槽自己决定
            break;
        }

        case QAbstractSocket::SocketState::ClosingState:
        {
            //纯过渡态：只有我们自己调 disconnectFromHost 且还有待发数据时才会进这里，
            //兜底定时器在 stop() 里已经起过了，这里什么都不用做，等它自动迁到 Unconnected就可以了
            qInfo().noquote() << "[tcp] closing ...";
            break;
        }

        default:
            break;
        }
    }

    void TcpClient::do_reconnect()
    {
        qInfo().noquote() << QStringLiteral("[tcp] reconnecting...");
        client_->connectToHost(cfg_.host, cfg_.port);
        //重连同样要建链超时，否则卡在 Connecting 时永远不会再进 UnconnectedState
        start_timeout(cfg_.connect_timeout);
    }

    void TcpClient::on_timeout()
    {
        //同步中止：abort 会立刻进入 UnconnectedState，由状态机走统一的重连预算
        client_->abort();
    }

    bool TcpClient::set_endpoint(const QString &host, quint16 port)
    {
        if(host.isEmpty() || port == 0)
        {
            qWarning().noquote() << "[tcp] invalid endpoint" << host << port;
            return false;
        }

        cfg_.host = host;
        cfg_.port = port;

        //连接中只改配置，不动现有 socket；下一次 connectToHost 自然用新地址
        if(client_->state() == QAbstractSocket::ConnectedState)
        {
            qInfo().noquote() << "[tcp] endpoint changed to" << NetBase::endpoint(host, port)
                              << "(takes effect on next connect)";
        }
        else
        {
            qInfo().noquote() << "[tcp] endpoint changed to" << NetBase::endpoint(host, port);
        }
        return true;
    }

    int TcpClient::heartbeat_tick_ms() const
    {
        //检查周期取"发送间隔"与"超时 1/3"中的较小值，保证超时最晚 1/3 个 timeout 内被发现
        return qMax(1, qMin(heart_interval_ms_, heart_timeout_ms_ / 3));
    }

    void TcpClient::set_heartbeat_paused(bool paused)
    {
        if(heart_paused_ == paused)
            return;
        if(!heart_enabled_)
        {
            qWarning().noquote() << "[tcp] heartbeat not enabled, pause ignored";
            return;
        }

        heart_paused_ = paused;
        if(paused)
        {
            heart_timer_->stop();
            qInfo().noquote() << "[tcp] heartbeat paused";
            return;
        }

        //恢复时重新计时：暂停期间积攒的时间不能立刻触发超时
        if(client_->state() == QAbstractSocket::ConnectedState)
        {
            last_tx_.start();
            last_rx_.start();
            heart_timer_->start(heartbeat_tick_ms());
        }
        qInfo().noquote() << "[tcp] heartbeat resumed";
    }

    void TcpClient::heartbeat_tick()
    {
        // 先判链路死活：任何合法帧都会刷新 last_rx，超过 timeout 没有数据说明连接已经半死
        if(last_rx_.isValid() && last_rx_.elapsed() >= heart_timeout_ms_)
        {
            qWarning().noquote() << "[tcp] heartbeat timeout," << last_rx_.elapsed()
                                 << "ms without data, abort";
            pending_.clear();
            client_->abort();//走 UnconnectedState，交由上层决定是否重连
            return;
        }

        // 只在双向都空闲时补心跳：任一方还在传数据，链路就是活的，不用额外发
        // 定时器可能比设定值提前几毫秒触发，按剩余时间重排一次，避免每次都差一点而永远发不出去
        const qint64 since_tx = last_tx_.isValid() ? last_tx_.elapsed() : heart_interval_ms_;
        const qint64 since_rx = last_rx_.isValid() ? last_rx_.elapsed() : heart_interval_ms_;
        const qint64 idle = qMin(since_tx, since_rx);

        if(idle < heart_interval_ms_)
        {
            const qint64 remain = heart_interval_ms_ - idle;
            heart_timer_->start(static_cast<int>(qMin<qint64>(remain, heartbeat_tick_ms())));
            return;
        }

        if(client_->state() != QAbstractSocket::ConnectedState)
            return;

        //心跳不走 send()：失败不缓存不入队，下一个周期自然会再发
        if(client_->bytesToWrite() > static_cast<qint64>(cfg_.max_write_buffer))
        {
            qWarning().noquote() << "[tcp] heartbeat skipped, write buffer backed up";
            return;
        }

        if(client_->write(heart_package_) < 0)
            qWarning().noquote() << "[tcp] heartbeat write failed" << client_->errorString();
        else
            last_tx_.restart();
    }

    bool TcpClient::send(const QByteArray &frame)
    {
        if(frame.isEmpty())
        {
            qWarning().noquote() << "[tcp] empty frame, ignore";
            return false;
        }

        if(client_->state() != QAbstractSocket::ConnectedState)
        {
            if(cfg_.local_cache_enable)
            {
                //只留最后一帧
                local_cache_frame_ = frame;
                has_local_cache_frame_ = true;
                qInfo().noquote() << "[tcp] not connected, cache last frame, size =" << frame.size();
            }
            else
            {
                qWarning().noquote() << "[tcp] not connected, drop frame, size =" << frame.size();
            }
            return false;
        }

        //背压：如果待发送积压数据超过上限 max_write_buffer → 拒绝发包，返回 false
        if(client_->bytesToWrite() > static_cast<qint64>(cfg_.max_write_buffer))
        {
            qWarning().noquote() << "[tcp] write buffer backed up"
                                 << client_->bytesToWrite()
                                 << "limit" << cfg_.max_write_buffer;
            return false;
        }

        const qint64 n = client_->write(frame);
        if(n < 0)
        {
            qWarning().noquote() << "[tcp] write failed" << client_->errorString();
            if(cfg_.local_cache_enable)
            {
                local_cache_frame_ = frame;
                has_local_cache_frame_ = true;
            }
            return false;
        }
        if(n < frame.size())
        {
            //半帧已经进了内核缓冲，整帧再缓存会导致对端重复解析，这里只告警不缓存
            qWarning().noquote() << "[tcp] short write" << n << "/" << frame.size();
            return false;
        }
        last_tx_.restart();
        return true;
    }

    void TcpClient::flush_local_cache()
    {
        if(!has_local_cache_frame_)
            return;

        QByteArray frame = local_cache_frame_;
        local_cache_frame_.clear();
        has_local_cache_frame_ = false;

        if(client_->write(frame) < 0)
        {
            qWarning().noquote() << "[tcp] flush last frame failed, dropped, size =" << frame.size();
            return;
        }
        last_tx_.restart();
        qInfo().noquote() << "[tcp] flushed last frame, size =" << frame.size();
    }

    void TcpClient::on_err(QAbstractSocket::SocketError err)
    {
        if (err == QAbstractSocket::RemoteHostClosedError)
            return;
        const QString msg = QStringLiteral("[tcp] err_reason :%1 (%2)")
                                .arg(detail::socket_error_name(err))
                                .arg(client_->errorString());
        qWarning().noquote() << msg;

        emit err_occurred(msg);
    }

    quint8 TcpClient::count_bad_frame()
    {
        //时间窗内计数：距上一次坏帧超过窗口就重新从 0 开始，
        //避免长期运行中零星错误慢慢攒够次数误判成链路故障
        if(bad_frame_window_ms_ > 0
           && bad_frame_timer_.isValid()
           && bad_frame_timer_.elapsed() >= bad_frame_window_ms_)
        {
            bad_frame_count_ = 0;
        }

        bad_frame_timer_.restart();//每次坏帧都重置窗口，要求错误"持续发生"才累计
        if(bad_frame_count_ < 255)
            ++bad_frame_count_;
        return bad_frame_count_;
    }

    void TcpClient::on_read()
    {
        pending_ += client_->readAll();

        //magic 算在 head_length 里：头部 = [magic][长度字段]具体看net.cc
        while(true)
        {
            if(magic_len_ > 0)
            {
                //错位重同步：丢掉 magic 之前的垃圾字节
                const qsizetype pos = pending_.indexOf(magic_);
                if(pos < 0)
                {
                    //没找到 magic，只保留可能是半个 magic 的尾巴，等下次数据
                    if(pending_.size() > magic_len_)
                        pending_.remove(0, pending_.size() - magic_len_ + 1);
                    return;
                }
                if(pos > 0)
                {
                    qWarning().noquote() << "[tcp] resync, drop" << pos << "byte(s)";
                    pending_.remove(0, pos);
                }
            }

            if(pending_.size() < head_len_) return;//头部都不完整，等下一次 readyRead

            FrameHeader hdr;
            if(!FrameHeader::parse(pending_, head_len_,
                len_offset_, len_size_,
                big_endian_, hdr))
            {
                return;//头部参数异常或数据不足，等下一次 readyRead
            }
            const quint32 body_len = hdr.body_len;

            //版本校验：不匹配默认只告警，version_strict 打开才断开
            if(version_len_ > 0)
            {
                const QByteArray got = pending_.mid(version_offset_, version_len_);
                if(got != version_)
                {
                    qWarning().noquote() << "[tcp] version mismatch, got" << got.toHex()
                                         << "expect" << version_.toHex();
                    if(version_strict_)
                    {
                        qWarning().noquote() << "[tcp] version_strict 已打开，断开重连";
                        pending_.clear();
                        client_->abort();
                        return;
                    }
                }
            }

            //标签校验：和版本同一套策略，tag_strict 打开才断开
            if(tag_len_ > 0)
            {
                const QByteArray got = pending_.mid(tag_offset_, tag_len_);
                if(got != tag_)
                {
                    qWarning().noquote() << "[tcp] tag mismatch, got" << got.toHex()
                                         << "expect" << tag_.toHex();
                    if(tag_strict_)
                    {
                        qWarning().noquote() << "[tcp] tag_strict 已打开，断开重连";
                        pending_.clear();
                        client_->abort();
                        return;
                    }
                }
            }

            if(body_len > max_package_)
            {
                const quint8 bad = count_bad_frame();
                qWarning().noquote() << "[tcp] frame too large" << body_len
                                     << "bad =" << bad << "/" << max_bad_frames_;

                //长度字段不可信，没法定点跳过整帧：有 magic 就只跳过这个头，重新找下一个
                pending_.remove(0, magic_len_ > 0 ? magic_len_ : 1);

                if(bad >= max_bad_frames_)
                {
                    qWarning().noquote() << "[tcp] too many bad frames, abort";
                    pending_.clear();
                    client_->abort();//走 UnconnectedState，交由上层决定是否重连
                    return;
                }
                continue;
            }

            const qsizetype total = head_len_ + body_len;
            if(pending_.size() < total) return;//半包，等剩下的

            const QByteArray frame = pending_.left(total);
            pending_.remove(0, total);

            //校验不过直接扔掉这一帧，不影响后面的帧；连续坏太多说明链路或配置有问题
            if(crc_width_ > 0 && !Crc::verify(crc_kind_, frame, crc_offset_, big_endian_))
            {
                const quint8 bad = count_bad_frame();
                qWarning().noquote() << "[tcp] crc mismatch, drop frame, size =" << total
                                     << "bad =" << bad << "/" << max_bad_frames_;
                if(bad >= max_bad_frames_)
                {
                    qWarning().noquote() << "[tcp] too many bad frames, abort";
                    pending_.clear();
                    client_->abort();//走 UnconnectedState，交由上层决定是否重连
                    return;
                }
                continue;
            }

            bad_frame_count_ = 0;//解析出正常帧，时间窗内的异常计数清零
            bad_frame_timer_.invalidate();
            last_rx_.restart();//链路还活着，心跳超时判定重新计时

            //帧头里各字段解析好再给上层，省得上层我自己还要算偏移
            hdr.magic   = magic_;
            hdr.version = version_;
            hdr.tag     = tag_;
            if(has_type_)
                hdr.type = static_cast<quint8>(frame.at(type_offset_));

            const QByteArray body = frame.mid(head_len_, body_len);
            emit frame_received(hdr, body);
            emit body_received(body);
        }
    }
}
