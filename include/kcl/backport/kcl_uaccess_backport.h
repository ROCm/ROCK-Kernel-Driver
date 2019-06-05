/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_UACCESS_BACKPORT_H
#define AMDKCL_UACCESS_BACKPORT_H
#include <linux/uaccess.h>

#if !defined(HAVE_ACCESS_OK_WITH_TWO_ARGUMENTS)
static inline int _kcl_access_ok(unsigned long addr, unsigned long size)
{
	return access_ok(VERIFY_WRITE, (addr), (size));
}
#undef access_ok
#define access_ok _kcl_access_ok
#endif
#endif
