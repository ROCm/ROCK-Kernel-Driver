/*
 * kernel/lvm.c
 *
 * Copyright (C) 1997 - 2000  Heinz Mauelshagen, Sistina Software
 *
 * February-November 1997
 * April-May,July-August,November 1998
 * January-March,May,July,September,October 1999
 * January,February,July,September-November 2000
 *
 *
 * LVM driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * LVM driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. 
 *
 */

/*
 * Changelog
 *
 *    09/11/1997 - added chr ioctls VG_STATUS_GET_COUNT
 *                 and VG_STATUS_GET_NAMELIST
 *    18/01/1998 - change lvm_chr_open/close lock handling
 *    30/04/1998 - changed LV_STATUS ioctl to LV_STATUS_BYNAME and
 *               - added   LV_STATUS_BYINDEX ioctl
 *               - used lvm_status_byname_req_t and
 *                      lvm_status_byindex_req_t vars
 *    04/05/1998 - added multiple device support
 *    08/05/1998 - added support to set/clear extendable flag in volume group
 *    09/05/1998 - changed output of lvm_proc_get_global_info() because of
 *                 support for free (eg. longer) logical volume names
 *    12/05/1998 - added spin_locks (thanks to Pascal van Dam
 *                 <pascal@ramoth.xs4all.nl>)
 *    25/05/1998 - fixed handling of locked PEs in lvm_map() and lvm_chr_ioctl()
 *    26/05/1998 - reactivated verify_area by access_ok
 *    07/06/1998 - used vmalloc/vfree instead of kmalloc/kfree to go
 *                 beyond 128/256 KB max allocation limit per call
 *               - #ifdef blocked spin_lock calls to avoid compile errors
 *                 with 2.0.x
 *    11/06/1998 - another enhancement to spinlock code in lvm_chr_open()
 *                 and use of LVM_VERSION_CODE instead of my own macros
 *                 (thanks to  Michael Marxmeier <mike@msede.com>)
 *    07/07/1998 - added statistics in lvm_map()
 *    08/07/1998 - saved statistics in lvm_do_lv_extend_reduce()
 *    25/07/1998 - used __initfunc macro
 *    02/08/1998 - changes for official char/block major numbers
 *    07/08/1998 - avoided init_module() and cleanup_module() to be static
 *    30/08/1998 - changed VG lv_open counter from sum of LV lv_open counters
 *                 to sum of LVs open (no matter how often each is)
 *    01/09/1998 - fixed lvm_gendisk.part[] index error
 *    07/09/1998 - added copying of lv_current_pe-array
 *                 in LV_STATUS_BYINDEX ioctl
 *    17/11/1998 - added KERN_* levels to printk
 *    13/01/1999 - fixed LV index bug in lvm_do_lv_create() which hit lvrename
 *    07/02/1999 - fixed spinlock handling bug in case of LVM_RESET
 *                 by moving spinlock code from lvm_chr_open()
 *                 to lvm_chr_ioctl()
 *               - added LVM_LOCK_LVM ioctl to lvm_chr_ioctl()
 *               - allowed LVM_RESET and retrieval commands to go ahead;
 *                 only other update ioctls are blocked now
 *               - fixed pv->pe to NULL for pv_status
 *               - using lv_req structure in lvm_chr_ioctl() now
 *               - fixed NULL ptr reference bug in lvm_do_lv_extend_reduce()
 *                 caused by uncontiguous PV array in lvm_chr_ioctl(VG_REDUCE)
 *    09/02/1999 - changed BLKRASET and BLKRAGET in lvm_chr_ioctl() to
 *                 handle lgoical volume private read ahead sector
 *               - implemented LV read_ahead handling with lvm_blk_read()
 *                 and lvm_blk_write()
 *    10/02/1999 - implemented 2.[12].* support function lvm_hd_name()
 *                 to be used in drivers/block/genhd.c by disk_name()
 *    12/02/1999 - fixed index bug in lvm_blk_ioctl(), HDIO_GETGEO
 *               - enhanced gendisk insert/remove handling
 *    16/02/1999 - changed to dynamic block minor number allocation to
 *                 have as much as 99 volume groups with 256 logical volumes
 *                 as the grand total; this allows having 1 volume group with
 *                 up to 256 logical volumes in it
 *    21/02/1999 - added LV open count information to proc filesystem
 *               - substituted redundant LVM_RESET code by calls
 *                 to lvm_do_vg_remove()
 *    22/02/1999 - used schedule_timeout() to be more responsive
 *                 in case of lvm_do_vg_remove() with lots of logical volumes
 *    19/03/1999 - fixed NULL pointer bug in module_init/lvm_init
 *    17/05/1999 - used DECLARE_WAIT_QUEUE_HEAD macro (>2.3.0)
 *               - enhanced lvm_hd_name support
 *    03/07/1999 - avoided use of KERNEL_VERSION macro based ifdefs and
 *                 memcpy_tofs/memcpy_fromfs macro redefinitions
 *    06/07/1999 - corrected reads/writes statistic counter copy in case
 *                 of striped logical volume
 *    28/07/1999 - implemented snapshot logical volumes
 *                 - lvm_chr_ioctl
 *                   - LV_STATUS_BYINDEX
 *                   - LV_STATUS_BYNAME
 *                 - lvm_do_lv_create
 *                 - lvm_do_lv_remove
 *                 - lvm_map
 *                 - new lvm_snapshot_remap_block
 *                 - new lvm_snapshot_remap_new_block
 *    08/10/1999 - implemented support for multiple snapshots per
 *                 original logical volume
 *    12/10/1999 - support for 2.3.19
 *    11/11/1999 - support for 2.3.28
 *    21/11/1999 - changed lvm_map() interface to buffer_head based
 *    19/12/1999 - support for 2.3.33
 *    01/01/2000 - changed locking concept in lvm_map(),
 *                 lvm_do_vg_create() and lvm_do_lv_remove()
 *    15/01/2000 - fixed PV_FLUSH bug in lvm_chr_ioctl()
 *    24/01/2000 - ported to 2.3.40 including Alan Cox's pointer changes etc.
 *    29/01/2000 - used kmalloc/kfree again for all small structures
 *    20/01/2000 - cleaned up lvm_chr_ioctl by moving code
 *                 to seperated functions
 *               - avoided "/dev/" in proc filesystem output
 *               - avoided inline strings functions lvm_strlen etc.
 *    14/02/2000 - support for 2.3.43
 *               - integrated Andrea Arcagneli's snapshot code
 *    25/06/2000 - james (chip) , IKKHAYD! roffl
 *    26/06/2000 - enhanced lv_extend_reduce for snapshot logical volume support
 *    06/09/2000 - added devfs support
 *    07/09/2000 - changed IOP version to 9
 *               - started to add new char ioctl LV_STATUS_BYDEV_T to support
 *                 getting an lv_t based on the dev_t of the Logical Volume
 *    14/09/2000 - enhanced lvm_do_lv_create to upcall VFS functions
 *                 to sync and lock, activate snapshot and unlock the FS
 *                 (to support journaled filesystems)
 *    18/09/2000 - hardsector size support
 *    27/09/2000 - implemented lvm_do_lv_rename() and lvm_do_vg_rename()
 *    30/10/2000 - added Andi Kleen's LV_BMAP ioctl to support LILO
 *    01/11/2000 - added memory information on hash tables to
 *                 lvm_proc_get_global_info()
 *    02/11/2000 - implemented /proc/lvm/ hierarchy
 *    07/12/2000 - make sure lvm_make_request_fn returns correct value - 0 or 1 - NeilBrown
 *
 */


static char *lvm_version = "LVM version 0.9  by Heinz Mauelshagen  (13/11/2000)\n";
static char *lvm_short_version = "version 0.9 (13/11/2000)";

#define MAJOR_NR	LVM_BLK_MAJOR
#define	DEVICE_OFF(device)

/* lvm_do_lv_create calls fsync_dev_lockfs()/unlockfs() */
/* #define	LVM_VFS_ENHANCEMENT */

#include <linux/config.h>
#include <linux/version.h>

#ifdef MODVERSIONS
#undef MODULE
#define MODULE
#include <linux/modversions.h>
#endif

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <linux/hdreg.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <asm/ioctl.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

#include <linux/blk.h>
#include <linux/blkpg.h>

#include <linux/errno.h>
#include <linux/lvm.h>

#define	LVM_CORRECT_READ_AHEAD( a) \
   if      ( a < LVM_MIN_READ_AHEAD || \
             a > LVM_MAX_READ_AHEAD) a = LVM_MAX_READ_AHEAD;

#ifndef WRITEA
#  define WRITEA WRITE
#endif

/*
 * External function prototypes
 */
#ifdef MODULE
int init_module(void);
void cleanup_module(void);
#else
extern int lvm_init(void);
#endif

static void lvm_dummy_device_request(request_queue_t *);
#define	DEVICE_REQUEST	lvm_dummy_device_request

static int lvm_make_request_fn(request_queue_t*, int, struct buffer_head*);

static int lvm_blk_ioctl(struct inode *, struct file *, uint, ulong);
static int lvm_blk_open(struct inode *, struct file *);

static int lvm_chr_open(struct inode *, struct file *);

static int lvm_chr_close(struct inode *, struct file *);
static int lvm_blk_close(struct inode *, struct file *);
static int lvm_user_bmap(struct inode *, struct lv_bmap *);

static int lvm_chr_ioctl(struct inode *, struct file *, uint, ulong);

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
int lvm_proc_read_vg_info(char *, char **, off_t, int, int *, void *);
int lvm_proc_read_lv_info(char *, char **, off_t, int, int *, void *);
int lvm_proc_read_pv_info(char *, char **, off_t, int, int *, void *);
static int lvm_proc_get_global_info(char *, char **, off_t, int, int *, void *);
void lvm_do_create_proc_entry_of_vg ( vg_t *);
inline void lvm_do_remove_proc_entry_of_vg ( vg_t *);
inline void lvm_do_create_proc_entry_of_lv ( vg_t *, lv_t *);
inline void lvm_do_remove_proc_entry_of_lv ( vg_t *, lv_t *);
inline void lvm_do_create_proc_entry_of_pv ( vg_t *, pv_t *);
inline void lvm_do_remove_proc_entry_of_pv ( vg_t *, pv_t *);
#endif

#ifdef LVM_HD_NAME
void lvm_hd_name(char *, int);
#endif
/* End external function prototypes */


/*
 * Internal function prototypes
 */
static void lvm_init_vars(void);

/* external snapshot calls */
extern inline int lvm_get_blksize(kdev_t);
extern int lvm_snapshot_alloc(lv_t *);
extern void lvm_snapshot_fill_COW_page(vg_t *, lv_t *);
extern int lvm_snapshot_COW(kdev_t, ulong, ulong, ulong, lv_t *);
extern int lvm_snapshot_remap_block(kdev_t *, ulong *, ulong, lv_t *);
extern void lvm_snapshot_release(lv_t *); 
extern int lvm_write_COW_table_block(vg_t *, lv_t *);
extern inline void lvm_hash_link(lv_block_exception_t *, kdev_t, ulong, lv_t *);
extern int lvm_snapshot_alloc_hash_table(lv_t *);
extern void lvm_drop_snapshot(lv_t *, char *);

#ifdef LVM_HD_NAME
extern void (*lvm_hd_name_ptr) (char *, int);
#endif
static int lvm_map(struct buffer_head *, int);
static int lvm_do_lock_lvm(void);
static int lvm_do_le_remap(vg_t *, void *);

static int lvm_do_pv_create(pv_t *, vg_t *, ulong);
static int lvm_do_pv_remove(vg_t *, ulong);
static int lvm_do_lv_create(int, char *, lv_t *);
static int lvm_do_lv_extend_reduce(int, char *, lv_t *);
static int lvm_do_lv_remove(int, char *, int);
static int lvm_do_lv_rename(vg_t *, lv_req_t *, lv_t *);
static int lvm_do_lv_status_byname(vg_t *r, void *);
static int lvm_do_lv_status_byindex(vg_t *, void *);
static int lvm_do_lv_status_bydev(vg_t *, void *);

static int lvm_do_pe_lock_unlock(vg_t *r, void *);

static int lvm_do_pv_change(vg_t*, void*);
static int lvm_do_pv_status(vg_t *, void *);

static int lvm_do_vg_create(int, void *);
static int lvm_do_vg_extend(vg_t *, void *);
static int lvm_do_vg_reduce(vg_t *, void *);
static int lvm_do_vg_rename(vg_t *, void *);
static int lvm_do_vg_remove(int);
static void lvm_geninit(struct gendisk *);
#ifdef LVM_GET_INODE
static struct inode *lvm_get_inode(int);
void lvm_clear_inode(struct inode *);
#endif
/* END Internal function prototypes */


/* volume group descriptor area pointers */
static vg_t *vg[ABS_MAX_VG];

#ifdef	CONFIG_DEVFS_FS
static devfs_handle_t lvm_devfs_handle;
static devfs_handle_t vg_devfs_handle[MAX_VG];
static devfs_handle_t ch_devfs_handle[MAX_VG];
static devfs_handle_t lv_devfs_handle[MAX_LV];
#endif

static pv_t *pvp = NULL;
static lv_t *lvp = NULL;
static pe_t *pep = NULL;
static pe_t *pep1 = NULL;
static char *basename = NULL;


/* map from block minor number to VG and LV numbers */
typedef struct {
	int vg_number;
	int lv_number;
} vg_lv_map_t;
static vg_lv_map_t vg_lv_map[ABS_MAX_LV];


/* Request structures (lvm_chr_ioctl()) */
static pv_change_req_t pv_change_req;
static pv_flush_req_t pv_flush_req;
static pv_status_req_t pv_status_req;
static pe_lock_req_t pe_lock_req;
static le_remap_req_t le_remap_req;
static lv_req_t lv_req;

#ifdef LVM_TOTAL_RESET
static int lvm_reset_spindown = 0;
#endif

static char pv_name[NAME_LEN];
/* static char rootvg[NAME_LEN] = { 0, }; */
const char *const lvm_name = LVM_NAME;
static int lock = 0;
static int loadtime = 0;
static uint vg_count = 0;
static long lvm_chr_open_count = 0;
static ushort lvm_iop_version = LVM_DRIVER_IOP_VERSION;
static DECLARE_WAIT_QUEUE_HEAD(lvm_snapshot_wait);
static DECLARE_WAIT_QUEUE_HEAD(lvm_wait);
static DECLARE_WAIT_QUEUE_HEAD(lvm_map_wait);

