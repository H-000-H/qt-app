#pragma once

#include <qdir.h>
namespace app::log
{
    /** @brief 日志级别，低于当前最低级别的消息直接丢弃 */
    enum class Level : int
    {
        Debug = 0,
        Info,
        Warning,
        Critical,
        Fatal
    };

    /** @brief 默认最低输出级别 */
    inline constexpr Level  kMinLevel       = Level::Debug;
    /** @brief 单个日志文件大小上限（字节），0 表示不限制 */
    inline constexpr qint64 kMaxFileSize = 5 * 1024 * 1024;

    /** @brief 是否打印时间 HH:mm:ss.zzz */
    inline constexpr bool kWithTime = true;
    /** @brief 是否打印日志级别 DBG/INF/WRN/CRT/FTL */
    inline constexpr bool kWithType = true;
    /** @brief 是否打印进程 id */
    inline constexpr bool kWithPid = true;
    /** @brief 是否打印线程 id（十六进制） */
    inline constexpr bool kWithThread = true;
    /** @brief 是否打印日志分类，默认分类 "default" 不打印 */
    inline constexpr bool kWithCategory = true;
    /** @brief 是否打印文件夹/文件名 */
    inline constexpr bool kWithFile = true;
    /** @brief 文件路径是否只保留「文件夹/文件名」，false 则输出完整路径 */
    inline constexpr bool kShortFile = true;
    /** @brief 是否打印行号，与文件独立开关 */
    inline constexpr bool kWithLine = true;
    /** @brief 是否打印函数名，通常较长，默认关闭 */
    inline constexpr bool kWithFunction = false;
    /** @brief 控制lock 时间 一般 关闭 不然锁影响并发 true 则全程时间准确点 */
    inline constexpr bool kLongLock = false;
    /** @brief 控制日志注册等级 一般只是失败警告 开启日志 注册失败直接死 */
    inline constexpr bool kCriticLog = false;
    /** @brief 打开日志输出系统 */
    inline constexpr bool kOpenLogOutput = false;
    /** 
     *@brief 设置打印日志的路径 默认和可执行文件一个目录可以自己设置
     */
    void set_log_file_name(QString log_path);
    /**
     * @brief 设置最低输出级别，运行期生效；Fatal 始终输出
     */
    void set_log_level(Level level);
    /**
     * @brief 设置单文件大小上限
     * @param max_bytes <=0 表示不限制
     */
    void set_log_limit(qint64 max_bytes);

    /**
     * @brief 开关原子写入：开启后每行写完后 fsync 落盘
     */
    void set_log_sync(bool on);
    /**
     * @brief 安装全局日志处理器，进程内调用一次即可
     * @details 之后全项目所有 qDebug/qInfo/qWarning/qCritical 输出都会带前缀
     */
    void install();
}
