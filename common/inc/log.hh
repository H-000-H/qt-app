#pragma once

namespace app::log
{
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
    /**
     * @brief 安装全局日志处理器，进程内调用一次即可
     * @details 之后全项目所有 qDebug/qInfo/qWarning/qCritical 输出都会带前缀
     */
    void install();
}