static spinlock_t lvm_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t lvm_snapshot_lock = SPIN_LOCK_UNLOCKED;

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
static struct proc_dir_entry *lvm_proc_dir = NULL;
static struct proc_dir_entry *lvm_proc_vg_subdir = NULL;
struct proc_dir_entry *pde = NULL;
#endif

static struct file_operations lvm_chr_fops =
{
	open:		lvm_chr_open,
	release:	lvm_chr_close,
	ioctl:		lvm_chr_ioctl,
};

#define BLOCK_DEVICE_OPERATIONS
/* block device operations structure needed for 2.3.38? and above */
static struct block_device_operations lvm_blk_dops =
{
	open: 		lvm_blk_open,
	release:	lvm_blk_close,
	ioctl:		lvm_blk_ioctl,
};


/* gendisk structures */
static struct hd_struct lvm_hd_struct[MAX_LV];
static int lvm_blocksizes[MAX_LV] =
{0,};
static int lvm_size[MAX_LV] =
{0,};
static struct gendisk lvm_gendisk =
{
	MAJOR_NR,		/* major # */
	LVM_NAME,		/* name of major */
	0,			/* number of times minor is shifted
				   to get real minor */
	1,			/* maximum partitions per device */
	lvm_hd_struct,		/* partition table */
	lvm_size,		/* device size in blocks, copied
				   to block_size[] */
	MAX_LV,			/* number or real devices */
	NULL,			/* internal */
	NULL,			/* pointer to next gendisk struct (internal) */
};


#ifdef MODULE
/*
 * Module initialization...
 */
int init_module(void)
#else
/*
 * Driver initialization...
 */
#ifdef __initfunc
__initfunc(int lvm_init(void))
#else
int __init lvm_init(void)
#endif
#endif				/* #ifdef MODULE */
{
	struct gendisk *gendisk_ptr = NULL;

	if (register_chrdev(LVM_CHAR_MAJOR, lvm_name, &lvm_chr_fops) < 0) {
		printk(KERN_ERR "%s -- register_chrdev failed\n", lvm_name);
		return -EIO;
	}
#ifdef BLOCK_DEVICE_OPERATIONS
	if (register_blkdev(MAJOR_NR, lvm_name, &lvm_blk_dops) < 0)
#else
	if (register_blkdev(MAJOR_NR, lvm_name, &lvm_blk_fops) < 0)
#endif
	{
		printk("%s -- register_blkdev failed\n", lvm_name);
		if (unregister_chrdev(LVM_CHAR_MAJOR, lvm_name) < 0)
			printk(KERN_ERR "%s -- unregister_chrdev failed\n", lvm_name);
		return -EIO;
	}

#ifdef	CONFIG_DEVFS_FS
	lvm_devfs_handle = devfs_register(
		0 , "lvm", 0, 0, LVM_CHAR_MAJOR,
		S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP,
		&lvm_chr_fops, NULL);
#endif

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	lvm_proc_dir = create_proc_entry (LVM_DIR, S_IFDIR, &proc_root);
	if (lvm_proc_dir != NULL) {
		lvm_proc_vg_subdir = create_proc_entry (LVM_VG_SUBDIR, S_IFDIR, lvm_proc_dir);
		pde = create_proc_entry(LVM_GLOBAL, S_IFREG, lvm_proc_dir);
		if ( pde != NULL) pde->read_proc = &lvm_proc_get_global_info;
	}
#endif

	lvm_init_vars();
	lvm_geninit(&lvm_gendisk);

	/* insert our gendisk at the corresponding major */
	if (gendisk_head != NULL) {
		gendisk_ptr = gendisk_head;
		while (gendisk_ptr->next != NULL &&
		       gendisk_ptr->major > lvm_gendisk.major) {
			gendisk_ptr = gendisk_ptr->next;
		}
		lvm_gendisk.next = gendisk_ptr->next;
		gendisk_ptr->next = &lvm_gendisk;
	} else {
		gendisk_head = &lvm_gendisk;
		lvm_gendisk.next = NULL;
	}

#ifdef LVM_HD_NAME
	/* reference from drivers/block/genhd.c */
	lvm_hd_name_ptr = lvm_hd_name;
#endif

	blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), DEVICE_REQUEST);
	blk_queue_make_request(BLK_DEFAULT_QUEUE(MAJOR_NR), lvm_make_request_fn);

	/* optional read root VGDA */
/*
   if ( *rootvg != 0) vg_read_with_pv_and_lv ( rootvg, &vg);
*/

	printk(KERN_INFO
	       "%s%s -- "
#ifdef MODULE
	       "Module"
#else
	       "Driver"
#endif
	       " successfully initialized\n",
	       lvm_version, lvm_name);

	return 0;
} /* init_module() / lvm_init() */


#ifdef MODULE
/*
 * Module cleanup...
 */
void cleanup_module(void)
{
	struct gendisk *gendisk_ptr = NULL, *gendisk_ptr_prev = NULL;

#ifdef	CONFIG_DEVFS_FS
	devfs_unregister (lvm_devfs_handle);
#endif

	if (unregister_chrdev(LVM_CHAR_MAJOR, lvm_name) < 0) {
		printk(KERN_ERR "%s -- unregister_chrdev failed\n", lvm_name);
	}
	if (unregister_blkdev(MAJOR_NR, lvm_name) < 0) {
		printk(KERN_ERR "%s -- unregister_blkdev failed\n", lvm_name);
	}
	blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));

	gendisk_ptr = gendisk_ptr_prev = gendisk_head;
	while (gendisk_ptr != NULL) {
		if (gendisk_ptr == &lvm_gendisk)
			break;
		gendisk_ptr_prev = gendisk_ptr;
		gendisk_ptr = gendisk_ptr->next;
	}
	/* delete our gendisk from chain */
	if (gendisk_ptr == &lvm_gendisk)
		gendisk_ptr_prev->next = gendisk_ptr->next;

	blk_size[MAJOR_NR] = NULL;
	blksize_size[MAJOR_NR] = NULL;
	hardsect_size[MAJOR_NR] = NULL;

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	remove_proc_entry(LVM_GLOBAL, lvm_proc_dir);
	remove_proc_entry(LVM_VG_SUBDIR, lvm_proc_dir);
	remove_proc_entry(LVM_DIR, &proc_root);
#endif

#ifdef LVM_HD_NAME
	/* reference from linux/drivers/block/genhd.c */
	lvm_hd_name_ptr = NULL;
#endif

	printk(KERN_INFO "%s -- Module successfully deactivated\n", lvm_name);

	return;
}	/* void cleanup_module() */
#endif	/* #ifdef MODULE */


/*
 * support function to initialize lvm variables
 */
#ifdef __initfunc
__initfunc(void lvm_init_vars(void))
#else
void __init lvm_init_vars(void)
#endif
{
	int v;

	loadtime = CURRENT_TIME;

	lvm_lock = lvm_snapshot_lock = SPIN_LOCK_UNLOCKED;

	pe_lock_req.lock = UNLOCK_PE;
	pe_lock_req.data.lv_dev = \
	pe_lock_req.data.pv_dev = \
	pe_lock_req.data.pv_offset = 0;

	/* Initialize VG pointers */
	for (v = 0; v < ABS_MAX_VG; v++) vg[v] = NULL;

	/* Initialize LV -> VG association */
	for (v = 0; v < ABS_MAX_LV; v++) {
		/* index ABS_MAX_VG never used for real VG */
		vg_lv_map[v].vg_number = ABS_MAX_VG;
		vg_lv_map[v].lv_number = -1;
	}

	return;
} /* lvm_init_vars() */


/********************************************************************
 *
 * Character device functions
 *
 ********************************************************************/

/*
 * character device open routine
 */
static int lvm_chr_open(struct inode *inode,
			struct file *file)
{
	int minor = MINOR(inode->i_rdev);

#ifdef DEBUG
	printk(KERN_DEBUG
	 "%s -- lvm_chr_open MINOR: %d  VG#: %d  mode: 0x%X  lock: %d\n",
	       lvm_name, minor, VG_CHR(minor), file->f_mode, lock);
#endif

	/* super user validation */
	if (!capable(CAP_SYS_ADMIN)) return -EACCES;

	/* Group special file open */
	if (VG_CHR(minor) > MAX_VG) return -ENXIO;

	lvm_chr_open_count++;

	MOD_INC_USE_COUNT;

	return 0;
} /* lvm_chr_open() */


/*
 * character device i/o-control routine
 *
 * Only one changing process can do changing ioctl at one time,
 * others will block.
 *
 */
static int lvm_chr_ioctl(struct inode *inode, struct file *file,
			 uint command, ulong a)
{
	int minor = MINOR(inode->i_rdev);
	uint extendable, l, v;
	void *arg = (void *) a;
	lv_t lv;
	vg_t* vg_ptr = vg[VG_CHR(minor)];

	/* otherwise cc will complain about unused variables */
	(void) lvm_lock;


#ifdef DEBUG_IOCTL
	printk(KERN_DEBUG
	       "%s -- lvm_chr_ioctl: command: 0x%X  MINOR: %d  "
	       "VG#: %d  mode: 0x%X\n",
	       lvm_name, command, minor, VG_CHR(minor), file->f_mode);
#endif

#ifdef LVM_TOTAL_RESET
	if (lvm_reset_spindown > 0) return -EACCES;
#endif

	/* Main command switch */
	switch (command) {
	case LVM_LOCK_LVM:
		/* lock the LVM */
		return lvm_do_lock_lvm();

	case LVM_GET_IOP_VERSION:
		/* check lvm version to ensure driver/tools+lib
		   interoperability */
		if (copy_to_user(arg, &lvm_iop_version, sizeof(ushort)) != 0)
			return -EFAULT;
		return 0;

#ifdef LVM_TOTAL_RESET
	case LVM_RESET:
		/* lock reset function */
		lvm_reset_spindown = 1;
		for (v = 0; v < ABS_MAX_VG; v++) {
			if (vg[v] != NULL) lvm_do_vg_remove(v);
		}

#ifdef MODULE
		while (GET_USE_COUNT(&__this_module) < 1)
			MOD_INC_USE_COUNT;
		while (GET_USE_COUNT(&__this_module) > 1)
			MOD_DEC_USE_COUNT;
#endif /* MODULE */
		lock = 0;	/* release lock */
		wake_up_interruptible(&lvm_wait);
		return 0;
#endif /* LVM_TOTAL_RESET */


	case LE_REMAP:
		/* remap a logical extent (after moving the physical extent) */
		return lvm_do_le_remap(vg_ptr,arg);

	case PE_LOCK_UNLOCK:
		/* lock/unlock i/o to a physical extent to move it to another
		   physical volume (move's done in user space's pvmove) */
		return lvm_do_pe_lock_unlock(vg_ptr,arg);

	case VG_CREATE:
		/* create a VGDA */
		return lvm_do_vg_create(minor, arg);

	case VG_EXTEND:
		/* extend a volume group */
		return lvm_do_vg_extend(vg_ptr, arg);

	case VG_REDUCE:
		/* reduce a volume group */
		return lvm_do_vg_reduce(vg_ptr, arg);

	case VG_RENAME:
		/* rename a volume group */
		return lvm_do_vg_rename(vg_ptr, arg);

	case VG_REMOVE:
		/* remove an inactive VGDA */
		return lvm_do_vg_remove(minor);


	case VG_SET_EXTENDABLE:
		/* set/clear extendability flag of volume group */
		if (vg_ptr == NULL) return -ENXIO;
		if (copy_from_user(&extendable, arg, sizeof(extendable)) != 0)
			return -EFAULT;

		if (extendable == VG_EXTENDABLE ||
		    extendable == ~VG_EXTENDABLE) {
			if (extendable == VG_EXTENDABLE)
				vg_ptr->vg_status |= VG_EXTENDABLE;
			else
				vg_ptr->vg_status &= ~VG_EXTENDABLE;
		} else return -EINVAL;
		return 0;


	case VG_STATUS:
		/* get volume group data (only the vg_t struct) */
		if (vg_ptr == NULL) return -ENXIO;
		if (copy_to_user(arg, vg_ptr, sizeof(vg_t)) != 0)
			return -EFAULT;
		return 0;


	case VG_STATUS_GET_COUNT:
		/* get volume group count */
		if (copy_to_user(arg, &vg_count, sizeof(vg_count)) != 0)
			return -EFAULT;
		return 0;


	case VG_STATUS_GET_NAMELIST:
		/* get volume group count */
		for (l = v = 0; v < ABS_MAX_VG; v++) {
			if (vg[v] != NULL) {
				if (copy_to_user(arg + l * NAME_LEN,
						 vg[v]->vg_name,
						 NAME_LEN) != 0)
					return -EFAULT;
				l++;
			}
		}
		return 0;


	case LV_CREATE:
	case LV_EXTEND:
	case LV_REDUCE:
	case LV_REMOVE:
	case LV_RENAME:
		/* create, extend, reduce, remove or rename a logical volume */
		if (vg_ptr == NULL) return -ENXIO;
		if (copy_from_user(&lv_req, arg, sizeof(lv_req)) != 0)
			return -EFAULT;

		if (command != LV_REMOVE) {
			if (copy_from_user(&lv, lv_req.lv, sizeof(lv_t)) != 0)
				return -EFAULT;
		}
		switch (command) {
		case LV_CREATE:
			return lvm_do_lv_create(minor, lv_req.lv_name, &lv);

		case LV_EXTEND:
		case LV_REDUCE:
			return lvm_do_lv_extend_reduce(minor, lv_req.lv_name, &lv);
		case LV_REMOVE:
			return lvm_do_lv_remove(minor, lv_req.lv_name, -1);

		case LV_RENAME:
			return lvm_do_lv_rename(vg_ptr, &lv_req, &lv);
		}




	case LV_STATUS_BYNAME:
		/* get status of a logical volume by name */
		return lvm_do_lv_status_byname(vg_ptr, arg);


	case LV_STATUS_BYINDEX:
		/* get status of a logical volume by index */
		return lvm_do_lv_status_byindex(vg_ptr, arg);


	case LV_STATUS_BYDEV:
		return lvm_do_lv_status_bydev(vg_ptr, arg);


	case PV_CHANGE:
		/* change a physical volume */
		return lvm_do_pv_change(vg_ptr,arg);


	case PV_STATUS:
		/* get physical volume data (pv_t structure only) */
		return lvm_do_pv_status(vg_ptr,arg);


	case PV_FLUSH:
		/* physical volume buffer flush/invalidate */
		if (copy_from_user(&pv_flush_req, arg,
				   sizeof(pv_flush_req)) != 0)
			return -EFAULT;

		fsync_dev(pv_flush_req.pv_dev);
		invalidate_buffers(pv_flush_req.pv_dev);
		return 0;


	default:
		printk(KERN_WARNING
		       "%s -- lvm_chr_ioctl: unknown command %x\n",
		       lvm_name, command);
		return -EINVAL;
	}

	return 0;
} /* lvm_chr_ioctl */


