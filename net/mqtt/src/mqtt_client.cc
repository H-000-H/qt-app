#include "mqtt_client.hh"
#include <memory>
#include <qdatetime.h>
#include <qeventloop.h>
#include <qdebug.h>
#include <qminmax.h>
#include <quuid.h>

namespace app
{
    constexpr qsizetype kDefaultUuidLength = 8;
    constexpr int kStopTimeoutMs = 5000;

    namespace detail
    {
        /** @brief 把 QMqttClient::ClientError 转成日志用的短名 */
        QString mqtt_error_name(QMqttClient::ClientError err)
        {
            switch (err)
            {
            case QMqttClient::NoError:                return QStringLiteral("NoError");
            case QMqttClient::InvalidProtocolVersion: return QStringLiteral("InvalidProtocolVersion");
            case QMqttClient::IdRejected:             return QStringLiteral("IdRejected");
            case QMqttClient::ServerUnavailable:      return QStringLiteral("ServerUnavailable");
            case QMqttClient::BadUsernameOrPassword:  return QStringLiteral("BadUsernameOrPassword");
            case QMqttClient::NotAuthorized:          return QStringLiteral("NotAuthorized");
            case QMqttClient::TransportInvalid:       return QStringLiteral("TransportInvalid");
            case QMqttClient::ProtocolViolation:      return QStringLiteral("ProtocolViolation");
            case QMqttClient::UnknownError:           return QStringLiteral("UnknownError");
            case QMqttClient::Mqtt5SpecificError:     return QStringLiteral("Mqtt5SpecificError");
            }
            return QStringLiteral("UnknownCode");
        }
    }

    MqttClient::~MqttClient()
    {
        //析构前自动检查：已经走过 stop 流程（stop_done_）就什么都不用做。
        if (stop_done_)
            return;
        //没停过就析构：先断掉信号，再关底层连接 不然broker 会当异常掉线并立刻发布遗嘱
        //这里不发 disconnected_permanently 信号
        if (client_ && client_->state() != QMqttClient::Disconnected)
        {
            QObject::disconnect(client_.get(), nullptr, this, nullptr);
            client_->disconnectFromHost();
        }
    }

    MqttClient::MqttClient(QObject*parent):QObject(parent)
    {
        client_             = std::make_unique<QMqttClient>(this);
        init_reconnect_timer(this);//重连定时器由基类创建，到点回调 do_reconnect()
        init_timeout_timer(this);//建链超时定时器由基类创建，到点回调 on_timeout()
        stop_timer_         = std::make_unique<QTimer>(this);
        stop_timer_->setSingleShot(true);

        wire_client();

        //stop 超时保护：disconnectFromHost 后迟迟等不到 Disconnected（如半开连接），重建 client_ 关掉 socket 再收尾
        QObject::connect(   stop_timer_.get(),
                            &QTimer::timeout,
                            this,
                            [this]()
                            {
                                qWarning().noquote() << QStringLiteral("[mqtt] stop timeout, force disconnect");
                                delete_client_later();
                                finalize_stop(CloseReason::UserStop);
                            });
    }

    void MqttClient::wire_client()
    {
        QObject::connect(   client_.get(),
                            &QMqttClient::stateChanged,
                            this,
                            &MqttClient::on_state_changed);

        QObject::connect(   client_.get(),
                            &QMqttClient::errorChanged,
                            this,
                            &MqttClient::on_err);

        QObject::connect(   client_.get(),
                            &QMqttClient::messageReceived,
                            this,
                            &MqttClient::on_message);
    }

    void MqttClient::delete_client_later()
    {
        //disconnectFromHost 没能改变状态时，旧对象内部仍持有未关闭的 socket 和收发缓冲。
        //这里可能正处在它自己的信号回调里，直接析构会踩到已释放内存，交给 deleteLater；
        //订阅表 subscriptions_ 保留，下次连上由 resubscribe_all 重放
        if (QMqttClient *old = client_.release())
        {
            //先摘掉旧对象的信号：deleteLater 真正执行前它还可能发 stateChanged，
            QObject::disconnect(old, nullptr, this, nullptr);
            old->deleteLater();
        }
        client_ = std::make_unique<QMqttClient>(this);
        wire_client();
    }

