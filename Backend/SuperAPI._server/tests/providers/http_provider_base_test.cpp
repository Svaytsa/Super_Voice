#include "providers/HttpProviderBase.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <cstdlib>

namespace {

class ScopedEnvVar {
   public:
    explicit ScopedEnvVar(std::string key) : key_(std::move(key)) {
        const char *value = std::getenv(key_.c_str());
        if (value != nullptr) {
            original_ = std::string(value);
        }
    }

    ~ScopedEnvVar() { restore(); }

    void set(const std::string &value) {
#ifdef _WIN32
        _putenv_s(key_.c_str(), value.c_str());
#else
        ::setenv(key_.c_str(), value.c_str(), 1);
#endif
    }

    void clear() {
#ifdef _WIN32
        _putenv((key_ + "=").c_str());
#else
        ::unsetenv(key_.c_str());
#endif
    }

    void restore() {
        if (original_.has_value()) {
#ifdef _WIN32
            _putenv_s(key_.c_str(), original_->c_str());
#else
            ::setenv(key_.c_str(), original_->c_str(), 1);
#endif
        } else {
#ifdef _WIN32
            _putenv((key_ + "=").c_str());
#else
            ::unsetenv(key_.c_str());
#endif
        }
    }

   private:
    std::string key_;
    std::optional<std::string> original_;
};

class TestHttpProvider : public superapi::providers::HttpProviderBase {
   public:
    using HttpProviderBase::HttpProviderBase;

    using superapi::providers::HttpProviderBase::JsonOperationResult;

    cpr::Header exposeBuildHeaders(const std::string &apiKey, const superapi::providers::RequestContext &context) const {
        return HttpProviderBase::buildHeaders(apiKey, context);
    }

    superapi::providers::ProviderResult<JsonOperationResult> invokePerformJsonOperation(
        superapi::providers::ProviderOperation operation,
        const Json::Value &payload,
        const superapi::providers::RequestContext &context,
        const std::string &resourceId = {}) {
        return HttpProviderBase::performJsonOperation(operation, payload, context, resourceId);
    }
};

superapi::providers::ProviderConfig makeBaseConfig() {
    superapi::providers::ProviderConfig config;
    config.name = "test";
    config.baseUrl = "https://example.com";
    config.apiKeyEnv = "TEST_HTTP_API_KEY";
    config.organizationEnv = "TEST_HTTP_ORG";
    config.defaultHeaders = {{"X-Custom", "value"}};
    config.maxRetries = 0;
    return config;
}

class HttpProviderBaseTest : public ::testing::Test {
   protected:
    HttpProviderBaseTest() : provider_(makeBaseConfig(), superapi::providers::AuthStrategy::BearerAuthorization) {}

    TestHttpProvider provider_;
};

}  // namespace

TEST_F(HttpProviderBaseTest, BuildHeadersIncludesAuthAndDefaults) {
    ScopedEnvVar orgGuard("TEST_HTTP_ORG");
    orgGuard.set("org-123");

    superapi::providers::RequestContext context;
    context.requestId = "req-1";

    auto header = provider_.exposeBuildHeaders("secret", context);

    EXPECT_EQ(header["Content-Type"], "application/json");
    EXPECT_EQ(header["Accept"], "application/json");
    EXPECT_EQ(header["User-Agent"], "superapi_server/0.1.0");
    EXPECT_EQ(header["X-Request-ID"], "req-1");
    EXPECT_EQ(header["X-Custom"], "value");
    EXPECT_EQ(header["Authorization"], "Bearer secret");
    EXPECT_EQ(header["OpenAI-Organization"], "org-123");
}

TEST_F(HttpProviderBaseTest, PerformJsonOperationFailsWhenBaseUrlMissing) {
    auto config = makeBaseConfig();
    config.baseUrl.clear();
    TestHttpProvider provider(config, superapi::providers::AuthStrategy::BearerAuthorization);

    superapi::providers::RequestContext context;
    context.requestId = "req-base-url";

    auto result = provider.invokePerformJsonOperation(superapi::providers::ProviderOperation::Chat,
                                                      Json::Value(Json::objectValue),
                                                      context);

    ASSERT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->code, "missing_base_url");
    EXPECT_EQ(result.error->requestId, context.requestId);
}

TEST_F(HttpProviderBaseTest, PerformJsonOperationFailsWhenApiKeyEnvMissing) {
    auto config = makeBaseConfig();
    config.apiKeyEnv.clear();
    TestHttpProvider provider(config, superapi::providers::AuthStrategy::BearerAuthorization);

    superapi::providers::RequestContext context;
    context.requestId = "req-missing-env";

    auto result = provider.invokePerformJsonOperation(superapi::providers::ProviderOperation::Chat,
                                                      Json::Value(Json::objectValue),
                                                      context);

    ASSERT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->code, "missing_api_key_env");
    EXPECT_EQ(result.error->requestId, context.requestId);
}

TEST_F(HttpProviderBaseTest, PerformJsonOperationFailsWhenApiKeyEmpty) {
    ScopedEnvVar apiKeyGuard("TEST_HTTP_API_KEY");
    apiKeyGuard.clear();

    superapi::providers::RequestContext context;
    context.requestId = "req-empty-api-key";

    auto result = provider_.invokePerformJsonOperation(superapi::providers::ProviderOperation::Chat,
                                                       Json::Value(Json::objectValue),
                                                       context);

    ASSERT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->code, "missing_api_key");
    EXPECT_EQ(result.error->type, "auth_error");
    EXPECT_EQ(result.error->requestId, context.requestId);
}

TEST_F(HttpProviderBaseTest, PerformJsonOperationReturnsDryRunError) {
    auto config = makeBaseConfig();
    config.dryRun = true;
    TestHttpProvider provider(config, superapi::providers::AuthStrategy::BearerAuthorization);

    superapi::providers::RequestContext context;
    context.requestId = "req-dry-run";

    auto result = provider.invokePerformJsonOperation(superapi::providers::ProviderOperation::Chat,
                                                      Json::Value(Json::objectValue),
                                                      context);

    ASSERT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error->code, "dry_run");
    EXPECT_EQ(result.error->type, "dry_run");
    EXPECT_EQ(result.error->requestId, context.requestId);
}
