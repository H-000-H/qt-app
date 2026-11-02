#include "csv-export.hh"
namespace app
{
    Csv::Csv(QObject *parent):QObject(parent)
    {
        out_.setEncoding(QStringConverter::Utf8);
        out_.setGenerateByteOrderMark(true);
    }

    Csv::~Csv()
    {
        if(file_.isOpen())
            close();
    }

    bool Csv::open(const Config &cfg)
    {
        if(file_.isOpen())
        {
            emit log_err(QStringLiteral("文件已打开: ") + file_.fileName());
            return false;
        }
        if(cfg.file_path.isEmpty())
        {
            emit log_err(QStringLiteral("文件路径为空"));
            return false;
        }

        cfg_ = cfg;
        file_.setFileName(cfg_.file_path);
        if(!file_.open(cfg_.open_mode))
        {
            emit log_err(QStringLiteral("CSV 打开失败: ")
                         + cfg_.file_path + ' ' + file_.errorString());
            return false;
        }

        out_.setDevice(&file_);//重新打开后重置流状态
        lines_ = 0;

        if(!cfg_.top_format.isEmpty() && !write_top_format(cfg_.top_format))
        {
            close();//表头写不进去，这个文件就别留着了
            return false;
        }
        return true;
    }

    bool Csv::close()
    {
        if(!file_.isOpen())
            return true;

        out_.flush();
        file_.close();
        return true;
    }

    bool Csv::set_file_path(const QString &new_path)
    {
        if(new_path.isEmpty())
        {
            emit log_err(QStringLiteral("文件路径为空"));
            return false;
        }
        if(file_.isOpen() && file_.fileName() == new_path)
        {
            emit log_err(QStringLiteral("路径相同无需更改: ") + new_path);
            return false;
        }

        const bool was_open = file_.isOpen();
        if(was_open)
            close();//先把旧文件刷盘，切换过程中不丢数据

        cfg_.file_path = new_path;

        //没打开时只记路径，等上层调 open()
        return was_open ? open(cfg_) : true;
    }

    bool Csv::write_top_format(const QString &format)
    {
        //Append 模式下写入永远落在文件末尾，定位不到开头
        if(cfg_.open_mode.testFlag(QIODevice::Append))
        {
            emit log_err(QStringLiteral("Append 模式无法写表头"));
            return false;
        }
        if(lines_ != 0)
        {
            emit log_err(QStringLiteral("表头已存在"));
            return false;
        }

        out_.flush();//先把流缓冲刷进设备，避免 seek 后位置错乱
        if(!out_.seek(0))
        {
            emit log_err(QStringLiteral("定位到文件开头失败: ") + file_.errorString());
            return false;
        }

        out_ << format << '\n';
        out_.flush();
        ++lines_;
        emit log_lines(format, 1);
        return true;
    }

    bool Csv::write(const QString &content)
    {
        if(!file_.isOpen())
        {
            emit log_err(QStringLiteral("文件未打开: ") + cfg_.file_path);
            return false;
        }

        out_ << content << '\n';
        out_.flush();
        ++lines_;
        return true;
    }

    qint32 Csv::get_lines() const
    {
        return lines_;
    }
}
