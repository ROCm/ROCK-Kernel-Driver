/*
 * Linux Rendering Resource Manager 
 *
 *          Implements the SGI-compatible rendering resource manager.
 *          This takes care of implementing the virtualized video hardware
 *          access required for OpenGL direct rendering.
 *
 * Author:  Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * Fixes:
 */
#include <asm/uaccess.h>
#include <asm/rrm.h>

#ifdef MODULE
#include <linux/module.h>
#endif

int
rrm_open_rn (int rnid, void *arg)
{
	return 0;
}

int
rrm_close_rn (int rnid, void *arg)
{
	return 0;
}

int
rrm_bind_proc_to_rn (int rnid, void *arg)
{
	return 0;
}

typedef int (*rrm_function )(void *arg);

struct {
	int (*r_fn)(int rnid, void *arg);
	int arg_size;
} rrm_functions [] = {
	{ rrm_open_rn,         sizeof (struct RRM_OpenRN) },
	{ rrm_close_rn,        sizeof (struct RRM_CloseRN) },
	{ rrm_bind_proc_to_rn, sizeof (struct RRM_BindProcToRN) }
};

#define RRM_FUNCTIONS (sizeof (rrm_functions)/sizeof (rrm_functions [0]))

/* cmd is a number in the range [0..RRM_CMD_LIMIT-RRM_BASE] */
int
rrm_command (unsigned int cmd, void *arg)
{
	int i, rnid;
	
	if (cmd > RRM_FUNCTIONS){
		printk ("Called unimplemented rrm ioctl: %d\n", cmd + RRM_BASE);
		return -EINVAL;
	}
	i = verify_area (VERIFY_READ, arg, rrm_functions [cmd].arg_size);
	if (i) return i;

	if (__get_user (rnid, (int *) arg))
		return -EFAULT;
	return (*(rrm_functions [cmd].r_fn))(rnid, arg);
}

int
rrm_close (struct inode *inode, struct file *file)
{
	/* This routine is invoked when the device is closed */
	return 0;
}
