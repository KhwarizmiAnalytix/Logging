/*
 * Logging library — shared-pointer aliases (standalone; mirrors Core common/pointer.h).
 *
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
#pragma once

#include <memory>

namespace logging
{
template <typename T>
using ptr_const = std::shared_ptr<const T>;

template <typename T>
using ptr_mutable = std::shared_ptr<T>;

template <typename T>
using ptr_unique_const = std::unique_ptr<const T>;

template <typename T>
using ptr_unique_mutable = std::unique_ptr<T>;

namespace util
{
template <typename T, typename... Args>
std::shared_ptr<const T> make_ptr_const(Args&&... args)
{
    return std::make_shared<const T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::shared_ptr<T> make_ptr_mutable(Args&&... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::unique_ptr<const T> make_ptr_unique_const(Args&&... args)
{
    return std::make_unique<const T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::unique_ptr<T> make_ptr_unique_mutable(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::shared_ptr<T> make_shared(Args&&... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}
}  // namespace util
}  // namespace logging
