#include "http/companies/Qwen.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerQwenRoutes(bool dryRun) {
    registerCompanyNamespace("qwen", dryRun, false);
}

}  // namespace superapi::http
