/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */


#ifndef _ASM_SN_SGI_H
#define _ASM_SN_SGI_H

#include <linux/config.h>
#include <asm/sn/types.h>
#include <asm/uaccess.h>		/* for copy_??_user */
#include <linux/mm.h>
#include <linux/devfs_fs_kernel.h>

// This devfs stuff needs a better home .....

struct directory_type
{
    struct devfs_entry *first;
    struct devfs_entry *last;
    unsigned int num_removable;
};

struct file_type
{
    unsigned long size;
};

struct device_type
{
    unsigned short major;
    unsigned short minor;
};

struct fcb_type  /*  File, char, block type  */
{
    uid_t default_uid;
    gid_t default_gid;
    void *ops;
    union 
    {
	struct file_type file;
	struct device_type device;
    }
    u;
    unsigned char auto_owner:1;
    unsigned char aopen_notify:1;
    unsigned char removable:1;  /*  Belongs in device_type, but save space   */
    unsigned char open:1;       /*  Not entirely correct                     */
};

struct symlink_type
{
    unsigned int length;  /*  Not including the NULL-termimator  */
    char *linkname;       /*  This is NULL-terminated            */
};

struct fifo_type
{
    uid_t uid;
    gid_t gid;
};

struct devfs_entry
{
    void *info;
    union 
    {
	struct directory_type dir;
	struct fcb_type fcb;
	struct symlink_type symlink;
	struct fifo_type fifo;
    }
    u;
    struct devfs_entry *prev;    /*  Previous entry in the parent directory  */
    struct devfs_entry *next;    /*  Next entry in the parent directory      */
    struct devfs_entry *parent;  /*  The parent directory                    */
    struct devfs_entry *slave;   /*  Another entry to unregister             */
    struct devfs_inode *first_inode;
    struct devfs_inode *last_inode;
    umode_t mode;
    unsigned short namelen;  /*  I think 64k+ filenames are a way off...  */
    unsigned char registered:1;
    unsigned char show_unreg:1;
    unsigned char hide:1;
    unsigned char no_persistence /*:1*/;
    char name[1];            /*  This is just a dummy: the allocated array is
				 bigger. This is NULL-terminated  */
};

#define MIN(_a,_b)		((_a)<(_b)?(_a):(_b))

typedef uint32_t app32_ptr_t;	/* needed by edt.h */
typedef int64_t  __psint_t;	/* needed by klgraph.c */

typedef enum { B_FALSE, B_TRUE } boolean_t;

#define ctob(x)			((uint64_t)(x)*NBPC)
#define btoc(x)			(((uint64_t)(x)+(NBPC-1))/NBPC)

typedef __psunsigned_t nic_data_t;


/*
** Possible return values from graph routines.
*/
typedef enum graph_error_e {
	GRAPH_SUCCESS,		/* 0 */
	GRAPH_DUP,		/* 1 */
	GRAPH_NOT_FOUND,	/* 2 */
	GRAPH_BAD_PARAM,	/* 3 */
	GRAPH_HIT_LIMIT,	/* 4 */
	GRAPH_CANNOT_ALLOC,	/* 5 */
	GRAPH_ILLEGAL_REQUEST,	/* 6 */
	GRAPH_IN_USE		/* 7 */
} graph_error_t;

#define SV_FIFO         0x0             /* sv_t is FIFO type */
#define SV_LIFO         0x2             /* sv_t is LIFO type */
#define SV_PRIO         0x4             /* sv_t is PRIO type */
#define SV_KEYED        0x6             /* sv_t is KEYED type */
#define SV_DEFAULT      SV_FIFO


#define MUTEX_DEFAULT	0x0		/* needed by mutex_init() calls */
#define PZERO		25		/* needed by mutex_lock(), sv_wait()
					 * psema() calls */

#define sema_t  uint64_t		/* FIXME */
#define KM_SLEEP   0x0000
#define KM_NOSLEEP 0x0001		/* needed by kmem_alloc_node(), kmem_zalloc()
					 * calls */
#define VM_NOSLEEP 0x0001		/* needed kmem_alloc_node(), kmem_zalloc_node
					 * calls */
#define XG_WIDGET_PART_NUM      0xC102          /* KONA/xt_regs.h     XG_XT_PART_NUM_VALUE */

#ifndef K1BASE
#define K1BASE 0xA0000000
#endif

#ifndef TO_PHYS_MASK
#define TO_PHYS_MASK 0x0000000fffffffff
#endif

typedef uint64_t vhandl_t;


#ifndef NBPP
#define NBPP 4096
#endif

#ifndef D_MP
#define D_MP 1
#endif

#ifndef MAXDEVNAME
#define MAXDEVNAME 256
#endif

#ifndef NBPC
#define NBPC 0
#endif

#ifndef _PAGESZ
#define _PAGESZ 4096
#endif

typedef uint64_t k_machreg_t;	/* needed by cmn_err.h */

typedef uint64_t mrlock_t;	/* needed by devsupport.c */

#define HUB_PIO_CONVEYOR 0x1
#define CNODEID_NONE (cnodeid_t)-1
#define XTALK_PCI_PART_NUM "030-1275-"
#define kdebug 0


#define COPYIN(a, b, c)		copy_from_user(b,a,c)
#define COPYOUT(a, b, c)	copy_to_user(b,a,c)

#define kvtophys(x)		(alenaddr_t) (x)
#define POFFMASK		(NBPP - 1)
#define poff(X)			((__psunsigned_t)(X) & POFFMASK)

#define initnsema(a,b,c) 	sema_init(a,b)

#define BZERO(a,b)		memset(a, 0, b)

#define kern_malloc(x)	kmalloc(x, GFP_KERNEL)
#define kern_free(x)	kfree(x)

typedef cpuid_t cpu_cookie_t;
#define CPU_NONE		-1


#if defined(DISABLE_ASSERT)
#define ASSERT(expr)
#define ASSERT_ALWAYS(expr)
#else
#define ASSERT(expr)	\
        if(!(expr)) { \
		printk( "Assertion [%s] failed! %s:%s(line=%d)\n",\
			#expr,__FILE__,__FUNCTION__,__LINE__); \
		panic("Assertion panic\n"); 	\
        }

#define ASSERT_ALWAYS(expr)	\
        if(!(expr)) { \
		printk( "Assertion [%s] failed! %s:%s(line=%d)\n",\
			#expr,__FILE__,__FUNCTION__,__LINE__); \
		panic("Assertion always panic\n"); 	\
        }
#endif	/* DISABLE_ASSERT */

/* These are defined as cmn_err() replacements */
#define PRINT_WARNING(x...)	{ printk("WARNING : "); printk(x); }
#define PRINT_NOTICE(x...)	{ printk("NOTICE : "); printk(x); }
#define PRINT_ALERT(x...)	{ printk("ALERT : "); printk(x); }
#define PRINT_PANIC		panic

#define mutex_t int
#define spinlock_init(x,name) mutex_init(x, MUTEX_DEFAULT, name);

#ifdef CONFIG_SMP
#define cpu_enabled(cpu)        (test_bit(cpu, &cpu_online_map))
#else
#define cpu_enabled(cpu)	(1)
#endif

#include <asm/sn/hack.h>	/* for now */

#endif	/* _ASM_SN_SGI_H */
