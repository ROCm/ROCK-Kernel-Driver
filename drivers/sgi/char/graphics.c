/* $Id: graphics.c,v 1.22 2000/02/18 00:24:43 ralf Exp $
 *
 * gfx.c: support for SGI's /dev/graphics, /dev/opengl
 *
 * Author: Miguel de Icaza (miguel@nuclecu.unam.mx)
 *         Ralf Baechle (ralf@gnu.org)
 *         Ulf Carlsson (ulfc@bun.falkenberg.se)
 *
 * On IRIX, /dev/graphics is [10, 146]
 *          /dev/opengl   is [10, 147]
 *
 * From a mail with Mark J. Kilgard, /dev/opengl and /dev/graphics are
 * the same thing, the use of /dev/graphics seems deprecated though.
 *
 * The reason that the original SGI programmer had to use only one
 * device for all the graphic cards on the system will remain a
 * mistery for the rest of our lives.  Why some ioctls take a board
 * number and some others not?  Mistery.  Why do they map the hardware
 * registers into the user address space with an ioctl instead of
 * mmap?  Mistery too.  Why they did not use the standard way of
 * making ioctl constants and instead sticked a random constant?
 * Mistery too.
 *
 * We implement those misterious things, and tried not to think about
 * the reasons behind them.
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/malloc.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include "gconsole.h"
#include "graphics.h"
#include "usema.h"
#include <asm/gfx.h>
#include <asm/rrm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <video/newport.h>

#define DEBUG

/* The boards */
extern struct graphics_ops *newport_probe (int, const char **);

static struct graphics_ops cards [MAXCARDS];
static int boards;

#define GRAPHICS_CARD(inode) 0

/*
void enable_gconsole(void) {};
void disable_gconsole(void) {};
*/


int
sgi_graphics_open (struct inode *inode, struct file *file)
{
	struct newport_regs *nregs =
		(struct newport_regs *) KSEG1ADDR(cards[0].g_regs);

	newport_wait();
	nregs->set.wrmask = 0xffffffff;
	nregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX |
			     NPORT_DMODE0_STOPY);
	nregs->set.colori = 1;
	nregs->set.xystarti = (0 << 16) | 0;
	nregs->go.xyendi = (1280 << 16) | 1024;

	return 0;
}

int
sgi_graphics_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int board;
	unsigned int devnum = GRAPHICS_CARD (inode->i_rdev);
	int i;

	if ((cmd >= RRM_BASE) && (cmd <= RRM_CMD_LIMIT))
		return rrm_command (cmd-RRM_BASE, (void *) arg);

	switch (cmd){
	case GFX_GETNUM_BOARDS:
		return boards;

	case GFX_GETBOARD_INFO: {
		struct gfx_getboardinfo_args *bia = (void *) arg;
		void   *dest_buf;
		int    max_len;
		
		i = verify_area (VERIFY_READ, (void *) arg, sizeof (struct gfx_getboardinfo_args));
		if (i) return i;
		
		if (__get_user (board,    &bia->board) ||
		    __get_user (dest_buf, &bia->buf) ||
		    __get_user (max_len,  &bia->len))
			return -EFAULT;

		if (board >= boards)
			return -EINVAL;
		if (max_len < sizeof (struct gfx_getboardinfo_args))
			return -EINVAL;
		if (max_len > cards [board].g_board_info_len)
			max_len = cards [boards].g_board_info_len;
		i = verify_area (VERIFY_WRITE, dest_buf, max_len);
		if (i) return i;
		if (copy_to_user (dest_buf, cards [board].g_board_info, max_len))
			return -EFAULT;
		return max_len;
	}

	case GFX_ATTACH_BOARD: {
		struct gfx_attach_board_args *att = (void *) arg;
		void *vaddr;
		int  r;

		i = verify_area (VERIFY_READ, (void *)arg, sizeof (struct gfx_attach_board_args));
		if (i) return i;

		if (__get_user (board, &att->board) ||
		    __get_user (vaddr, &att->vaddr))
			return -EFAULT;

		/* Ok for now we are assuming /dev/graphicsN -> head N even
		 * if the ioctl api suggests that this is not quite the case.
		 *
		 * Otherwise we fail, we use this assumption in the mmap code
		 * below to find our board information.
		 */
		if (board != devnum){
			printk ("Parameter board does not match the current board\n");
			return -EINVAL;
		}

		if (board >= boards)
			return -EINVAL;

		/* If it is the first opening it, then make it the board owner */
		if (!cards [board].g_owner)
			cards [board].g_owner = current;

		/*
		 * Ok, we now call mmap on this file, which will end up calling
		 * sgi_graphics_mmap
		 */
		disable_gconsole ();
		down(&current->mm->mmap_sem);
		r = do_mmap (file, (unsigned long)vaddr,
			     cards[board].g_regs_size, PROT_READ|PROT_WRITE,
			     MAP_FIXED|MAP_PRIVATE, 0);
		up(&current->mm->mmap_sem);
		if (r)
			return r;
	}

	/* Strange, the real mapping seems to be done at GFX_ATTACH_BOARD,
	 * GFX_MAPALL is not even used by IRIX X server
	 */
	case GFX_MAPALL:
		return 0;

	case GFX_LABEL:
		return 0;

		/* Version check
		 * for my IRIX 6.2 X server, this is what the kernel returns
		 */
	case 1:
		return 3;

	/* Xsgi does not use this one, I assume minor is the board being queried */
	case GFX_IS_MANAGED:
		if (devnum > boards)
			return -EINVAL;
		return (cards [devnum].g_owner != 0);

	default:
		if (cards [devnum].g_ioctl)
			return (*cards [devnum].g_ioctl)(devnum, cmd, arg);
		
	}
	return -EINVAL;
}

