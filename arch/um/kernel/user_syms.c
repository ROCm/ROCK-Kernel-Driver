#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
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

struct module_symbol
{
	unsigned long value;
	const char *name;
};

/* Indirect stringification.  */

#define __MODULE_STRING_1(x)	#x
#define __MODULE_STRING(x)	__MODULE_STRING_1(x)

#if !defined(__AUTOCONF_INCLUDED__)

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

EXPORT_SYMBOL(find_iomem);
