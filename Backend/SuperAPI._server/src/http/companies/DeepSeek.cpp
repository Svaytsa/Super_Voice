#include "http/companies/DeepSeek.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerDeepSeekRoutes(bool dryRun) {
    registerCompanyNamespace("deepseek", dryRun, false);
}

}  // namespace superapi::http
