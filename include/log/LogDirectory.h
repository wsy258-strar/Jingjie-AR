// ARServer 日志目录管理：创建落盘目录并在启动阶段清理过期的本服务日志文件。
#pragma once

#include <string>

// 确保 path 存在且为目录；失败时将原因写入 error。
bool ensureLogDirectory(const std::string& path, std::string* error);

// 删除 path 中超过保留期的 ar_server.*.log 普通文件；保留天数为 0 时不删除。
bool removeExpiredLogFiles(const std::string& path, int retentionDays, std::string* error);
