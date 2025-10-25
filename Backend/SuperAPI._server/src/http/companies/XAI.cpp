#include "http/companies/XAI.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerXAIRoutes(bool dryRun) {
    registerCompanyNamespace("xai", dryRun, true);
}

}  // namespace superapi::http
