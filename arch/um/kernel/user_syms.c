#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/ioctl.h>
#include "user_util.h"
#include "mem_user.h"
#include "uml-config.h"

/* Had to steal this from linux/module.h because that file can't be included
 * since this includes various user-level headers.
 */

/* Had to update this: this changed in late 2.5 to add CRC and other beasts
 * and was never updated here- 13 Dec 2003-Blaisorblade*/

/* v850 toolchain uses a `_' prefix for all user symbols */
#ifndef MODULE_SYMBOL_PREFIX
#define MODULE_SYMBOL_PREFIX ""
#endif

struct kernel_symbol
{
	unsigned long value;
	const char *name;
};

#if !defined(UML_CONFIG_MODULES)
#define EXPORT_SYMBOL(sym)
#define EXPORT_SYMBOL_GPL(sym)
#define EXPORT_SYMBOL_NOVERS(sym)

#else /*UML_CONFIG_MODULES*/
#ifndef __GENKSYMS__
#ifdef UML_CONFIG_MODVERSIONS
/* Mark the CRC weak since genksyms apparently decides not to
 * generate a checksums for some symbols */
#define __CRC_SYMBOL(sym, sec)					\
	extern void *__crc_##sym __attribute__((weak));		\
	static const unsigned long __kcrctab_##sym		\
	__attribute__((section("__kcrctab" sec), unused))	\
	= (unsigned long) &__crc_##sym;
#else
#define __CRC_SYMBOL(sym, sec)
#endif

/* For every exported symbol, place a struct in the __ksymtab section */
#define __EXPORT_SYMBOL(sym, sec)				\
	__CRC_SYMBOL(sym, sec)					\
	static const char __kstrtab_##sym[]			\
	__attribute__((section("__ksymtab_strings")))		\
	= MODULE_SYMBOL_PREFIX #sym;                    	\
	static const struct kernel_symbol __ksymtab_##sym	\
	__attribute__((section("__ksymtab" sec), unused))	\
	= { (unsigned long)&sym, __kstrtab_##sym }

#define EXPORT_SYMBOL(sym)					\
	__EXPORT_SYMBOL(sym, "")

#define EXPORT_SYMBOL_GPL(sym)					\
	__EXPORT_SYMBOL(sym, "_gpl")

#endif

/* We don't mangle the actual symbol anymore, so no need for
 * special casing EXPORT_SYMBOL_NOVERS.  FIXME: Deprecated */
#define EXPORT_SYMBOL_NOVERS(sym) EXPORT_SYMBOL(sym)
#endif
#if 0
struct module_symbol
{
	unsigned long value;
	const char *name;
};

/* Indirect stringification.  */

#define __MODULE_STRING_1(x)	#x
#define __MODULE_STRING(x)	__MODULE_STRING_1(x)

#if !defined(AUTOCONF_INCLUDED)

#define __EXPORT_SYMBOL(sym,str)   error config_must_be_included_before_module
#define EXPORT_SYMBOL(var)	   error config_must_be_included_before_module
#define EXPORT_SYMBOL_NOVERS(var)  error config_must_be_included_before_module

#elif !defined(UML_CONFIG_MODULES)

#define __EXPORT_SYMBOL(sym,str)
#define EXPORT_SYMBOL(var)
#define EXPORT_SYMBOL_NOVERS(var)

#else

#define __EXPORT_SYMBOL(sym, str)			\
const char __kstrtab_##sym[]				\
__attribute__((section(".kstrtab"))) = str;		\
const struct module_symbol __ksymtab_##sym 		\
__attribute__((section("__ksymtab"))) =			\
{ (unsigned long)&sym, __kstrtab_##sym }

#if defined(__MODVERSIONS__) || !defined(UML_CONFIG_MODVERSIONS)
#define EXPORT_SYMBOL(var)  __EXPORT_SYMBOL(var, __MODULE_STRING(var))
#else
#define EXPORT_SYMBOL(var)  __EXPORT_SYMBOL(var, __MODULE_STRING(__VERSIONED_SYMBOL(var)))
#endif

#define EXPORT_SYMBOL_NOVERS(var)  __EXPORT_SYMBOL(var, __MODULE_STRING(var))

#endif
#endif

EXPORT_SYMBOL(__errno_location);

EXPORT_SYMBOL(access);
EXPORT_SYMBOL(open);
EXPORT_SYMBOL(open64);
EXPORT_SYMBOL(close);
EXPORT_SYMBOL(read);
EXPORT_SYMBOL(write);
EXPORT_SYMBOL(dup2);
EXPORT_SYMBOL(__xstat);
EXPORT_SYMBOL(__lxstat);
EXPORT_SYMBOL(__lxstat64);
EXPORT_SYMBOL(lseek);
EXPORT_SYMBOL(lseek64);
EXPORT_SYMBOL(chown);
EXPORT_SYMBOL(truncate);
EXPORT_SYMBOL(utime);
EXPORT_SYMBOL(chmod);
EXPORT_SYMBOL(rename);
EXPORT_SYMBOL(__xmknod);

EXPORT_SYMBOL(symlink);
EXPORT_SYMBOL(link);
EXPORT_SYMBOL(unlink);
EXPORT_SYMBOL(readlink);

EXPORT_SYMBOL(mkdir);
EXPORT_SYMBOL(rmdir);
EXPORT_SYMBOL(opendir);
EXPORT_SYMBOL(readdir);
EXPORT_SYMBOL(closedir);
EXPORT_SYMBOL(seekdir);
EXPORT_SYMBOL(telldir);

EXPORT_SYMBOL(ioctl);

extern ssize_t pread64 (int __fd, void *__buf, size_t __nbytes,
			__off64_t __offset);
extern ssize_t pwrite64 (int __fd, __const void *__buf, size_t __n,
			 __off64_t __offset);
EXPORT_SYMBOL(pread64);
EXPORT_SYMBOL(pwrite64);

EXPORT_SYMBOL(statfs);
EXPORT_SYMBOL(statfs64);

EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(getuid);

EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(printf);
EXPORT_SYMBOL(strlen);

EXPORT_SYMBOL(find_iomem);
