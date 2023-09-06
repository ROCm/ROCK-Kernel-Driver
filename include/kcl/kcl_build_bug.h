/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_LINUX_BUILD_BUG_H
#define AMDKCL_LINUX_BUILD_BUG_H

#include <linux/build_bug.h>

#ifndef static_assert
#define static_assert(expr, ...) __static_assert(expr, ##__VA_ARGS__, #expr)
#define __static_assert(expr, msg, ...) _Static_assert(expr, msg)
#endif

#endif /* AMDKCL_LINUX_BUILD_BUG_H */