    void MqttClient::resubscribe_all()
    {
        if (subscriptions_.isEmpty())
            return;

        qInfo().noquote() << QStringLiteral("[mqtt] resubscribing %1 topic(s)").arg(subscriptions_.size());
        for (auto it = subscriptions_.cbegin(); it != subscriptions_.cend(); ++it)
        {
            if (!client_->subscribe(it.key(), it.value()))
                qWarning().noquote() << "[mqtt] resubscribe failed" << it.key();
        }
    }

    bool MqttClient::is_connected()const
    {
        return client_->state() == QMqttClient::Connected;
    }

    quint64 MqttClient::resent_total()const
    {
        return resent_total_;
    }

    void MqttClient::flush_pending()
    {
        if (pending_.isEmpty())
            return;

        const QDateTime now = QDateTime::currentDateTime();
        qInfo().noquote() << QStringLiteral("[mqtt] flush start %1, %2 topic(s)")
                                 .arg(now.toString(Qt::ISODateWithMs))
                                 .arg(pending_.size());

        const qint64 total = pending_.size();
        qint64 sent = 0;
        for (auto it = pending_.begin(); it != pending_.end();)
        {
            const Pending &msg = it.value();
            if (client_->publish(msg.topic, msg.payload, msg.qos, msg.retain) < 0)
            {
                //发不出去就留着，下次连上再试，避免半途丢掉后面所有主题
                qWarning().noquote() << "[mqtt] flush failed, keep" << pending_.size() << "topic(s)";
                break;
            }
            qInfo().noquote() << QStringLiteral("[mqtt] resent %1 at %2, queued %3 (%4 ms ago), qos=%5 retain=%6")
                                     .arg(msg.topic)
                                     .arg(now.toString(Qt::ISODateWithMs))
                                     .arg(msg.queued_at.toString(Qt::ISODateWithMs))
                                     .arg(msg.queued_at.msecsTo(now))
                                     .arg(msg.qos)
                                     .arg(msg.retain);
            it = pending_.erase(it);
            resent_total_++;
            sent++;
        }
        qInfo().noquote() << QStringLiteral("[mqtt] resent %1/%2, cumulative = %3")
                                 .arg(sent).arg(total).arg(resent_total_);
    }

    void MqttClient::on_message(const QByteArray &payload,const QMqttTopicName &topic)
    {
        qDebug().noquote() << "[mqtt] recv" << topic.name() << "size =" << payload.size();
        emit message_received(topic.name(), payload);
    }

    bool MqttClient::start(const Config &cfg)
    {
        if(started_ == true)
        {
            qWarning().noquote()<<"have been started not repeat start";
            return false;
        }
        stop_timer_->stop();//先清洗stop计时器以防跳到stop
        cfg_ = cfg;
        if (cfg_.host.isEmpty())
        {
            qWarning().noquote() << "[mqtt] host is empty, start aborted";
            return false;
        }
        stopping_ = false;
        stop_done_ = false;
        reset_retry();
        if (cfg_.clientId.isEmpty())
        {
            cfg_.clientId = QStringLiteral("mqtt-client-%1")
                            .arg(QUuid::createUuid()
                            .toString()
                            .left(kDefaultUuidLength));
        }
        client_->setHostname(cfg_.host);
        client_->setPort(cfg_.port);
        client_->setUsername(cfg_.username);
        client_->setPassword(cfg_.password);
        client_->setCleanSession(cfg_.clean_session);
        client_->setClientId(cfg_.clientId);
        client_->setAutoKeepAlive(cfg_.auto_keep_alive);
        client_->setKeepAlive(cfg_.keep_alive);
        if (cfg_.will_msg.isEmpty())
        {
            client_->setWillTopic(QString());
            client_->setWillMessage(QByteArray());
        }
        else//遗嘱子主题拼上 prefix，和 publish/subscribe 的命名保持一致
        {
            client_->setWillTopic(full_topic(cfg_.will_subtopic));
            client_->setWillMessage(cfg_.will_msg);
            client_->setWillQoS(1);//直接1至少一次就行了不给动态设置遗嘱了一般都是qos = 1
            client_->setWillRetain(cfg_.will_retain);
        }
        //先置位：connectToHost 可能同步失败并立刻回到 Disconnected，
        //那时状态机需要知道"还在运行"才会继续重连或发终态
        started_ = true;
        client_->connectToHost();
        start_timeout(0);//建链超时：卡在 Connecting 时强制断开，回到 Disconnected 走统一重连
        return true;
    }