/*
 * character device close routine
 */
static int lvm_chr_close(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	int minor = MINOR(inode->i_rdev);
	printk(KERN_DEBUG
	     "%s -- lvm_chr_close   VG#: %d\n", lvm_name, VG_CHR(minor));
#endif

#ifdef LVM_TOTAL_RESET
	if (lvm_reset_spindown > 0) {
		lvm_reset_spindown = 0;
		lvm_chr_open_count = 0;
	}
#endif

	if (lvm_chr_open_count > 0) lvm_chr_open_count--;
	if (lock == current->pid) {
		lock = 0;	/* release lock */
		wake_up_interruptible(&lvm_wait);
	}

	MOD_DEC_USE_COUNT;

	return 0;
} /* lvm_chr_close() */



/********************************************************************
 *
 * Block device functions
 *
 ********************************************************************/

/*
 * block device open routine
 */
static int lvm_blk_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	lv_t *lv_ptr;
	vg_t *vg_ptr = vg[VG_BLK(minor)];

#ifdef DEBUG_LVM_BLK_OPEN
	printk(KERN_DEBUG
	  "%s -- lvm_blk_open MINOR: %d  VG#: %d  LV#: %d  mode: 0x%X\n",
	    lvm_name, minor, VG_BLK(minor), LV_BLK(minor), file->f_mode);
#endif

#ifdef LVM_TOTAL_RESET
	if (lvm_reset_spindown > 0)
		return -EPERM;
#endif

	if (vg_ptr != NULL &&
	    (vg_ptr->vg_status & VG_ACTIVE) &&
	    (lv_ptr = vg_ptr->lv[LV_BLK(minor)]) != NULL &&
	    LV_BLK(minor) >= 0 &&
	    LV_BLK(minor) < vg_ptr->lv_max) {

		/* Check parallel LV spindown (LV remove) */
		if (lv_ptr->lv_status & LV_SPINDOWN) return -EPERM;

		/* Check inactive LV and open for read/write */
		if (file->f_mode & O_RDWR) {
			if (!(lv_ptr->lv_status & LV_ACTIVE)) return -EPERM;
			if (!(lv_ptr->lv_access & LV_WRITE))  return -EACCES;
		}

#ifndef BLOCK_DEVICE_OPERATIONS
		file->f_op = &lvm_blk_fops;
#endif

                /* be sure to increment VG counter */
		if (lv_ptr->lv_open == 0) vg_ptr->lv_open++;
		lv_ptr->lv_open++;

		MOD_INC_USE_COUNT;

#ifdef DEBUG_LVM_BLK_OPEN
		printk(KERN_DEBUG
		       "%s -- lvm_blk_open MINOR: %d  VG#: %d  LV#: %d  size: %d\n",
		       lvm_name, minor, VG_BLK(minor), LV_BLK(minor),
		       lv_ptr->lv_size);
#endif

		return 0;
	}
	return -ENXIO;
} /* lvm_blk_open() */


/*
 * block device i/o-control routine
 */
static int lvm_blk_ioctl(struct inode *inode, struct file *file,
			 uint command, ulong a)
{
	int minor = MINOR(inode->i_rdev);
	vg_t *vg_ptr = vg[VG_BLK(minor)];
	lv_t *lv_ptr = vg_ptr->lv[LV_BLK(minor)];
	void *arg = (void *) a;
	struct hd_geometry *hd = (struct hd_geometry *) a;

#ifdef DEBUG_IOCTL
	printk(KERN_DEBUG
	       "%s -- lvm_blk_ioctl MINOR: %d  command: 0x%X  arg: %X  "
	       "VG#: %dl  LV#: %d\n",
	       lvm_name, minor, command, (ulong) arg,
	       VG_BLK(minor), LV_BLK(minor));
#endif

	switch (command) {
	case BLKGETSIZE:
		/* return device size */
#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- BLKGETSIZE: %u\n",
		       lvm_name, lv_ptr->lv_size);
#endif
		if (put_user(lv_ptr->lv_size, (long *)arg))
			return -EFAULT; 
		break;


	case BLKFLSBUF:
		/* flush buffer cache */
		if (!capable(CAP_SYS_ADMIN)) return -EACCES;

#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- BLKFLSBUF\n", lvm_name);
#endif
		fsync_dev(inode->i_rdev);
		invalidate_buffers(inode->i_rdev);
		break;


	case BLKRASET:
		/* set read ahead for block device */
		if (!capable(CAP_SYS_ADMIN)) return -EACCES;

#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- BLKRASET: %d sectors for %02X:%02X\n",
		       lvm_name, (long) arg, MAJOR(inode->i_rdev), minor);
#endif
		if ((long) arg < LVM_MIN_READ_AHEAD ||
		    (long) arg > LVM_MAX_READ_AHEAD)
			return -EINVAL;
		lv_ptr->lv_read_ahead = (long) arg;
		break;


	case BLKRAGET:
		/* get current read ahead setting */
#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- BLKRAGET\n", lvm_name);
#endif
		if (put_user(lv_ptr->lv_read_ahead, (long *)arg))
			return -EFAULT;
		break;


	case HDIO_GETGEO:
		/* get disk geometry */
#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- HDIO_GETGEO\n", lvm_name);
#endif
		if (hd == NULL)
			return -EINVAL;
		{
			unsigned char heads = 64;
			unsigned char sectors = 32;
			long start = 0;
			short cylinders = lv_ptr->lv_size / heads / sectors;

			if (copy_to_user((char *) &hd->heads, &heads,
					 sizeof(heads)) != 0 ||
			    copy_to_user((char *) &hd->sectors, &sectors,
					 sizeof(sectors)) != 0 ||
			    copy_to_user((short *) &hd->cylinders,
				   &cylinders, sizeof(cylinders)) != 0 ||
			    copy_to_user((long *) &hd->start, &start,
					 sizeof(start)) != 0)
				return -EFAULT;
		}

#ifdef DEBUG_IOCTL
		printk(KERN_DEBUG
		       "%s -- lvm_blk_ioctl -- cylinders: %d\n",
		       lvm_name, lv_ptr->lv_size / heads / sectors);
#endif
		break;


	case LV_SET_ACCESS:
		/* set access flags of a logical volume */
		if (!capable(CAP_SYS_ADMIN)) return -EACCES;
		lv_ptr->lv_access = (ulong) arg;
		if ( lv_ptr->lv_access & LV_WRITE)
			set_device_ro(lv_ptr->lv_dev, 0);
		else
			set_device_ro(lv_ptr->lv_dev, 1);
		break;


	case LV_SET_STATUS:
		/* set status flags of a logical volume */
		if (!capable(CAP_SYS_ADMIN)) return -EACCES;
		if (!((ulong) arg & LV_ACTIVE) && lv_ptr->lv_open > 1)
			return -EPERM;
		lv_ptr->lv_status = (ulong) arg;
		break;

	case LV_BMAP:
		/* turn logical block into (dev_t, block). non privileged. */
		return lvm_user_bmap(inode, (struct lv_bmap *) arg);
		break;

	case LV_SET_ALLOCATION:
		/* set allocation flags of a logical volume */
		if (!capable(CAP_SYS_ADMIN)) return -EACCES;
		lv_ptr->lv_allocation = (ulong) arg;
		break;

	case LV_SNAPSHOT_USE_RATE:
		if (!(lv_ptr->lv_access & LV_SNAPSHOT)) return -EPERM;
		{
			lv_snapshot_use_rate_req_t	lv_snapshot_use_rate_req;

			if (copy_from_user(&lv_snapshot_use_rate_req, arg,
					   sizeof(lv_snapshot_use_rate_req_t)))
				return -EFAULT;
			if (lv_snapshot_use_rate_req.rate < 0 ||
			    lv_snapshot_use_rate_req.rate  > 100) return -EFAULT;

			switch (lv_snapshot_use_rate_req.block)
			{
			case 0:
				lv_ptr->lv_snapshot_use_rate = lv_snapshot_use_rate_req.rate;
				if (lv_ptr->lv_remap_ptr * 100 / lv_ptr->lv_remap_end < lv_ptr->lv_snapshot_use_rate)
					interruptible_sleep_on (&lv_ptr->lv_snapshot_wait);
				break;

			case O_NONBLOCK:
				break;

			default:
				return -EFAULT;
			}
			lv_snapshot_use_rate_req.rate = lv_ptr->lv_remap_ptr * 100 / lv_ptr->lv_remap_end;
			if (copy_to_user(arg, &lv_snapshot_use_rate_req,
					 sizeof(lv_snapshot_use_rate_req_t)))
				return -EFAULT;
		}
		break;

	default:
		printk(KERN_WARNING
		       "%s -- lvm_blk_ioctl: unknown command %d\n",
		       lvm_name, command);
		return -EINVAL;
	}

	return 0;
} /* lvm_blk_ioctl() */


/*
 * block device close routine
 */
static int lvm_blk_close(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	vg_t *vg_ptr = vg[VG_BLK(minor)];
	lv_t *lv_ptr = vg_ptr->lv[LV_BLK(minor)];

#ifdef DEBUG
	printk(KERN_DEBUG
	       "%s -- lvm_blk_close MINOR: %d  VG#: %d  LV#: %d\n",
	       lvm_name, minor, VG_BLK(minor), LV_BLK(minor));
#endif

	sync_dev(inode->i_rdev);
	if (lv_ptr->lv_open == 1) vg_ptr->lv_open--;
	lv_ptr->lv_open--;

	MOD_DEC_USE_COUNT;

	return 0;
} /* lvm_blk_close() */


static int lvm_user_bmap(struct inode *inode, struct lv_bmap *user_result)
{
	struct buffer_head bh;
	unsigned long block;
	int err;
	
	if (get_user(block, &user_result->lv_block))
	return -EFAULT;
	
	memset(&bh,0,sizeof bh);
	bh.b_rsector = block;
	bh.b_dev = bh.b_rdev = inode->i_dev;
	bh.b_size = lvm_get_blksize(bh.b_dev);
	if ((err=lvm_map(&bh, READ)) < 0)  {
	printk("lvm map failed: %d\n", err);
	return -EINVAL;
	}
	
	return put_user(  kdev_t_to_nr(bh.b_rdev), &user_result->lv_dev) ||
	put_user(bh.b_rsector, &user_result->lv_block) ? -EFAULT : 0;
}     


/*
 * provide VG info for proc filesystem use (global)
 */
int lvm_vg_info(vg_t *vg_ptr, char *buf) {
	int sz = 0;
	char inactive_flag = ' ';

	if (!(vg_ptr->vg_status & VG_ACTIVE)) inactive_flag = 'I';
	sz = sprintf(buf,
		     "\nVG: %c%s  [%d PV, %d LV/%d open] "
		     " PE Size: %d KB\n"
		     "  Usage [KB/PE]: %d /%d total  "
		     "%d /%d used  %d /%d free",
		     inactive_flag,
		     vg_ptr->vg_name,
		     vg_ptr->pv_cur,
		     vg_ptr->lv_cur,
		     vg_ptr->lv_open,
	     	     vg_ptr->pe_size >> 1,
		     vg_ptr->pe_size * vg_ptr->pe_total >> 1,
		     vg_ptr->pe_total,
		     vg_ptr->pe_allocated * vg_ptr->pe_size >> 1,
	     	     vg_ptr->pe_allocated,
		     (vg_ptr->pe_total - vg_ptr->pe_allocated) *	
	     	     vg_ptr->pe_size >> 1,
		     vg_ptr->pe_total - vg_ptr->pe_allocated);
	return sz;
}


/*
 * provide LV info for proc filesystem use (global)
 */
int lvm_lv_info(vg_t *vg_ptr, lv_t *lv_ptr, char *buf) {
	int sz = 0;
	char inactive_flag = 'A', allocation_flag = ' ',
	     stripes_flag = ' ', rw_flag = ' ';

	if (!(lv_ptr->lv_status & LV_ACTIVE))
		inactive_flag = 'I';
	rw_flag = 'R';
	if (lv_ptr->lv_access & LV_WRITE)
		rw_flag = 'W';
	allocation_flag = 'D';
	if (lv_ptr->lv_allocation & LV_CONTIGUOUS)
		allocation_flag = 'C';
	stripes_flag = 'L';
	if (lv_ptr->lv_stripes > 1)
		stripes_flag = 'S';
	sz += sprintf(buf+sz,
		      "[%c%c%c%c",
		      inactive_flag,
	 rw_flag,
		      allocation_flag,
		      stripes_flag);
	if (lv_ptr->lv_stripes > 1)
		sz += sprintf(buf+sz, "%-2d",
			      lv_ptr->lv_stripes);
	else
		sz += sprintf(buf+sz, "  ");
	basename = strrchr(lv_ptr->lv_name, '/');
	if ( basename == 0) basename = lv_ptr->lv_name;
	else                basename++;
	sz += sprintf(buf+sz, "] %-25s", basename);
	if (strlen(basename) > 25)
		sz += sprintf(buf+sz,
			      "\n                              ");
	sz += sprintf(buf+sz, "%9d /%-6d   ",
		      lv_ptr->lv_size >> 1,
		      lv_ptr->lv_size / vg_ptr->pe_size);

	if (lv_ptr->lv_open == 0)
		sz += sprintf(buf+sz, "close");
	else
		sz += sprintf(buf+sz, "%dx open",
			      lv_ptr->lv_open);

	return sz;
}


/*
 * provide PV info for proc filesystem use (global)
 */
int lvm_pv_info(pv_t *pv_ptr, char *buf) {
	int sz = 0;
	char inactive_flag = 'A', allocation_flag = ' ';
	char *pv_name = NULL;

	if (!(pv_ptr->pv_status & PV_ACTIVE))
		inactive_flag = 'I';
	allocation_flag = 'A';
	if (!(pv_ptr->pv_allocatable & PV_ALLOCATABLE))
		allocation_flag = 'N';
	pv_name = strrchr(pv_ptr->pv_name+1,'/');
	if ( pv_name == 0) pv_name = pv_ptr->pv_name;
	else               pv_name++;
	sz = sprintf(buf,
		     "[%c%c] %-21s %8d /%-6d  "
		     "%8d /%-6d  %8d /%-6d",
		     inactive_flag,
		     allocation_flag,
		     pv_name,
		     pv_ptr->pe_total *
		     pv_ptr->pe_size >> 1,
		     pv_ptr->pe_total,
		     pv_ptr->pe_allocated *
		     pv_ptr->pe_size >> 1,
		     pv_ptr->pe_allocated,
		     (pv_ptr->pe_total -
		      pv_ptr->pe_allocated) *
		     pv_ptr->pe_size >> 1,
		     pv_ptr->pe_total -
		     pv_ptr->pe_allocated);
	return sz;
}


