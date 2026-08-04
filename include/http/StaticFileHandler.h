// 静态文件处理器：校验请求路径、条件缓存头并选择内存响应或异步文件发送。
#pragma once

#include <http/HttpRequest.h>
#include <http/HttpResponse.h>
#include <http/StaticFileCache.h>

#include <functional>
#include <time.h>

class TaskWorkerPool;
class AsyncResponder;

class StaticFileHandler
{
public:
    /// FilePlan 只保存元数据与状态，避免在 I/O 线程同步读取大文件内容。
    struct FilePlan {
        FilePlan()
            : status(HttpResponse::k200Ok), lastModified(0), fileSize(0), fileOffset(0),
              responseSize(0), partial(false), isFile(false) {}
        HttpResponse::HttpStatusCode status;
        std::string path;
        std::string etag;
        time_t lastModified;
        size_t fileSize;
        size_t fileOffset;
        size_t responseSize;
        bool partial;
        bool isFile;
    };
    typedef std::function<bool(const std::string&, CachedFileEntry&)> CacheGet;
    typedef std::function<void(const std::string&, const CachedFileEntry&)> CachePut;
    StaticFileHandler(const std::string& root, const CacheGet& get, const CachePut& put,
                      TaskWorkerPool* workers, size_t largeFileThreshold);
    bool handle(const HttpRequest& request, HttpResponse* response) const;
    bool prepare(const HttpRequest& request, FilePlan* plan) const;
    void handleAsync(const HttpRequest& request, const AsyncResponder& responder) const;
private:
    static std::string mime(const std::string& path);
    static bool prepareInRoot(const std::string& root, const HttpRequest& request, FilePlan* plan);
    static void populateResponse(const HttpRequest& request, const FilePlan& plan,
                                 const CacheGet& get, const CachePut& put,
                                 size_t threshold, HttpResponse* response);
    std::string root_;
    CacheGet get_;
    CachePut put_;
    TaskWorkerPool* workers_;
    size_t threshold_;
};
