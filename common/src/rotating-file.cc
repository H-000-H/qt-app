#include "rotating-file.hh"

#include <iterator>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace app::common
{
    namespace fs = std::filesystem;

    RotatingFile::RotatingFile(fs::path path, std::uintmax_t max_bytes)
        : path_(std::move(path)), max_bytes_(max_bytes)
    {
    }

    bool RotatingFile::open()
    {
        if (file_.is_open())
            return true;
        if (path_.empty())
            return false;

        file_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
        if (!file_.is_open())
            return false;

        file_.seekp(0, std::ios::end);
        written_ = static_cast<std::uintmax_t>(file_.tellp());
        return true;
    }

    void RotatingFile::close()
    {
        if (file_.is_open())
            file_.close();
    }

    bool RotatingFile::is_open() const
    {
        return file_.is_open();
    }

    void RotatingFile::set_path(fs::path path)
    {
        if (file_.is_open())
            file_.close();
        path_ = std::move(path);
    }

    void RotatingFile::set_limit(std::uintmax_t max_bytes)
    {
        max_bytes_ = max_bytes;
    }

    bool RotatingFile::trim()
    {
        file_.close();

        std::error_code ec;
        const auto size = fs::file_size(path_, ec);
        if (ec)
            return open();

        const std::uintmax_t keep = max_bytes_ / 2;
        if (size <= keep)
            return open();

        std::ifstream in(path_, std::ios::binary);
        if (!in)
            return false;

        in.seekg(static_cast<std::streamoff>(size - keep));
        std::string buf((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        in.close();

        const auto pos = buf.find('\n');
        if (pos != std::string::npos)
            buf.erase(0, pos + 1);

        fs::path tmp = path_;
        tmp += ".tmp";

        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
            if (!out)
                return false;
        }

        fs::rename(tmp, path_, ec);
        if (ec)
        {
            std::error_code ignored;
            fs::remove(tmp, ignored);
            return false;
        }

        return open();
    }

    bool RotatingFile::write(std::string_view data)
    {
        if (!open())
            return false;

        if (max_bytes_ > 0 && written_ + data.size() > max_bytes_)
        {
            if (!trim())
                return false;
        }

        file_.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file_)
            return false;

        written_ += data.size();
        return true;
    }

    bool RotatingFile::flush()
    {
        if (!file_.is_open())
            return false;

        file_.flush();
        if (!file_)
            return false;

        if (sync_on_write_.load(std::memory_order_relaxed))
            return sync_to_disk();
        return true;
    }

    bool RotatingFile::sync_to_disk()
    {
#if defined(_WIN32)
        HANDLE handle = CreateFileW(path_.wstring().c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return false;

        const BOOL ok = FlushFileBuffers(handle);
        CloseHandle(handle);
        return ok != FALSE;
#else
        const int fd = ::open(path_.c_str(), O_WRONLY | O_CLOEXEC);
        if (fd < 0)
            return false;

        const int rc = ::fsync(fd);
        ::close(fd);
        return rc == 0;
#endif
    }
}
