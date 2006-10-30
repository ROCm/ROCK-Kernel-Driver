/* 
 *   Creation Date: <2003/08/20 17:31:44 samuel>
 *   Time-stamp: <2004/02/14 14:43:13 samuel>
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
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/atomic.h>
#include "kernel_vars.h"
#include "mol-ioctl.h"
#include "version.h"
#include "mmu.h"
#include "misc.h"
#include "mtable.h"
#include "atomic.h"

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

int
get_irqs( kernel_vars_t *kv, irq_bitfield_t *irq_info_p )
{
	irq_bitfield_t irq_mask;
	int i;

	/* copy the interrupt mask from userspace */
	if (copy_from_user(&irq_mask, irq_info_p, sizeof(irq_mask)))
		return -EFAULT;

	/* see which of the mapped interrupts need to be enabled */
	for (i = 0; i < NR_HOST_IRQS; i++) {
		if (check_bit_mol(i, (char *) kv->mregs.mapped_irqs.irqs)
				&& check_bit_mol(i, (char *) irq_mask.irqs)
				&& check_bit_mol(i, (char *) kv->mregs.active_irqs.irqs)) {
			if (test_and_clear_bit(i, kv->mregs.active_irqs.irqs))
				atomic_dec_mol((mol_atomic_t *) &(kv->mregs.hostirq_active_cnt));
			enable_irq(i);
		}
	}

	/* if one of the enabled interrupts was pending, it should have fired
	 * now, updating active_irqs */
	if (copy_to_user(irq_info_p, &(kv->mregs.active_irqs), sizeof(kv->mregs.active_irqs)))
		return -EFAULT;

	return 0;
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
	char *rompage;
	int ret = -EFAULT;

	switch( cmd ) {
	case MOL_IOCTL_GET_IRQS:
		return get_irqs( kv, (irq_bitfield_t *) p1 );

	case MOL_IOCTL_GET_DIRTY_FBLINES:  /* short *retbuf, int size -- npairs */
		if( compat_verify_area(VERIFY_WRITE, (short*)p1, p2) )
			break;
		ret = get_dirty_fb_lines( kv, (short*)p1, p2 );
		break;

	case MOL_IOCTL_DEBUGGER_OP:
		ret = debugger_op( kv, (dbg_op_params_t*)p1 );
		break;
		
	case MOL_IOCTL_GRAB_IRQ:
		ret = grab_host_irq(kv, p1);
		break;

	case MOL_IOCTL_RELEASE_IRQ:
		ret = release_host_irq(kv, p1);
		break;

	case MOL_IOCTL_COPY_LAST_ROMPAGE: /* p1 = dest */
		ret = -ENODEV;
		if( (rompage=ioremap(0xfffff000, 0x1000)) ) {
			ret = copy_to_user( (char*)p1, rompage, 0x1000 );
			iounmap( rompage );
		}
		break;

	case MOL_IOCTL_SET_RAM: /* void ( char *lvbase, size_t size ) */
		if( compat_verify_area(VERIFY_WRITE, (char*)p1, p2) )
			break;
		ret = 0;
		kv->mmu.userspace_ram_base = p1;
		kv->mmu.ram_size = p2;
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
		if( common_init() ) {
			ret = -ENOMEM;
			opencnt = 0;
		}
	}
	up( &initmutex );

	file->private_data = NULL;
	return ret;
}

static int
mol_release( struct inode *inode, struct file *file )
{
	kernel_vars_t *kv = (kernel_vars_t*)file->private_data;
	
	down( &initmutex );
	if( kv )
		destroy_session( kv->session_index );

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
	uint session;
	
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

	case MOL_IOCTL_DBG_COPY_KVARS:
		session = pb.arg1;
		ret = -EINVAL;
		down( &initmutex );
		if( session < MAX_NUM_SESSIONS && (kv=g_sesstab->kvars[session]) )
			ret = copy_to_user( (char*)pb.arg2, kv, sizeof(*kv) );
		up( &initmutex );
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
