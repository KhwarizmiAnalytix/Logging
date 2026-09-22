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

// This translation unit simulates being compiled by the CUDA/HIP device
// compiler (nvcc/Clang-CUDA or hipcc) *without* requiring an actual GPU
// toolchain to be installed. __HIPCC__ is defined before the first
// inclusion of exception.h so that LOGGING_CHECK_IF_NOT_ON_CUDA and
// LOGGING_CHECK_DEBUG_IF_NOT_ON_CUDA pick their "compiled to nothing"
// branch, exactly as they would under nvcc/hipcc. No other header in this
// repository inspects __CUDACC__/__HIPCC__, so this definition is safely
// scoped to this one file.
#define __HIPCC__ 1

#include "LoggingTest.h"
#include "include/util/exception.h"

using namespace logging;

// ============================================================================
// LOGGING_CHECK_IF_NOT_ON_CUDA / LOGGING_CHECK_DEBUG_IF_NOT_ON_CUDA under a
// simulated CUDA/HIP device compiler: both macros must expand to nothing, so
// a failing condition must neither throw nor abort.
// ============================================================================

TEST(ExceptionCudaGuard, check_if_not_on_cuda_is_elided_under_device_compiler)
{
    logging::set_exception_mode(logging::exception_mode::THROW);

    // Would throw under LOGGING_CHECK; must be a silent no-op here.
    LOGGING_CHECK_IF_NOT_ON_CUDA(false, "elided under __HIPCC__");
    LOGGING_CHECK_IF_NOT_ON_CUDA(1 == 2);

    SUCCEED();
}

TEST(ExceptionCudaGuard, check_debug_if_not_on_cuda_is_elided_under_device_compiler)
{
    logging::set_exception_mode(logging::exception_mode::THROW);

    // Would throw under LOGGING_CHECK_DEBUG in a debug build; must still be
    // a silent no-op here regardless of NDEBUG.
    LOGGING_CHECK_DEBUG_IF_NOT_ON_CUDA(false, "elided under __HIPCC__");
    LOGGING_CHECK_DEBUG_IF_NOT_ON_CUDA(1 == 2);

    SUCCEED();
}
