#ifndef __V850_DIV64_H__
#define __V850_DIV64_H__

/* We're not 64-bit, but... */
#define do_div(n,base) ({ \
	int __res; \
	__res = ((unsigned long) n) % (unsigned) base; \
	n = ((unsigned long) n) / (unsigned) base; \
	__res; })

#endif /* __V850_DIV64_H__ */
