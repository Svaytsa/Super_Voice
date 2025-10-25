#include "http/companies/Anphropic.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerAnphropicRoutes(bool dryRun) {
    registerCompanyNamespace("anphropic", dryRun, false);
}

}  // namespace superapi::http
