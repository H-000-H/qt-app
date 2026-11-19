#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace app::common
{
    /**
     * @brief 带大小上限的追加写入器（纯 C++，无 Qt 依赖）
     * @details 始终写入同一文件；超过上限时丢弃文件头部的旧内容，保留最近的日志，
     *          并按行边界截断，避免留下半行
     * @note 非线程安全，多线程使用需由调用方加锁
     */
    class RotatingFile
    {
    public:
        RotatingFile() = default;
        explicit RotatingFile(std::filesystem::path path,
                              std::uintmax_t max_bytes = 5u * 1024u * 1024u);

        /**
         * @brief 以追加方式打开文件；未设置路径时返回 false
         */
        bool open();
        void close();
        bool is_open() const;

        /**
         * @brief 写入一段字节；写入前检查上限，超出先截掉头部旧内容
         * @retval false 表示打开或写入失败
         */
        bool write(std::string_view data);
        bool flush();

        void set_path(std::filesystem::path path);
        void set_limit(std::uintmax_t max_bytes);

        /**
         * @brief 开关原子写入：开启后每次 flush() 会调用 fsync/FlushFileBuffers 
         * @note 可跨线程随时切换
         */
        void set_sync_on_write(bool on) { sync_on_write_.store(on, std::memory_order_relaxed); }
        bool sync_on_write() const { return sync_on_write_.load(std::memory_order_relaxed); }

        const std::filesystem::path &path() const { return path_; }
        std::uintmax_t               max_bytes() const { return max_bytes_; }

    private:
        bool trim();
        bool sync_to_disk();

        std::ofstream         file_;
        std::filesystem::path path_;
        std::uintmax_t        max_bytes_ = 5u * 1024u * 1024u;
        std::uintmax_t        written_   = 0;
        std::atomic<bool>     sync_on_write_{false};
    };
}
