/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/ioerror_handling.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/slotnum.h>
#include <asm/sn/vector.h>

/******
 ****** hack defines ......
 ******/

int pcibr_prefetch_enable_rev, pcibr_wg_enable_rev;
int default_intr_pri;
int force_fire_and_forget;
int ignore_conveyor_override;

#define spinlock_init(x,name) mutex_init(x, MUTEX_DEFAULT, name);

devfs_handle_t dummy_vrtx;	/* Needed for cpuid_to_vertex() in hack.h */


/* ARGSUSED */
void hub_widgetdev_enable(devfs_handle_t xconn_vhdl, int devnum)
        {FIXME("hub_widgetdev_enable");}

/* ARGSUSED */
void hub_widgetdev_shutdown(devfs_handle_t xconn_vhdl, int devnum)
        {FIXME("hub_widgetdev_shutdown");}

/* ARGSUSED */
void hub_widget_reset(devfs_handle_t hubv, xwidgetnum_t widget)
        {FIXME("hub_widget_reset");}

boolean_t
is_sys_critical_vertex(devfs_handle_t x)
{
	FIXME("is_sys_critical_vertex : returns 0");
	return(0);
}

char *
nic_bridge_vertex_info(devfs_handle_t v, nic_data_t mcr)
{
	FIXME("nic_bridge_vertex_info : returns NULL");
	return((char *)0);
}

void *
kmem_alloc_node(register size_t size, register int flags, cnodeid_t node)
{
        /* Allocates on node 'node' */
	FIXME("kmem_alloc_node : use kmalloc");
	return(kmalloc(size, GFP_KERNEL));
}

void *
kmem_zalloc_node(register size_t size, register int flags, cnodeid_t node)
{
	FIXME("kmem_zalloc_node : use kmalloc");
	return(kmalloc(size, GFP_KERNEL));
}

void
kmem_free(void *where, int size)
{
	FIXME("kmem_free : use kfree");
	return(kfree(where));
}


void *
kmem_zone_alloc(register zone_t *zone, int flags)
{
	FIXME("kmem_zone_alloc : return null");
	return((void *)0);
}

void
kmem_zone_free(register zone_t *zone, void *ptr)
{
	FIXME("kmem_zone_free : no-op");
}

zone_t *
kmem_zone_init(register int size, char *zone_name)
{
	FIXME("kmem_zone_free : returns NULL");
	return((zone_t *)0);
}

uint64_t
rmalloc(struct map *mp, size_t size)
{
	FIXME("rmalloc : returns NULL");
	return((uint64_t)0);
}

void
rmfree(struct map *mp, size_t size, uint64_t a)
{
	FIXME("rmfree : no-op");
}

struct map *
rmallocmap(uint64_t mapsiz)
{
	FIXME("rmallocmap : returns NULL");
	return((struct map *)0);
}

void
rmfreemap(struct map *mp)
{
	FIXME("rmfreemap : no-op");
}

int
compare_and_swap_ptr(void **location, void *old_ptr, void *new_ptr)
{
	FIXME("compare_and_swap_ptr : NOT ATOMIC");
	if (*location == old_ptr) {
		*location = new_ptr;
		return(1);
	}
	else
		return(0);
}

void *
swap_ptr(void **loc, void *new)
{
	FIXME("swap_ptr : returns null");
	return((void *)0);
}

/* For ml/SN/SN1/slots.c */
/* ARGSUSED */
slotid_t get_widget_slotnum(int xbow, int widget)
        {FIXME("get_widget_slotnum"); return (unsigned char)NULL;}

/* For router */
int
router_init(cnodeid_t cnode,int writeid, void *npda_rip)
        {FIXME("router_init"); return(0);}

/* From io/ioerror_handling.c */
error_return_code_t
sys_critical_graph_vertex_add(devfs_handle_t parent, devfs_handle_t child)
	{FIXME("sys_critical_graph_vertex_add"); return(0);}

/* From io/ioc3.c */
devfs_handle_t
ioc3_console_vhdl_get(void)
	{FIXME("ioc3_console_vhdl_get"); return( (devfs_handle_t)-1);}


#if 0
#define io_splock(l) 1
#define io_spunlock(l,s)

#define spinlock_destroy(a)     /* needed by pcibr_detach() */
#define mutex_spinlock(a) 0
#define mutex_spinunlock(a,b)
#define mutex_init(a,b,c)               ;
#define mutex_lock(a,b)                 ;
#define mutex_unlock(a)                 ;
#define dev_to_vhdl(dev) 0
#define get_timestamp() 0
#define us_delay(a)
#define v_mapphys(a,b,c) 0
#define splhi()  0
#define splx(s)
#define spinlock_init(x,name) mutex_init(x, MUTEX_DEFAULT, name);
#endif /* 0 */

int
cap_able(uint64_t x)
{
	FIXME("cap_able : returns 1");
	return(1);
}

int
cap_able_cred(uint64_t a, uint64_t b)
{
	FIXME("cap_able_cred : returns 1");
	return(1);
}

void
nic_vmc_check(devfs_handle_t vhdl, char *nicinfo)
{

	FIXME("nic_vmc_check\n");

}

char *
nic_vertex_info_get(devfs_handle_t v)
{

	FIXME("nic_vertex_info_get\n");
	return(NULL);

}

int
vector_read_node(net_vec_t dest, nasid_t nasid,
             int write_id, int address,
             uint64_t *value)
{
	FIXME("vector_read_node\n");
	return(0);
}

int
vector_write_node(net_vec_t dest, nasid_t nasid,
              int write_id, int address,
              uint64_t value)
{
	FIXME("vector_write_node\n");
	return(0);
}

int
atomicAddInt(int *int_ptr, int value)
{
//	FIXME("atomicAddInt : simple add\n");
	*int_ptr += value;
	return(0);
}

int
atomicClearInt(int *int_ptr, int value)
{
	FIXME("atomicClearInt : simple clear\n");
	*int_ptr &= ~value;
	return(0);
}
