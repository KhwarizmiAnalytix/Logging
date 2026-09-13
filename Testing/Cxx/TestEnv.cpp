/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

#include <cstdint>
#include <cstdlib>
#include <string>

#include "LoggingTest.h"
#include "util/env.h"

using logging::utils::check_env;
using logging::utils::get_env;
using logging::utils::has_env;
using logging::utils::read_env_bool;
using logging::utils::read_env_int64;
using logging::utils::set_env;

namespace
{
constexpr const char* kFlag = "XSIGMA_LOGGING_TEST_FLAG";
constexpr const char* kInt  = "XSIGMA_LOGGING_TEST_INT64";

class env_var_guard
{
public:
    env_var_guard(const char* name, const char* value) : name_(name)
    {
        const char* old_val = std::getenv(name);
        if (old_val != nullptr)
        {
            old_value_ = old_val;
            had_value_ = true;
        }
        set_env(name, value, true);
    }

    ~env_var_guard()
    {
        if (had_value_)
        {
            set_env(name_.c_str(), old_value_.c_str(), true);
        }
        else
        {
            set_env(name_.c_str(), "", true);
        }
    }

private:
    std::string name_;
    std::string old_value_;
    bool        had_value_ = false;
};
}  // namespace

TEST(Env, set_get_has)
{
    env_var_guard guard(kFlag, "1");
    EXPECT_TRUE(has_env(kFlag));
    auto value = get_env(kFlag);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "1");
}

TEST(Env, check_env_bool_tokens)
{
    {
        env_var_guard guard(kFlag, "1");
        auto          parsed = check_env(kFlag);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_TRUE(*parsed);
    }
    {
        env_var_guard guard(kFlag, "0");
        auto          parsed = check_env(kFlag);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_FALSE(*parsed);
    }
    {
        env_var_guard guard(kFlag, "maybe");
        EXPECT_FALSE(check_env(kFlag).has_value());
    }
}

TEST(Env, read_env_bool)
{
    bool value = true;
    {
        env_var_guard guard(kFlag, "false");
        EXPECT_TRUE(read_env_bool(kFlag, true, &value));
        EXPECT_FALSE(value);
    }
    {
        env_var_guard guard(kFlag, "TRUE");
        EXPECT_TRUE(read_env_bool(kFlag, false, &value));
        EXPECT_TRUE(value);
    }
    {
        env_var_guard guard(kFlag, "nope");
        EXPECT_FALSE(read_env_bool(kFlag, true, &value));
        EXPECT_TRUE(value);
    }
}

TEST(Env, read_env_int64)
{
    int64_t value = 0;
    {
        env_var_guard guard(kInt, "  42  ");
        EXPECT_TRUE(read_env_int64(kInt, 0, &value));
        EXPECT_EQ(value, 42);
    }
    {
        env_var_guard guard(kInt, "not-a-number");
        EXPECT_FALSE(read_env_int64(kInt, 7, &value));
        EXPECT_EQ(value, 7);
    }
}
