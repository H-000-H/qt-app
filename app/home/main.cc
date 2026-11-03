#include "data-view.hh"
#include "data-test.hh"
#include <qapplication.h>
#include <qdatetime.h>
int main(int argc, char *argv[])
{
    QApplication app(argc,argv);
    app::Chart<app::SensorRecord>::Config cfg;
    cfg.title = "传感器记录";

    //  带配置构造：视图在构造里就开好了
    app::Chart<app::SensorRecord> data(cfg);

    app::SensorRecord demo_data;
    demo_data.hum =1.09;
    demo_data.status = "ok";
    demo_data.temp = 23.999;
    demo_data.timestamp = QDateTime::currentSecsSinceEpoch();
    data.write(demo_data);

    return app.exec();
}
