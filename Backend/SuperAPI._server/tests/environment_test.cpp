#include "superapi/environment.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
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

class EnvironmentTest : public ::testing::Test {};

}  // namespace

TEST_F(EnvironmentTest, LoadDotEnvPopulatesEnvironment) {
    auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
                ("superapi-test-" + std::to_string(timestamp) + ".env");

    {
        std::ofstream output(path);
        ASSERT_TRUE(output.is_open());
        output << "# comment\n";
        output << "TEST_KEY = test_value\n";
        output << "QUOTED='quoted value'\n";
        output << "EMPTY=\n";
    }

    ScopedEnvVar testKeyGuard("TEST_KEY");
    ScopedEnvVar quotedGuard("QUOTED");
    ScopedEnvVar emptyGuard("EMPTY");
    testKeyGuard.clear();
    quotedGuard.clear();
    emptyGuard.clear();

    ASSERT_TRUE(superapi::loadDotEnv(path));

    auto testValue = superapi::getEnv("TEST_KEY");
    ASSERT_TRUE(testValue.has_value());
    EXPECT_EQ(*testValue, "test_value");

    auto quotedValue = superapi::getEnv("QUOTED");
    ASSERT_TRUE(quotedValue.has_value());
    EXPECT_EQ(*quotedValue, "quoted value");

    auto emptyValue = superapi::getEnv("EMPTY");
    ASSERT_TRUE(emptyValue.has_value());
    EXPECT_TRUE(emptyValue->empty());

    std::filesystem::remove(path);
}

TEST_F(EnvironmentTest, GetEnvOrDefaultReturnsFallbackWhenUnset) {
    ScopedEnvVar guard("MISSING_KEY");
    guard.clear();

    EXPECT_EQ(superapi::getEnvOrDefault("MISSING_KEY", "default"), "default");

    guard.set("configured");
    EXPECT_EQ(superapi::getEnvOrDefault("MISSING_KEY", "default"), "configured");
}

TEST_F(EnvironmentTest, GetEnvFlagParsesCommonValues) {
    ScopedEnvVar guard("FLAG_KEY");

    guard.set("true");
    EXPECT_TRUE(superapi::getEnvFlag("FLAG_KEY", false));

    guard.set("off");
    EXPECT_FALSE(superapi::getEnvFlag("FLAG_KEY", true));

    guard.set("unexpected");
    EXPECT_TRUE(superapi::getEnvFlag("FLAG_KEY", true));

    guard.clear();
    EXPECT_FALSE(superapi::getEnvFlag("FLAG_KEY", false));
}
