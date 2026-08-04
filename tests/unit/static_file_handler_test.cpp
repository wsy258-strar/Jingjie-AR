#include "TestSupport.h"

#include <base/TaskWorkerPool.h>
#include <http/AsyncResponder.h>
#include <http/StaticFileHandler.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void writeFile(const std::string& path, const std::string& content)
{
    std::ofstream output(path.c_str(), std::ios::binary);
    output.write(content.data(), content.size());
}

HttpRequest request(HttpRequest::Method method, const std::string& path)
{
    HttpRequest value;
    value.setMethod(method);
    value.setPath(path);
    return value;
}

void testStaticFilesAreSafeAndHaveExpectedMetadata()
{
    char templatePath[] = "/tmp/static-file-handler-XXXXXX";
    char* rootPath = mkdtemp(templatePath);
    CHECK(rootPath != 0);
    const std::string root(rootPath);
    const std::string external = root + "-external";
    writeFile(root + "/index.html", "index");
    writeFile(root + "/asset.glb", "model");
    writeFile(root + "/asset.webp", "webp");
    writeFile(root + "/marker.patt", "pattern");
    writeFile(root + "/large.bin", std::string(1024 * 1024 + 1, 'x'));
    writeFile(external, "outside");

    StaticFileHandler handler(root, StaticFileHandler::CacheGet(),
                              StaticFileHandler::CachePut(), 0, 1024 * 1024);
    HttpResponse response(false);
    CHECK(handler.handle(request(HttpRequest::kGet, "/"), &response));
    CHECK(response.body() == "index");

    HttpResponse glb(false);
    CHECK(handler.handle(request(HttpRequest::kGet, "/asset.glb"), &glb));
    CHECK(glb.header("Content-Type") == "model/gltf-binary");
    HttpResponse webp(false);
    CHECK(handler.handle(request(HttpRequest::kGet, "/asset.webp"), &webp));
    CHECK(webp.header("Content-Type") == "image/webp");
    HttpResponse patt(false);
    CHECK(handler.handle(request(HttpRequest::kGet, "/marker.patt"), &patt));
    CHECK(patt.header("Content-Type") == "text/plain");

    HttpResponse traversal(false);
    CHECK(handler.handle(request(HttpRequest::kGet, "/../" + external), &traversal));
    CHECK(traversal.statusCode() == HttpResponse::k400BadRequest);
    HttpResponse encodedTraversal(false);
    CHECK(handler.handle(request(HttpRequest::kGet, "/%2e%2e/external"), &encodedTraversal));
    CHECK(encodedTraversal.statusCode() == HttpResponse::k400BadRequest);

    HttpRequest conditional = request(HttpRequest::kGet, "/index.html");
    HttpResponse first(false);
    CHECK(handler.handle(conditional, &first));
    CHECK(!first.header("Last-Modified").empty());
    conditional.addHeader("If-None-Match", first.header("ETag"));
    HttpResponse notModified(false);
    CHECK(handler.handle(conditional, &notModified));
    CHECK(notModified.statusCode() == HttpResponse::k304NotModified);
    CHECK(notModified.body().empty());

    HttpResponse large(false);
    CHECK(handler.handle(request(HttpRequest::kGet, "/large.bin"), &large));
    CHECK(large.hasFile());
    CHECK(large.fileCount() == 1024 * 1024 + 1);
    CHECK(large.body().empty());

    HttpRequest largeRangeRequest = request(HttpRequest::kGet, "/large.bin");
    largeRangeRequest.addHeader("Range", "bytes=7-9");
    HttpResponse largeRange(false);
    CHECK(handler.handle(largeRangeRequest, &largeRange));
    CHECK(largeRange.statusCode() == HttpResponse::k206PartialContent);
    CHECK(largeRange.hasFile());
    CHECK(largeRange.fileOffset() == 7);
    CHECK(largeRange.fileCount() == 3);
    CHECK(largeRange.header("Content-Range") == "bytes 7-9/1048577");

    HttpResponse head(false);
    CHECK(handler.handle(request(HttpRequest::kHead, "/index.html"), &head));
    CHECK(head.body().empty());
    CHECK(head.header("Content-Length") == "5");

    unlink((root + "/index.html").c_str());
    unlink((root + "/asset.glb").c_str());
    unlink((root + "/asset.webp").c_str());
    unlink((root + "/marker.patt").c_str());
    unlink((root + "/large.bin").c_str());
    rmdir(root.c_str());
    unlink(external.c_str());
}

