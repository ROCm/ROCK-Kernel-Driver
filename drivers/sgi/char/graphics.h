#define MAXCARDS 4

struct graphics_ops {
	/* SGIism: Board owner, gets the shmiq requests from the kernel */
	struct task_struct *g_owner;

	/* Last process that got the graphics registers mapped  */
	struct task_struct *g_user;
	
	/* Board info */
	void               *g_board_info;
	int                g_board_info_len;

	/* These point to hardware registers that should be mapped with
	 * GFX_ATTACH_BOARD and the size of the information pointed to
	 */
	unsigned long      g_regs;
	int                g_regs_size;

	void (*g_save_context)(void *);
	void (*g_restore_context)(void *);
	void (*g_reset_console)(void);
	int  (*g_ioctl)(int device, int cmd, unsigned long arg);
};

void shmiq_init (void);
void streamable_init (void);
