#include "http/companies/AgentRouter.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerAgentRouterRoutes(bool dryRun) {
    registerCompanyNamespace("agentrouter", dryRun, false);
}

}  // namespace superapi::http
