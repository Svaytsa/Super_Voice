#include "http/companies/Gemini.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerGeminiRoutes(bool dryRun) {
    registerCompanyNamespace("gemini", dryRun, false);
}

}  // namespace superapi::http
