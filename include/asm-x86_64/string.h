#ifndef _X86_64_STRING_H_
#define _X86_64_STRING_H_

#ifdef __KERNEL__
#include <linux/config.h>

#define struct_cpy(x,y) (*(x)=*(y))

#define __HAVE_ARCH_MEMCMP
#define __HAVE_ARCH_STRLEN

#define memset __builtin_memset
#define memcpy __builtin_memcpy
#define memcmp __builtin_memcmp

/* Work around "undefined reference to strlen" linker errors.  */
/* #define strlen __builtin_strlen */

#define __HAVE_ARCH_STRLEN
static inline size_t strlen(const char * s)
{
int d0;
register int __res;
__asm__ __volatile__(
	"repne\n\t"
	"scasb\n\t"
	"notl %0\n\t"
	"decl %0"
	:"=c" (__res), "=&D" (d0) :"1" (s),"a" (0), "0" (0xffffffff));
return __res;
}


extern char *strstr(const char *cs, const char *ct);

#endif /* __KERNEL__ */

#endif