#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
/*
 * Support functions /proc-Filesystem
 */

#define  LVM_PROC_BUF   ( i == 0 ? dummy_buf : &buf[sz])

/*
 * provide global LVM information
 */
static int lvm_proc_get_global_info(char *page, char **start, off_t pos, int count, int *eof, void *data)
{
	int c, i, l, p, v, vg_counter, pv_counter, lv_counter, lv_open_counter,
	 lv_open_total, pe_t_bytes, hash_table_bytes, lv_block_exception_t_bytes, seconds;
	static off_t sz;
	off_t sz_last;
	static char *buf = NULL;
	static char dummy_buf[160];	/* sized for 2 lines */
	vg_t *vg_ptr;
	lv_t *lv_ptr;
	pv_t *pv_ptr;


#ifdef DEBUG_LVM_PROC_GET_INFO
	printk(KERN_DEBUG
	       "%s - lvm_proc_get_global_info CALLED  pos: %lu  count: %d  whence: %d\n",
	       lvm_name, pos, count, whence);
#endif

	MOD_INC_USE_COUNT;

	if (pos == 0 || buf == NULL) {
		sz_last = vg_counter = pv_counter = lv_counter = lv_open_counter = \
		lv_open_total = pe_t_bytes = hash_table_bytes = \
		lv_block_exception_t_bytes = 0;

		/* search for activity */
		for (v = 0; v < ABS_MAX_VG; v++) {
			if ((vg_ptr = vg[v]) != NULL) {
				vg_counter++;
				pv_counter += vg_ptr->pv_cur;
				lv_counter += vg_ptr->lv_cur;
				if (vg_ptr->lv_cur > 0) {
					for (l = 0; l < vg[v]->lv_max; l++) {
						if ((lv_ptr = vg_ptr->lv[l]) != NULL) {
							pe_t_bytes += lv_ptr->lv_allocated_le;
							hash_table_bytes += lv_ptr->lv_snapshot_hash_table_size;
							if (lv_ptr->lv_block_exception != NULL)
								lv_block_exception_t_bytes += lv_ptr->lv_remap_end;
							if (lv_ptr->lv_open > 0) {
								lv_open_counter++;
								lv_open_total += lv_ptr->lv_open;
							}
						}
					}
				}
			}
		}
		pe_t_bytes *= sizeof(pe_t);
		lv_block_exception_t_bytes *= sizeof(lv_block_exception_t);

		if (buf != NULL) {
#ifdef DEBUG_KFREE
			printk(KERN_DEBUG
			       "%s -- vfree %d\n", lvm_name, __LINE__);
#endif
			lock_kernel();
			vfree(buf);
			unlock_kernel();
			buf = NULL;
		}
		/* 2 times: first to get size to allocate buffer,
		   2nd to fill the malloced buffer */
		for (i = 0; i < 2; i++) {
			sz = 0;
			sz += sprintf(LVM_PROC_BUF,
				      "LVM "
#ifdef MODULE
				      "module"
#else
				      "driver"
#endif
				      " %s\n\n"
				    "Total:  %d VG%s  %d PV%s  %d LV%s ",
				      lvm_short_version,
				  vg_counter, vg_counter == 1 ? "" : "s",
				  pv_counter, pv_counter == 1 ? "" : "s",
				 lv_counter, lv_counter == 1 ? "" : "s");
			sz += sprintf(LVM_PROC_BUF,
				      "(%d LV%s open",
				      lv_open_counter,
				      lv_open_counter == 1 ? "" : "s");
			if (lv_open_total > 0)
				sz += sprintf(LVM_PROC_BUF,
					      " %d times)\n",
					      lv_open_total);
			else
				sz += sprintf(LVM_PROC_BUF, ")");
			sz += sprintf(LVM_PROC_BUF,
				      "\nGlobal: %lu bytes malloced   IOP version: %d   ",
				      vg_counter * sizeof(vg_t) +
				      pv_counter * sizeof(pv_t) +
				      lv_counter * sizeof(lv_t) +
				      pe_t_bytes + hash_table_bytes + lv_block_exception_t_bytes + sz_last,
				      lvm_iop_version);

			seconds = CURRENT_TIME - loadtime;
			if (seconds < 0)
				loadtime = CURRENT_TIME + seconds;
			if (seconds / 86400 > 0) {
				sz += sprintf(LVM_PROC_BUF, "%d day%s ",
					      seconds / 86400,
					      seconds / 86400 == 0 ||
					 seconds / 86400 > 1 ? "s" : "");
			}
			sz += sprintf(LVM_PROC_BUF, "%d:%02d:%02d active\n",
				      (seconds % 86400) / 3600,
				      (seconds % 3600) / 60,
				      seconds % 60);

			if (vg_counter > 0) {
				for (v = 0; v < ABS_MAX_VG; v++) {
					/* volume group */
					if ((vg_ptr = vg[v]) != NULL) {
						sz += lvm_vg_info(vg_ptr, LVM_PROC_BUF);

						/* physical volumes */
						sz += sprintf(LVM_PROC_BUF,
							      "\n  PV%s ",
							      vg_ptr->pv_cur == 1 ? ": " : "s:");
						c = 0;
						for (p = 0; p < vg_ptr->pv_max; p++) {
							if ((pv_ptr = vg_ptr->pv[p]) != NULL) {
								sz += lvm_pv_info(pv_ptr, LVM_PROC_BUF);

								c++;
								if (c < vg_ptr->pv_cur)
									sz += sprintf(LVM_PROC_BUF,
										      "\n       ");
							}
						}

						/* logical volumes */
						sz += sprintf(LVM_PROC_BUF,
							   "\n    LV%s ",
							      vg_ptr->lv_cur == 1 ? ": " : "s:");
						c = 0;
						for (l = 0; l < vg_ptr->lv_max; l++) {
							if ((lv_ptr = vg_ptr->lv[l]) != NULL) {
								sz += lvm_lv_info(vg_ptr, lv_ptr, LVM_PROC_BUF);
								c++;
								if (c < vg_ptr->lv_cur)
									sz += sprintf(LVM_PROC_BUF,
										      "\n         ");
							}
						}
						if (vg_ptr->lv_cur == 0) sz += sprintf(LVM_PROC_BUF, "none");
						sz += sprintf(LVM_PROC_BUF, "\n");
					}
				}
			}
			if (buf == NULL) {
				lock_kernel();
				buf = vmalloc(sz);
				unlock_kernel();
				if (buf == NULL) {
					sz = 0;
					MOD_DEC_USE_COUNT;
					return sprintf(page, "%s - vmalloc error at line %d\n",
						     lvm_name, __LINE__);
				}
			}
			sz_last = sz;
		}
	}
	MOD_DEC_USE_COUNT;
	if (pos > sz - 1) {
		lock_kernel();
		vfree(buf);
		unlock_kernel();
		buf = NULL;
		return 0;
	}
	*start = &buf[pos];
	if (sz - pos < count)
		return sz - pos;
	else
		return count;
} /* lvm_proc_get_global_info() */
#endif /* #if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS */


/*
 * provide VG information
 */
int lvm_proc_read_vg_info(char *page, char **start, off_t off,
			  int count, int *eof, void *data) {
	int sz = 0;
	vg_t *vg = data;

	sz += sprintf ( page+sz, "name:         %s\n", vg->vg_name);
	sz += sprintf ( page+sz, "size:         %u\n",
		        vg->pe_total * vg->pe_size / 2);
	sz += sprintf ( page+sz, "access:       %u\n", vg->vg_access);
	sz += sprintf ( page+sz, "status:       %u\n", vg->vg_status);
	sz += sprintf ( page+sz, "number:       %u\n", vg->vg_number);
	sz += sprintf ( page+sz, "LV max:       %u\n", vg->lv_max);
	sz += sprintf ( page+sz, "LV current:   %u\n", vg->lv_cur);
	sz += sprintf ( page+sz, "LV open:      %u\n", vg->lv_open);
	sz += sprintf ( page+sz, "PV max:       %u\n", vg->pv_max);
	sz += sprintf ( page+sz, "PV current:   %u\n", vg->pv_cur);
	sz += sprintf ( page+sz, "PV active:    %u\n", vg->pv_act);
	sz += sprintf ( page+sz, "PE size:      %u\n", vg->pe_size / 2);
	sz += sprintf ( page+sz, "PE total:     %u\n", vg->pe_total);
	sz += sprintf ( page+sz, "PE allocated: %u\n", vg->pe_allocated);
	sz += sprintf ( page+sz, "uuid:         %s\n", vg->vg_uuid);

	return sz;
}


/*
 * provide LV information
 */
int lvm_proc_read_lv_info(char *page, char **start, off_t off,
			  int count, int *eof, void *data) {
	int sz = 0;
	lv_t *lv = data;

	sz += sprintf ( page+sz, "name:         %s\n", lv->lv_name);
	sz += sprintf ( page+sz, "size:         %u\n", lv->lv_size);
	sz += sprintf ( page+sz, "access:       %u\n", lv->lv_access);
	sz += sprintf ( page+sz, "status:       %u\n", lv->lv_status);
	sz += sprintf ( page+sz, "number:       %u\n", lv->lv_number);
	sz += sprintf ( page+sz, "open:         %u\n", lv->lv_open);
	sz += sprintf ( page+sz, "allocation:   %u\n", lv->lv_allocation);
	sz += sprintf ( page+sz, "device:       %02u:%02u\n",
                        MAJOR(lv->lv_dev), MINOR(lv->lv_dev));

	return sz;
}


/*
 * provide PV information
 */
int lvm_proc_read_pv_info(char *page, char **start, off_t off,
			  int count, int *eof, void *data) {
	int sz = 0;
	pv_t *pv = data;

	sz += sprintf ( page+sz, "name:         %s\n", pv->pv_name);
	sz += sprintf ( page+sz, "size:         %u\n", pv->pv_size);
	sz += sprintf ( page+sz, "status:       %u\n", pv->pv_status);
	sz += sprintf ( page+sz, "number:       %u\n", pv->pv_number);
	sz += sprintf ( page+sz, "allocatable:  %u\n", pv->pv_allocatable);
	sz += sprintf ( page+sz, "LV current:   %u\n", pv->lv_cur);
	sz += sprintf ( page+sz, "PE size:      %u\n", pv->pe_size / 2);
	sz += sprintf ( page+sz, "PE total:     %u\n", pv->pe_total);
	sz += sprintf ( page+sz, "PE allocated: %u\n", pv->pe_allocated);
	sz += sprintf ( page+sz, "device:       %02u:%02u\n",
                        MAJOR(pv->pv_dev), MINOR(pv->pv_dev));
	sz += sprintf ( page+sz, "uuid:         %s\n", pv->pv_uuid);


	return sz;
}


/*
 * block device support function for /usr/src/linux/drivers/block/ll_rw_blk.c
 * (see init_module/lvm_init)
 */
