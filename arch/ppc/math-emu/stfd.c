/*
 * BK Id: SCCS/s.stfd.c 1.6 05/17/01 18:14:23 cort
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
 
int
stfd(void *frS, void *ea)
{
#if 0
#ifdef DEBUG
	printk("%s: S %p, ea %p: ", __FUNCTION__, frS, ea);
	dump_double(frS);
	printk("\n");
#endif
#endif

	if (copy_to_user(ea, frS, sizeof(double)))
		return -EFAULT;

	return 0;
}
