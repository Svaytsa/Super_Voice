#include "http/HttpServer.hpp"

#include "http/ChatWebSocket.hpp"
#include "http/companies/AgentRouter.hpp"
#include "http/companies/Anphropic.hpp"
#include "http/companies/DeepSeek.hpp"
#include "http/companies/Gemini.hpp"
#include "http/companies/HuggingFace.hpp"
#include "http/companies/LaMA.hpp"
#include "http/companies/MiniMax.hpp"
#include "http/companies/OpenAI.hpp"
#include "http/companies/OpenRouter.hpp"
#include "http/companies/Perplexety.hpp"
#include "http/companies/Qwen.hpp"
#include "http/companies/Vertex.hpp"
#include "http/companies/XAI.hpp"

namespace superapi::http {

void HttpServer::registerRoutes(const AppConfig &config) {
    registerAgentRouterRoutes(config.dryRun);
    registerAnphropicRoutes(config.dryRun);
    registerDeepSeekRoutes(config.dryRun);
    registerGeminiRoutes(config.dryRun);
    registerHuggingFaceRoutes(config.dryRun);
    registerLaMARoutes(config.dryRun);
    registerMiniMaxRoutes(config.dryRun);
    registerOpenAIRoutes(config.dryRun);
    registerOpenRouterRoutes(config.dryRun);
    registerPerplexetyRoutes(config.dryRun);
    registerQwenRoutes(config.dryRun);
    registerVertexRoutes(config.dryRun);
    registerXAIRoutes(config.dryRun);

    registerChatWebSocketControllers(config.dryRun, true);
}

}  // namespace superapi::http
