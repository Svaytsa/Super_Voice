#include "http/companies/Perplexety.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerPerplexetyRoutes(bool dryRun) {
    registerCompanyNamespace("perplexety", dryRun, false);
}

}  // namespace superapi::http
