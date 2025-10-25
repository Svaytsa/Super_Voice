#include "http/companies/Vertex.hpp"

#include "http/CompanyNamespace.hpp"

namespace superapi::http {

void registerVertexRoutes(bool dryRun) {
    registerCompanyNamespace("vertex", dryRun, false);
}

}  // namespace superapi::http
