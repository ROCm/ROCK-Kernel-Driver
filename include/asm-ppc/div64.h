/*
 * BK Id: SCCS/s.div64.h 1.5 05/17/01 18:14:24 cort
 */
#ifndef __PPC_DIV64
#define __PPC_DIV64

#define do_div(n,base) ({ \
int __res; \
__res = ((unsigned long) n) % (unsigned) base; \
n = ((unsigned long) n) / (unsigned) base; \
__res; })

#endif
