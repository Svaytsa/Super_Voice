#include "http/SSE.hpp"

#include <drogon/drogon.h>

#include <sstream>
#include <utility>

namespace superapi::http {
namespace {
constexpr std::string_view kHeartbeatComment{": heartbeat\n\n"};
constexpr std::string_view kDefaultRetry{"retry: 5000\n"};

std::string escapeData(std::string_view data) {
    std::string escaped;
    escaped.reserve(data.size() + 8);
    std::string_view::size_type start = 0;
    while (start < data.size()) {
        auto end = data.find('\n', start);
        if (end == std::string_view::npos) {
            escaped.append("data: ");
            escaped.append(data.substr(start));
            escaped.append("\n");
            break;
        }
        escaped.append("data: ");
        escaped.append(data.substr(start, end - start));
        escaped.append("\n");
        start = end + 1;
    }
    if (data.empty()) {
        escaped.append("data: \n");
    }
    escaped.append("\n");
    return escaped;
}

}  // namespace

std::pair<drogon::HttpResponsePtr, SseStream::Ptr> SseStream::create(const drogon::HttpRequestPtr &request,
                                                                     Options options) {
    auto stream = std::shared_ptr<SseStream>(new SseStream(request, options));
    std::weak_ptr<SseStream> weak = stream;
    auto response = drogon::HttpResponse::newAsyncStreamResponse(
        [weak](drogon::ResponseStreamPtr responseStream) {
            if (auto locked = weak.lock()) {
                locked->onStreamReady(std::move(responseStream));
            } else if (responseStream) {
                responseStream->close();
            }
        },
        true);
    response->setStatusCode(drogon::k200OK);
    response->setContentTypeString("text/event-stream; charset=utf-8");
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Connection", "keep-alive");
    response->addHeader("X-Accel-Buffering", "no");
    if (request) {
        auto reqId = request->getHeader("X-Request-ID");
        if (!reqId.empty()) {
            response->addHeader("X-Request-ID", reqId);
        }
    }
    return {response, std::move(stream)};
}

SseStream::SseStream(const drogon::HttpRequestPtr &, Options options) : options_(options) {}

void SseStream::onStreamReady(drogon::ResponseStreamPtr stream) {
    std::lock_guard lock(mutex_);
    if (closed_) {
        if (stream) {
            stream->close();
        }
        return;
    }
    stream_ = std::move(stream);
    streamReady_ = true;
    loop_ = trantor::EventLoop::getEventLoopOfCurrentThread();
    enqueueUnlocked(std::string{kDefaultRetry});
    flushUnlocked();
    scheduleHeartbeatUnlocked();
}

void SseStream::enqueueUnlocked(std::string frame) {
    if (closed_) {
        return;
    }
    if (frame.empty()) {
        return;
    }
    bufferedBytes_ += frame.size();
    pending_.push_back(std::move(frame));
    if (bufferedBytes_ > options_.maxBufferedBytes) {
        sendError("buffer_overflow", "Event buffer exceeded capacity.");
        close();
        return;
    }
    flushUnlocked();
}

void SseStream::flushUnlocked() {
    if (!streamReady_ || !stream_) {
        return;
    }
    while (!pending_.empty()) {
        auto &front = pending_.front();
        if (!stream_->send(front)) {
            closed_ = true;
            stream_.reset();
            pending_.clear();
            bufferedBytes_ = 0;
            cancelHeartbeatUnlocked();
            return;
        }
        bufferedBytes_ -= front.size();
        pending_.pop_front();
    }
}

void SseStream::scheduleHeartbeatUnlocked() {
    if (!loop_ || closed_) {
        return;
    }
    if (options_.heartbeatInterval.count() <= 0) {
        return;
    }
    auto weak = weak_from_this();
    heartbeatTimer_ = loop_->runEvery(options_.heartbeatInterval, [weak]() {
        if (auto self = weak.lock()) {
            std::lock_guard guard(self->mutex_);
            if (!self->closed_) {
                self->sendHeartbeatUnlocked();
            }
        }
    });
}

void SseStream::cancelHeartbeatUnlocked() {
    if (loop_ && heartbeatTimer_.id() >= 0) {
        loop_->invalidateTimer(heartbeatTimer_);
        heartbeatTimer_ = {};
    }
}

void SseStream::sendHeartbeatUnlocked() {
    if (!streamReady_ || closed_) {
        return;
    }
    pending_.emplace_back(kHeartbeatComment);
    bufferedBytes_ += kHeartbeatComment.size();
    flushUnlocked();
}

std::string SseStream::formatJsonPayload(const Json::Value &payload) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, payload);
}

std::string SseStream::formatEvent(std::string_view event, std::string_view data) {
    std::ostringstream oss;
    if (!event.empty()) {
        oss << "event: " << event << "\n";
    }
    oss << escapeData(data);
    return oss.str();
}

void SseStream::sendJsonEvent(std::string_view event, const Json::Value &payload) {
    auto json = formatJsonPayload(payload);
    std::lock_guard lock(mutex_);
    enqueueUnlocked(formatEvent(event, json));
}

void SseStream::sendTextEvent(std::string_view event, std::string_view text) {
    std::lock_guard lock(mutex_);
    enqueueUnlocked(formatEvent(event, text));
}

void SseStream::sendError(std::string_view code, std::string_view message, int status) {
    Json::Value payload(Json::objectValue);
    payload["type"] = "error";
    payload["code"] = std::string(code);
    payload["message"] = std::string(message);
    if (status > 0) {
        payload["status"] = status;
    }
    sendJsonEvent("error", payload);
}

void SseStream::sendDone(const Json::Value &finalPayload) {
    Json::Value payload(Json::objectValue);
    if (!finalPayload.isNull()) {
        payload["payload"] = finalPayload;
    }
    sendJsonEvent("done", payload);
    close();
}

void SseStream::close() {
    std::lock_guard lock(mutex_);
    if (closed_) {
        return;
    }
    closed_ = true;
    cancelHeartbeatUnlocked();
    pending_.clear();
    bufferedBytes_ = 0;
    if (stream_) {
        stream_->close();
        stream_.reset();
    }
}

bool SseStream::isOpen() const {
    std::lock_guard lock(mutex_);
    return !closed_;
}

}  // namespace superapi::http
