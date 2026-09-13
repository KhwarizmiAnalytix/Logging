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

/**
 * @file TestStringUtil.cpp
 * @brief Comprehensive test suite for string utility functions
 *
 * This file contains extensive tests for all string utility functions
 * including edge cases, error conditions, and performance validation.
 *
 * @author XSigma Development Team
 * @version 2.0
 * @date 2024
 */

#include <gtest/gtest.h>  // for AssertionResult, Message, TestPartResult, EXPECT_EQ, EXPECT_FALSE, EXPECT_TRUE

#include <cstddef>      // for size_t
#include <fstream>      // for basic_ostream, filebuf, ostream
#include <memory>       // for _Simple_types
#include <string>       // for string, basic_string, char_traits
#include <string_view>  // for string_view
#include <vector>       // for vector, _Vector_const_iterato

#include "LoggingTest.h"
#include "common/logging_macros.h"  // for LOGGING_UNUSED
#include "util/string_util.h"  // for is_float, is_integer, exclude_file_extension, file_extension, strip_basename

namespace logging
{
using namespace logging;
void testStringManipulation()
{
    // Test erase_all_sub_string function
    std::string      s   = "blabla";
    std::string_view sub = "la";
    erase_all_sub_string(s, sub);
    EXPECT_EQ(s, "bb");

    // More comprehensive erase tests
    s = "abcabcabc";
    erase_all_sub_string(s, "abc");
    EXPECT_EQ(s, "");

    s = "hello world hello";
    erase_all_sub_string(s, "hello");
    EXPECT_EQ(s, " world ");

    s = "test";
    erase_all_sub_string(s, "xyz");  // Non-existent substring
    EXPECT_EQ(s, "test");

    // Test replace_all function
    s            = "bb";
    size_t count = replace_all(s, "b", "c");
    EXPECT_EQ(s, "cc");
    EXPECT_EQ(count, 2u);

    // More comprehensive replace tests
    s     = "hello world hello";
    count = replace_all(s, "hello", "hi");
    EXPECT_EQ(s, "hi world hi");
    EXPECT_EQ(count, 2u);

    s     = "abcdef";
    count = replace_all(s, "xyz", "123");  // Non-existent substring
    EXPECT_EQ(s, "abcdef");
    EXPECT_EQ(count, 0u);

    // Test overlapping replacements
    s     = "aaa";
    count = replace_all(s, "aa", "b");
    EXPECT_EQ(s, "ba");  // Should replace first occurrence
    EXPECT_EQ(count, 1u);
}

void testCompatibilityFunctions()
{
    // Test starts_with function with std::string
    std::string str1    = "Hello World";
    std::string prefix1 = "Hello";
    EXPECT_TRUE(starts_with(str1, prefix1));

    std::string prefix2 = "hello";
    EXPECT_FALSE(starts_with(str1, prefix2));  // Case sensitive

    std::string str2    = "Hi";
    std::string prefix3 = "Hello";
    EXPECT_FALSE(starts_with(str2, prefix3));

    std::string empty1 = "";
    std::string empty2 = "";
    EXPECT_TRUE(starts_with(empty1, empty2));  // Edge case

    std::string prefix4 = "test";
    EXPECT_FALSE(starts_with(empty1, prefix4));

    // Test starts_with with string_view
    std::string_view sv1 = "Hello World";
    std::string_view sv2 = "Hello";
    EXPECT_TRUE(starts_with(sv1, sv2));

    // Test ends_with function with std::string
    std::string doc  = "document.pdf";
    std::string ext1 = ".pdf";
    EXPECT_TRUE(ends_with(doc, ext1));

    std::string ext2 = ".PDF";
    EXPECT_FALSE(ends_with(doc, ext2));  // Case sensitive

    std::string short_str = "doc";
    std::string long_ext  = ".pdf";
    EXPECT_FALSE(ends_with(short_str, long_ext));

    EXPECT_TRUE(ends_with(empty1, empty2));  // Edge case
    EXPECT_FALSE(ends_with(empty1, prefix4));

    // Test ends_with with string_view
    std::string_view sv3 = "document.pdf";
    std::string_view sv4 = ".pdf";
    EXPECT_TRUE(ends_with(sv3, sv4));
}

void testSourceLocation()
{
    logging::source_location loc;
    loc.file     = "test.cpp";
    loc.function = "testFunction";
    loc.line     = 42;

    // Test basic functionality (skip stream output for now due to linking issues)
    EXPECT_EQ(std::string(loc.file), "test.cpp");
    EXPECT_EQ(std::string(loc.function), "testFunction");
    EXPECT_EQ(loc.line, 42);

    // TODO: Fix operator<< linking issue
    // std::ostringstream oss;
    // oss << loc;
    // EXPECT_EQ(oss.str(), "testFunction at test.cpp:42");
}

void testStringConcatenation()
{
    // Test str_cat with multiple arguments
    std::string result1 = logging::strings::str_cat("Hello", " ", "World");
    EXPECT_EQ(result1, "Hello World");

    // Test str_cat with numeric types
    std::string result2 = logging::strings::str_cat("Value: ", 42);
    EXPECT_EQ(result2, "Value: 42");

    // Test str_cat with floating point
    std::string result3 = logging::strings::str_cat("Pi: ", 3.14);
    EXPECT_TRUE(result3.find("Pi:") != std::string::npos);
    EXPECT_TRUE(result3.find("3.14") != std::string::npos);

    // Test str_cat with empty strings
    std::string result4 = logging::strings::str_cat("", "test", "");
    EXPECT_EQ(result4, "test");

    // Test str_cat with single argument
    std::string result5 = logging::strings::str_cat("single");
    EXPECT_EQ(result5, "single");

    // Test str_cat with string_view
    std::string_view sv      = "view";
    std::string      result6 = logging::strings::str_cat("string_", sv);
    EXPECT_EQ(result6, "string_view");
}

void testStringAppend()
{
    // Test str_append with multiple arguments
    std::string s = "Start";
    logging::strings::str_append(&s, " ", "Middle", " ", 123);
    EXPECT_EQ(s, "Start Middle 123");

    // Test str_append with single argument
    std::string s2 = "Hello";
    logging::strings::str_append(&s2, " World");
    EXPECT_EQ(s2, "Hello World");

    // Test str_append with null pointer (should not crash)
    logging::strings::str_append(nullptr, "test");  // Should be safe

    // Test str_append with empty string
    std::string s3 = "";
    logging::strings::str_append(&s3, "content");
    EXPECT_EQ(s3, "content");

    // Test str_append with numeric types
    std::string s4 = "Numbers: ";
    logging::strings::str_append(&s4, 1, ", ", 2, ", ", 3);
    EXPECT_EQ(s4, "Numbers: 1, 2, 3");
}

void testStringContains()
{
    // Test str_contains with character found
    EXPECT_TRUE(logging::strings::str_contains("hello:world", ':'));

    // Test str_contains with character not found
    EXPECT_FALSE(logging::strings::str_contains("hello", 'x'));

    // Test str_contains with empty string
    EXPECT_FALSE(logging::strings::str_contains("", 'a'));

    // Test str_contains with single character string
    EXPECT_TRUE(logging::strings::str_contains("a", 'a'));

    // Test str_contains with multiple occurrences
    EXPECT_TRUE(logging::strings::str_contains("aaa", 'a'));
}

void testFormatHex()
{
    // Test format_hex with no padding
    std::string hex1 = logging::strings::format_hex(255);
    EXPECT_EQ(hex1, "ff");

    // Test format_hex with padding
    std::string hex2 = logging::strings::format_hex(255, logging::strings::hex_pad::pad4);
    EXPECT_EQ(hex2, "00ff");

    // Test format_hex with larger value
    std::string hex3 = logging::strings::format_hex(0x1234, logging::strings::hex_pad::pad8);
    EXPECT_EQ(hex3, "00001234");

    // Test format_hex with zero
    std::string hex4 = logging::strings::format_hex(0);
    EXPECT_EQ(hex4, "0");

    // Test format_hex with pad2
    std::string hex5 = logging::strings::format_hex(15, logging::strings::hex_pad::pad2);
    EXPECT_EQ(hex5, "0f");
}

void testFormat()
{
    // Zero placeholders / zero arguments
    EXPECT_EQ(logging::strings::format("no placeholders"), "no placeholders");

    // Ordinary substitution
    EXPECT_EQ(logging::strings::format("{} + {}", 1, 2), "1 + 2");
    EXPECT_EQ(logging::strings::format("{}", std::string("text")), "text");
    EXPECT_EQ(logging::strings::format("{}", "literal"), "literal");
    EXPECT_EQ(logging::strings::format("{}", std::string_view("view")), "view");

    // A null const char* contributes an empty placeholder rather than crashing
    const char* null_str = nullptr;
    EXPECT_EQ(logging::strings::format("[{}]", null_str), "[]");

    // bool must render as true/false, not as 1/0
    EXPECT_EQ(logging::strings::format("{} {}", true, false), "true false");

    // Floating point must not be truncated to the stream default of 6
    // significant digits.
    EXPECT_EQ(logging::strings::format("{}", 3.14159265358979), "3.14159265358979");
    EXPECT_EQ(logging::strings::format("{}", 0.1), "0.1");
    EXPECT_EQ(logging::strings::format("{}", 1.5F), "1.5");
    EXPECT_EQ(logging::strings::format("{}", 1.5L), "1.5");

    // More arguments than the std::format dispatch binds directly: every value
    // must still appear in the result.
    const std::string many =
        logging::strings::format("{} {} {} {} {} {} {} {}", 1, 2, 3, 4, 5, 6, 7, 8);
    EXPECT_EQ(many, "1 2 3 4 5 6 7 8");

    const std::string more =
        logging::strings::format("{} {} {} {} {} {} {} {} {} {}", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    EXPECT_EQ(more, "1 2 3 4 5 6 7 8 9 10");

    // Brace escapes survive substitution
    EXPECT_EQ(logging::strings::format("{{{}}}", 7), "{7}");
}

void testToLower()
{
    // Test to_lower with uppercase
    std::string lower1 = logging::strings::to_lower("HELLO");
    EXPECT_EQ(lower1, "hello");

    // Test to_lower with mixed case
    std::string lower2 = logging::strings::to_lower("HeLLo WoRLd");
    EXPECT_EQ(lower2, "hello world");

    // Test to_lower with already lowercase
    std::string lower3 = logging::strings::to_lower("hello");
    EXPECT_EQ(lower3, "hello");

    // Test to_lower with numbers and special chars
    std::string lower4 = logging::strings::to_lower("Test123!@#");
    EXPECT_EQ(lower4, "test123!@#");

    // Test to_lower with empty string
    std::string lower5 = logging::strings::to_lower("");
    EXPECT_EQ(lower5, "");
}

void testDemangleFunction()
{
    // Test demangle with null pointer
    std::string demangled_null = logging::demangle(nullptr);
    EXPECT_EQ(demangled_null, "<unknown>");

    // Test demangle with empty string
    std::string demangled_empty = logging::demangle("");
    EXPECT_EQ(demangled_empty, "<unknown>");

    // Test demangle with simple name (not mangled)
    std::string demangled_simple = logging::demangle("main");
    EXPECT_FALSE(demangled_simple.empty());

    // Test demangle with actual mangled name (if available)
    // This is platform-dependent, so we just verify it doesn't crash
    std::string demangled_complex = logging::demangle("_Z1gv");
    EXPECT_FALSE(demangled_complex.empty());
#if LOGGING_HAS_CXA_DEMANGLE
    EXPECT_EQ(demangled_complex, "g()");
#endif

    // Regression test: demangle() must not strip spaces out of multi-word
    // fundamental type names. "unsigned int" is not a valid mangled symbol,
    // so __cxa_demangle (where available) fails and demangle() falls back
    // to the input unchanged before applying its cleanup passes -- making
    // this assertion valid on every platform, including MSVC where
    // demangling itself is unavailable.
    std::string demangled_unsigned = logging::demangle("unsigned int");
    EXPECT_EQ(demangled_unsigned, "unsigned int");

    // Regression test: nested closing template brackets ("> >") should be
    // collapsed to ">>", without touching other spaces in the string.
    std::string demangled_nested = logging::demangle("std::vector<std::vector<int> >");
    EXPECT_EQ(demangled_nested, "std::vector<std::vector<int>>");

    // Regression test: triple-nested closing brackets ("> > >") must fully
    // collapse to ">>>", not just merge the first adjacent pair.
    std::string demangled_triple_nested =
        logging::demangle("std::vector<std::vector<std::vector<int> > >");
    EXPECT_EQ(demangled_triple_nested, "std::vector<std::vector<std::vector<int>>>");
}

void testReplaceAllEdgeCases()
{
    // Test replace_all with overlapping patterns
    std::string s1     = "aaa";
    size_t      count1 = logging::replace_all(s1, "aa", "b");
    EXPECT_EQ(s1, "ba");
    EXPECT_EQ(count1, 1u);

    // Test replace_all with replacement longer than original
    std::string s2     = "a";
    size_t      count2 = logging::replace_all(s2, "a", "hello");
    EXPECT_EQ(s2, "hello");
    EXPECT_EQ(count2, 1u);

    // Test replace_all with empty replacement
    std::string s3     = "hello";
    size_t      count3 = logging::replace_all(s3, "l", "");
    EXPECT_EQ(s3, "heo");
    EXPECT_EQ(count3, 2u);

    // Test replace_all with single character
    std::string s4     = "aaa";
    size_t      count4 = logging::replace_all(s4, "a", "b");
    EXPECT_EQ(s4, "bbb");
    EXPECT_EQ(count4, 3u);
}

void testEraseAllSubstringEdgeCases()
{
    // Test erase_all_sub_string with single character
    std::string s1 = "aaa";
    logging::erase_all_sub_string(s1, "a");
    EXPECT_EQ(s1, "");

    // Test erase_all_sub_string with multi-character substring
    std::string s2 = "abcabcabc";
    logging::erase_all_sub_string(s2, "abc");
    EXPECT_EQ(s2, "");

    // Test erase_all_sub_string with partial match
    std::string s3 = "abcdefabc";
    logging::erase_all_sub_string(s3, "abc");
    EXPECT_EQ(s3, "def");

    // Test erase_all_sub_string with no match
    std::string s4 = "hello";
    logging::erase_all_sub_string(s4, "xyz");
    EXPECT_EQ(s4, "hello");
}

void testStartsWithEdgeCases()
{
    // Test starts_with with equal strings
    EXPECT_TRUE(logging::starts_with("hello", "hello"));

    // Test starts_with with longer prefix than string
    EXPECT_FALSE(logging::starts_with("hi", "hello"));

    // Test starts_with with single character
    EXPECT_TRUE(logging::starts_with("hello", "h"));

    // Test starts_with with empty prefix
    EXPECT_TRUE(logging::starts_with("hello", ""));

    // Test starts_with with empty string and non-empty prefix
    EXPECT_FALSE(logging::starts_with("", "a"));
}

void testEndsWithEdgeCases()
{
    // Test ends_with with equal strings
    EXPECT_TRUE(logging::ends_with("hello", "hello"));

    // Test ends_with with longer suffix than string
    EXPECT_FALSE(logging::ends_with("hi", "hello"));

    // Test ends_with with single character
    EXPECT_TRUE(logging::ends_with("hello", "o"));

    // Test ends_with with empty suffix
    EXPECT_TRUE(logging::ends_with("hello", ""));

    // Test ends_with with empty string and non-empty suffix
    EXPECT_FALSE(logging::ends_with("", "a"));
}

void testAllFunctions()
{
    testStringManipulation();
    testCompatibilityFunctions();
    testSourceLocation();
    testStringConcatenation();
    testStringAppend();
    testStringContains();
    testFormatHex();
    testFormat();
    testToLower();
    testDemangleFunction();
    testReplaceAllEdgeCases();
    testEraseAllSubstringEdgeCases();
    testStartsWithEdgeCases();
    testEndsWithEdgeCases();
}

}  // namespace logging

TEST(StringUtil, test)
{
    logging::testAllFunctions();
}
