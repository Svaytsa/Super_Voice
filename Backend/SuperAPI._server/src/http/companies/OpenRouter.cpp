#include "http/companies/OpenRouter.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerOpenRouterRoutes(bool dryRun) {
    registerCompanyNamespace("openrouter", dryRun, false);
}

}  // namespace superapi::http
