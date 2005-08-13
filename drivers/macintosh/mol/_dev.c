/* 
 *   Creation Date: <2003/08/20 17:31:44 samuel>
 *   Time-stamp: <2004/01/14 21:44:17 samuel>
 *   
 *	<dev.c>
 *	
 *	misc device
 *   
 *   Copyright (C) 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *   
 */

#include "archinclude.h"
#include <linux/config.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <asm/atomic.h>
#include <asm/prom.h>
#include "kernel_vars.h"
#include "mol-ioctl.h"
#include "version.h"
#include "mmu.h"
#include "misc.h"
#include "mtable.h"

MODULE_AUTHOR("Samuel Rydh <samuel@ibrium.se>");
MODULE_DESCRIPTION("Mac-on-Linux kernel module");
MODULE_LICENSE("GPL");

static DECLARE_MUTEX( initmutex );
static int opencnt;


/************************************************************************/
/*	misc								*/
/************************************************************************/
		
#ifdef CONFIG_SMP
#define HAS_SMP		1

static void
dummy_ipi( void *dummy )
{
	/* we don't need to _do_ anything, the exception itself is sufficient */
}
static inline void
send_ipi( void )
{
	smp_call_function( dummy_ipi, NULL, 1, 0 );
}
#else /* CONFIG_SMP */

#define HAS_SMP		0
#define send_ipi()	do {} while(0)

#endif /* CONFIG_SMP */


static int
find_physical_rom( int *base, int *size )
{
	struct device_node *dn;
	int len, *p;
	
	if( !(dn=find_devices("boot-rom")) && !(dn=find_type_devices("rom")) )
		return 0;
	do {
		if( !(p=(int*)get_property(dn, "reg", &len)) || len != sizeof(int[2]) )
			return 0;
		if( (unsigned int)(0xfff00100 - p[0]) < (unsigned int)p[1] ) {
			*base = p[0];
			*size = p[1];
			return 1;
		}
		dn = dn->next;
	} while( dn );

	return 0;
}

static int
get_info( mol_kmod_info_t *user_retinfo, int size ) 
{
	mol_kmod_info_t info;

	memset( &info, 0, sizeof(info) );
	asm volatile("mfpvr %0" : "=r" (info.pvr) : );
	info.version = MOL_VERSION;
	find_physical_rom( &info.rombase, &info.romsize );
	info.tb_freq = HZ * tb_ticks_per_jiffy;
	info.smp_kernel = HAS_SMP;

	if( (uint)size > sizeof(info) )
		size = sizeof(info);
	
	if( copy_to_user(user_retinfo, &info, size) )
		return -EFAULT;
	return 0;
}


void
prevent_mod_unload( void )
{
#ifndef LINUX_26
	MOD_INC_USE_COUNT;
#else
	__module_get( THIS_MODULE );
#endif
}


/************************************************************************/
/*	ioctl 								*/
/************************************************************************/

static int
debugger_op( kernel_vars_t *kv, dbg_op_params_t *upb )
{
	dbg_op_params_t pb;
	int ret;

	if( copy_from_user(&pb, upb, sizeof(pb)) )
		return -EFAULT;

	switch( pb.operation ) {
	case DBG_OP_GET_PHYS_PAGE:
		ret = dbg_get_linux_page( pb.ea, &pb.ret.page );
		break;
	default:
		ret = do_debugger_op( kv, &pb );
		break;
	}

	if( copy_to_user(upb, &pb, sizeof(pb)) )
		return -EFAULT;
	return ret;
}

