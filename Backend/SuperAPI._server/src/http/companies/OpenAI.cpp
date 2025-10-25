#include "http/companies/OpenAI.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerOpenAIRoutes(bool dryRun) {
    registerCompanyNamespace("openai", dryRun, false);
}

}  // namespace superapi::http
