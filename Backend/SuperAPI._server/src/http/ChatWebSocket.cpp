#include "http/ChatWebSocket.hpp"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/WebSocketController.h>
#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

namespace superapi::http {
namespace {

bool g_dryRun{false};
bool g_requireVendorForXai{true};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

Json::Value buildDeltaFrame(const std::string &company) {
    Json::Value payload(Json::objectValue);
    payload["type"] = "delta";
    Json::Value delta(Json::objectValue);
    delta["role"] = "assistant";
    delta["content"] = "Dry-run stream for " + company;
    payload["delta"] = std::move(delta);
    return payload;
}

Json::Value buildToolCallFrame(const std::string &company) {
    Json::Value payload(Json::objectValue);
    payload["type"] = "tool_call";
    Json::Value tool(Json::objectValue);
    tool["id"] = "dryrun-tool-" + company;
    tool["name"] = company + "_synthetic";
    tool["arguments"] = "{}";
    payload["tool_call"] = std::move(tool);
    return payload;
}

Json::Value buildDoneFrame() {
    Json::Value payload(Json::objectValue);
    payload["type"] = "done";
    payload["done"] = true;
    return payload;
}

std::string toJsonString(const Json::Value &value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::string getAttributeString(const drogon::HttpRequestPtr &req, const std::string &key) {
    if (!req) {
        return {};
    }
    try {
        auto attributes = req->attributes();
        if (!attributes) {
            return {};
        }
        return attributes->get<std::string>(key);
    } catch (const std::exception &) {
        return {};
    }
}

std::string resolveRequestId(const drogon::HttpRequestPtr &req, std::string_view fallback) {
    auto requestId = getAttributeString(req, "request_id");
    if (requestId.empty()) {
        requestId = req->getHeader("X-Request-ID");
    }
    if (requestId.empty()) {
        requestId = std::string{fallback};
    }
    return requestId;
}

bool ensureVendor(const drogon::HttpRequestPtr &req, std::string_view company, std::string &vendor) {
    static const std::unordered_set<std::string> kAllowedVendors{"grok", "zhipu"};

    const bool requireVendor = (company == "xai" ? g_requireVendorForXai : false);
    if (!requireVendor) {
        return true;
    }
    vendor = req->getParameter("vendor");
    if (vendor.empty()) {
        vendor = req->getHeader("X-Vendor");
    }
    vendor = lower(vendor);
    if (kAllowedVendors.find(vendor) != kAllowedVendors.end()) {
        if (auto attributes = req->attributes()) {
            attributes->insert("observability.vendor", vendor);
        }
        return true;
    }
    return false;
}

std::string makeIdentifier(std::string_view company) {
    const auto now = std::chrono::system_clock::now();
    const auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return "dryrun-" + std::string(company) + "-ws-" + std::to_string(ts);
}

template <const char *Company>
class CompanyChatWebSocket : public drogon::WebSocketController<CompanyChatWebSocket<Company>> {
  public:
    void handleNewMessage(const drogon::WebSocketConnectionPtr &, std::string &&, const drogon::WebSocketMessageType &) override {
    }

    void handleNewConnection(const drogon::HttpRequestPtr &req, const drogon::WebSocketConnectionPtr &conn) override {
        const std::string_view company{Company};
        std::string vendor;
        if (!ensureVendor(req, company, vendor)) {
            Json::Value error(Json::objectValue);
            error["type"] = "error";
            Json::Value body(Json::objectValue);
            body["message"] = "vendor must be either 'grok' or 'zhipu'";
            body["provider"] = std::string(company);
            body["code"] = 400;
            body["request_id"] = resolveRequestId(req, makeIdentifier(company));
            error["error"] = std::move(body);
            conn->send(toJsonString(error));
            conn->shutdown();
            return;
        }
        if (!g_dryRun) {
            Json::Value error(Json::objectValue);
            error["type"] = "error";
            Json::Value body(Json::objectValue);
            body["message"] = "Provider integration has not been implemented yet.";
            body["provider"] = std::string(company);
            body["code"] = 501;
            error["error"] = std::move(body);
            conn->send(toJsonString(error));
            conn->shutdown();
            return;
        }
        auto delta = buildDeltaFrame(std::string(company));
        delta["id"] = makeIdentifier(company);
        conn->send(toJsonString(delta));
        conn->send(toJsonString(buildToolCallFrame(std::string(company))));
        conn->send(toJsonString(buildDoneFrame()));
    }

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr &) override {}

    static std::vector<std::string> WS_PATH_LIST;

    static void initPathRouting() {
        for (const auto &path : WS_PATH_LIST) {
            CompanyChatWebSocket::registerSelf__(path, {});
        }
    }
};

template <const char *Company>
std::vector<std::string> CompanyChatWebSocket<Company>::WS_PATH_LIST = {
    "/ws/" + std::string(Company) + "/chat/completions"};

constexpr char kOpenAI[] = "openai";
constexpr char kAnphropic[] = "anphropic";
constexpr char kDeepSeek[] = "deepseek";
constexpr char kGemini[] = "gemini";
constexpr char kHuggingFace[] = "huggingface";
constexpr char kLaMA[] = "lama";
constexpr char kMiniMax[] = "minimax";
constexpr char kOpenRouter[] = "openrouter";
constexpr char kPerplexety[] = "perplexety";
constexpr char kQwen[] = "qwen";
constexpr char kVertex[] = "vertex";
constexpr char kAgentRouter[] = "agentrouter";
constexpr char kXai[] = "xai";

using OpenAIChatWebSocket = CompanyChatWebSocket<kOpenAI>;
using AnphropicChatWebSocket = CompanyChatWebSocket<kAnphropic>;
using DeepSeekChatWebSocket = CompanyChatWebSocket<kDeepSeek>;
using GeminiChatWebSocket = CompanyChatWebSocket<kGemini>;
using HuggingFaceChatWebSocket = CompanyChatWebSocket<kHuggingFace>;
using LaMAChatWebSocket = CompanyChatWebSocket<kLaMA>;
using MiniMaxChatWebSocket = CompanyChatWebSocket<kMiniMax>;
using OpenRouterChatWebSocket = CompanyChatWebSocket<kOpenRouter>;
using PerplexetyChatWebSocket = CompanyChatWebSocket<kPerplexety>;
using QwenChatWebSocket = CompanyChatWebSocket<kQwen>;
using VertexChatWebSocket = CompanyChatWebSocket<kVertex>;
using AgentRouterChatWebSocket = CompanyChatWebSocket<kAgentRouter>;
using XaiChatWebSocket = CompanyChatWebSocket<kXai>;

}  // namespace

void registerChatWebSocketControllers(bool dryRun, bool requireVendorForXai) {
    g_dryRun = dryRun;
    g_requireVendorForXai = requireVendorForXai;

    OpenAIChatWebSocket::initPathRouting();
    AnphropicChatWebSocket::initPathRouting();
    DeepSeekChatWebSocket::initPathRouting();
    GeminiChatWebSocket::initPathRouting();
    HuggingFaceChatWebSocket::initPathRouting();
    LaMAChatWebSocket::initPathRouting();
    MiniMaxChatWebSocket::initPathRouting();
    OpenRouterChatWebSocket::initPathRouting();
    PerplexetyChatWebSocket::initPathRouting();
    QwenChatWebSocket::initPathRouting();
    VertexChatWebSocket::initPathRouting();
    AgentRouterChatWebSocket::initPathRouting();
    XaiChatWebSocket::initPathRouting();
}

}  // namespace superapi::http
