/*
 * BK Id: SCCS/s.frsqrte.c 1.6 05/17/01 18:14:22 cort
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

int
frsqrte(void *frD, void *frB)
{
#ifdef DEBUG
	printk("%s: %p %p\n", __FUNCTION__, frD, frB);
#endif
	return 0;
}