static int lvm_map(struct buffer_head *bh, int rw)
{
	int minor = MINOR(bh->b_dev);
	int ret = 0;
	ulong index;
	ulong pe_start;
	ulong size = bh->b_size >> 9;
	ulong rsector_tmp = bh->b_blocknr * size;
	ulong rsector_sav;
	kdev_t rdev_tmp = bh->b_dev;
	kdev_t rdev_sav;
	vg_t *vg_this = vg[VG_BLK(minor)];
	lv_t *lv = vg_this->lv[LV_BLK(minor)];


	if (!(lv->lv_status & LV_ACTIVE)) {
		printk(KERN_ALERT
		       "%s - lvm_map: ll_rw_blk for inactive LV %s\n",
		       lvm_name, lv->lv_name);
		return -1;
	}

	if ((rw == WRITE || rw == WRITEA) &&
	    !(lv->lv_access & LV_WRITE)) {
		printk(KERN_CRIT
		    "%s - lvm_map: ll_rw_blk write for readonly LV %s\n",
		       lvm_name, lv->lv_name);
		return -1;
	}
#ifdef DEBUG_MAP
	printk(KERN_DEBUG
	       "%s - lvm_map minor:%d  *rdev: %02d:%02d  *rsector: %lu  "
	       "size:%lu\n",
	       lvm_name, minor,
	       MAJOR(rdev_tmp),
	       MINOR(rdev_tmp),
	       rsector_tmp, size);
#endif

	if (rsector_tmp + size > lv->lv_size) {
		printk(KERN_ALERT
		       "%s - lvm_map access beyond end of device; *rsector: "
                       "%lu or size: %lu wrong for minor: %2d\n",
                       lvm_name, rsector_tmp, size, minor);
		return -1;
	}
	rsector_sav = rsector_tmp;
	rdev_sav = rdev_tmp;

lvm_second_remap:
	/* linear mapping */
	if (lv->lv_stripes < 2) {
		/* get the index */
		index = rsector_tmp / vg_this->pe_size;
		pe_start = lv->lv_current_pe[index].pe;
		rsector_tmp = lv->lv_current_pe[index].pe +
		    (rsector_tmp % vg_this->pe_size);
		rdev_tmp = lv->lv_current_pe[index].dev;

#ifdef DEBUG_MAP
		printk(KERN_DEBUG
		       "lv_current_pe[%ld].pe: %ld  rdev: %02d:%02d  rsector:%ld\n",
		       index,
		       lv->lv_current_pe[index].pe,
		       MAJOR(rdev_tmp),
		       MINOR(rdev_tmp),
		       rsector_tmp);
#endif

		/* striped mapping */
	} else {
		ulong stripe_index;
		ulong stripe_length;

		stripe_length = vg_this->pe_size * lv->lv_stripes;
		stripe_index = (rsector_tmp % stripe_length) / lv->lv_stripesize;
		index = rsector_tmp / stripe_length +
		    (stripe_index % lv->lv_stripes) *
		    (lv->lv_allocated_le / lv->lv_stripes);
		pe_start = lv->lv_current_pe[index].pe;
		rsector_tmp = lv->lv_current_pe[index].pe +
		    (rsector_tmp % stripe_length) -
		    (stripe_index % lv->lv_stripes) * lv->lv_stripesize -
		    stripe_index / lv->lv_stripes *
		    (lv->lv_stripes - 1) * lv->lv_stripesize;
		rdev_tmp = lv->lv_current_pe[index].dev;
	}

#ifdef DEBUG_MAP
	printk(KERN_DEBUG
	     "lv_current_pe[%ld].pe: %ld  rdev: %02d:%02d  rsector:%ld\n"
	       "stripe_length: %ld  stripe_index: %ld\n",
	       index,
	       lv->lv_current_pe[index].pe,
	       MAJOR(rdev_tmp),
	       MINOR(rdev_tmp),
	       rsector_tmp,
	       stripe_length,
	       stripe_index);
#endif

	/* handle physical extents on the move */
	if (pe_lock_req.lock == LOCK_PE) {
		if (rdev_tmp == pe_lock_req.data.pv_dev &&
		    rsector_tmp >= pe_lock_req.data.pv_offset &&
		    rsector_tmp < (pe_lock_req.data.pv_offset +
				   vg_this->pe_size)) {
			sleep_on(&lvm_map_wait);
			rsector_tmp = rsector_sav;
			rdev_tmp = rdev_sav;
			goto lvm_second_remap;
		}
	}
	/* statistic */
	if (rw == WRITE || rw == WRITEA)
		lv->lv_current_pe[index].writes++;
	else
		lv->lv_current_pe[index].reads++;

	/* snapshot volume exception handling on physical device address base */
	if (lv->lv_access & (LV_SNAPSHOT|LV_SNAPSHOT_ORG)) {
		/* original logical volume */
		if (lv->lv_access & LV_SNAPSHOT_ORG) {
			if (rw == WRITE || rw == WRITEA)
			{
				lv_t *lv_ptr;

				/* start with first snapshot and loop thrugh all of them */
				for (lv_ptr = lv->lv_snapshot_next;
				     lv_ptr != NULL;
				     lv_ptr = lv_ptr->lv_snapshot_next) {
					/* Check for inactive snapshot */
					if (!(lv_ptr->lv_status & LV_ACTIVE)) continue;
					down(&lv->lv_snapshot_org->lv_snapshot_sem);
					/* do we still have exception storage for this snapshot free? */
					if (lv_ptr->lv_block_exception != NULL) {
						rdev_sav = rdev_tmp;
						rsector_sav = rsector_tmp;
						if (!lvm_snapshot_remap_block(&rdev_tmp,
									      &rsector_tmp,
									      pe_start,
									      lv_ptr)) {
							/* create a new mapping */
							if (!(ret = lvm_snapshot_COW(rdev_tmp,
									       	     rsector_tmp,
									             pe_start,
									             rsector_sav,
									             lv_ptr)))
								ret = lvm_write_COW_table_block(vg_this,
												lv_ptr);
						}
						rdev_tmp = rdev_sav;
						rsector_tmp = rsector_sav;
					}
					up(&lv->lv_snapshot_org->lv_snapshot_sem);
				}
			}
		} else {
			/* remap snapshot logical volume */
			down(&lv->lv_snapshot_sem);
			if (lv->lv_block_exception != NULL)
				lvm_snapshot_remap_block(&rdev_tmp, &rsector_tmp, pe_start, lv);
			up(&lv->lv_snapshot_sem);
		}
	}
	bh->b_rdev = rdev_tmp;
	bh->b_rsector = rsector_tmp;

	return ret;
} /* lvm_map() */


/*
 * internal support functions
 */

#ifdef LVM_HD_NAME
/*
 * generate "hard disk" name
 */
void lvm_hd_name(char *buf, int minor)
{
	int len = 0;
	lv_t *lv_ptr;

	if (vg[VG_BLK(minor)] == NULL ||
	    (lv_ptr = vg[VG_BLK(minor)]->lv[LV_BLK(minor)]) == NULL)
		return;
	len = strlen(lv_ptr->lv_name) - 5;
	memcpy(buf, &lv_ptr->lv_name[5], len);
	buf[len] = 0;
	return;
}
#endif


/*
 * this one never should be called...
 */
static void lvm_dummy_device_request(request_queue_t * t)
{
	printk(KERN_EMERG
	     "%s -- oops, got lvm request for %02d:%02d [sector: %lu]\n",
	       lvm_name,
	       MAJOR(CURRENT->rq_dev),
	       MINOR(CURRENT->rq_dev),
	       CURRENT->sector);
	return;
}


/*
 * make request function
 */
static int lvm_make_request_fn(request_queue_t *q,
			       int rw,
			       struct buffer_head *bh)
{
	if (lvm_map(bh, rw)<0)
		return 0; /* failure, buffer_IO_error has been called, don't recurse */
	else
		return 1; /* all ok, mapping done, call lower level driver */
}


/********************************************************************
 *
 * Character device support functions
 *
 ********************************************************************/
/*
 * character device support function logical volume manager lock
 */
static int lvm_do_lock_lvm(void)
{
lock_try_again:
	spin_lock(&lvm_lock);
	if (lock != 0 && lock != current->pid) {
#ifdef DEBUG_IOCTL
		printk(KERN_INFO "lvm_do_lock_lvm: %s is locked by pid %d ...\n",
		       lvm_name, lock);
#endif
		spin_unlock(&lvm_lock);
		interruptible_sleep_on(&lvm_wait);
		if (current->sigpending != 0)
			return -EINTR;
#ifdef LVM_TOTAL_RESET
		if (lvm_reset_spindown > 0)
			return -EACCES;
#endif
		goto lock_try_again;
	}
	lock = current->pid;
	spin_unlock(&lvm_lock);
	return 0;
} /* lvm_do_lock_lvm */


/*
 * character device support function lock/unlock physical extend
 */
