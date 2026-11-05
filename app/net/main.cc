#include "csv-export.hh"
#include "data-view.hh"
#include "data-test.hh"
#include "tcp_client.hh"
#include <cstdint>
#include <qapplication.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qobject.h>
#include <qstring.h>
#include <qstringview.h>
#include <qtimer.h>
int main(int argc, char *argv[])
{
    QApplication app(argc,argv);
    app::Chart<app::SensorRecord>::Config cfg;
    cfg.title = "传感器记录";

    //  带配置构造：视图在构造里就开好了
    app::Chart<app::SensorRecord> data(cfg);

    auto csv = new app::Csv();
    app::Csv::Config csv_cfg;
    csv_cfg.file_path = QDir::homePath() + "/file_tmp_auto_clean/ss.csv";
    csv_cfg.top_format = data.headers().join(cfg.separator);   // 表头：问模型要
    csv->open(csv_cfg);                                       // 开文件，表头落第一行

    app::TcpClient::Config tcp_cfg;
    tcp_cfg.enable_heart = true;
    tcp_cfg.heart_package = QByteArray::fromHex("00000000");
    tcp_cfg.host        = "127.0.0.1";
    tcp_cfg.port        = 9000;

    constexpr int kSendTotal = 100;

    auto client =  new app::TcpClient();
    auto send_timer = new QTimer(client);

    //sent 放在 main 作用域：lambda 只捕获引用，变量必须活得比定时器久
    int sent = 0;

    QObject::connect(client, &app::TcpClient::connected, client,
        [client, send_timer, &sent]()
        {
            qInfo().noquote() << QStringLiteral("[main] connected, start sending %1 frames")
                                    .arg(kSendTotal);
            sent = 0;
            send_timer->start(100);//100ms 一帧
        });

    //断线就停发，别对着空气刷 false
    QObject::connect(client, &app::TcpClient::disconnected, client,
        [send_timer]()
        {
            send_timer->stop();
        });

    //连不上且不会再重连（重试次数用尽），直接收工，免得要手动 Ctrl+C
    QObject::connect(client, &app::TcpClient::disconnected_permanently, client,
        [](app::TcpClient::CloseReason reason)
        {
            qWarning().noquote() << "[main] give up, reason =" << static_cast<int>(reason);
            QCoreApplication::quit();
        });

    QObject::connect(send_timer, &QTimer::timeout, client,
        [client, send_timer, &sent]()
        {
            QByteArray frame = QByteArray::fromHex("00000006") + "测试";
            if (client->send(frame))
                qInfo() << "sent" << ++sent;
            if (sent >= kSendTotal)
            {
                send_timer->stop();
                QTimer::singleShot(300, qApp, &QCoreApplication::quit);
            }
        });

    client->start(tcp_cfg);

    //每 2 秒往图表/CSV 落一条，不影响发送节奏
    auto timer = new QTimer;
    QObject::connect(timer, &QTimer::timeout, [&]()
        {
            app::SensorRecord demo_data;
            demo_data.hum = 1.09;
            demo_data.status = "ok";
            demo_data.temp = 23.999;
            demo_data.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);

            data.write(demo_data);

            QString line;
            if(data.export_last_line(line))
                csv->write(line);
        });
    timer->start(2000);

    return app.exec();
}
