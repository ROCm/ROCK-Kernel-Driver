#ifndef __UM_BUG_H
#define __UM_BUG_H

#ifndef __ASSEMBLY__

#define BUG() do { \
	panic("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
} while (0)

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

extern int foo;

#endif

#endif
