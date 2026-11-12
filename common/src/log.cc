#include "log.hh"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qlogging.h>
#include <qstring.h>
#include <qthread.h>

namespace app::log
{
    /** @brief 多线程可能同时打日志，整条消息拼完并一次写出，避免内容交错 */
    std::mutex g_log_mutex;

    const char *type_tag(QtMsgType type)
    {
        switch (type)
        {
        case QtDebugMsg:    return "DBG";
        case QtInfoMsg:     return "INF";
        case QtWarningMsg:  return "WRN";
        case QtCriticalMsg: return "CRT";
        case QtFatalMsg:    return "FTL";
        }
        return "???";
    }

    /**
     * @brief 全局日志处理器：时间 + 级别 + 进程/线程 + 分类 + 文件:行号 + 函数 + 内容
     * @details 每一段都由 log.hh 里的 constexpr 开关单独控制
     */
    void message_handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
    {

        QString out;

        if constexpr (log::kLongLock) 
            std::lock_guard<std::mutex> lock(g_log_mutex);
        
        if constexpr (log::kWithTime)
        {
            out += QLatin1Char('[');
            out += QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
            out += QLatin1String("] ");
        }

        if constexpr (log::kWithType)
        {
            out += QLatin1Char('[');
            out += QLatin1String(type_tag(type));
            out += QLatin1String("] ");
        }

        if constexpr (log::kWithPid || log::kWithThread)
        {
            out += QLatin1Char('[');
            if constexpr (log::kWithPid)
            {
                out += QLatin1String("pid=");
                out += QString::number(QCoreApplication::applicationPid());
            }
            if constexpr (log::kWithPid && log::kWithThread)
                out += QLatin1Char(' ');
            if constexpr (log::kWithThread)
            {
                out += QLatin1String("tid=");
                out += QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
            }
            out += QLatin1String("] ");
        }

        if constexpr (log::kWithCategory)
        {
            // 默认分类太吵，直接跳过
            if (ctx.category != nullptr && std::strcmp(ctx.category, "default") != 0)
            {
                out += QLatin1Char('[');
                out += QLatin1String(ctx.category);
                out += QLatin1String("] ");
            }
        }

        if constexpr (log::kWithFile || log::kWithLine)
        {
            bool had_file = false;

            if constexpr (log::kWithFile)
            {
                if (ctx.file != nullptr)
                {
                    // Windows 的 '\' 先统一成 '/'，后面按 '/' 切分才通用用qt自包装的切不同路径
                    QString file = QDir::fromNativeSeparators(QString::fromUtf8(ctx.file));
                    if constexpr (log::kShortFile)
                    {
                        const int last = file.lastIndexOf(QLatin1Char('/'));
                        const int prev = last > 0 ? file.lastIndexOf(QLatin1Char('/'), last - 1) : -1;
                        file = file.mid(prev + 1);
                    }
                    out += file;
                    had_file = true;
                }
            }

            if constexpr (log::kWithLine)
            {
                if (had_file)
                    out += QLatin1Char(':');
                out += QString::number(ctx.line);
            }

            out += QLatin1Char(' ');
        }

        if constexpr (log::kWithFunction)
        {
            if (ctx.function != nullptr)
            {
                out += QLatin1Char('[');
                out += QLatin1String(ctx.function);
                out += QLatin1String("] ");
            }
        }

        out += msg;
        out += QLatin1Char('\n');

        const QByteArray local = out.toLocal8Bit();
        
        /*短暂持锁  写入极端并发下可能出现:时间早的日志比时间晚的先打印
        (一般不影响纠错时间岔开点影响不大为了时间吃并发划不来)*/
        if constexpr (!log::kLongLock) 
            std::lock_guard<std::mutex> lock(g_log_mutex);
        std::fputs(local.constData(), stderr);
    
    }
}

namespace app::log
{
    void install()
    {
        qInstallMessageHandler(app::log::message_handler);
    }
}
