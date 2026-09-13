/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

#include <atomic>
#include <string>
#include <thread>

#include "LoggingTest.h"
#include "logger/logger.h"

TEST(Logger, thread_name_is_per_thread)
{
    std::atomic_bool t1_ready{false};
    std::atomic_bool t2_ready{false};
    std::string      t1_seen;
    std::string      t2_seen;

    std::thread t1(
        [&]()
        {
            logging::logger::set_thread_name("T1");
            t1_ready.store(true);
            while (!t2_ready.load()) {}
            t1_seen = logging::logger::get_thread_name();
        });
    std::thread t2(
        [&]()
        {
            logging::logger::set_thread_name("T2");
            t2_ready.store(true);
            while (!t1_ready.load()) {}
            logging::logger::init();
            t2_seen = logging::logger::get_thread_name();
        });

    t1.join();
    t2.join();

    EXPECT_EQ(t1_seen, "T1");
    EXPECT_EQ(t2_seen, "T2");
}
