/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_BACKPORT_KCL_MIGRATE_H
#define _KCL_BACKPORT_KCL_MIGRATE_H

#include <linux/migrate.h>

/* Compatibility with kernels before ab09243aa95a ("mm/migrate.c: remove
 * MIGRATE_PFN_LOCKED")
 */
#ifndef MIGRATE_PFN_LOCKED
#define MIGRATE_PFN_LOCKED 0
#endif

#endif
