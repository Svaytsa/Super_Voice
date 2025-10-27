#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <trantor/net/EventLoop.h>

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace superapi::http {

class SseStream : public std::enable_shared_from_this<SseStream> {
   public:
    using Ptr = std::shared_ptr<SseStream>;

    struct Options {
        std::chrono::milliseconds heartbeatInterval{std::chrono::seconds(15)};
        std::size_t maxBufferedBytes{1 << 16};
    };

    static std::pair<drogon::HttpResponsePtr, Ptr> create(const drogon::HttpRequestPtr &request,
                                                          Options options = {});

    void sendJsonEvent(std::string_view event, const Json::Value &payload);
    void sendTextEvent(std::string_view event, std::string_view text);
    void sendError(std::string_view code, std::string_view message, int status = 0);
    void sendDone(const Json::Value &finalPayload = {});
    void close();
    [[nodiscard]] bool isOpen() const;

   private:
    SseStream(const drogon::HttpRequestPtr &request, Options options);

    void onStreamReady(drogon::ResponseStreamPtr stream);
    void enqueueUnlocked(std::string frame);
    void flushUnlocked();
    void scheduleHeartbeatUnlocked();
    void cancelHeartbeatUnlocked();
    void sendHeartbeatUnlocked();
    static std::string formatJsonPayload(const Json::Value &payload);
    static std::string formatEvent(std::string_view event, std::string_view data);

    mutable std::mutex mutex_;
    Options options_;
    drogon::ResponseStreamPtr stream_;
    trantor::EventLoop *loop_{nullptr};
    trantor::TimerId heartbeatTimer_{};
    bool streamReady_{false};
    bool closed_{false};
    std::deque<std::string> pending_;
    std::size_t bufferedBytes_{0};
};

}  // namespace superapi::http
