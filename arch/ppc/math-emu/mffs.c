/* $Id: mffs.c,v 1.1 1999/08/23 19:00:14 cort Exp $
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "soft-fp.h"

int
mffs(u32 *frD)
{
	frD[1] = __FPU_FPSCR;

#ifdef DEBUG
	printk("%s: frD %p: %08x.%08x\n", __FUNCTION__, frD, frD[0], frD[1]);
#endif

	return 0;
}
