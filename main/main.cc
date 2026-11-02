#include <QApplication>
#include <QTextStream>
#include <qapplication.h>
#include <qcontainerfwd.h>
#include <qdir.h>
#include <qobject.h>
#include <qtimer.h>
#include "csv-export.hh"

int main(int argc , char *argv[])
{
    QApplication app(argc,argv);
    auto csv = new app::Csv(&app);
    app::Csv::Config csv_cfg;
    csv_cfg.file_path  = QDir::homePath() + "/file_tmp_auto_clean/test.csv";
    csv_cfg.top_format = QStringLiteral("姓名,过程,分数,行数");
    csv->open(csv_cfg);
    csv->write(QStringLiteral("h-000h,perfect,500,%1").arg(csv->get_lines()));
    for(int i = 0; i < 500 ; i++)
    {
        csv->write(QStringLiteral("%1,%2,500,%3").arg(i).arg(QChar('a' + i)).arg(csv->get_lines()));
    }
    QObject::connect(csv, &app::Csv::log_err, &app, [](const QString &err)
    {
        QTextStream out(stderr);
        out << "err: " << err << "\n";
        out.flush();
    });
    QObject::connect(csv, &app::Csv::log_lines, &app, [](const QString &msg, qint32 lines)
    {
        QTextStream out(stdout);
        out << msg << "," << lines << "\n";
        out.flush();
    });
    QTimer::singleShot(5000, &app, &QCoreApplication::quit);
    return app.exec();
}
