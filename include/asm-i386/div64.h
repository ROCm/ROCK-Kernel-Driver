#ifndef __I386_DIV64
#define __I386_DIV64

#define do_div(n,base) ({ \
	unsigned long __upper, __low, __high, __mod; \
	asm("":"=a" (__low), "=d" (__high):"A" (n)); \
	__upper = __high; \
	if (__high) { \
		__upper = __high % (base); \
		__high = __high / (base); \
	} \
	asm("divl %2":"=a" (__low), "=d" (__mod):"rm" (base), "0" (__low), "1" (__upper)); \
	asm("":"=A" (n):"a" (__low),"d" (__high)); \
	__mod; \
})

/*
 * (long)X = ((long long)divs) / (long)div
 * (long)rem = ((long long)divs) % (long)div
 *
 * Warning, this will do an exception if X overflows.
 */
#define div_long_long_rem(a,b,c) div_ll_X_l_rem(a,b,c)

extern inline long
div_ll_X_l_rem(long long divs, long div, long *rem)
{
	long dum2;
      __asm__("divl %2":"=a"(dum2), "=d"(*rem)
      :	"rm"(div), "A"(divs));

	return dum2;

}
#endif
