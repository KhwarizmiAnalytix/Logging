/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "LoggingTest.h"
#include "util/lazy.h"

using namespace logging;

// ============================================================================
// Basic optimistic_lazy Tests
// ============================================================================

TEST(Lazy, Test)
{
    {
        optimistic_lazy<int> lazy_int;

        // Test that factory is called on first access
        int  call_count = 0;
        auto factory    = [&call_count]() -> int
        {
            call_count++;
            return 42;
        };

        int& value1 = lazy_int.ensure(factory);
        EXPECT_EQ(value1, 42);
        EXPECT_EQ(call_count, 1);

        // Test that factory is not called on subsequent access
        int& value2 = lazy_int.ensure(factory);
        EXPECT_EQ(value2, 42);
        EXPECT_EQ(call_count, 1);  // Should still be 1

        // Test that both references point to same object
        EXPECT_EQ(&value1, &value2);
    }

    {
        // Test with std::string
        optimistic_lazy<std::string> lazy_str;
        std::string& str = lazy_str.ensure([]() { return std::string("Hello, World!"); });
        EXPECT_EQ(str, "Hello, World!");

        // Test with std::vector
        optimistic_lazy<std::vector<int>> lazy_vec;
        std::vector<int>& vec = lazy_vec.ensure([]() { return std::vector<int>{1, 2, 3}; });
        EXPECT_EQ(vec.size(), 3);
        EXPECT_EQ(vec[0], 1);
        EXPECT_EQ(vec[2], 3);
    }

    // ============================================================================
    // lazy_value Tests
    // ============================================================================
    {
        optimistic_lazy<int> lazy_int;

        // Initialize with first value
        int& value1 = lazy_int.ensure([]() { return 42; });
        EXPECT_EQ(value1, 42);

        // Reset
        lazy_int.reset();

        // Re-initialize with different value
        int& value2 = lazy_int.ensure([]() { return 99; });
        EXPECT_EQ(value2, 99);
    }

    {
        // Test with optimistic_lazy_value
        class test_lazy_value : public optimistic_lazy_value<int>
        {
        private:
            int compute() const override { return 42; }
        };

        test_lazy_value lazy_val;
        const int&      value1 = lazy_val.get();
        EXPECT_EQ(value1, 42);

        // Test that value is cached
        const int& value2 = lazy_val.get();
        EXPECT_EQ(value2, 42);
        EXPECT_EQ(&value1, &value2);
    }

    {
        precomputed_lazy_value<int> lazy_val(42);

        const int& value1 = lazy_val.get();
        EXPECT_EQ(value1, 42);

        const int& value2 = lazy_val.get();
        EXPECT_EQ(value2, 42);
        EXPECT_EQ(&value1, &value2);

        // Test with complex type
        precomputed_lazy_value<std::string> lazy_str(std::string("Hello"));
        const std::string&                  str = lazy_str.get();
        EXPECT_EQ(str, "Hello");
    }

    // ============================================================================
    // Edge Cases Tests
    // ============================================================================
    {
        // Test with empty string
        optimistic_lazy<std::string> lazy_empty;
        std::string&                 empty = lazy_empty.ensure([]() { return std::string(); });
        EXPECT_TRUE(empty.empty());

        // Test with zero value
        optimistic_lazy<int> lazy_zero;
        int&                 zero = lazy_zero.ensure([]() { return 0; });
        EXPECT_EQ(zero, 0);
    }

    // ============================================================================
    // Copy and Move Semantics Tests
    // ============================================================================
    {
        optimistic_lazy<int> lazy_original;
        int&                 value1 = lazy_original.ensure([]() { return 42; });
        EXPECT_EQ(value1, 42);

        // Copy constructor
        optimistic_lazy<int> lazy_copy(lazy_original);
        int&                 value2 = lazy_copy.ensure([]() { return 99; });
        EXPECT_EQ(value2, 42);  // Should have copied the value
    }

    {
        optimistic_lazy<int> lazy_original;
        int&                 value1 = lazy_original.ensure([]() { return 42; });
        EXPECT_EQ(value1, 42);

        // Move constructor
        optimistic_lazy<int> lazy_moved(std::move(lazy_original));
        int&                 value2 = lazy_moved.ensure([]() { return 99; });
        EXPECT_EQ(value2, 42);
    }

    {
        optimistic_lazy<int> lazy_original;
        int&                 value1 = lazy_original.ensure([]() { return 42; });
        EXPECT_EQ(value1, 42);

        optimistic_lazy<int> lazy_copy;
        lazy_copy   = lazy_original;
        int& value2 = lazy_copy.ensure([]() { return 99; });
        EXPECT_EQ(value2, 42);
    }

    {
        optimistic_lazy<int> lazy_original;
        int&                 value1 = lazy_original.ensure([]() { return 42; });
        EXPECT_EQ(value1, 42);

        optimistic_lazy<int> lazy_moved;
        lazy_moved  = std::move(lazy_original);
        int& value2 = lazy_moved.ensure([]() { return 99; });
        EXPECT_EQ(value2, 42);
    }

    {
        optimistic_lazy<int> lazy;
        int&                 value1 = lazy.ensure([]() { return 42; });
        EXPECT_EQ(value1, 42);

        // Self-assignment should be safe
        lazy        = lazy;
        int& value2 = lazy.ensure([]() { return 99; });
        EXPECT_EQ(value2, 42);
    }

    // ============================================================================
    // Reset and Reinitialization Tests
    // ============================================================================
    {
        optimistic_lazy<int> lazy;
        // Reset on uninitialized lazy should be safe
        lazy.reset();
        EXPECT_NO_THROW({
            int& value = lazy.ensure([]() { return 42; });
            EXPECT_EQ(value, 42);
        });
    }

    {
        optimistic_lazy<int> lazy;

        // First initialization
        int& value1 = lazy.ensure([]() { return 42; });
        EXPECT_EQ(value1, 42);

        // Reset and reinitialize multiple times
        for (int i = 0; i < 3; ++i)
        {
            lazy.reset();
            int& value = lazy.ensure([i]() { return i * 10; });
            EXPECT_EQ(value, i * 10);
        }
    }

    // ============================================================================
    // Complex Type Tests
    // ============================================================================
    {
        struct complex_type
        {
            int              value;
            std::string      name;
            std::vector<int> data;

            complex_type() : value(0), name("default"), data{1, 2, 3} {}
        };

        optimistic_lazy<complex_type> lazy;
        complex_type&                 obj = lazy.ensure([]() { return complex_type(); });

        EXPECT_EQ(obj.value, 0);
        EXPECT_EQ(obj.name, "default");
        EXPECT_EQ(obj.data.size(), 3);
    }

    // ============================================================================
    // Lazy Value Interface Tests
    // ============================================================================
    {
        class test_lazy_value : public optimistic_lazy_value<int>
        {
        private:
            int compute() const override { return 42; }
        };

        std::unique_ptr<lazy_value<int>> lazy_ptr = std::make_unique<test_lazy_value>();
        const int&                       value    = lazy_ptr->get();
        EXPECT_EQ(value, 42);
    }

    {
        std::unique_ptr<lazy_value<int>> lazy_ptr =
            std::make_unique<precomputed_lazy_value<int>>(99);
        const int& value = lazy_ptr->get();
        EXPECT_EQ(value, 99);
    }

    // ============================================================================
    // Boundary Condition Tests
    // ============================================================================
    {
        optimistic_lazy<std::vector<int>> lazy_vec;
        std::vector<int>&                 vec = lazy_vec.ensure(
            []()
            {
                std::vector<int> v;
                for (int i = 0; i < 10000; ++i)
                {
                    v.push_back(i);
                }
                return v;
            });

        EXPECT_EQ(vec.size(), 10000);
        EXPECT_EQ(vec[0], 0);
        EXPECT_EQ(vec[9999], 9999);
    }

    {
        optimistic_lazy<int> lazy;

        // First call with exception-throwing factory
        EXPECT_THROW(
            { lazy.ensure([]() -> int { throw std::runtime_error("Factory error"); }); },
            std::runtime_error);

        // After exception, lazy should still be uninitialized
        // and we can try again with a working factory
        int& value = lazy.ensure([]() { return 42; });
        EXPECT_EQ(value, 42);
    }
}