    void MqttClient::stop()
    {
        if (stopping_)
            return;
        stopping_ = true;
        reset_retry();
        cancel_reconnect();
        stop_timeout();//建链超时和挥手超时不是一回事，先掐掉前者

        if (client_->state() == QMqttClient::Disconnected)
        {
            finalize_stop(CloseReason::UserStop);//本来就没连，没有状态迁移可等，直接收尾
            started_ = false;
            return;
        }

        stop_timer_->start(kStopTimeoutMs);//超时保护：防止 disconnectFromHost 后状态不迁移导致收不到 disconnected_permanently
        client_->disconnectFromHost();//直接调用就行，状态迁移在 stateChanged 里处理
        started_ = false;
    }

    void MqttClient::on_err(QMqttClient::ClientError err)
    {
        //Qt 在重置错误状态时也会发射 errorChanged，不是真错误直接跳过
        if (err == QMqttClient::NoError)
            return;

        const QString msg = QStringLiteral("time :%1 , err_reason :%2")
                                .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                                .arg(detail::mqtt_error_name(err));
        qWarning().noquote()<<msg<<'\n';

        emit error_occurred(msg);
    }

    void MqttClient::finalize_stop(CloseReason reason)
    {
        if (stop_done_)
            return;
        stop_done_ = true;
        stop_timer_->stop();
        stop_timeout();
        started_ = false;//彻底停了，允许上层重新 start()
        emit disconnected_permanently(reason);
    }

    bool MqttClient::stop_and_wait(int timeout_ms)
    {
        if (client_->state() == QMqttClient::Disconnected)
        {
            stop();//可能还排着一次重连，必须走 stop 取消掉
            return true;
        }

        //先接管信号再调 stop()，避免收尾信号在 connect 之前就发出来
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(this, &MqttClient::disconnected_permanently, &loop, &QEventLoop::quit);

        stop();
        if (client_->state() == QMqttClient::Disconnected) return true;//disconnectFromHost 可能同步就完成

        guard.start(timeout_ms);
        loop.exec();
        return client_->state() == QMqttClient::Disconnected;
    }

    void MqttClient::on_state_changed(QMqttClient::ClientState state)
    {
        switch (state)
        {
        case QMqttClient::ClientState::Connected:
            reset_retry();
            stop_timeout();//已连上，建链超时不能再留着
            qInfo().noquote() << QStringLiteral("[mqtt] connected %1")
                                    .arg(QDateTime::currentDateTime()
                                    .toString(Qt::ISODate));
            resubscribe_all();//clean_session=true 时重连后订阅全丢，靠本地表重放
            flush_pending();//订阅恢复后再补发断线期间缓存的消息，避免对端漏收
            emit connected();
            break;

        case QMqttClient::ClientState::Disconnected:
            qWarning().noquote() << QStringLiteral("[mqtt] connection lost, retry = %1")
                                        .arg(retry_);
            stop_timeout();//掉线了，建链超时定时器一并停掉
            emit disconnected();

            if (stopping_)//主动 stop，不再重连
            {
                finalize_stop(CloseReason::UserStop);
                break;
            }
            if (NetBase::give_up(retry_, cfg_.max_retry, cfg_.reconnect_infinite))//重连次数耗尽
            {
                qWarning().noquote() << QStringLiteral("[mqtt] reconnect failed, give up");
                finalize_stop(CloseReason::NetworkLost);
                break;
            }
            schedule_reconnect(cfg_.max_retry, cfg_.reconnect_infinite, cfg_.reconnect_interval);
            break;

        case QMqttClient::ClientState::Connecting:
            qInfo().noquote() << QStringLiteral("[mqtt] connecting...");
            break;
        }
    }

    void MqttClient::do_reconnect()
    {
        qInfo().noquote() << QStringLiteral("[mqtt] reconnecting...");
        client_->connectToHost();
        start_timeout(0);//重连同样要建链超时
    }

    void MqttClient::on_timeout()
    {
        //QMqttClient 没有 abort，disconnectFromHost 在连接中会直接放弃本次握手并回到 Disconnected
        client_->disconnectFromHost();
    }

