/* $Id: fres.c,v 1.1 1999/08/23 18:59:56 cort Exp $
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

int
fres(void *frD, void *frB)
{
#ifdef DEBUG
	printk("%s: %p %p\n", __FUNCTION__, frD, frB);
#endif
	return -ENOSYS;
}
