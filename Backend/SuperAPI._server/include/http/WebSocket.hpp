#pragma once

#include <drogon/WebSocketConnection.h>
#include <json/json.h>

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace superapi::http {

class WebSocketStream : public std::enable_shared_from_this<WebSocketStream> {
   public:
    using Ptr = std::shared_ptr<WebSocketStream>;

    struct Options {
        std::size_t maxBufferedFrames{64};
        std::chrono::milliseconds heartbeatInterval{std::chrono::seconds(20)};
    };

    static Ptr create(const drogon::WebSocketConnectionPtr &connection, Options options = {});

    void sendJson(std::string_view event, const Json::Value &payload);
    void sendText(std::string_view event, std::string_view payload);
    void sendError(std::string_view code, std::string_view message, int status = 0);
    void sendDone(const Json::Value &payload = {});
    void close(drogon::CloseCode code = drogon::CloseCode::kNormalClosure,
               std::string_view reason = {});

    [[nodiscard]] bool isOpen() const;

   private:
    WebSocketStream(drogon::WebSocketConnectionPtr connection, Options options);

    void enqueueUnlocked(Json::Value frame);
    void flushUnlocked();
    static Json::Value makeFrame(std::string_view event, const Json::Value &payload);

    mutable std::mutex mutex_;
    drogon::WebSocketConnectionPtr connection_;
    Options options_;
    std::deque<Json::Value> pending_;
    bool closed_{false};
};

}  // namespace superapi::http
