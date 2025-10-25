#include "http/companies/MiniMax.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerMiniMaxRoutes(bool dryRun) {
    registerCompanyNamespace("minimax", dryRun, false);
}

}  // namespace superapi::http
