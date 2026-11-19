#include "log.hh"
#include "rotating-file.hh"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <QtLogging>
#include <QMutex>
#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qstring.h>
#include <qthread.h>
#include <string>
#ifdef _WIN32
#include <windows.h>
std::string getExePath()
{
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    // 宽字符转utf8
    int len = WideCharToMultiByte(CP_UTF8,0,buf,-1,nullptr,0,nullptr,nullptr);
    std::string res(len,0);
    WideCharToMultiByte(CP_UTF8,0,buf,-1,&res[0],len,nullptr,nullptr);
    return res;
}
#elif __linux__
#include <unistd.h>
std::string getExePath()
{
    char buf[1024]{};
    auto r = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    return r>0 ? std::string(buf,r) : "";
}
#elif __APPLE__
#include <mach-o/dyld.h>
std::string getExePath()
{
    char buf[1024];
    uint32_t sz = sizeof(buf);
    _NSGetExecutablePath(buf, &sz);
    return std::string(buf);
}
#else
std::string getExePath()
{
    return {};
}
#endif

namespace app::log
{
    QString  g_log_file_path = " "; 
    /** @brief 多线程可能同时打日志，整条消息拼完并一次写出，避免内容交错 */
    static QMutex      g_logMutex;
    static common::RotatingFile g_file;
    static bool        g_file_failed = false;

    /** @brief QString 路径转 std::filesystem::path，Windows 下保留 Unicode */
    static std::filesystem::path to_path(const QString &path)
    {
#if defined(_WIN32)
        return std::filesystem::path(path.toStdU16String());
#else
        return std::filesystem::path(path.toStdString());
#endif
    }

    static std::atomic<int> g_min_level{static_cast<int>(kMinLevel)};

    static const char *type_tag(QtMsgType type) noexcept
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
     * @brief 组装一行日志文本：时间 + 级别 + 进程/线程 + 分类 + 文件:行号 + 函数 + 内容
     * @details 每一段都由 log.hh 里的 constexpr 开关单独控制
     */
    static QString format_message(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
    {
        QString out;

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
                if (ctx.line > 0)
                {
                    if (had_file)
                        out += QLatin1Char(':');
                    out += QString::number(ctx.line);
                }
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
        return out;
    }

    /** @brief QtMsgType 对应的级别序号，用于过滤 */
    static int level_rank(QtMsgType type) noexcept
    {
        switch (type)
        {
        case QtDebugMsg:    return static_cast<int>(Level::Debug);
        case QtInfoMsg:     return static_cast<int>(Level::Info);
        case QtWarningMsg:  return static_cast<int>(Level::Warning);
        case QtCriticalMsg: return static_cast<int>(Level::Critical);
        case QtFatalMsg:    return static_cast<int>(Level::Fatal);
        }
        return static_cast<int>(Level::Fatal);
    }

    /** @brief 写出到 stderr 与文件；*/
    static void write_line(const QString &out) noexcept
    {
        const QByteArray local = out.toLocal8Bit();
        std::fputs(local.constData(), stderr);

        if constexpr (kOpenLogOutput)
        {
            if (!g_file_failed)
            {
                const QByteArray data = out.toUtf8();
                if (!g_file.write(std::string_view(data.constData(),
                                                   static_cast<std::size_t>(data.size()))))
                {
                    g_file_failed = true;
                    std::fputs("log record fail\n", stderr);
                    if constexpr (kCriticLog)
                        std::abort();
                    return;
                }
                g_file.flush();
            }
        }
    }

    /**
     * @brief 全局日志处理器
     * @note 处理器内部禁止再调用 qDebug/qWarning 等，否则会递归进入自身
     */
    void message_handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) noexcept
    {
        if (type != QtFatalMsg
            && level_rank(type) < g_min_level.load(std::memory_order_relaxed))
            return;

        if constexpr (kLongLock)
        {
            QMutexLocker locker(&g_logMutex);
            write_line(format_message(type, ctx, msg));
        }
        else
        {
            const QString out = format_message(type, ctx, msg);
            QMutexLocker locker(&g_logMutex);
            write_line(out);
        }
    }
    void set_log_file_name(QString log_path)
    {
        QMutexLocker locker(&g_logMutex);
        g_file_failed   = false;
        g_log_file_path = std::move(log_path);
        g_file.set_path(to_path(g_log_file_path));
    }

    void set_log_level(Level level)
    {
        g_min_level.store(static_cast<int>(level), std::memory_order_relaxed);
    }

    void set_log_limit(qint64 max_bytes)
    {
        QMutexLocker locker(&g_logMutex);
        g_file.set_limit(max_bytes < 0 ? 0 : static_cast<std::uintmax_t>(max_bytes));
    }

    void set_log_sync(bool on)
    {
        g_file.set_sync_on_write(on);
    }
}

namespace app::log
{
    void install()
    {
        if constexpr (kOpenLogOutput)
        {
            if (g_log_file_path.trimmed().isEmpty())
            {
                const QString exe = QString::fromStdString(getExePath());
                if (exe.isEmpty())
                    g_log_file_path = QDir::currentPath() + QStringLiteral("/app.log");
                else
                {
                    const QFileInfo info(exe);
                    g_log_file_path = info.absolutePath() + QLatin1Char('/')
                                 + info.completeBaseName() + QStringLiteral(".log");
                }
            }

            g_file.set_limit(static_cast<std::uintmax_t>(kMaxFileSize));
            g_file.set_path(to_path(g_log_file_path));
        }
        qInstallMessageHandler(app::log::message_handler);
    }
}
