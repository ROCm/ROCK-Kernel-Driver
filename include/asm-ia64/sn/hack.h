/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */


#ifndef _ASM_SN_HACK_H
#define _ASM_SN_HACK_H

#include <asm/sn/types.h>
#include <asm/uaccess.h>		/* for copy_??_user */

/******************************************
 * Definitions that do not exist in linux *
 ******************************************/

typedef int cred_t;	/* This is for compilation reasons */
struct cred { int x; };

/*
 * Hardware Graph routines that are currently stubbed!
 */
#include <linux/devfs_fs_kernel.h>

#define DELAY(a)
#define cpuid() 0

/************************************************
 * Routines redefined to use linux equivalents. *
 ************************************************/

#define FIXME(s) printk("FIXME: [ %s ] in %s at %s:%d\n", s, __FUNCTION__, __FILE__, __LINE__)

#define sv_init(a,b,c)          FIXME("Fixme: sv_init : no-op")
#define sv_wait(a,b,c,d)        FIXME("Fixme: sv_wait : no-op")
#define sv_broadcast(a)  	FIXME("Fixme: sv_broadcast : no-op")
#define sv_destroy(a)		FIXME("Fixme: sv_destroy : no-op")

extern devfs_handle_t dummy_vrtx;
#define cpuid_to_vertex(cpuid) dummy_vrtx /* (pdaindr[cpuid].pda->p_vertex) */

#define PUTBUF_LOCK(a) { FIXME("PUTBUF_LOCK"); }
#define PUTBUF_UNLOCK(a) { FIXME("PUTBUF_UNLOCK"); }
static inline int sv_signal(sv_t *a) {FIXME("sv_signal : return 0"); return (0); }

#define cmn_err(x,y...)         { FIXME("cmn_err : use printk"); printk(x y); }

typedef int (*splfunc_t)(void);
extern int badaddr_val(volatile void *, int , volatile void *);

extern int cap_able_cred(uint64_t a, uint64_t b);

#define _CAP_CRABLE(cr,c)	(cap_able_cred(cr,c))
#define CAP_MEMORY_MGT          (0x01LL << 25)
#define CAP_DEVICE_MGT          (0x01LL << 37)

#define io_splock(l) l
#define io_spunlock(l,s)

/* move to stubs.c yet */
#define spinlock_destroy(a)     /* needed by pcibr_detach() */
#define mutex_spinlock(a) 0
#define mutex_spinunlock(a,b)
#define mutex_spinlock_spl(x,y) y
#define mutex_init(a,b,c)               ;
#define mutex_lock(a,b)                 ;
#define mutex_unlock(a)                 ;
#define dev_to_vhdl(dev) 0
#define get_timestamp() 0
#define us_delay(a)
#define v_mapphys(a,b,c) printk("Fixme: v_mapphys - soft->base 0x%p\n", b);
#define splhi()  0
#define spl7	splhi()
#define splx(s)
#define spinlock_init(x,name) mutex_init(x, MUTEX_DEFAULT, name);

extern void * kmem_alloc_node(register size_t, register int, cnodeid_t);
extern void * kmem_zalloc(size_t, int);
extern void * kmem_zalloc_node(register size_t, register int, cnodeid_t );
extern void * kmem_zone_alloc(register zone_t *, int);
extern zone_t * kmem_zone_init(register int , char *);
extern void kmem_zone_free(register zone_t *, void *);
extern int is_specified(char *);
extern int cap_able(uint64_t);
extern int compare_and_swap_ptr(void **, void *, void *);

#endif	/* _ASM_SN_HACK_H */
