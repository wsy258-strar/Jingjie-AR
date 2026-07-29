// ARServer 日志目录管理实现：只处理自身生成的普通日志文件，避免误删其他内容。
#include "LogDirectory.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
namespace {

void setError(std::string* error, const std::string& message)
{
    if (error) *error = message;
}

bool hasLogFileName(const char* name)
{
    const std::string filename(name);
    const std::string prefix("ar_server.");
    const std::string suffix(".log");
    return filename.size() > prefix.size() + suffix.size() &&
           filename.compare(0, prefix.size(), prefix) == 0 &&
           filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

bool ensureLogDirectory(const std::string& path, std::string* error)
{
    struct stat status;
    if (stat(path.c_str(), &status) == 0)
    {
        if (S_ISDIR(status.st_mode))
        {
            if (access(path.c_str(), W_OK | X_OK) == 0) return true;
            setError(error, "cannot write to " + path + ": " + std::strerror(errno));
            return false;
        }
        setError(error, path + " exists but is not a directory");
        return false;
    }
    if (errno != ENOENT)
    {
        setError(error, "cannot inspect " + path + ": " + std::strerror(errno));
        return false;
    }
    if (mkdir(path.c_str(), 0755) == 0) return true;
    setError(error, "cannot create " + path + ": " + std::strerror(errno));
    return false;
}

bool removeExpiredLogFiles(const std::string& path, int retentionDays, std::string* error)
{
    if (retentionDays == 0) return true;
    DIR* directory = opendir(path.c_str());
    if (!directory)
    {
        setError(error, "cannot open " + path + ": " + std::strerror(errno));
        return false;
    }

    const time_t cutoff = time(0) - static_cast<time_t>(retentionDays) * 24 * 60 * 60;
    struct dirent* entry = 0;
    while ((entry = readdir(directory)) != 0)
    {
        if (!hasLogFileName(entry->d_name)) continue;
        const std::string filePath = path + "/" + entry->d_name;
        struct stat status;
        if (lstat(filePath.c_str(), &status) != 0)
        {
            closedir(directory);
            setError(error, "cannot inspect " + filePath + ": " + std::strerror(errno));
            return false;
        }
        if (!S_ISREG(status.st_mode) || status.st_mtime >= cutoff) continue;
        if (unlink(filePath.c_str()) != 0)
        {
            closedir(directory);
            setError(error, "cannot remove " + filePath + ": " + std::strerror(errno));
            return false;
        }
    }
    closedir(directory);
    return true;
}
