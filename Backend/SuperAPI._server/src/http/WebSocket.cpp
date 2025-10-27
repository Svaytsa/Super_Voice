#include "http/WebSocket.hpp"

#include <drogon/drogon.h>

#include <utility>

namespace superapi::http {
namespace {
Json::Value makeErrorFrame(std::string_view code, std::string_view message, int status) {
    Json::Value payload(Json::objectValue);
    payload["type"] = "error";
    payload["code"] = std::string(code);
    payload["message"] = std::string(message);
    if (status > 0) {
        payload["status"] = status;
    }
    return payload;
}

}  // namespace

WebSocketStream::Ptr WebSocketStream::create(const drogon::WebSocketConnectionPtr &connection, Options options) {
    if (!connection) {
        return nullptr;
    }
    auto stream = Ptr(new WebSocketStream(connection, options));
    if (options.heartbeatInterval.count() > 0) {
        connection->setPingMessage("ping", options.heartbeatInterval);
    } else {
        connection->disablePing();
    }
    return stream;
}

WebSocketStream::WebSocketStream(drogon::WebSocketConnectionPtr connection, Options options)
    : connection_(std::move(connection)), options_(options) {}

void WebSocketStream::sendJson(std::string_view event, const Json::Value &payload) {
    enqueueUnlocked(makeFrame(event, payload));
}

void WebSocketStream::sendText(std::string_view event, std::string_view payload) {
    Json::Value body(Json::objectValue);
    body["data"] = std::string(payload);
    enqueueUnlocked(makeFrame(event, body));
}

void WebSocketStream::sendError(std::string_view code, std::string_view message, int status) {
    enqueueUnlocked(makeFrame("error", makeErrorFrame(code, message, status)));
}

void WebSocketStream::sendDone(const Json::Value &payload) {
    enqueueUnlocked(makeFrame("done", payload));
    close();
}

void WebSocketStream::close(drogon::CloseCode code, std::string_view reason) {
    std::lock_guard lock(mutex_);
    if (closed_) {
        return;
    }
    closed_ = true;
    pending_.clear();
    if (connection_) {
        connection_->shutdown(code, std::string(reason));
    }
}

bool WebSocketStream::isOpen() const {
    std::lock_guard lock(mutex_);
    return !closed_ && connection_ && connection_->connected();
}

void WebSocketStream::enqueueUnlocked(Json::Value frame) {
    std::lock_guard lock(mutex_);
    if (closed_) {
        return;
    }
    if (pending_.size() >= options_.maxBufferedFrames) {
        closed_ = true;
        pending_.clear();
        if (connection_) {
            connection_->shutdown(drogon::CloseCode::kTryAgainLater,
                                  "backpressure: too many queued frames");
        }
        return;
    }
    pending_.push_back(std::move(frame));
    flushUnlocked();
}

void WebSocketStream::flushUnlocked() {
    if (closed_ || !connection_) {
        return;
    }
    while (!pending_.empty()) {
        if (!connection_->connected()) {
            closed_ = true;
            pending_.clear();
            return;
        }
        auto payload = std::move(pending_.front());
        pending_.pop_front();
        connection_->sendJson(payload);
    }
}

Json::Value WebSocketStream::makeFrame(std::string_view event, const Json::Value &payload) {
    Json::Value frame(Json::objectValue);
    frame["event"] = std::string(event);
    frame["data"] = payload;
    return frame;
}

}  // namespace superapi::http
