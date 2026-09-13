/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 */

#include <cstdlib>

#include "logger/logger.h"

int main(int /*unused*/, char* /*unused*/[])
{
    logging::logger::set_enable_unsafe_signal_handler(false);
    logging::logger::init();
    abort();
    return 0;
}
