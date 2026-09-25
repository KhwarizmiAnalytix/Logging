/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

#include <gtest/gtest.h>

#include "include/logger/logger_verbosity_enum.h"

// Pins the Loguru-numbered severity ordering and the single should_log()/is_fatal()
// gate that every macro routes through. Loguru numbering is inverted from the
// conventional ordering: "smaller (more negative) value == more severe", so at a
// given threshold every equal-or-more-severe message is emitted and every less
// severe message is dropped. A regression that flips the comparison direction
// would fail here immediately.

using logging::is_fatal;
using logging::logger_verbosity_enum;
using logging::should_log;

TEST(SeveritySemantics, Ordering)
{
    EXPECT_LT(static_cast<int>(logger_verbosity_enum::VERBOSITY_OFF),
        static_cast<int>(logger_verbosity_enum::VERBOSITY_FATAL));
    EXPECT_LT(static_cast<int>(logger_verbosity_enum::VERBOSITY_FATAL),
        static_cast<int>(logger_verbosity_enum::VERBOSITY_ERROR));
    EXPECT_LT(static_cast<int>(logger_verbosity_enum::VERBOSITY_ERROR),
        static_cast<int>(logger_verbosity_enum::VERBOSITY_WARNING));
    EXPECT_LT(static_cast<int>(logger_verbosity_enum::VERBOSITY_WARNING),
        static_cast<int>(logger_verbosity_enum::VERBOSITY_INFO));
    EXPECT_LT(static_cast<int>(logger_verbosity_enum::VERBOSITY_INFO),
        static_cast<int>(logger_verbosity_enum::VERBOSITY_TRACE));
}

TEST(SeveritySemantics, InfoThreshold)
{
    // At an info threshold, fatal/error/warning/info pass and trace is dropped.
    EXPECT_FALSE(should_log(logger_verbosity_enum::VERBOSITY_TRACE, logger_verbosity_enum::VERBOSITY_INFO));
    EXPECT_TRUE(should_log(logger_verbosity_enum::VERBOSITY_INFO, logger_verbosity_enum::VERBOSITY_INFO));
    EXPECT_TRUE(should_log(logger_verbosity_enum::VERBOSITY_WARNING, logger_verbosity_enum::VERBOSITY_INFO));
    EXPECT_TRUE(should_log(logger_verbosity_enum::VERBOSITY_ERROR, logger_verbosity_enum::VERBOSITY_INFO));
    EXPECT_TRUE(should_log(logger_verbosity_enum::VERBOSITY_FATAL, logger_verbosity_enum::VERBOSITY_INFO));
}

TEST(SeveritySemantics, TraceThresholdPassesEverything)
{
    EXPECT_TRUE(should_log(logger_verbosity_enum::VERBOSITY_TRACE, logger_verbosity_enum::VERBOSITY_TRACE));
    EXPECT_TRUE(should_log(logger_verbosity_enum::VERBOSITY_INFO, logger_verbosity_enum::VERBOSITY_TRACE));
    EXPECT_TRUE(should_log(logger_verbosity_enum::VERBOSITY_FATAL, logger_verbosity_enum::VERBOSITY_TRACE));
}

TEST(SeveritySemantics, ErrorThresholdDropsWarningAndBelow)
{
    EXPECT_FALSE(should_log(logger_verbosity_enum::VERBOSITY_TRACE, logger_verbosity_enum::VERBOSITY_ERROR));
    EXPECT_FALSE(should_log(logger_verbosity_enum::VERBOSITY_INFO, logger_verbosity_enum::VERBOSITY_ERROR));
    EXPECT_FALSE(should_log(logger_verbosity_enum::VERBOSITY_WARNING, logger_verbosity_enum::VERBOSITY_ERROR));
    EXPECT_TRUE(should_log(logger_verbosity_enum::VERBOSITY_ERROR, logger_verbosity_enum::VERBOSITY_ERROR));
    EXPECT_TRUE(should_log(logger_verbosity_enum::VERBOSITY_FATAL, logger_verbosity_enum::VERBOSITY_ERROR));
}

TEST(SeveritySemantics, OffThresholdDropsEverything)
{
    // off is a sentinel meaning "emit nothing"; even fatal is filtered by the
    // gate itself. Callers keep fatal alive via the separate is_fatal() bypass.
    EXPECT_FALSE(should_log(logger_verbosity_enum::VERBOSITY_TRACE, logger_verbosity_enum::VERBOSITY_OFF));
    EXPECT_FALSE(should_log(logger_verbosity_enum::VERBOSITY_INFO, logger_verbosity_enum::VERBOSITY_OFF));
    EXPECT_FALSE(should_log(logger_verbosity_enum::VERBOSITY_FATAL, logger_verbosity_enum::VERBOSITY_OFF));
}

TEST(SeveritySemantics, FatalBypassesFiltering)
{
    // Fatal must survive any threshold, including off, because macros OR
    // is_fatal() with should_log() before emitting.
    EXPECT_TRUE(is_fatal(logger_verbosity_enum::VERBOSITY_FATAL));
    EXPECT_FALSE(is_fatal(logger_verbosity_enum::VERBOSITY_ERROR));
    EXPECT_FALSE(is_fatal(logger_verbosity_enum::VERBOSITY_INFO));

    for (logger_verbosity_enum threshold : {logger_verbosity_enum::VERBOSITY_TRACE,
             logger_verbosity_enum::VERBOSITY_INFO,
             logger_verbosity_enum::VERBOSITY_ERROR,
             logger_verbosity_enum::VERBOSITY_OFF})
    {
        EXPECT_TRUE(is_fatal(logger_verbosity_enum::VERBOSITY_FATAL) ||
            should_log(logger_verbosity_enum::VERBOSITY_FATAL, threshold))
            << "fatal was filtered at threshold " << static_cast<int>(threshold);
    }
}