int
sgi_graphics_close (struct inode *inode, struct file *file)
{
	int board = GRAPHICS_CARD (inode->i_rdev);

	/* Tell the rendering manager that one client is going away */
	lock_kernel();
	rrm_close (inode, file);

	/* Was this file handle from the board owner?, clear it */
	if (current == cards [board].g_owner){
		cards [board].g_owner = 0;
		if (cards [board].g_reset_console)
			(*cards [board].g_reset_console)();
		enable_gconsole ();
	}
	unlock_kernel();
	return 0;
}

/* 
 * This is the core of the direct rendering engine.
 */

unsigned long
sgi_graphics_nopage (struct vm_area_struct *vma, unsigned long address, int
		     no_share)
{
	pgd_t *pgd; pmd_t *pmd; pte_t *pte; 
	int board = GRAPHICS_CARD (vma->vm_dentry->d_inode->i_rdev);

	unsigned long virt_add, phys_add;

#ifdef DEBUG
	printk ("Got a page fault for board %d address=%lx guser=%lx\n", board,
		address, (unsigned long) cards[board].g_user);
#endif
	
	/* Figure out if another process has this mapped, and revoke the mapping
	 * in that case. */
	if (cards[board].g_user && cards[board].g_user != current) {
		/* FIXME: save graphics context here, dump it to rendering
		 * node? */

		remove_mapping(cards[board].g_user, vma->vm_start, vma->vm_end);
	}

	cards [board].g_user = current;

	/* Map the physical address of the newport registers into the address
	 * space of this process */

	virt_add = address & PAGE_MASK;
	phys_add = cards[board].g_regs + virt_add - vma->vm_start;
	remap_page_range(virt_add, phys_add, PAGE_SIZE, vma->vm_page_prot);

	pgd = pgd_offset(current->mm, address);
	pmd = pmd_offset(pgd, address);
	pte = pte_offset(pmd, address);
	printk("page: %08lx\n", pte_page(*pte));
	return pte_page(*pte);
}

/*
 * We convert a GFX ioctl for mapping hardware registers, in a nice sys_mmap
 * call, which takes care of everything that must be taken care of.
 *
 */

static struct vm_operations_struct graphics_mmap = {
	nopage:	sgi_graphics_nopage,	/* our magic no-page fault handler */
};
	
int
sgi_graphics_mmap (struct file *file, struct vm_area_struct *vma)
{
	uint size;

	size = vma->vm_end - vma->vm_start;

	/* 1. Set our special graphic virtualizer  */
	vma->vm_ops = &graphics_mmap;

	/* 2. Set the special tlb permission bits */
	vma->vm_page_prot = PAGE_USERIO;

	/* final setup */
	vma->vm_file = file;
	return 0;
}

#if 0
/* Do any post card-detection setup on graphics_ops */
static void
graphics_ops_post_init (int slot)
{
	/* There is no owner for the card initially */
	cards [slot].g_owner = (struct task_struct *) 0;
	cards [slot].g_user  = (struct task_struct *) 0;
}
#endif

struct file_operations sgi_graphics_fops = {
	ioctl:		sgi_graphics_ioctl,
	mmap:		sgi_graphics_mmap,
	open:		sgi_graphics_open,
	release:	sgi_graphics_close,
};

/* /dev/graphics */
static struct miscdevice dev_graphics = {
	SGI_GRAPHICS_MINOR, "sgi-graphics", &sgi_graphics_fops
};

/* /dev/opengl */
static struct miscdevice dev_opengl = {
	SGI_OPENGL_MINOR, "sgi-opengl", &sgi_graphics_fops
};

/* This is called later from the misc-init routine */
void __init gfx_register (void)
{
	misc_register (&dev_graphics);
	misc_register (&dev_opengl);
}

void __init gfx_init (const char **name)
{
#if 0
	struct console_ops *console;
	struct graphics_ops *g;
#endif

	printk ("GFX INIT: ");
	shmiq_init ();
	usema_init ();

	boards++;

#if 0
	if ((g = newport_probe (boards, name)) != 0) {
		cards [boards] = *g;
		graphics_ops_post_init (boards);
		boards++;
		console = 0;
	}
	/* Add more graphic drivers here */
	/* Keep passing console around */
#endif

	if (boards > MAXCARDS)
		printk (KERN_WARNING "Too many cards found on the system\n");
}

#ifdef MODULE
int init_module(void) {
	static int initiated = 0;

	printk("SGI Newport Graphics version %i.%i.%i\n",42,54,69);

	if (!initiated++) {
		shmiq_init();
		usema_init();
		printk("Adding first board\n");
		boards++;
		cards[0].g_regs = 0x1f0f0000;
		cards[0].g_regs_size = sizeof (struct newport_regs);
	}

	printk("Boards: %d\n", boards);

	misc_register (&dev_graphics);
	misc_register (&dev_opengl);

	return 0;
}

void cleanup_module(void) {
	printk("Shutting down SGI Newport Graphics\n");

	misc_deregister (&dev_graphics);
	misc_deregister (&dev_opengl);
}
#endif