static int lvm_do_pe_lock_unlock(vg_t *vg_ptr, void *arg)
{
	uint p;

	if (vg_ptr == NULL) return -ENXIO;
	if (copy_from_user(&pe_lock_req, arg,
			   sizeof(pe_lock_req_t)) != 0) return -EFAULT;

	switch (pe_lock_req.lock) {
	case LOCK_PE:
		for (p = 0; p < vg_ptr->pv_max; p++) {
			if (vg_ptr->pv[p] != NULL &&
			    pe_lock_req.data.pv_dev ==
			    vg_ptr->pv[p]->pv_dev)
				break;
		}
		if (p == vg_ptr->pv_max) return -ENXIO;

		pe_lock_req.lock = UNLOCK_PE;
		fsync_dev(pe_lock_req.data.lv_dev);
		pe_lock_req.lock = LOCK_PE;
		break;

	case UNLOCK_PE:
		pe_lock_req.lock = UNLOCK_PE;
		pe_lock_req.data.lv_dev = \
		pe_lock_req.data.pv_dev = \
		pe_lock_req.data.pv_offset = 0;
		wake_up(&lvm_map_wait);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}


/*
 * character device support function logical extend remap
 */
static int lvm_do_le_remap(vg_t *vg_ptr, void *arg)
{
	uint l, le;
	lv_t *lv_ptr;

	if (vg_ptr == NULL) return -ENXIO;
	if (copy_from_user(&le_remap_req, arg,
			   sizeof(le_remap_req_t)) != 0)
		return -EFAULT;

	for (l = 0; l < vg_ptr->lv_max; l++) {
		lv_ptr = vg_ptr->lv[l];
		if (lv_ptr != NULL &&
		    strcmp(lv_ptr->lv_name,
			       le_remap_req.lv_name) == 0) {
			for (le = 0; le < lv_ptr->lv_allocated_le; le++) {
				if (lv_ptr->lv_current_pe[le].dev ==
				    le_remap_req.old_dev &&
				    lv_ptr->lv_current_pe[le].pe ==
				    le_remap_req.old_pe) {
					lv_ptr->lv_current_pe[le].dev =
					    le_remap_req.new_dev;
					lv_ptr->lv_current_pe[le].pe =
					    le_remap_req.new_pe;
					return 0;
				}
			}
			return -EINVAL;
		}
	}
	return -ENXIO;
} /* lvm_do_le_remap() */


/*
 * character device support function VGDA create
 */
int lvm_do_vg_create(int minor, void *arg)
{
	int ret = 0;
	ulong l, ls = 0, p, size;
	lv_t lv;
	vg_t *vg_ptr;
	lv_t **snap_lv_ptr;

	if (vg[VG_CHR(minor)] != NULL) return -EPERM;

	if ((vg_ptr = kmalloc(sizeof(vg_t),GFP_KERNEL)) == NULL) {
		printk(KERN_CRIT
		       "%s -- VG_CREATE: kmalloc error VG at line %d\n",
		       lvm_name, __LINE__);
		return -ENOMEM;
	}
	/* get the volume group structure */
	if (copy_from_user(vg_ptr, arg, sizeof(vg_t)) != 0) {
		kfree(vg_ptr);
		return -EFAULT;
	}

	/* we are not that active so far... */
	vg_ptr->vg_status &= ~VG_ACTIVE;
	vg[VG_CHR(minor)] = vg_ptr;
	vg[VG_CHR(minor)]->pe_allocated = 0;

	if (vg_ptr->pv_max > ABS_MAX_PV) {
		printk(KERN_WARNING
		       "%s -- Can't activate VG: ABS_MAX_PV too small\n",
		       lvm_name);
		kfree(vg_ptr);
		vg[VG_CHR(minor)] = NULL;
		return -EPERM;
	}
	if (vg_ptr->lv_max > ABS_MAX_LV) {
		printk(KERN_WARNING
		"%s -- Can't activate VG: ABS_MAX_LV too small for %u\n",
		       lvm_name, vg_ptr->lv_max);
		kfree(vg_ptr);
		vg_ptr = NULL;
		return -EPERM;
	}

	/* get the physical volume structures */
	vg_ptr->pv_act = vg_ptr->pv_cur = 0;
	for (p = 0; p < vg_ptr->pv_max; p++) {
		/* user space address */
		if ((pvp = vg_ptr->pv[p]) != NULL) {
			ret = lvm_do_pv_create(pvp, vg_ptr, p);
			if ( ret != 0) {
				lvm_do_vg_remove(minor);
				return ret;
			}
		}
	}

	size = vg_ptr->lv_max * sizeof(lv_t *);
	if ((snap_lv_ptr = vmalloc ( size)) == NULL) {
		printk(KERN_CRIT
		       "%s -- VG_CREATE: vmalloc error snapshot LVs at line %d\n",
		       lvm_name, __LINE__);
		lvm_do_vg_remove(minor);
		return -EFAULT;
	}
	memset(snap_lv_ptr, 0, size);

	/* get the logical volume structures */
	vg_ptr->lv_cur = 0;
	for (l = 0; l < vg_ptr->lv_max; l++) {
		/* user space address */
		if ((lvp = vg_ptr->lv[l]) != NULL) {
			if (copy_from_user(&lv, lvp, sizeof(lv_t)) != 0) {
				lvm_do_vg_remove(minor);
				return -EFAULT;
			}
			if ( lv.lv_access & LV_SNAPSHOT) {
				snap_lv_ptr[ls] = lvp;
				vg_ptr->lv[l] = NULL;
				ls++;
				continue;
			}
			vg_ptr->lv[l] = NULL;
			/* only create original logical volumes for now */
			if (lvm_do_lv_create(minor, lv.lv_name, &lv) != 0) {
				lvm_do_vg_remove(minor);
				return -EFAULT;
			}
		}
	}

	/* Second path to correct snapshot logical volumes which are not
	   in place during first path above */
	for (l = 0; l < ls; l++) {
		lvp = snap_lv_ptr[l];
		if (copy_from_user(&lv, lvp, sizeof(lv_t)) != 0) {
			lvm_do_vg_remove(minor);
			return -EFAULT;
		}
		if (lvm_do_lv_create(minor, lv.lv_name, &lv) != 0) {
			lvm_do_vg_remove(minor);
			return -EFAULT;
		}
	}

#ifdef	CONFIG_DEVFS_FS
	vg_devfs_handle[vg_ptr->vg_number] = devfs_mk_dir(0, vg_ptr->vg_name, NULL);
	ch_devfs_handle[vg_ptr->vg_number] = devfs_register(
		vg_devfs_handle[vg_ptr->vg_number] , "group",
		DEVFS_FL_DEFAULT, LVM_CHAR_MAJOR, vg_ptr->vg_number,
		S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP,
		&lvm_chr_fops, NULL);
#endif

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	lvm_do_create_proc_entry_of_vg ( vg_ptr);
#endif

	vfree(snap_lv_ptr);

	vg_count++;


	MOD_INC_USE_COUNT;

	/* let's go active */
	vg_ptr->vg_status |= VG_ACTIVE;

	return 0;
} /* lvm_do_vg_create() */


/*
 * character device support function VGDA extend
 */
static int lvm_do_vg_extend(vg_t *vg_ptr, void *arg)
{
	int ret = 0;
	uint p;
	pv_t *pv_ptr;

	if (vg_ptr == NULL) return -ENXIO;
	if (vg_ptr->pv_cur < vg_ptr->pv_max) {
		for (p = 0; p < vg_ptr->pv_max; p++) {
			if ( ( pv_ptr = vg_ptr->pv[p]) == NULL) {
				ret = lvm_do_pv_create(arg, vg_ptr, p);
				lvm_do_create_proc_entry_of_pv ( vg_ptr, pv_ptr);
				if ( ret != 0) return ret;
	
				/* We don't need the PE list
				   in kernel space like LVs pe_t list */
				pv_ptr->pe = NULL;
				vg_ptr->pv_cur++;
				vg_ptr->pv_act++;
				vg_ptr->pe_total +=
				    pv_ptr->pe_total;
#ifdef LVM_GET_INODE
				/* insert a dummy inode for fs_may_mount */
				pv_ptr->inode = lvm_get_inode(pv_ptr->pv_dev);
#endif
				return 0;
			}
		}
	}
return -EPERM;
} /* lvm_do_vg_extend() */


/*
 * character device support function VGDA reduce
 */
static int lvm_do_vg_reduce(vg_t *vg_ptr, void *arg) {
	uint p;
	pv_t *pv_ptr;

	if (vg_ptr == NULL) return -ENXIO;
	if (copy_from_user(pv_name, arg, sizeof(pv_name)) != 0)
		return -EFAULT;

	for (p = 0; p < vg_ptr->pv_max; p++) {
		pv_ptr = vg_ptr->pv[p];
		if (pv_ptr != NULL &&
		    strcmp(pv_ptr->pv_name,
			       pv_name) == 0) {
			if (pv_ptr->lv_cur > 0) return -EPERM;
			vg_ptr->pe_total -=
			    pv_ptr->pe_total;
			vg_ptr->pv_cur--;
			vg_ptr->pv_act--;
			lvm_do_pv_remove(vg_ptr, p);
			/* Make PV pointer array contiguous */
			for (; p < vg_ptr->pv_max - 1; p++)
				vg_ptr->pv[p] = vg_ptr->pv[p + 1];
			vg_ptr->pv[p + 1] = NULL;
			return 0;
		}
	}
	return -ENXIO;
} /* lvm_do_vg_reduce */


/*
 * character device support function VG rename
 */
static int lvm_do_vg_rename(vg_t *vg_ptr, void *arg)
{
	int l = 0, p = 0, len = 0;
	char vg_name[NAME_LEN] = { 0,};
	char lv_name[NAME_LEN] = { 0,};
	char *ptr = NULL;
	lv_t *lv_ptr = NULL;
	pv_t *pv_ptr = NULL;

	if (copy_from_user(vg_name, arg, sizeof(vg_name)) != 0)
		return -EFAULT;

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	lvm_do_remove_proc_entry_of_vg ( vg_ptr);
#endif

	strncpy ( vg_ptr->vg_name, vg_name, sizeof ( vg_name)-1);
	for ( l = 0; l < vg_ptr->lv_max; l++)
	{
		if ((lv_ptr = vg_ptr->lv[l]) == NULL) continue;
		strncpy(lv_ptr->vg_name, vg_name, sizeof ( vg_name));
		ptr = strrchr(lv_ptr->lv_name, '/');
		if (ptr == NULL) ptr = lv_ptr->lv_name;
		strncpy(lv_name, ptr, sizeof ( lv_name));
		len = sizeof(LVM_DIR_PREFIX);
		strcpy(lv_ptr->lv_name, LVM_DIR_PREFIX);
		strncat(lv_ptr->lv_name, vg_name, NAME_LEN - len);
		len += strlen ( vg_name);
		strncat(lv_ptr->lv_name, lv_name, NAME_LEN - len);
	}
	for ( p = 0; p < vg_ptr->pv_max; p++)
	{
		if ( (pv_ptr = vg_ptr->pv[p]) == NULL) continue;
		strncpy(pv_ptr->vg_name, vg_name, NAME_LEN);
	}

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	lvm_do_create_proc_entry_of_vg ( vg_ptr);
#endif

	return 0;
} /* lvm_do_vg_rename */


/*
 * character device support function VGDA remove
 */
static int lvm_do_vg_remove(int minor)
{
	int i;
	vg_t *vg_ptr = vg[VG_CHR(minor)];
	pv_t *pv_ptr;

	if (vg_ptr == NULL) return -ENXIO;

#ifdef LVM_TOTAL_RESET
	if (vg_ptr->lv_open > 0 && lvm_reset_spindown == 0)
#else
	if (vg_ptr->lv_open > 0)
#endif
		return -EPERM;

	/* let's go inactive */
	vg_ptr->vg_status &= ~VG_ACTIVE;

	/* free LVs */
	/* first free snapshot logical volumes */
	for (i = 0; i < vg_ptr->lv_max; i++) {
		if (vg_ptr->lv[i] != NULL &&
		    vg_ptr->lv[i]->lv_access & LV_SNAPSHOT) {
			lvm_do_lv_remove(minor, NULL, i);
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(1);
		}
	}
	/* then free the rest of the LVs */
	for (i = 0; i < vg_ptr->lv_max; i++) {
		if (vg_ptr->lv[i] != NULL) {
			lvm_do_lv_remove(minor, NULL, i);
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(1);
		}
	}

	/* free PVs */
	for (i = 0; i < vg_ptr->pv_max; i++) {
		if ((pv_ptr = vg_ptr->pv[i]) != NULL) {
#ifdef DEBUG_KFREE
			printk(KERN_DEBUG
			       "%s -- kfree %d\n", lvm_name, __LINE__);
#endif
			lvm_do_pv_remove(vg_ptr, i);
		}
	}

#ifdef	CONFIG_DEVFS_FS
	devfs_unregister (ch_devfs_handle[vg_ptr->vg_number]);
	devfs_unregister (vg_devfs_handle[vg_ptr->vg_number]);
#endif

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	lvm_do_remove_proc_entry_of_vg ( vg_ptr);
#endif

#ifdef DEBUG_KFREE
	printk(KERN_DEBUG "%s -- kfree %d\n", lvm_name, __LINE__);
#endif

	kfree(vg_ptr);
	vg[VG_CHR(minor)] = NULL;

	vg_count--;

	MOD_DEC_USE_COUNT;

	return 0;
} /* lvm_do_vg_remove() */


/*
 * character device support function physical volume create
 */
static int lvm_do_pv_create(pv_t *pvp, vg_t *vg_ptr, ulong p) {
	pv_t *pv_ptr = NULL;

	pv_ptr = vg_ptr->pv[p] = kmalloc(sizeof(pv_t),GFP_KERNEL);
	if (pv_ptr == NULL) {
		printk(KERN_CRIT
		       "%s -- VG_CREATE: kmalloc error PV at line %d\n",
		       lvm_name, __LINE__);
		return -ENOMEM;
	}
	if (copy_from_user(pv_ptr, pvp, sizeof(pv_t)) != 0) {
		return -EFAULT;
	}
	/* We don't need the PE list
	   in kernel space as with LVs pe_t list (see below) */
	pv_ptr->pe = NULL;
	pv_ptr->pe_allocated = 0;
	pv_ptr->pv_status = PV_ACTIVE;
	vg_ptr->pv_act++;
	vg_ptr->pv_cur++;

#ifdef LVM_GET_INODE
	/* insert a dummy inode for fs_may_mount */
	pv_ptr->inode = lvm_get_inode(pv_ptr->pv_dev);
#endif

	return 0;
} /* lvm_do_pv_create() */


/*
 * character device support function physical volume create
 */
static int lvm_do_pv_remove(vg_t *vg_ptr, ulong p) {
	pv_t *pv_ptr = vg_ptr->pv[p];

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	lvm_do_remove_proc_entry_of_pv ( vg_ptr, pv_ptr);
#endif
	vg_ptr->pe_total -=
	    pv_ptr->pe_total;
	vg_ptr->pv_cur--;
	vg_ptr->pv_act--;
#ifdef LVM_GET_INODE
	lvm_clear_inode(pv_ptr->inode);
#endif
	kfree(pv_ptr);
	vg_ptr->pv[p] = NULL;

	return 0;
}


/*
 * character device support function logical volume create
 */
static int lvm_do_lv_create(int minor, char *lv_name, lv_t *lv)
{
	int e, ret, l, le, l_new, p, size;
	ulong lv_status_save;
	lv_block_exception_t *lvbe = lv->lv_block_exception;
	vg_t *vg_ptr = vg[VG_CHR(minor)];
	lv_t *lv_ptr = NULL;

	if ((pep = lv->lv_current_pe) == NULL) return -EINVAL;
	if (lv->lv_chunk_size > LVM_SNAPSHOT_MAX_CHUNK)
		return -EINVAL;

	for (l = 0; l < vg_ptr->lv_max; l++) {
		if (vg_ptr->lv[l] != NULL &&
		    strcmp(vg_ptr->lv[l]->lv_name, lv_name) == 0)
			return -EEXIST;
	}

	/* in case of lv_remove(), lv_create() pair */
	l_new = -1;
	if (vg_ptr->lv[lv->lv_number] == NULL)
		l_new = lv->lv_number;
	else {
		for (l = 0; l < vg_ptr->lv_max; l++) {
			if (vg_ptr->lv[l] == NULL)
				if (l_new == -1) l_new = l;
		}
	}
	if (l_new == -1) return -EPERM;
	else             l = l_new;

	if ((lv_ptr = kmalloc(sizeof(lv_t),GFP_KERNEL)) == NULL) {;
		printk(KERN_CRIT "%s -- LV_CREATE: kmalloc error LV at line %d\n",
		       lvm_name, __LINE__);
		return -ENOMEM;
	}
	/* copy preloaded LV */
	memcpy((char *) lv_ptr, (char *) lv, sizeof(lv_t));

	lv_status_save = lv_ptr->lv_status;
	lv_ptr->lv_status &= ~LV_ACTIVE;
	lv_ptr->lv_snapshot_org = \
	lv_ptr->lv_snapshot_prev = \
	lv_ptr->lv_snapshot_next = NULL;
	lv_ptr->lv_block_exception = NULL;
	lv_ptr->lv_iobuf = NULL;
	lv_ptr->lv_snapshot_hash_table = NULL;
	lv_ptr->lv_snapshot_hash_table_size = 0;
	lv_ptr->lv_snapshot_hash_mask = 0;
	lv_ptr->lv_COW_table_page = NULL;
	init_MUTEX(&lv_ptr->lv_snapshot_sem);
	lv_ptr->lv_snapshot_use_rate = 0;
	vg_ptr->lv[l] = lv_ptr;

	/* get the PE structures from user space if this
	   is no snapshot logical volume */
	if (!(lv_ptr->lv_access & LV_SNAPSHOT)) {
		size = lv_ptr->lv_allocated_le * sizeof(pe_t);
		if ((lv_ptr->lv_current_pe = vmalloc(size)) == NULL) {
			printk(KERN_CRIT
			       "%s -- LV_CREATE: vmalloc error LV_CURRENT_PE of %d Byte "
			       "at line %d\n",
			       lvm_name, size, __LINE__);
#ifdef DEBUG_KFREE
			printk(KERN_DEBUG "%s -- kfree %d\n", lvm_name, __LINE__);
#endif
			kfree(lv_ptr);
			vg[VG_CHR(minor)]->lv[l] = NULL;
			return -ENOMEM;
		}
		if (copy_from_user(lv_ptr->lv_current_pe, pep, size)) {
			vfree(lv_ptr->lv_current_pe);
			kfree(lv_ptr);
			vg_ptr->lv[l] = NULL;
			return -EFAULT;
		}
		/* correct the PE count in PVs */
		for (le = 0; le < lv_ptr->lv_allocated_le; le++) {
			vg_ptr->pe_allocated++;
			for (p = 0; p < vg_ptr->pv_cur; p++) {
				if (vg_ptr->pv[p]->pv_dev ==
				    lv_ptr->lv_current_pe[le].dev)
					vg_ptr->pv[p]->pe_allocated++;
			}
		}
	} else {
		/* Get snapshot exception data and block list */
		if (lvbe != NULL) {
			lv_ptr->lv_snapshot_org =
			    vg_ptr->lv[LV_BLK(lv_ptr->lv_snapshot_minor)];
			if (lv_ptr->lv_snapshot_org != NULL) {
				size = lv_ptr->lv_remap_end * sizeof(lv_block_exception_t);
				if ((lv_ptr->lv_block_exception = vmalloc(size)) == NULL) {
					printk(KERN_CRIT
					       "%s -- lvm_do_lv_create: vmalloc error LV_BLOCK_EXCEPTION "
					       "of %d byte at line %d\n",
					       lvm_name, size, __LINE__);
#ifdef DEBUG_KFREE
					printk(KERN_DEBUG "%s -- kfree %d\n", lvm_name, __LINE__);
#endif
					kfree(lv_ptr);
					vg_ptr->lv[l] = NULL;
					return -ENOMEM;
				}
				if (copy_from_user(lv_ptr->lv_block_exception, lvbe, size)) {
					vfree(lv_ptr->lv_block_exception);
					kfree(lv_ptr);
					vg[VG_CHR(minor)]->lv[l] = NULL;
					return -EFAULT;
				}
				/* point to the original logical volume */
				lv_ptr = lv_ptr->lv_snapshot_org;

				lv_ptr->lv_snapshot_minor = 0;
				lv_ptr->lv_snapshot_org = lv_ptr;
				lv_ptr->lv_snapshot_prev = NULL;
				/* walk thrugh the snapshot list */
				while (lv_ptr->lv_snapshot_next != NULL)
					lv_ptr = lv_ptr->lv_snapshot_next;
				/* now lv_ptr points to the last existing snapshot in the chain */
				vg_ptr->lv[l]->lv_snapshot_prev = lv_ptr;
				/* our new one now back points to the previous last in the chain
				   which can be the original logical volume */
				lv_ptr = vg_ptr->lv[l];
				/* now lv_ptr points to our new last snapshot logical volume */
				lv_ptr->lv_snapshot_org = lv_ptr->lv_snapshot_prev->lv_snapshot_org;
				lv_ptr->lv_snapshot_next = NULL;
				lv_ptr->lv_current_pe = lv_ptr->lv_snapshot_org->lv_current_pe;
				lv_ptr->lv_allocated_le = lv_ptr->lv_snapshot_org->lv_allocated_le;
				lv_ptr->lv_current_le = lv_ptr->lv_snapshot_org->lv_current_le;
				lv_ptr->lv_size = lv_ptr->lv_snapshot_org->lv_size;
				lv_ptr->lv_stripes = lv_ptr->lv_snapshot_org->lv_stripes;
				lv_ptr->lv_stripesize = lv_ptr->lv_snapshot_org->lv_stripesize;
				if ((ret = lvm_snapshot_alloc(lv_ptr)) != 0)
				{
					vfree(lv_ptr->lv_block_exception);
					kfree(lv_ptr);
					vg[VG_CHR(minor)]->lv[l] = NULL;
					return ret;
				}
				for ( e = 0; e < lv_ptr->lv_remap_ptr; e++)
					lvm_hash_link (lv_ptr->lv_block_exception + e, lv_ptr->lv_block_exception[e].rdev_org, lv_ptr->lv_block_exception[e].rsector_org, lv_ptr);
				/* need to fill the COW exception table data
				   into the page for disk i/o */
				lvm_snapshot_fill_COW_page(vg_ptr, lv_ptr);
				init_waitqueue_head(&lv_ptr->lv_snapshot_wait);
			} else {
				vfree(lv_ptr->lv_block_exception);
				kfree(lv_ptr);
				vg_ptr->lv[l] = NULL;
				return -EFAULT;
			}
		} else {
			kfree(vg_ptr->lv[l]);
			vg_ptr->lv[l] = NULL;
			return -EINVAL;
		}
	} /* if ( vg[VG_CHR(minor)]->lv[l]->lv_access & LV_SNAPSHOT) */

	lv_ptr = vg_ptr->lv[l];
	lvm_gendisk.part[MINOR(lv_ptr->lv_dev)].start_sect = 0;
	lvm_gendisk.part[MINOR(lv_ptr->lv_dev)].nr_sects = lv_ptr->lv_size;
	lvm_size[MINOR(lv_ptr->lv_dev)] = lv_ptr->lv_size >> 1;
	vg_lv_map[MINOR(lv_ptr->lv_dev)].vg_number = vg_ptr->vg_number;
	vg_lv_map[MINOR(lv_ptr->lv_dev)].lv_number = lv_ptr->lv_number;
	LVM_CORRECT_READ_AHEAD(lv_ptr->lv_read_ahead);
	vg_ptr->lv_cur++;
	lv_ptr->lv_status = lv_status_save;

#ifdef	CONFIG_DEVFS_FS
	{
	char *lv_tmp, *lv_buf = NULL;

	strtok(lv->lv_name, "/");       /* /dev */
	while((lv_tmp = strtok(NULL, "/")) != NULL)
		lv_buf = lv_tmp;

	lv_devfs_handle[lv->lv_number] = devfs_register(
		vg_devfs_handle[vg_ptr->vg_number], lv_buf,
		DEVFS_FL_DEFAULT, LVM_BLK_MAJOR, lv->lv_number,
		S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP,
		&lvm_blk_dops, NULL);
	}
#endif

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	lvm_do_create_proc_entry_of_lv ( vg_ptr, lv_ptr);
#endif

	/* optionally add our new snapshot LV */
	if (lv_ptr->lv_access & LV_SNAPSHOT) {
		/* sync the original logical volume */
		fsync_dev(lv_ptr->lv_snapshot_org->lv_dev);
#ifdef	LVM_VFS_ENHANCEMENT
		/* VFS function call to sync and lock the filesystem */
		fsync_dev_lockfs(lv_ptr->lv_snapshot_org->lv_dev);
#endif
		lv_ptr->lv_snapshot_org->lv_access |= LV_SNAPSHOT_ORG;
		lv_ptr->lv_access &= ~LV_SNAPSHOT_ORG;
		/* put ourselve into the chain */
		lv_ptr->lv_snapshot_prev->lv_snapshot_next = lv_ptr;
	}

	/* activate the logical volume */
	lv_ptr->lv_status |= LV_ACTIVE;
	if ( lv_ptr->lv_access & LV_WRITE)
		set_device_ro(lv_ptr->lv_dev, 0);
	else
		set_device_ro(lv_ptr->lv_dev, 1);

#ifdef	LVM_VFS_ENHANCEMENT
/* VFS function call to unlock the filesystem */
	if (lv_ptr->lv_access & LV_SNAPSHOT) {
		unlockfs(lv_ptr->lv_snapshot_org->lv_dev);
	}
#endif

	lv_ptr->vg = vg_ptr;

	return 0;
} /* lvm_do_lv_create() */


/*
 * character device support function logical volume remove
 */
static int lvm_do_lv_remove(int minor, char *lv_name, int l)
{
	uint le, p;
	vg_t *vg_ptr = vg[VG_CHR(minor)];
	lv_t *lv_ptr;

	if (l == -1) {
		for (l = 0; l < vg_ptr->lv_max; l++) {
			if (vg_ptr->lv[l] != NULL &&
			    strcmp(vg_ptr->lv[l]->lv_name, lv_name) == 0) {
				break;
			}
		}
	}
	if (l == vg_ptr->lv_max) return -ENXIO;

	lv_ptr = vg_ptr->lv[l];
#ifdef LVM_TOTAL_RESET
	if (lv_ptr->lv_open > 0 && lvm_reset_spindown == 0)
#else
	if (lv_ptr->lv_open > 0)
#endif
		return -EBUSY;

	/* check for deletion of snapshot source while
	   snapshot volume still exists */
	if ((lv_ptr->lv_access & LV_SNAPSHOT_ORG) &&
	    lv_ptr->lv_snapshot_next != NULL)
		return -EPERM;

	lv_ptr->lv_status |= LV_SPINDOWN;

	/* sync the buffers */
	fsync_dev(lv_ptr->lv_dev);

	lv_ptr->lv_status &= ~LV_ACTIVE;

	/* invalidate the buffers */
	invalidate_buffers(lv_ptr->lv_dev);

	/* reset generic hd */
	lvm_gendisk.part[MINOR(lv_ptr->lv_dev)].start_sect = -1;
	lvm_gendisk.part[MINOR(lv_ptr->lv_dev)].nr_sects = 0;
	lvm_size[MINOR(lv_ptr->lv_dev)] = 0;

	/* reset VG/LV mapping */
	vg_lv_map[MINOR(lv_ptr->lv_dev)].vg_number = ABS_MAX_VG;
	vg_lv_map[MINOR(lv_ptr->lv_dev)].lv_number = -1;

	/* correct the PE count in PVs if this is no snapshot logical volume */
	if (!(lv_ptr->lv_access & LV_SNAPSHOT)) {
		/* only if this is no snapshot logical volume because
		   we share the lv_current_pe[] structs with the
		   original logical volume */
		for (le = 0; le < lv_ptr->lv_allocated_le; le++) {
			vg_ptr->pe_allocated--;
			for (p = 0; p < vg_ptr->pv_cur; p++) {
				if (vg_ptr->pv[p]->pv_dev ==
				    lv_ptr->lv_current_pe[le].dev)
					vg_ptr->pv[p]->pe_allocated--;
			}
		}
		vfree(lv_ptr->lv_current_pe);
	/* LV_SNAPSHOT */
	} else {
		/* remove this snapshot logical volume from the chain */
		lv_ptr->lv_snapshot_prev->lv_snapshot_next = lv_ptr->lv_snapshot_next;
		if (lv_ptr->lv_snapshot_next != NULL) {
			lv_ptr->lv_snapshot_next->lv_snapshot_prev =
			    lv_ptr->lv_snapshot_prev;
		}
		/* no more snapshots? */
		if (lv_ptr->lv_snapshot_org->lv_snapshot_next == NULL)
			lv_ptr->lv_snapshot_org->lv_access &= ~LV_SNAPSHOT_ORG;
		lvm_snapshot_release(lv_ptr);
	}

#ifdef	CONFIG_DEVFS_FS
	devfs_unregister(lv_devfs_handle[lv_ptr->lv_number]);
#endif

#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
	lvm_do_remove_proc_entry_of_lv ( vg_ptr, lv_ptr);
#endif

#ifdef DEBUG_KFREE
	printk(KERN_DEBUG "%s -- kfree %d\n", lvm_name, __LINE__);
#endif
	kfree(lv_ptr);
	vg_ptr->lv[l] = NULL;
	vg_ptr->lv_cur--;
	return 0;
} /* lvm_do_lv_remove() */


/*
 * character device support function logical volume extend / reduce
 */
static int lvm_do_lv_extend_reduce(int minor, char *lv_name, lv_t *lv)
{
	ulong end, l, le, p, size, old_allocated_le;
	vg_t *vg_ptr = vg[VG_CHR(minor)];
	lv_t *lv_ptr;
	pe_t *pe;

	if ((pep = lv->lv_current_pe) == NULL) return -EINVAL;

	for (l = 0; l < vg_ptr->lv_max; l++) {
		if (vg_ptr->lv[l] != NULL &&
		    strcmp(vg_ptr->lv[l]->lv_name, lv_name) == 0)
			break;
	}
	if (l == vg_ptr->lv_max) return -ENXIO;
	lv_ptr = vg_ptr->lv[l];

	/* check for active snapshot */
	if (lv->lv_access & LV_SNAPSHOT)
	{
		ulong e;
		lv_block_exception_t *lvbe, *lvbe_old;
		struct list_head * lvs_hash_table_old;

		if (lv->lv_block_exception == NULL) return -ENXIO;
		size = lv->lv_remap_end * sizeof ( lv_block_exception_t);
		if ((lvbe = vmalloc(size)) == NULL)
		{
			printk(KERN_CRIT
			"%s -- lvm_do_lv_extend_reduce: vmalloc error LV_BLOCK_EXCEPTION "
			       "of %lu Byte at line %d\n",
			       lvm_name, size, __LINE__);
			return -ENOMEM;
		}
		if (lv->lv_remap_end > lv_ptr->lv_remap_end)
		{
			if (copy_from_user(lvbe, lv->lv_block_exception, size))
			{
				vfree(lvbe);
				return -EFAULT;
			}
		}

		lvbe_old = lv_ptr->lv_block_exception;
		lvs_hash_table_old = lv_ptr->lv_snapshot_hash_table;

		/* we need to play on the safe side here... */
		down(&lv_ptr->lv_snapshot_org->lv_snapshot_sem);
		if (lv_ptr->lv_block_exception == NULL ||
		    lv_ptr->lv_remap_ptr > lv_ptr->lv_remap_end)
		{
			up(&lv_ptr->lv_snapshot_org->lv_snapshot_sem);
			vfree(lvbe);
			return -EPERM;
		}
		memcpy(lvbe,
		       lv_ptr->lv_block_exception,
		       (lv->lv_remap_end > lv_ptr->lv_remap_end ? lv_ptr->lv_remap_ptr : lv->lv_remap_end) * sizeof(lv_block_exception_t));

		lv_ptr->lv_block_exception = lvbe;
		lv_ptr->lv_remap_end = lv->lv_remap_end;
		if (lvm_snapshot_alloc_hash_table(lv_ptr) != 0)
		{
			lvm_drop_snapshot(lv_ptr, "hash_alloc");
			up(&lv_ptr->lv_snapshot_org->lv_snapshot_sem);
			vfree(lvbe_old);
			vfree(lvs_hash_table_old);
			return 1;
		}

		for (e = 0; e < lv_ptr->lv_remap_ptr; e++)
			lvm_hash_link (lv_ptr->lv_block_exception + e, lv_ptr->lv_block_exception[e].rdev_org, lv_ptr->lv_block_exception[e].rsector_org, lv_ptr);

		up(&lv_ptr->lv_snapshot_org->lv_snapshot_sem);

		vfree(lvbe_old);
		vfree(lvs_hash_table_old);

		return 0;
	}


	/* we drop in here in case it is an original logical volume */
	if ((pe = vmalloc(size = lv->lv_current_le * sizeof(pe_t))) == NULL) {
		printk(KERN_CRIT
		"%s -- lvm_do_lv_extend_reduce: vmalloc error LV_CURRENT_PE "
		       "of %lu Byte at line %d\n",
		       lvm_name, size, __LINE__);
		return -ENOMEM;
	}
	/* get the PE structures from user space */
	if (copy_from_user(pe, pep, size)) {
		vfree(pe);
		return -EFAULT;
	}

#ifdef DEBUG
	printk(KERN_DEBUG
	       "%s -- fsync_dev and "
	       "invalidate_buffers for %s [%s] in %s\n",
	       lvm_name, lv_ptr->lv_name,
	       kdevname(lv_ptr->lv_dev),
	       vg_ptr->vg_name);
#endif

	/* reduce allocation counters on PV(s) */
	for (le = 0; le < lv_ptr->lv_allocated_le; le++) {
		vg_ptr->pe_allocated--;
		for (p = 0; p < vg_ptr->pv_cur; p++) {
			if (vg_ptr->pv[p]->pv_dev ==
			lv_ptr->lv_current_pe[le].dev) {
				vg_ptr->pv[p]->pe_allocated--;
				break;
			}
		}
	}


	/* save pointer to "old" lv/pe pointer array */
	pep1 = lv_ptr->lv_current_pe;
	end = lv_ptr->lv_current_le;

	/* save open counter... */
	lv->lv_open = lv_ptr->lv_open;
	lv->lv_snapshot_prev = lv_ptr->lv_snapshot_prev;
	lv->lv_snapshot_next = lv_ptr->lv_snapshot_next;
	lv->lv_snapshot_org  = lv_ptr->lv_snapshot_org;

	lv->lv_current_pe = pe;

	/* save # of old allocated logical extents */
	old_allocated_le = lv_ptr->lv_allocated_le;

        /* in case of shrinking -> let's flush */
        if ( end > lv->lv_current_le) fsync_dev(lv_ptr->lv_dev);

	/* copy preloaded LV */
	memcpy((char *) lv_ptr, (char *) lv, sizeof(lv_t));

	lvm_gendisk.part[MINOR(lv_ptr->lv_dev)].start_sect = 0;
	lvm_gendisk.part[MINOR(lv_ptr->lv_dev)].nr_sects = lv_ptr->lv_size;
	lvm_size[MINOR(lv_ptr->lv_dev)] = lv_ptr->lv_size >> 1;
	/* vg_lv_map array doesn't have to be changed here */

	LVM_CORRECT_READ_AHEAD(lv_ptr->lv_read_ahead);

	/* save availiable i/o statistic data */
	/* linear logical volume */
	if (lv_ptr->lv_stripes < 2) {
		/* Check what last LE shall be used */
		if (end > lv_ptr->lv_current_le) end = lv_ptr->lv_current_le;
		for (le = 0; le < end; le++) {
			lv_ptr->lv_current_pe[le].reads  += pep1[le].reads;
			lv_ptr->lv_current_pe[le].writes += pep1[le].writes;
		}
		/* striped logical volume */
	} else {
		uint i, j, source, dest, end, old_stripe_size, new_stripe_size;

		old_stripe_size = old_allocated_le / lv_ptr->lv_stripes;
		new_stripe_size = lv_ptr->lv_allocated_le / lv_ptr->lv_stripes;
		end = old_stripe_size;
		if (end > new_stripe_size) end = new_stripe_size;
		for (i = source = dest = 0;
		     i < lv_ptr->lv_stripes; i++) {
			for (j = 0; j < end; j++) {
				lv_ptr->lv_current_pe[dest + j].reads +=
				    pep1[source + j].reads;
				lv_ptr->lv_current_pe[dest + j].writes +=
				    pep1[source + j].writes;
			}
			source += old_stripe_size;
			dest += new_stripe_size;
		}
	}

	/* extend the PE count in PVs */
	for (le = 0; le < lv_ptr->lv_allocated_le; le++) {
		vg_ptr->pe_allocated++;
		for (p = 0; p < vg_ptr->pv_cur; p++) {
			if (vg_ptr->pv[p]->pv_dev ==
                            lv_ptr->lv_current_pe[le].dev) {
				vg_ptr->pv[p]->pe_allocated++;
				break;
			}
		}
	}

	vfree ( pep1);
	pep1 = NULL;

	if (lv->lv_access & LV_SNAPSHOT_ORG)
	{
		/* Correct the snapshot size information */
		while ((lv_ptr = lv_ptr->lv_snapshot_next) != NULL)
		{
			lv_ptr->lv_current_pe = lv_ptr->lv_snapshot_org->lv_current_pe;
			lv_ptr->lv_allocated_le = lv_ptr->lv_snapshot_org->lv_allocated_le;
			lv_ptr->lv_current_le = lv_ptr->lv_snapshot_org->lv_current_le;
			lv_ptr->lv_size = lv_ptr->lv_snapshot_org->lv_size;
			lvm_gendisk.part[MINOR(lv_ptr->lv_dev)].nr_sects = lv_ptr->lv_size;
			lvm_size[MINOR(lv_ptr->lv_dev)] = lv_ptr->lv_size >> 1;
		}
	}

	return 0;
} /* lvm_do_lv_extend_reduce() */


/*
 * character device support function logical volume status by name
 */
static int lvm_do_lv_status_byname(vg_t *vg_ptr, void *arg)
{
	uint l;
	ulong size;
	lv_t lv;
	lv_t *lv_ptr;
	lv_status_byname_req_t lv_status_byname_req;

	if (vg_ptr == NULL) return -ENXIO;
	if (copy_from_user(&lv_status_byname_req, arg,
			   sizeof(lv_status_byname_req_t)) != 0)
		return -EFAULT;

	if (lv_status_byname_req.lv == NULL) return -EINVAL;
	if (copy_from_user(&lv, lv_status_byname_req.lv,
			   sizeof(lv_t)) != 0)
		return -EFAULT;

	for (l = 0; l < vg_ptr->lv_max; l++) {
		lv_ptr = vg_ptr->lv[l];
		if (lv_ptr != NULL &&
		    strcmp(lv_ptr->lv_name,
			    lv_status_byname_req.lv_name) == 0) {
			if (copy_to_user(lv_status_byname_req.lv,
					 lv_ptr,
					 sizeof(lv_t)) != 0)
				return -EFAULT;

			if (lv.lv_current_pe != NULL) {
				size = lv_ptr->lv_allocated_le *
				       sizeof(pe_t);
				if (copy_to_user(lv.lv_current_pe,
						 lv_ptr->lv_current_pe,
						 size) != 0)
					return -EFAULT;
			}
			return 0;
		}
	}
	return -ENXIO;
} /* lvm_do_lv_status_byname() */


/*
 * character device support function logical volume status by index
 */
static int lvm_do_lv_status_byindex(vg_t *vg_ptr,void *arg)
{
	ulong size;
	lv_t lv;
	lv_t *lv_ptr;
	lv_status_byindex_req_t lv_status_byindex_req;

	if (vg_ptr == NULL) return -ENXIO;
	if (copy_from_user(&lv_status_byindex_req, arg,
			   sizeof(lv_status_byindex_req)) != 0)
		return -EFAULT;

	if ((lvp = lv_status_byindex_req.lv) == NULL)
		return -EINVAL;
	if ( ( lv_ptr = vg_ptr->lv[lv_status_byindex_req.lv_index]) == NULL)
		return -ENXIO;

	if (copy_from_user(&lv, lvp, sizeof(lv_t)) != 0)
		return -EFAULT;

	if (copy_to_user(lvp, lv_ptr, sizeof(lv_t)) != 0)
		return -EFAULT;

	if (lv.lv_current_pe != NULL) {
		size = lv_ptr->lv_allocated_le * sizeof(pe_t);
		if (copy_to_user(lv.lv_current_pe,
			 	 lv_ptr->lv_current_pe,
				 size) != 0)
			return -EFAULT;
	}
	return 0;
} /* lvm_do_lv_status_byindex() */


/*
 * character device support function logical volume status by device number
 */
static int lvm_do_lv_status_bydev(vg_t * vg_ptr, void * arg) {
	int l;
	lv_status_bydev_req_t lv_status_bydev_req;

	if (vg_ptr == NULL) return -ENXIO;
	if (copy_from_user(&lv_status_bydev_req, arg,
			   sizeof(lv_status_bydev_req)) != 0)
		return -EFAULT;

	for ( l = 0; l < vg_ptr->lv_max; l++) {
		if ( vg_ptr->lv[l] == NULL) continue;
		if ( vg_ptr->lv[l]->lv_dev == lv_status_bydev_req.dev) break;
	}

	if ( l == vg_ptr->lv_max) return -ENXIO;

	if (copy_to_user(lv_status_bydev_req.lv,
			 vg_ptr->lv[l], sizeof(lv_t)) != 0)
		return -EFAULT;

	return 0;
} /* lvm_do_lv_status_bydev() */


/*
 * character device support function rename a logical volume
 */
static int lvm_do_lv_rename(vg_t *vg_ptr, lv_req_t *lv_req, lv_t *lv)
{
	int l = 0;
	int ret = 0;
	lv_t *lv_ptr = NULL;

	for (l = 0; l < vg_ptr->lv_max; l++)
	{
		if ( (lv_ptr = vg_ptr->lv[l]) == NULL) continue;
		if (lv_ptr->lv_dev == lv->lv_dev)
		{
#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
			lvm_do_remove_proc_entry_of_lv ( vg_ptr, lv_ptr);
#endif
			strncpy(lv_ptr->lv_name,
				lv_req->lv_name,
				NAME_LEN);
#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
			lvm_do_create_proc_entry_of_lv ( vg_ptr, lv_ptr);
#endif
			break;
		}
	}
	if (l == vg_ptr->lv_max) ret = -ENODEV;

	return ret;
} /* lvm_do_lv_rename */


/*
 * character device support function physical volume change
 */
static int lvm_do_pv_change(vg_t *vg_ptr, void *arg)
{
	uint p;
	pv_t *pv_ptr;
#ifdef LVM_GET_INODE
	struct inode *inode_sav;
#endif

	if (vg_ptr == NULL) return -ENXIO;
	if (copy_from_user(&pv_change_req, arg,
			   sizeof(pv_change_req)) != 0)
		return -EFAULT;

	for (p = 0; p < vg_ptr->pv_max; p++) {
		pv_ptr = vg_ptr->pv[p];
		if (pv_ptr != NULL &&
		    strcmp(pv_ptr->pv_name,
			       pv_change_req.pv_name) == 0) {
#ifdef LVM_GET_INODE
			inode_sav = pv_ptr->inode;
#endif
			if (copy_from_user(pv_ptr,
					   pv_change_req.pv,
					   sizeof(pv_t)) != 0)
				return -EFAULT;

			/* We don't need the PE list
			   in kernel space as with LVs pe_t list */
			pv_ptr->pe = NULL;
#ifdef LVM_GET_INODE
			pv_ptr->inode = inode_sav;
#endif
			return 0;
		}
	}
	return -ENXIO;
} /* lvm_do_pv_change() */

/*
 * character device support function get physical volume status
 */
static int lvm_do_pv_status(vg_t *vg_ptr, void *arg)
{
	uint p;
	pv_t *pv_ptr;

	if (vg_ptr == NULL) return -ENXIO;
	if (copy_from_user(&pv_status_req, arg,
			   sizeof(pv_status_req)) != 0)
		return -EFAULT;

	for (p = 0; p < vg_ptr->pv_max; p++) {
		pv_ptr = vg_ptr->pv[p];
		if (pv_ptr != NULL &&
		    strcmp(pv_ptr->pv_name,
			       pv_status_req.pv_name) == 0) {
			if (copy_to_user(pv_status_req.pv,
					 pv_ptr,
				         sizeof(pv_t)) != 0)
				return -EFAULT;
			return 0;
		}
	}
	return -ENXIO;
} /* lvm_do_pv_status() */



/*
 * create a /proc entry for a logical volume
 */
inline void lvm_do_create_proc_entry_of_lv ( vg_t *vg_ptr, lv_t *lv_ptr) {
	char *basename;

	if ( vg_ptr->lv_subdir_pde != NULL) {
		basename = strrchr(lv_ptr->lv_name, '/');
		if (basename == NULL) basename = lv_ptr->lv_name;
		else		      basename++;
		pde = create_proc_entry(basename, S_IFREG,
					vg_ptr->lv_subdir_pde);
		if ( pde != NULL) {
			pde->read_proc = lvm_proc_read_lv_info;
			pde->data = lv_ptr;
		}
	}
}


/*
 * remove a /proc entry for a logical volume
 */
inline void lvm_do_remove_proc_entry_of_lv ( vg_t *vg_ptr, lv_t *lv_ptr) {
	char *basename;

	if ( vg_ptr->lv_subdir_pde != NULL) {
		basename = strrchr(lv_ptr->lv_name, '/');
		if (basename == NULL) basename = lv_ptr->lv_name;
		else		      basename++;
		remove_proc_entry(basename, vg_ptr->lv_subdir_pde);
	}
}


/*
 * create a /proc entry for a physical volume
 */
inline void lvm_do_create_proc_entry_of_pv ( vg_t *vg_ptr, pv_t *pv_ptr) {
	char *basename;

	basename = strrchr(pv_ptr->pv_name, '/');
	if (basename == NULL) basename = pv_ptr->pv_name;
	else		      basename++;
	pde = create_proc_entry(basename, S_IFREG, vg_ptr->pv_subdir_pde);
	if ( pde != NULL) {
		pde->read_proc = lvm_proc_read_pv_info;
		pde->data = pv_ptr;
	}
}


/*
 * remove a /proc entry for a physical volume
 */
inline void lvm_do_remove_proc_entry_of_pv ( vg_t *vg_ptr, pv_t *pv_ptr) {
	char *basename;

	basename = strrchr(pv_ptr->pv_name, '/');
	if ( vg_ptr->pv_subdir_pde != NULL) {
		basename = strrchr(pv_ptr->pv_name, '/');
		if (basename == NULL) basename = pv_ptr->pv_name;
		else		      basename++;
		remove_proc_entry(basename, vg_ptr->pv_subdir_pde);
	}
}


/*
 * create a /proc entry for a volume group
 */
#if defined CONFIG_LVM_PROC_FS && defined CONFIG_PROC_FS
void lvm_do_create_proc_entry_of_vg ( vg_t *vg_ptr) {
	int l, p;
	pv_t *pv_ptr;
	lv_t *lv_ptr;

	pde = create_proc_entry(vg_ptr->vg_name, S_IFDIR,
				lvm_proc_vg_subdir);
	if ( pde != NULL) {
		vg_ptr->vg_dir_pde = pde;
		pde = create_proc_entry("group", S_IFREG,
					vg_ptr->vg_dir_pde);
		if ( pde != NULL) {
			pde->read_proc = lvm_proc_read_vg_info;
			pde->data = vg_ptr;
		}
		vg_ptr->lv_subdir_pde =
			create_proc_entry(LVM_LV_SUBDIR, S_IFDIR,
					  vg_ptr->vg_dir_pde);
		vg_ptr->pv_subdir_pde =
			create_proc_entry(LVM_PV_SUBDIR, S_IFDIR,
					  vg_ptr->vg_dir_pde);
	}

	if ( vg_ptr->pv_subdir_pde != NULL) {
		for ( l = 0; l < vg_ptr->lv_max; l++) {
			if ( ( lv_ptr = vg_ptr->lv[l]) == NULL) continue;
			lvm_do_create_proc_entry_of_lv ( vg_ptr, lv_ptr);
		}
		for ( p = 0; p < vg_ptr->pv_max; p++) {
			if ( ( pv_ptr = vg_ptr->pv[p]) == NULL) continue;
			lvm_do_create_proc_entry_of_pv ( vg_ptr, pv_ptr);
		}
	}
}

/*
 * remove a /proc entry for a volume group
 */
void lvm_do_remove_proc_entry_of_vg ( vg_t *vg_ptr) {
	int l, p;
	lv_t *lv_ptr;
	pv_t *pv_ptr;

	for ( l = 0; l < vg_ptr->lv_max; l++) {
		if ( ( lv_ptr = vg_ptr->lv[l]) == NULL) continue;
		lvm_do_remove_proc_entry_of_lv ( vg_ptr, vg_ptr->lv[l]);
	}
	for ( p = 0; p < vg_ptr->pv_max; p++) {
		if ( ( pv_ptr = vg_ptr->pv[p]) == NULL) continue;
		lvm_do_remove_proc_entry_of_pv ( vg_ptr, vg_ptr->pv[p]);
	}
	if ( vg_ptr->vg_dir_pde != NULL) {
		remove_proc_entry(LVM_LV_SUBDIR, vg_ptr->vg_dir_pde);
		remove_proc_entry(LVM_PV_SUBDIR, vg_ptr->vg_dir_pde);
		remove_proc_entry("group", vg_ptr->vg_dir_pde);
		remove_proc_entry(vg_ptr->vg_name, lvm_proc_vg_subdir);
	}
}
#endif


/*
 * support function initialize gendisk variables
 */
#ifdef __initfunc
__initfunc(void lvm_geninit(struct gendisk *lvm_gdisk))
#else
void __init
 lvm_geninit(struct gendisk *lvm_gdisk)
#endif
{
	int i = 0;

#ifdef DEBUG_GENDISK
	printk(KERN_DEBUG "%s -- lvm_gendisk\n", lvm_name);
#endif

	for (i = 0; i < MAX_LV; i++) {
		lvm_gendisk.part[i].start_sect = -1;	/* avoid partition check */
		lvm_size[i] = lvm_gendisk.part[i].nr_sects = 0;
		lvm_blocksizes[i] = BLOCK_SIZE;
	}

	blk_size[MAJOR_NR] = lvm_size;
	blksize_size[MAJOR_NR] = lvm_blocksizes;
	hardsect_size[MAJOR_NR] = lvm_blocksizes;

	return;
} /* lvm_gen_init() */


#ifdef LVM_GET_INODE
/*
 * support function to get an empty inode
 *
 * Gets an empty inode to be inserted into the inode hash,
 * so that a physical volume can't be mounted.
 * This is analog to drivers/block/md.c
 *
 * Is this the real thing?
 *
 */
struct inode *lvm_get_inode(int dev)
{
	struct inode *inode_this = NULL;

	/* Lock the device by inserting a dummy inode. */
	inode_this = get_empty_inode();
	inode_this->i_dev = dev;
	insert_inode_hash(inode_this);
	return inode_this;
}


/*
 * support function to clear an inode
 *
 */
void lvm_clear_inode(struct inode *inode)
{
#ifdef I_FREEING
	inode->i_state |= I_FREEING;
#endif
	clear_inode(inode);
	return;
}
#endif /* #ifdef LVM_GET_INODE */
