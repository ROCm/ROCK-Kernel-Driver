#ifndef H8300_DIV64_H
#define H8300_DIV64_H

/* n = n / base; return rem; */

#define do_div(n,base) ({					\
	int __res;						\
	__res = ((unsigned long) n) % (unsigned) base;		\
	n = ((unsigned long) n) / (unsigned) base;		\
	__res;							\
})

#endif /* _H8300_DIV64_H */
