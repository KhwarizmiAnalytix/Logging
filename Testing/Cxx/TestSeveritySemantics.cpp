/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

#include <gtest/gtest.h>

#include "level.h"

// Pins the library-owned severity ordering and the single should_log()/is_fatal()
// gate that every macro and template routes through. The conventional ordering is
// "larger value == more severe", so at a given threshold every equal-or-more-severe
// message is emitted and every less-severe message is dropped. A regression that
// flips the comparison (as an earlier revision did) would fail here immediately.

using logging::is_fatal;
using logging::level;
using logging::should_log;

TEST(SeveritySemantics, Ordering)
{
    EXPECT_LT(static_cast<int>(level::trace), static_cast<int>(level::debug));
    EXPECT_LT(static_cast<int>(level::debug), static_cast<int>(level::info));
    EXPECT_LT(static_cast<int>(level::info), static_cast<int>(level::warn));
    EXPECT_LT(static_cast<int>(level::warn), static_cast<int>(level::error));
    EXPECT_LT(static_cast<int>(level::error), static_cast<int>(level::critical));
    EXPECT_LT(static_cast<int>(level::critical), static_cast<int>(level::off));
}

TEST(SeveritySemantics, InfoThreshold)
{
    // The exact truth table the reviewer called out: at an info threshold,
    // warn/error/critical pass and debug/trace are dropped.
    EXPECT_FALSE(should_log(level::trace, level::info));
    EXPECT_FALSE(should_log(level::debug, level::info));
    EXPECT_TRUE(should_log(level::info, level::info));
    EXPECT_TRUE(should_log(level::warn, level::info));
    EXPECT_TRUE(should_log(level::error, level::info));
    EXPECT_TRUE(should_log(level::critical, level::info));
}

TEST(SeveritySemantics, TraceThresholdPassesEverything)
{
    EXPECT_TRUE(should_log(level::trace, level::trace));
    EXPECT_TRUE(should_log(level::debug, level::trace));
    EXPECT_TRUE(should_log(level::critical, level::trace));
}

TEST(SeveritySemantics, ErrorThresholdDropsWarnAndBelow)
{
    EXPECT_FALSE(should_log(level::info, level::error));
    EXPECT_FALSE(should_log(level::warn, level::error));
    EXPECT_TRUE(should_log(level::error, level::error));
    EXPECT_TRUE(should_log(level::critical, level::error));
}

TEST(SeveritySemantics, OffThresholdDropsEverything)
{
    // off is a sentinel meaning "emit nothing"; even critical is filtered by the
    // gate itself. Callers keep critical alive via the separate is_fatal() bypass.
    EXPECT_FALSE(should_log(level::trace, level::off));
    EXPECT_FALSE(should_log(level::info, level::off));
    EXPECT_FALSE(should_log(level::critical, level::off));
}

TEST(SeveritySemantics, FatalBypassesFiltering)
{
    // Fatal/critical must survive any threshold, including off, because macros
    // OR is_fatal() with should_log() before emitting.
    EXPECT_TRUE(is_fatal(level::critical));
    EXPECT_FALSE(is_fatal(level::error));
    EXPECT_FALSE(is_fatal(level::info));

    for (level threshold : {level::trace, level::info, level::error, level::off})
    {
        EXPECT_TRUE(is_fatal(level::critical) || should_log(level::critical, threshold))
            << "critical was filtered at threshold " << static_cast<int>(threshold);
    }
}