void testPrepareSupportsSingleRangesAndRejectsInvalidRanges()
{
    char templatePath[] = "/tmp/static-file-plan-XXXXXX";
    char* rootPath = mkdtemp(templatePath);
    CHECK(rootPath != 0);
    const std::string root(rootPath);
    writeFile(root + "/index.html", "index");

    StaticFileHandler handler(root, StaticFileHandler::CacheGet(),
                              StaticFileHandler::CachePut(), 0, 1024 * 1024);
    StaticFileHandler::FilePlan plan;
    HttpRequest ranged = request(HttpRequest::kGet, "/index.html");
    ranged.addHeader("Range", "bytes=0-1");
    HttpResponse partial(false);
    CHECK(handler.handle(ranged, &partial));
    CHECK(partial.statusCode() == HttpResponse::k206PartialContent);
    CHECK(partial.header("Accept-Ranges") == "bytes");
    CHECK(partial.header("Content-Range") == "bytes 0-1/5");
    CHECK(partial.header("Content-Length") == "2");
    CHECK(partial.body() == "in");

    HttpRequest openEnded = request(HttpRequest::kGet, "/index.html");
    openEnded.addHeader("Range", "bytes=2-");
    HttpResponse openEndedResponse(false);
    CHECK(handler.handle(openEnded, &openEndedResponse));
    CHECK(openEndedResponse.statusCode() == HttpResponse::k206PartialContent);
    CHECK(openEndedResponse.header("Content-Range") == "bytes 2-4/5");
    CHECK(openEndedResponse.body() == "dex");

    HttpRequest suffix = request(HttpRequest::kGet, "/index.html");
    suffix.addHeader("Range", "bytes=-2");
    HttpResponse suffixResponse(false);
    CHECK(handler.handle(suffix, &suffixResponse));
    CHECK(suffixResponse.statusCode() == HttpResponse::k206PartialContent);
    CHECK(suffixResponse.header("Content-Range") == "bytes 3-4/5");
    CHECK(suffixResponse.body() == "ex");

    HttpRequest invalid = request(HttpRequest::kGet, "/index.html");
    invalid.addHeader("Range", "bytes=5-");
    HttpResponse invalidResponse(false);
    CHECK(handler.handle(invalid, &invalidResponse));
    CHECK(invalidResponse.statusCode() == HttpResponse::k416RangeNotSatisfiable);
    CHECK(invalidResponse.header("Content-Range") == "bytes */5");

    HttpRequest conditional = request(HttpRequest::kGet, "/index.html");
    conditional.addHeader("If-Modified-Since", "Wed, 31 Dec 2999 23:59:59 GMT");
    CHECK(handler.prepare(conditional, &plan));
    CHECK(plan.status == HttpResponse::k304NotModified);

    HttpRequest invalidConditional = request(HttpRequest::kGet, "/index.html");
    invalidConditional.addHeader("If-Modified-Since", "not an HTTP date");
    CHECK(handler.prepare(invalidConditional, &plan));
    CHECK(plan.status == HttpResponse::k200Ok);

    unlink((root + "/index.html").c_str());
    rmdir(root.c_str());
}

void testAsyncHandleUsesWorkerAndCompletesResponse()
{
    char templatePath[] = "/tmp/static-file-async-XXXXXX";
    char* rootPath = mkdtemp(templatePath);
    CHECK(rootPath != 0);
    const std::string root(rootPath);
    writeFile(root + "/index.html", "async body");

    TaskWorkerPool workers(1, 1);
    StaticFileHandler handler(root, StaticFileHandler::CacheGet(),
                              StaticFileHandler::CachePut(), &workers, 1024 * 1024);
    std::mutex mutex;
    std::condition_variable completion;
    bool received = false;
    HttpResponse result(false);
    AsyncResponder responder([&](HttpResponse response) {
        std::lock_guard<std::mutex> lock(mutex);
        result = response;
        received = true;
        completion.notify_one();
    });
    handler.handleAsync(request(HttpRequest::kGet, "/index.html"), responder);
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(completion.wait_for(lock, std::chrono::seconds(2), [&] { return received; }));
    }
    CHECK(result.statusCode() == HttpResponse::k200Ok);
    CHECK(result.body() == "async body");

    unlink((root + "/index.html").c_str());
    rmdir(root.c_str());
}

} // namespace

int main()
{
    testStaticFilesAreSafeAndHaveExpectedMetadata();
    testPrepareSupportsSingleRangesAndRejectsInvalidRanges();
    testAsyncHandleUsesWorkerAndCompletesResponse();
    return 0;
}
