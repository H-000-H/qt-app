#pragma once
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QTextStream>
namespace  app
{
    class Csv : public QObject
    {
        Q_OBJECT
    public:
        explicit Csv(   QIODevice::OpenMode flag = QIODevice::WriteOnly|QIODevice::Text ,
                        QString path =QDir::homePath(),
                        bool is_open = true ,
                        QObject *parent = nullptr);
        ~Csv();
        bool set_file_path(QString new_path);

        /**
         * @brief 传递一个qstring的字符串然后 , 是分隔 \n是结束 显式调用不内嵌触发自己写的slots可以跳过但是本类里面都需要写
         * @param format[in] 传递的字符串
         * @details num , type , class '\n' |num|type|class|    换行
         * @note 在开头后用一次就行了不能动态改
         */
        bool set_top_format(const QString format);

        /**
         * @brief 直接写入数据的函数一般不用一般都是信号了connect触发
         * @param content 写入数据
         */
        bool write(const QString context);

        qint32 get_lines() const{return lines_;}
    public slots:
        /**
         * @brief 打开文件
         * @param [in] 写入文件的权限和写入的方法
         */
        bool on_open(QIODevice::OpenMode flag);
        bool on_close();
        
    signals:
        void log_err(const QString &err);
        void log_line(const QString &msg);
        void log_lines(const QString &msg , qint32 lines);
    private:
        QString file_path_ = QDir::homePath();
        QFile file_;
        qint32 lines_ = 0;//行数
        QIODevice::OpenMode flag_ = QIODevice::WriteOnly | QIODevice::Text;
        QTextStream out_{&file_};//始终绑定 file_ 的文本流
    };
}