static int
arch_handle_ioctl( kernel_vars_t *kv, int cmd, int p1, int p2, int p3 )
{
	struct mmu_mapping map;	
	perf_ctr_t pctr;
	char *rompage;
	int ret = -EFAULT;

	switch( cmd ) {
	case MOL_IOCTL_GET_DIRTY_FBLINES:  /* short *retbuf, int size -- npairs */
		if( verify_area(VERIFY_WRITE, (short*)p1, p2) )
			break;
		ret = get_dirty_fb_lines( kv, (short*)p1, p2 );
		break;

	case MOL_IOCTL_DEBUGGER_OP:
		ret = debugger_op( kv, (dbg_op_params_t*)p1 );
		break;
		
	case MOL_IOCTL_GET_PERF_INFO:
		ret = get_performance_info( kv, p1, &pctr );
		if( copy_to_user((perf_ctr_t*)p2, &pctr, sizeof(pctr)) )
			ret = -EFAULT;
		break;

	case MOL_IOCTL_MMU_MAP: /* p1 = struct mmu_mapping *m, p2 = map/unmap */
		if( copy_from_user(&map, (struct mmu_mapping*)p1, sizeof(map)) )
			break;
		if( p2 )
			mmu_add_map( kv, &map );
		else 
			mmu_remove_map( kv, &map );
		if( copy_to_user((struct mmu_mapping*)p1, &map, sizeof(map)) )
			ret = -EFAULT;
		break;

	case MOL_IOCTL_COPY_LAST_ROMPAGE: /* p1 = dest */
		ret = -ENODEV;
		if( (rompage=ioremap(0xfffff000, 0x1000)) ) {
			ret = copy_to_user( (char*)p1, rompage, 0x1000 );
			iounmap( rompage );
		}
		break;

	case MOL_IOCTL_SET_RAM: /* void ( char *lvbase, size_t size ) */
		if( verify_area(VERIFY_WRITE, (char*)p1, p2) )
			break;
		kv->mmu.linux_ram_base = (char*)p1;
		kv->mmu.ram_size = p2;
		kv->mmu.mac_ram_base = 0;
		mtable_tune_alloc_limit( kv, p2/(1024 * 1024) );
		break;

	case MOL_IOCTL_GET_MREGS_PHYS:
		ret = virt_to_phys( &kv->mregs );
		break;

	default:
		ret = handle_ioctl( kv, cmd, p1, p2, p3 );
		break;
	}
	return ret;
}


/************************************************************************/
/*	device interface						*/
/************************************************************************/

static int
mol_open( struct inode *inode, struct file *file )
{
	int ret=0;

	if( !(file->f_mode & FMODE_READ) )
		return -EPERM;
	
	down( &initmutex );
	if( !opencnt++ ) {
		if( common_init() )
			ret = -ENOMEM;
	}
	up( &initmutex );

	file->private_data = NULL;
	return ret;
}

static int
mol_release( struct inode *inode, struct file *file )
{
	kernel_vars_t *kv = (kernel_vars_t*)file->private_data;
	
	if( kv )
		destroy_session( kv->session_index );

	down( &initmutex );
	if( !--opencnt )
		common_cleanup();
	up( &initmutex );
	return 0;
}

static int
mol_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg )
{
	mol_ioctl_pb_t pb;
	kernel_vars_t *kv;
	int ret;

	/* fast path */
	if( cmd == MOL_IOCTL_SMP_SEND_IPI ) {
		send_ipi();
		return 0;
	}

	if( copy_from_user(&pb, (void*)arg, sizeof(pb)) )
		return -EFAULT;

	switch( cmd ) {
	case MOL_IOCTL_GET_INFO:
		return get_info( (mol_kmod_info_t*)pb.arg1, pb.arg2 );

	case MOL_IOCTL_CREATE_SESSION:
		if( !(file->f_mode & FMODE_WRITE) || !capable(CAP_SYS_ADMIN) )
			return -EPERM;
		ret = -EINVAL;
		down( &initmutex );
		if( (uint)pb.arg1 < MAX_NUM_SESSIONS && !file->private_data ) {
			if( !(ret=initialize_session(pb.arg1)) ) {
				kv = g_sesstab->kvars[pb.arg1];
				init_MUTEX( &kv->ioctl_sem );
				file->private_data = kv;
			}
		}
		up( &initmutex );
		return ret;

	case MOL_IOCTL_DBG_GET_KVARS_PHYS:
		ret = ((uint)pb.arg1 < MAX_NUM_SESSIONS) ? 
			virt_to_phys(g_sesstab->kvars[pb.arg1]) : 0;
		return ret;
	}

	if( !(kv=(kernel_vars_t*)file->private_data) )
		return -EINVAL;

	down( &kv->ioctl_sem );
	ret = arch_handle_ioctl( kv, cmd, pb.arg1, pb.arg2, pb.arg3 );
	up( &kv->ioctl_sem );

	return ret;
}

static struct file_operations mol_device_fops = {
	.owner		= THIS_MODULE,
	.open		= mol_open,
	.release	= mol_release,
	.ioctl		= mol_ioctl,
//	.poll		= mol_poll,
//	.mmap:		= mol_mmap,
};

static struct miscdevice mol_device = {
	MISC_DYNAMIC_MINOR, "mol", &mol_device_fops
};

static int __init
dev_register( void )
{
	printk("MOL %s kernel module loaded\n", MOL_RELEASE );
	return misc_register( &mol_device );
}

static void __exit
dev_unregister( void )
{
	misc_deregister( &mol_device );
}

module_init( dev_register );
module_exit( dev_unregister );
