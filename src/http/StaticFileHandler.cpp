#include <http/StaticFileHandler.h>
#include <http/AsyncResponder.h>

#include <base/TaskWorkerPool.h>

#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>

namespace {

bool parseHttpDate(const std::string& value, time_t* result)
{
    if (!result) return false;
    struct tm parsed;
    memset(&parsed, 0, sizeof(parsed));
    char* end = strptime(value.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &parsed);
    if (!end || *end != '\0') return false;
    const time_t converted = timegm(&parsed);
    if (converted == static_cast<time_t>(-1)) return false;
    *result = converted;
    return true;
}

std::string formatHttpDate(time_t value)
{
    struct tm timestamp;
    if (!gmtime_r(&value, &timestamp)) return std::string();
    char buffer[64];
    if (strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &timestamp) == 0)
        return std::string();
    return buffer;
}

} // namespace

bool StaticFileHandler::prepareInRoot(const std::string& root, const HttpRequest& request,
                                      FilePlan* plan)
{
    if (!plan) return false;
    *plan = FilePlan();
    std::string path = urlDecode(request.path());
    if (path.empty() || path[0] != '/' || path.find("..") != std::string::npos) {
        plan->status = HttpResponse::k400BadRequest; return true;
    }
    if (path == "/") path = "/index.html";
    char resolved[PATH_MAX];
    const std::string candidate = root + path;
    if (!realpath(candidate.c_str(), resolved) ||
        std::string(resolved).compare(0, root.size(), root) != 0 ||
        std::string(resolved).size() <= root.size() || std::string(resolved)[root.size()] != '/') {
        plan->status = HttpResponse::k404NotFound; return true;
    }
    struct stat st;
    if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) { plan->status = HttpResponse::k404NotFound; return true; }
    std::ostringstream etag; etag << '"' << st.st_ino << '-' << st.st_size << '-' << st.st_mtime << '"';
    plan->path = resolved; plan->etag = etag.str(); plan->lastModified = st.st_mtime;
    plan->fileSize = static_cast<size_t>(st.st_size); plan->isFile = true;
    if (!request.getHeader("Range").empty()) { plan->status = HttpResponse::k416RangeNotSatisfiable; return true; }
    time_t ifModifiedSince = 0;
    const std::string& ifModifiedSinceHeader = request.getHeader("If-Modified-Since");
    if (request.getHeader("If-None-Match") == plan->etag ||
        (parseHttpDate(ifModifiedSinceHeader, &ifModifiedSince) && st.st_mtime <= ifModifiedSince))
        plan->status = HttpResponse::k304NotModified;
    return true;
}

bool StaticFileHandler::prepare(const HttpRequest& request, FilePlan* plan) const
{
    return prepareInRoot(root_, request, plan);
}

StaticFileHandler::StaticFileHandler(const std::string& root, const CacheGet& get,
                                     const CachePut& put, TaskWorkerPool* workers,
                                     size_t largeFileThreshold)
    : get_(get), put_(put), workers_(workers), threshold_(largeFileThreshold)
{
    char resolved[PATH_MAX];
    if (realpath(root.c_str(), resolved)) root_ = resolved;
}

std::string StaticFileHandler::mime(const std::string& path)
{
    if (path.size() >= 5 && path.substr(path.size()-5) == ".webp") return "image/webp";
    if (path.size() >= 4 && path.substr(path.size()-4) == ".glb") return "model/gltf-binary";
    if (path.size() >= 5 && path.substr(path.size()-5) == ".patt") return "text/plain";
    if (path.size() >= 5 && path.substr(path.size()-5) == ".gltf") return "model/gltf+json";
    if (path.size() >= 4 && path.substr(path.size()-4) == ".bin") return "application/octet-stream";
    return StaticFileCache::getMimeType(path);
}

bool StaticFileHandler::handle(const HttpRequest& request, HttpResponse* response) const
{
    if (!response) return false;
    FilePlan plan;
    prepare(request, &plan);
    populateResponse(request, plan, get_, put_, threshold_, response);
    return true;
}

void StaticFileHandler::populateResponse(const HttpRequest& request, const FilePlan& plan,
                                         const CacheGet& get, const CachePut& put,
                                         size_t threshold, HttpResponse* response)
{
    response->setStatusCode(plan.status);
    if (!plan.isFile) return;

    response->addHeader("ETag", plan.etag);
    response->addHeader("Last-Modified", formatHttpDate(plan.lastModified));
    response->setContentType(mime(plan.path));
    if (plan.status != HttpResponse::k200Ok) return;
    response->addHeader("Content-Length", std::to_string(plan.fileSize));
    if (request.method() == HttpRequest::kHead) return;
    if (plan.fileSize > threshold)
    {
        response->setFile(plan.path, 0, plan.fileSize);
        return;
    }

    CachedFileEntry entry;
    if (!get || !get(plan.path, entry) || entry.lastModified != plan.lastModified ||
        entry.fileSize != plan.fileSize)
    {
        std::ifstream input(plan.path.c_str(), std::ios::binary);
        if (!input)
        {
            response->setStatusCode(HttpResponse::k404NotFound);
            return;
        }
        entry.content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        entry.contentType = mime(plan.path);
        entry.lastModified = plan.lastModified;
        entry.fileSize = plan.fileSize;
        if (put) put(plan.path, entry);
    }
    response->setBody(entry.content);
}

void StaticFileHandler::handleAsync(const HttpRequest& request, const AsyncResponder& responder) const
{
    const std::string root = root_;
    const CacheGet get = get_;
    const CachePut put = put_;
    const size_t threshold = threshold_;
    if (!workers_ || !workers_->submit([root, get, put, threshold, request, responder] {
        FilePlan plan;
        prepareInRoot(root, request, &plan);
        HttpResponse response(false);
        populateResponse(request, plan, get, put, threshold, &response);
        responder.send(response);
    }))
    {
        HttpResponse unavailable(false);
        unavailable.setStatusCode(HttpResponse::k503ServiceUnavailable);
        responder.send(unavailable);
    }
}
