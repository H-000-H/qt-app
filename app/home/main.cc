#include "csv-export.hh"
#include "data-view.hh"
#include "data-test.hh"
#include <qapplication.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qobject.h>
#include <qstring.h>
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
    csv_cfg.file_path = QDir::homePath() + "/file_tmp_auto_clean.ss.csv";
    csv_cfg.top_format = data.headers().join(cfg.separator);   // 表头：问模型要
    csv->open(csv_cfg);                                       // 开文件，表头落第一行

    auto timer = new QTimer;
    QObject::connect(timer, &QTimer::timeout, [&]
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

            demo_data.hum = 2.3232;
            demo_data.status = "disabled";
            demo_data.temp = 444455.32;
            demo_data.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);

            data.write(demo_data);

            if(data.export_last_line(line))
                csv->write(line);
        });
    timer->start(2000);
    QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
    return app.exec();
}
