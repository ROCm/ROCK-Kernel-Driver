/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */


#ifndef _ASM_IA64_SN_SGI_H
#define _ASM_IA64_SN_SGI_H

#include <linux/config.h>

#include <asm/sn/types.h>
#include <asm/uaccess.h>		/* for copy_??_user */
#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/sn/hwgfs.h>

typedef hwgfs_handle_t vertex_hdl_t;

typedef int64_t  __psint_t;	/* needed by klgraph.c */

typedef enum { B_FALSE, B_TRUE } boolean_t;


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

#define KM_SLEEP   0x0000
#define KM_NOSLEEP 0x0001		/* needed by kmem_alloc_node(), kmem_zalloc()
					 * calls */
#define VM_NOSLEEP 0x0001		/* needed kmem_alloc_node(), kmem_zalloc_node
					 * calls */
#define XG_WIDGET_PART_NUM      0xC102          /* KONA/xt_regs.h     XG_XT_PART_NUM_VALUE */

typedef uint64_t vhandl_t;


#define NBPP PAGE_SIZE
#define _PAGESZ PAGE_SIZE

#ifndef MAXDEVNAME
#define MAXDEVNAME 256
#endif

#define HUB_PIO_CONVEYOR 0x1
#define CNODEID_NONE ((cnodeid_t)-1)
#define XTALK_PCI_PART_NUM "030-1275-"
#define kdebug 0


#define COPYIN(a, b, c)		copy_from_user(b,a,c)
#define COPYOUT(a, b, c)	copy_to_user(b,a,c)

#define BZERO(a,b)		memset(a, 0, b)

#define kern_malloc(x)		kmalloc(x, GFP_KERNEL)
#define kern_free(x)		kfree(x)

typedef cpuid_t cpu_cookie_t;
#define CPU_NONE		(-1)

/*
 * mutext support mapping
 */

#define mutex_spinlock_init(s)	spin_lock_init(s)
inline static unsigned long
mutex_spinlock(spinlock_t *sem) {
	unsigned long flags = 0;
//	spin_lock_irqsave(sem, flags);
	spin_lock(sem);
	return(flags);
}
// #define mutex_spinunlock(s,t)	spin_unlock_irqrestore(s,t)
#define mutex_spinunlock(s,t)	spin_unlock(s)


#define mutex_t			struct semaphore
#define mutex_init(s)		init_MUTEX(s)
#define mutex_init_locked(s)	init_MUTEX_LOCKED(s)
#define mutex_lock(s)		down(s)
#define mutex_unlock(s)		up(s)

#define io_splock(s)		mutex_spinlock(s)
#define io_spunlock(s,t)	spin_unlock(s)

#define spin_lock_destroy(s)

#if defined(DISABLE_ASSERT)
#define ASSERT(expr)
#define ASSERT_ALWAYS(expr)
#else
#define ASSERT(expr)  do {	\
        if(!(expr)) { \
		printk( "Assertion [%s] failed! %s:%s(line=%d)\n",\
			#expr,__FILE__,__FUNCTION__,__LINE__); \
		panic("Assertion panic\n"); 	\
        } } while(0)

#define ASSERT_ALWAYS(expr)	do {\
        if(!(expr)) { \
		printk( "Assertion [%s] failed! %s:%s(line=%d)\n",\
			#expr,__FILE__,__FUNCTION__,__LINE__); \
		panic("Assertion always panic\n"); 	\
        } } while(0)
#endif	/* DISABLE_ASSERT */

#define PRINT_PANIC		panic

/* print_register() defs */

/*
 * register values
 * map between numeric values and symbolic values
 */
struct reg_values {
	unsigned long long rv_value;
	char *rv_name;
};

/*
 * register descriptors are used for formatted prints of register values
 * rd_mask and rd_shift must be defined, other entries may be null
 */
struct reg_desc {
	unsigned long long rd_mask;	/* mask to extract field */
	int rd_shift;		/* shift for extracted value, - >>, + << */
	char *rd_name;		/* field name */
	char *rd_format;	/* format to print field */
	struct reg_values *rd_values;	/* symbolic names of values */
};

extern void print_register(unsigned long long, struct reg_desc *);

/******************************************
 * Definitions that do not exist in linux *
 ******************************************/

#define DELAY(a)

/************************************************
 * Routines redefined to use linux equivalents. *
 ************************************************/

/* #define FIXME(s) printk("FIXME: [ %s ] in %s at %s:%d\n", s, __FUNCTION__, __FILE__, __LINE__) */

#define FIXME(s)

/* move to stubs.c yet */
#define dev_to_vhdl(dev) 0
#define get_timestamp() 0
#define us_delay(a)
#define v_mapphys(a,b,c) 0    // printk("Fixme: v_mapphys - soft->base 0x%p\n", b);
#define splhi()  0
#define splx(s)

extern void * snia_kmem_alloc_node(register size_t, register int, cnodeid_t);
extern void * snia_kmem_zalloc(size_t, int);
extern void * snia_kmem_zalloc_node(register size_t, register int, cnodeid_t );
extern int is_specified(char *);

#endif /* _ASM_IA64_SN_SGI_H */
