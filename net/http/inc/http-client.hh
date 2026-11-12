#include "net-base.hh"
#include <QObject>
#include <memory>
#include <QJsonArray>
#include <QSet>
#include <QVector>
#include <qjsondocument.h>
#include <qnetworkaccessmanager.h>
#include <qobject.h>
#include <qtmetamacros.h>
/**
 *@details
    - app::HttpClient http;
    - app::HttpClient::Config cfg;
    - cfg.host = "api.test.com";
    - cfg.tls  = false;
    - http.open(cfg);            // 切测试环境，明文

    - cfg.host = "api.prod.com";
    - cfg.tls  = true;
    - http.open(cfg);            // 切生产，TLS
    - 默认就这样用没有单独set dns 和 tls
*/
namespace app
{
    class HttpClient : public QObject , public NetBase
    {
    public:
        struct Config :public NetConfig
        {
            bool tls = false;
            int  recv_hold_buf_size =10;
        };
        explicit HttpClient(QObject *parent = nullptr);
        ~HttpClient();
        bool open(const Config &cfg);
        bool stop();

        bool post_json(QJsonArray &data_json,
            QJsonDocument::JsonFormat style = QJsonDocument::Indented);

        bool post_bytes(const QByteArray &data);

        /** @brief 发送 UTF-8 纯文本，Content-Type: text/plain */
        bool post_text(const QString &text);

        bool peek(QByteArray &data, int &len, int index = -1);
    signals:
        void errored(const QString &msg);
        void finished(const QByteArray &msg);
    protected:
        /** @brief 重连定时器到点：http 是短连接所以本地直接空实现 */
        void do_reconnect() override{};

        /**@brief 同理直接空实现 */
        void on_timeout() override{};
        /** @brief 共用调度日志的协议前缀 */
        const char *log_tag() const override { return cfg_.tls==false?"http":"https"; }
    private slots:
        void on_finished(QNetworkReply *reply);
    private:
        /** @brief 一条已完成的响应，由 replies_ 保存最近几条 */
        struct Reply
        {
            QByteArray data;             /* 已读出的响应体 */
            int        len  = 0;         /* 响应体字节数 */
            int        code = 0;         /* HTTP 状态码 */
            bool       ok   = false;     /* 是否无传输错误 */
        };
        void wire_client();
        std::unique_ptr<QNetworkAccessManager>  client_;
        std::unique_ptr<QNetworkRequest >       req_;
        /** @brief 最近几条响应，容量由 cfg_.recv_hold_buf_size 控制 */
        QVector<Reply>                          replies_;
        /** @brief 在途请求，stop()/析构时统一取消 */
        QSet<QNetworkReply *>                   inflight_;
        Config                                  cfg_;
    };
}
