#pragma once
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QTextStream>
namespace app
{
    /**
     * @brief 按行写 CSV 文件
     * @details 字段分隔由调用方自己拼好，本类只负责换行、表头和行数统计
     */
    class Csv : public QObject
    {
        Q_OBJECT
    public:
        explicit Csv(QObject *parent = nullptr);
        ~Csv() override;

        /** @brief 打开文件所需的参数 */
        struct Config
        {
            /** @brief 文件路径，含文件名 */
            QString file_path = QDir::homePath();
            /** @brief 打开模式，Append 模式下不能写表头 */
            QIODevice::OpenMode open_mode = QIODevice::WriteOnly | QIODevice::Text;
            /** @brief 表头，非空则 open 成功后写在第一行 */
            QString top_format;
        };

        /**
         * @brief 按配置打开文件，表头非空时一并写入
         * @param[in] cfg 文件路径、打开模式和表头
         * @return false 表示文件已打开、路径为空或打开失败
         */
        bool open(const Config &cfg);

        /**
         * @brief 关闭文件
         * @return 重复调用返回 true
         */
        bool close();

        /**
         * @brief 换一个文件继续写
         * @param[in] new_path 新的文件路径
         * @return false 表示路径为空或重开失败
         * @details 原来打开着就先关，再用当前配置重开；原来没打开就只记下路径
         */
        bool set_file_path(const QString &new_path);

        /**
         * @brief 写一行数据
         * @param[in] content 一整行内容，分隔符和列数由调用方保证
         * @return false 表示文件未打开或写失败
         */
        bool write(const QString &content);

        /** @brief 已写入行数，表头也算一行 */
        qint32 get_lines() const;

    signals:
        /** @brief 出错信息，可直接展示 */
        void log_err(const QString &err);
        /** @brief 带行号的日志 */
        void log_lines(const QString &msg, qint32 lines);

    private:
        /**
         * @brief 把表头写到文件开头
         * @param[in] format 表头内容
         * @return false 表示 Append 模式、表头已存在或定位失败
         */
        bool write_top_format(const QString &format);

        Config cfg_;
        QFile file_;
        /** @brief 已写入行数 */
        qint32 lines_ = 0;
        /** @brief 始终绑定 file_ 的文本流 */
        QTextStream out_{&file_};
    };
}
