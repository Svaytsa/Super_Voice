#pragma once

#include <string>

namespace superapi::http {

void registerCompanyNamespace(const std::string &company, bool dryRun, bool requireVendor);

}  // namespace superapi::http
