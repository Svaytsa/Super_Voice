#include "http/companies/LaMA.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerLaMARoutes(bool dryRun) {
    registerCompanyNamespace("lama", dryRun, false);
}

}  // namespace superapi::http
