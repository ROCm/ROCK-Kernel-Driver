/*
 * SGI Rendering Resource Manager API (?).
 *
 * written by Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * Ok, even if SGI choosed to do mmap trough ioctls, their
 * kernel support for virtualizing the graphics card is nice.
 *
 * We should be able to make graphic applications on Linux
 * fly.
 *
 * This header file should be included from GNU libc as well.
 */


/* Why like this you say?  Well, gdb can print enums */
#define RRM_BASE 1000
#define RRM_CMD_LIMIT (RRM_BASE + 100)

enum {
	RRM_OPENRN = RRM_BASE,	/* open rendering node */
	RRM_CLOSERN,
	RRM_BINDPROCTORN,	/* set current rendering region for node */
	RRM_BINDRNTOCLIP,
	RRM_UNBINDRNFROMCLIP,
	RRM_SWAPBUF,
	RRM_SETSWAPINTERVAL,
	RRM_WAITFORRETRACE,
	RRM_SETDISPLAYMODE,
	RRM_MESSAGE,
	RRM_INVALIDATERN,
	RRM_VALIDATECLIP,
	RRM_VALIDATESWAPBUF,
	RRM_SWAPGROUP,
	RRM_SWAPUNGROUP,
	RRM_VALIDATEMESSAGE,
	RRM_GETDISPLAYMODES,
	RRM_LOADDISPLAYMODE,
	RRM_CUSHIONBUFFER,
	RRM_SWAPREADY,
	RRM_MGR_SWAPBUF,
	RRM_SETVSYNC,
	RRM_GETVSYNC,
	RRM_WAITVSYNC,
	RRM_BINDRNTOREADANDCLIP,
	RRM_MAPCLIPTOSWPBUFID
};

/* Parameters for the above ioctls
 *
 * All of the ioctls take as their first argument the rendering node id.
 *
 */

/*
 * RRM_OPENRN:
 *
 * This is called by the IRIX X server with:
 * rnid = 0xffffffff rmask = 0
 *
 * Returns a number like this: 0x10001.
 * If you run the X server over and over, you get a value
 * that is of the form (n * 0x10000) + 1.
 *
 * The return value seems to be the RNID.
 */
struct RRM_OpenRN {
	int      rnid;
	unsigned int rmask;
};

struct RRM_CloseRN {
	int rnid;
};

/*
 * RRM_BINDPROCTORN:
 *
 * Return value when the X server calls it: 0
 */ 
struct RRM_BindProcToRN {
	int      rnid;
};

#ifdef __KERNEL__
int rrm_command (unsigned int cmd, void *arg);
int rrm_close (struct inode *inode, struct file *file);
#endif