    QString MqttClient::full_topic(const QString &sub) const
    {
        QString prefix = cfg_.topic_prefix;
        while (prefix.endsWith('/'))
        {
            prefix.chop(1);
        }

        QString tail = sub;
        while (tail.startsWith('/'))
        {
            tail.remove(0, 1);
        }

        if (prefix.isEmpty())
            return tail;
        if (tail.isEmpty())
            return prefix;
        return prefix + '/' + tail;
    }

    bool MqttClient::publish_bytes(const QString &topic,const QByteArray &payload,quint8 qos,bool retain)
    {
        if(qos > 2)
        {
            qWarning().noquote() << "[mqtt] invalid qos, should be 0/1/2, got" << qos;
            return false;
        }

        if(!is_connected())
        {
            if(cfg_.max_pending == 0)
            {
                qWarning().noquote() << "[mqtt] not connected, drop" << topic;
                return false;
            }
            //同主题只留最新一条：retain 消息 broker 侧本来就只认最后一条，
            //非 retain 的状态类消息补发中间态也没意义，所以一律覆盖
            if(!pending_.contains(topic) && pending_.size() >= cfg_.max_pending)
            {
                //满了且是新主题：丢掉入队最早的那条
                auto oldest = pending_.begin();
                for(auto it = pending_.begin(); it != pending_.end(); ++it)
                {
                    if(it.value().queued_at < oldest.value().queued_at)
                        oldest = it;
                }
                qWarning().noquote() << "[mqtt] pending full, drop oldest" << oldest.key();
                pending_.erase(oldest);
            }
            const bool replaced = pending_.contains(topic);
            pending_.insert(topic, Pending{topic, payload, qos, retain, QDateTime::currentDateTime()});
            qInfo().noquote() << "[mqtt] not connected, queued" << topic
                              << (replaced ? QStringLiteral("(replaced)") : QStringLiteral("(new)"))
                              << "pending =" << pending_.size();
            return false;//没发到网上，调用方按失败处理；消息已进队列，重连后补发
        }

        const qint32 id = client_->publish(topic, payload, qos, retain);
        if(id < 0)
        {
            qWarning().noquote() << "[mqtt] publish failed, topic =" << topic
                                 << "time =" << QDateTime::currentDateTime().toString(Qt::ISODate);
            return false;
        }
        return true;
    }

    bool MqttClient::publish_sub_json(  const QString &subtopic,
                                    const QJsonObject &json_obj,
                                    quint8 qos ,
                                    bool retain ,
                                    QJsonDocument::JsonFormat style)
    {
        return publish_bytes(full_topic(subtopic),
                             QJsonDocument(json_obj).toJson(style),
                             qos,
                             retain);
    }

    bool MqttClient::publish_sub_data(const QString &subtopic,const QByteArray &payload,quint8 qos , bool retain)
    {
        return publish_bytes(full_topic(subtopic), payload, qos, retain);
    }

    bool MqttClient::publish_topic_data(const QString &topic,const QByteArray &payload,quint8 qos , bool retain)
    {
        return publish_bytes(topic, payload, qos, retain);
    }

    bool MqttClient::publish_topic_json(    const QString &topic,
                                            const QJsonObject &json_obj,
                                            quint8 qos ,
                                            bool retain ,
                                            QJsonDocument::JsonFormat style)
    {
        return publish_bytes(topic,
                             QJsonDocument(json_obj).toJson(style),
                             qos,
                             retain);
    }

    bool MqttClient::do_subscribe(const QString &topic,quint8 qos)
    {
        if(!is_connected())
        {
            qWarning().noquote() << "[mqtt] not connected, subscribe drop" << topic;
            return false;
        }
        if(qos > 2)
        {
            qWarning().noquote() << "[mqtt] invalid qos, should be 0/1/2, got" << qos;
            return false;
        }

        auto sub = client_->subscribe(topic, qos);
        if(!sub)
        {
            qWarning().noquote() << "[mqtt] subscribe failed" << topic;
            return false;
        }
        subscriptions_.insert(topic, qos);//记下供重连后重放，同一主题重复订阅以最后一次 qos 为准
        qInfo().noquote() << "[mqtt] subscribed" << topic;
        return true;
    }

    bool MqttClient::subscribe_sub(const QString &subtopic , quint8 qos)
    {
        return do_subscribe(full_topic(subtopic), qos);

    }

    bool MqttClient::subscribe_topic(const QString &topic , quint8 qos)
    {
        return do_subscribe(topic, qos);
    }
}
