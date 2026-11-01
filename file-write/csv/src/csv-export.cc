#include "csv-export.hh"
namespace app
{
    Csv::Csv(QIODevice::OpenMode flag,QString path ,bool is_open , QObject *parent):QObject(parent)
    {
        file_path_ = path;
        file_.setFileName(file_path_);
        if(is_open)
            on_open(flag);
        flag_ = flag;
        out_.setEncoding(QStringConverter::Utf8);
        out_.setGenerateByteOrderMark(true);

    }

    Csv::~Csv()
    {
        if(file_.isOpen())
            on_close();
    }
    bool Csv::set_file_path(QString new_path)
    {
        if(file_.fileName() == new_path)
        {
            emit log_err("地址相同无需更改"+file_.errorString());
            return false;
        }
        if(file_.isOpen())
        {
            out_.flush();
            file_.close();
            file_.setFileName(new_path);
            file_path_ = new_path;
        }
        on_open(flag_);

        return true;
    }

    bool Csv::set_top_format(const QString format)
    {
        if(!file_.isOpen())
        {
            emit log_err(__FILE__+file_.errorString()+"func Csv::set_top_format");
            return false;
        }
        // Append 模式下写入永远落在文件末尾，无法定位到开头
        if(flag_.testFlag(QIODevice::Append))
        {
            emit log_err(__FILE__+QStringLiteral(" Append 模式无法写表头 func Csv::set_top_format"));
            return false;
        }
        // 已有表头/数据时再定位到 0 会覆盖开头的内容
        if(lines_ != 0)
        {
            emit log_err(__FILE__+QStringLiteral(" 表头已存在 func Csv::set_top_format"));
            return false;
        }
        out_.flush();//先把流缓冲刷进设备，避免 seek 后位置错乱
        if(!out_.seek(0))//强制回到文件开头
        {
            emit log_err(__FILE__+file_.errorString()+"func Csv::set_top_format seek");
            return false;
        }
        out_ << format<<'\n';
        out_.flush();
        lines_++;
        emit log_lines(format, 1);
        return true;
    }

    bool Csv::on_close()
    {
        if(!file_.isOpen())
            return false;
        out_.flush();
        file_.close();
        return true;
    }
    bool Csv::on_open(QIODevice::OpenMode flag)
    {
        if(file_.isOpen())
            return true;
        if(!file_.open(flag))
        {
            emit log_err("CSV 打开失败: " + file_.errorString());
            return false;
        }
        out_.setDevice(&file_);//重新打开后重置流状态
        lines_ = 0;
        flag_ = flag;
        return true;
    }

    bool Csv::write(const QString content)
    {
        if(!file_.isOpen()||lines_ == 0)
        {
            emit log_err(__FILE__+file_.errorString()+"func Csv::write");
            return false;
        }
        out_<<content<<'\n';
        out_.flush();
        lines_++;
        return true;
    }
}
