#ifndef __ASM_SH_DIV64
#define __ASM_SH_DIV64

#define do_div(n,base) ({ \
int __res; \
__res = ((unsigned long) n) % (unsigned) base; \
n = ((unsigned long) n) / (unsigned) base; \
__res; })

#endif /* __ASM_SH_DIV64 */
