#include "http/companies/HuggingFace.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerHuggingFaceRoutes(bool dryRun) {
    registerCompanyNamespace("huggingface", dryRun, false);
}

}  // namespace superapi::http
