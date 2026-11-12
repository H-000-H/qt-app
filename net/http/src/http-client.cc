#include "http-client.hh"
#include <qdebug.h>
#include <qhttpheaders.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qlogging.h>
#include <qnetworkaccessmanager.h>
#include <qnetworkreply.h>
#include <qnetworkrequest.h>
#include <qobject.h>
#include <qsslerror.h>
#include <qtmetamacros.h>
#include <qurl.h>
namespace app
{
    HttpClient::HttpClient(QObject *parent):QObject(parent)
    {
        client_ = std::make_unique<QNetworkAccessManager>(this);
        req_    = std::make_unique<QNetworkRequest >();
        wire_client();
    }

    HttpClient::~HttpClient()
    {
        QObject::disconnect(client_.get(), nullptr, this, nullptr);
        stop();
    }

    void HttpClient::wire_client()
    {
        QObject::connect(client_.get(), &QNetworkAccessManager::finished,
                         this, &HttpClient::on_finished);

        QObject::connect(client_.get(),&QNetworkAccessManager::sslErrors,
                        this,[this](QNetworkReply *reply, const QList<QSslError> &errors)
        {
            Q_UNUSED(reply);
            for (const QSslError &err : errors)
                qWarning().noquote() << log_tag() << err.errorString();
            emit errored(QString(log_tag()) + " ssl error");
        });
    }

    void HttpClient::on_finished(QNetworkReply *reply)
    {
        if(reply==nullptr)
            return;

        inflight_.remove(reply);

        // 主动 stop 取消的请求不算错误，也不进历史
        if (reply->error() == QNetworkReply::OperationCanceledError)
        {
            reply->deleteLater();
            return;
        }

        Reply item;
        item.code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        item.ok   = (reply->error() == QNetworkReply::NoError);
        item.data = reply->readAll();
        item.len  = static_cast<int>(item.data.size());

        if (item.ok)
        {
            qDebug() << "http ok, code:" << item.code;
            qDebug() << "resp:" << item.data;
        }

        /** @brief 只保留最近 recv_hold_buf_size 条，超出丢弃最旧的 */
        const int cap = cfg_.recv_hold_buf_size > 0 ? cfg_.recv_hold_buf_size : 1;
        replies_.push_back(item);
        while (replies_.size() > cap)
            replies_.removeFirst();

        if (item.ok)
            emit finished(item.data);
        else
            emit errored(QString(log_tag()) + " error");

        reply->deleteLater();
    }

    bool HttpClient::open(const Config &cfg)
    {
        cfg_ = cfg;

        if (cfg_.host.isEmpty())
        {
            qWarning().noquote() << log_tag() << "invalid endpoint" << cfg_.host << cfg_.port;
            return false;
        }

        QUrl url;
        url.setScheme(cfg_.tls ? QStringLiteral("https") : QStringLiteral("http"));
        url.setHost(cfg_.host);
        if (cfg_.port > 0)
            url.setPort(cfg_.port);

        *req_ = QNetworkRequest(url);
        return true;
    }

    bool HttpClient::post_json(QJsonArray &data_json,QJsonDocument::JsonFormat style)
    {
        const QByteArray body = QJsonDocument(data_json).toJson(style);
        QNetworkRequest req(*req_);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *reply = client_->post(req, body);
        if(!reply)
        {
            qWarning().noquote() << log_tag() << "post failed";
            return false;
        }
        inflight_.insert(reply);
        return true;
    }

    bool HttpClient::post_bytes(const QByteArray &data)
    {
        QNetworkRequest req(*req_);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        QNetworkReply *reply = client_->post(req, data);
        if(reply==nullptr)
            return false;
        inflight_.insert(reply);
        return true;
    }

    bool HttpClient::post_text(const QString &text)
    {
        QNetworkRequest req(*req_);
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("text/plain; charset=utf-8"));
        QNetworkReply *reply = client_->post(req, text.toUtf8());
        if(reply==nullptr)
            return false;
        inflight_.insert(reply);
        return true;
    }

    bool HttpClient::stop()
    {
        // abort 会同步触发 finished，先拷贝一份避免遍历时集合被修改
        const auto pending = inflight_;
        inflight_.clear();
        for (QNetworkReply *reply : pending)
            reply->abort();

        qInfo().noquote() << log_tag() << "stopped";
        return true;
    }

    bool HttpClient::peek(QByteArray &data, int &len, int index)
    {
        if (replies_.isEmpty())
        {
            qWarning().noquote() << log_tag() << QString("no response yet :%1").arg(__FILE__);
            return false;
        }
        /** @brief 负数从最新一条往前数：-1 最新，-2 次新；非负按 0=最旧 取 */
        if (index < 0)
            index += replies_.size();
        if (index < 0 || index >= replies_.size())
        {
            qWarning().noquote() << log_tag() << QString("index out of range :%1").arg(index);
            return false;
        }
        const Reply &item = replies_.at(index);
        data = item.data;
        len  = item.len;
        if (data.isEmpty())
        {
            qInfo().noquote()<< log_tag()<< "not receive data";
        }
        return true;
    }
}