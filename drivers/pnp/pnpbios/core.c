/*
 * pnpbios -- PnP BIOS driver
 *
 * This driver provides access to Plug-'n'-Play services provided by
 * the PnP BIOS firmware, described in the following documents:
 *   Plug and Play BIOS Specification, Version 1.0A, 5 May 1994
 *   Plug and Play BIOS Clarification Paper, 6 October 1994
 *     Compaq Computer Corporation, Phoenix Technologies Ltd., Intel Corp.
 * 
 * Originally (C) 1998 Christian Schmidt <schmidt@digadd.de>
 * Modifications (C) 1998 Tom Lees <tom@lpsg.demon.co.uk>
 * Minor reorganizations by David Hinds <dahinds@users.sourceforge.net>
 * Further modifications (C) 2001, 2002 by:
 *   Alan Cox <alan@redhat.com>
 *   Thomas Hood <jdthood@mail.com>
 *   Brian Gerst <bgerst@didntduck.org>
 *
 * Ported to the PnP Layer and several additional improvements (C) 2002
 * by Adam Belay <ambx1@neo.rr.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
/* Change Log
 *
 * Adam Belay - <ambx1@neo.rr.com> - March 16, 2003
 * rev 1.01	Only call pnp_bios_dev_node_info once
 *		Added pnpbios_print_status
 *		Added several new error messages and info messages
 *		Added pnpbios_interface_attach_device
 *		integrated core and proc init system
 *		Introduced PNPMODE flags
 *		Removed some useless includes
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/pnpbios.h>
#include <linux/device.h>
#include <linux/pnp.h>
#include <asm/page.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <asm/desc.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/byteorder.h>


/*
 *
 * PnP BIOS INTERFACE
 *
 */

/* PnP BIOS signature: "$PnP" */
#define PNP_SIGNATURE   (('$' << 0) + ('P' << 8) + ('n' << 16) + ('P' << 24))

#pragma pack(1)
union pnp_bios_expansion_header {
	struct {
		u32 signature;    /* "$PnP" */
		u8 version;	  /* in BCD */
		u8 length;	  /* length in bytes, currently 21h */
		u16 control;	  /* system capabilities */
		u8 checksum;	  /* all bytes must add up to 0 */

		u32 eventflag;    /* phys. address of the event flag */
		u16 rmoffset;     /* real mode entry point */
		u16 rmcseg;
		u16 pm16offset;   /* 16 bit protected mode entry */
		u32 pm16cseg;
		u32 deviceID;	  /* EISA encoded system ID or 0 */
		u16 rmdseg;	  /* real mode data segment */
		u32 pm16dseg;	  /* 16 bit pm data segment base */
	} fields;
	char chars[0x21];	  /* To calculate the checksum */
};
#pragma pack()

static struct {
	u16	offset;
	u16	segment;
} pnp_bios_callpoint;

static union pnp_bios_expansion_header * pnp_bios_hdr = NULL;
struct pnp_dev_node_info node_info;

/* The PnP BIOS entries in the GDT */
#define PNP_GDT    (GDT_ENTRY_PNPBIOS_BASE * 8)

#define PNP_CS32   (PNP_GDT+0x00)	/* segment for calling fn */
#define PNP_CS16   (PNP_GDT+0x08)	/* code segment for BIOS */
#define PNP_DS     (PNP_GDT+0x10)	/* data segment for BIOS */
#define PNP_TS1    (PNP_GDT+0x18)	/* transfer data segment */
#define PNP_TS2    (PNP_GDT+0x20)	/* another data segment */

/* 
 * These are some opcodes for a "static asmlinkage"
 * As this code is *not* executed inside the linux kernel segment, but in a
 * alias at offset 0, we need a far return that can not be compiled by
 * default (please, prove me wrong! this is *really* ugly!) 
 * This is the only way to get the bios to return into the kernel code,
 * because the bios code runs in 16 bit protected mode and therefore can only
 * return to the caller if the call is within the first 64kB, and the linux
 * kernel begins at offset 3GB...
 */

asmlinkage void pnp_bios_callfunc(void);

__asm__(
	".text			\n"
	__ALIGN_STR "\n"
	"pnp_bios_callfunc:\n"
	"	pushl %edx	\n"
	"	pushl %ecx	\n"
	"	pushl %ebx	\n"
	"	pushl %eax	\n"
	"	lcallw *pnp_bios_callpoint\n"
	"	addl $16, %esp	\n"
	"	lret		\n"
	".previous		\n"
);

#define Q_SET_SEL(cpu, selname, address, size) \
do { \
set_base(cpu_gdt_table[cpu][(selname) >> 3], __va((u32)(address))); \
set_limit(cpu_gdt_table[cpu][(selname) >> 3], size); \
} while(0)

#define Q2_SET_SEL(cpu, selname, address, size) \
do { \
set_base(cpu_gdt_table[cpu][(selname) >> 3], (u32)(address)); \
set_limit(cpu_gdt_table[cpu][(selname) >> 3], size); \
} while(0)

static struct desc_struct bad_bios_desc = { 0, 0x00409200 };

/*
 * At some point we want to use this stack frame pointer to unwind
 * after PnP BIOS oopses.
 */

u32 pnp_bios_fault_esp;
u32 pnp_bios_fault_eip;
u32 pnp_bios_is_utter_crap = 0;

static spinlock_t pnp_bios_lock;

static inline u16 call_pnp_bios(u16 func, u16 arg1, u16 arg2, u16 arg3,
				u16 arg4, u16 arg5, u16 arg6, u16 arg7,
				void *ts1_base, u32 ts1_size,
				void *ts2_base, u32 ts2_size)
{
	unsigned long flags;
	u16 status;
	struct desc_struct save_desc_40;
	int cpu;

	/*
	 * PnP BIOSes are generally not terribly re-entrant.
	 * Also, don't rely on them to save everything correctly.
	 */
	if(pnp_bios_is_utter_crap)
		return PNP_FUNCTION_NOT_SUPPORTED;

	cpu = get_cpu();
	save_desc_40 = cpu_gdt_table[cpu][0x40 / 8];
	cpu_gdt_table[cpu][0x40 / 8] = bad_bios_desc;

	/* On some boxes IRQ's during PnP BIOS calls are deadly.  */
	spin_lock_irqsave(&pnp_bios_lock, flags);

	/* The lock prevents us bouncing CPU here */
	if (ts1_size)
		Q2_SET_SEL(smp_processor_id(), PNP_TS1, ts1_base, ts1_size);
	if (ts2_size)
		Q2_SET_SEL(smp_processor_id(), PNP_TS2, ts2_base, ts2_size);

	__asm__ __volatile__(
	        "pushl %%ebp\n\t"
		"pushl %%edi\n\t"
		"pushl %%esi\n\t"
		"pushl %%ds\n\t"
		"pushl %%es\n\t"
		"pushl %%fs\n\t"
		"pushl %%gs\n\t"
		"pushfl\n\t"
		"movl %%esp, pnp_bios_fault_esp\n\t"
		"movl $1f, pnp_bios_fault_eip\n\t"
		"lcall %5,%6\n\t"
		"1:popfl\n\t"
		"popl %%gs\n\t"
		"popl %%fs\n\t"
		"popl %%es\n\t"
		"popl %%ds\n\t"
	        "popl %%esi\n\t"
		"popl %%edi\n\t"
		"popl %%ebp\n\t"
		: "=a" (status)
		: "0" ((func) | (((u32)arg1) << 16)),
		  "b" ((arg2) | (((u32)arg3) << 16)),
		  "c" ((arg4) | (((u32)arg5) << 16)),
		  "d" ((arg6) | (((u32)arg7) << 16)),
		  "i" (PNP_CS32),
		  "i" (0)
		: "memory"
	);
	spin_unlock_irqrestore(&pnp_bios_lock, flags);

	cpu_gdt_table[cpu][0x40 / 8] = save_desc_40;
	put_cpu();
	
	/* If we get here and this is set then the PnP BIOS faulted on us. */
	if(pnp_bios_is_utter_crap)
	{
		printk(KERN_ERR "PnPBIOS: Warning! Your PnP BIOS caused a fatal error. Attempting to continue\n");
		printk(KERN_ERR "PnPBIOS: You may need to reboot with the \"nobiospnp\" option to operate stably\n");
		printk(KERN_ERR "PnPBIOS: Check with your vendor for an updated BIOS\n");
	}

	return status;
}


/*
 *
 * UTILITY FUNCTIONS
 *
 */

static void pnpbios_print_status(const char * module, u16 status)
{
	switch(status) {
	case PNP_SUCCESS:
	printk(KERN_ERR "PnPBIOS: %s: function successful\n", module);
	case PNP_NOT_SET_STATICALLY:
	printk(KERN_ERR "PnPBIOS: %s: unable to set static resources\n", module);
	case PNP_UNKNOWN_FUNCTION:
	printk(KERN_ERR "PnPBIOS: %s: invalid function number passed\n", module);
	case PNP_FUNCTION_NOT_SUPPORTED:
	printk(KERN_ERR "PnPBIOS: %s: function not supported on this system\n", module);
	case PNP_INVALID_HANDLE:
	printk(KERN_ERR "PnPBIOS: %s: invalid handle\n", module);
	case PNP_BAD_PARAMETER:
	printk(KERN_ERR "PnPBIOS: %s: invalid parameters were passed\n", module);
	case PNP_SET_FAILED:
	printk(KERN_ERR "PnPBIOS: %s: unable to set resources\n", module);
	case PNP_EVENTS_NOT_PENDING:
	printk(KERN_ERR "PnPBIOS: %s: no events are pending\n", module);
	case PNP_SYSTEM_NOT_DOCKED:
	printk(KERN_ERR "PnPBIOS: %s: the system is not docked\n", module);
	case PNP_NO_ISA_PNP_CARDS:
	printk(KERN_ERR "PnPBIOS: %s: no isapnp cards are installed on this system\n", module);
	case PNP_UNABLE_TO_DETERMINE_DOCK_CAPABILITIES:
	printk(KERN_ERR "PnPBIOS: %s: cannot determine the capabilities of the docking station\n", module);
	case PNP_CONFIG_CHANGE_FAILED_NO_BATTERY:
	printk(KERN_ERR "PnPBIOS: %s: unable to undock, the system does not have a battery\n", module);
	case PNP_CONFIG_CHANGE_FAILED_RESOURCE_CONFLICT:
	printk(KERN_ERR "PnPBIOS: %s: could not dock due to resource conflicts\n", module);
	case PNP_BUFFER_TOO_SMALL:
	printk(KERN_ERR "PnPBIOS: %s: the buffer passed is too small\n", module);
	case PNP_USE_ESCD_SUPPORT:
	printk(KERN_ERR "PnPBIOS: %s: use ESCD instead\n", module);
	case PNP_MESSAGE_NOT_SUPPORTED:
	printk(KERN_ERR "PnPBIOS: %s: the message is unsupported\n", module);
	case PNP_HARDWARE_ERROR:
	printk(KERN_ERR "PnPBIOS: %s: a hardware failure has occured\n", module);
	default:
	printk(KERN_ERR "PnPBIOS: %s: unexpected status 0x%x\n", module, status);
	}
}

void *pnpbios_kmalloc(size_t size, int f)
{
	void *p = kmalloc( size, f );
	if ( p == NULL )
		printk(KERN_ERR "PnPBIOS: kmalloc() failed\n");
	else
		memset(p, 0, size);
	return p;
}

/*
 * Call this only after init time
 */
static int pnp_bios_present(void)
{
	return (pnp_bios_hdr != NULL);
}


/*
 *
 * PnP BIOS ACCESS FUNCTIONS
 *
 */

#define PNP_GET_NUM_SYS_DEV_NODES       0x00
#define PNP_GET_SYS_DEV_NODE            0x01
#define PNP_SET_SYS_DEV_NODE            0x02
#define PNP_GET_EVENT                   0x03
#define PNP_SEND_MESSAGE                0x04
#define PNP_GET_DOCKING_STATION_INFORMATION 0x05
#define PNP_SET_STATIC_ALLOCED_RES_INFO 0x09
#define PNP_GET_STATIC_ALLOCED_RES_INFO 0x0a
#define PNP_GET_APM_ID_TABLE            0x0b
#define PNP_GET_PNP_ISA_CONFIG_STRUC    0x40
#define PNP_GET_ESCD_INFO               0x41
#define PNP_READ_ESCD                   0x42
#define PNP_WRITE_ESCD                  0x43

/*
 * Call PnP BIOS with function 0x00, "get number of system device nodes"
 */
static int __pnp_bios_dev_node_info(struct pnp_dev_node_info *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_NUM_SYS_DEV_NODES, 0, PNP_TS1, 2, PNP_TS1, PNP_DS, 0, 0,
			       data, sizeof(struct pnp_dev_node_info), 0, 0);
	data->no_nodes &= 0xff;
	return status;
}

int pnp_bios_dev_node_info(struct pnp_dev_node_info *data)
{
	int status = __pnp_bios_dev_node_info( data );
	if ( status )
		pnpbios_print_status( "dev_node_info", status );
	return status;
}

/*
 * Note that some PnP BIOSes (e.g., on Sony Vaio laptops) die a horrible
 * death if they are asked to access the "current" configuration.
 * Therefore, if it's a matter of indifference, it's better to call
 * get_dev_node() and set_dev_node() with boot=1 rather than with boot=0.
 */

/* 
 * Call PnP BIOS with function 0x01, "get system device node"
 * Input: *nodenum = desired node, 
 *        boot = whether to get nonvolatile boot (!=0)
 *               or volatile current (0) config
 * Output: *nodenum=next node or 0xff if no more nodes
 */
static int __pnp_bios_get_dev_node(u8 *nodenum, char boot, struct pnp_bios_node *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	if ( !boot && pnpbios_dont_use_current_config )
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_SYS_DEV_NODE, 0, PNP_TS1, 0, PNP_TS2, boot ? 2 : 1, PNP_DS, 0,
			       nodenum, sizeof(char), data, 65536);
	return status;
}

int pnp_bios_get_dev_node(u8 *nodenum, char boot, struct pnp_bios_node *data)
{
	int status;
	status =  __pnp_bios_get_dev_node( nodenum, boot, data );
	if ( status )
		pnpbios_print_status( "get_dev_node", status );
	return status;
}


/*
 * Call PnP BIOS with function 0x02, "set system device node"
 * Input: *nodenum = desired node, 
 *        boot = whether to set nonvolatile boot (!=0)
 *               or volatile current (0) config
 */
static int __pnp_bios_set_dev_node(u8 nodenum, char boot, struct pnp_bios_node *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	if ( !boot && pnpbios_dont_use_current_config )
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_SET_SYS_DEV_NODE, nodenum, 0, PNP_TS1, boot ? 2 : 1, PNP_DS, 0, 0,
			       data, 65536, 0, 0);
	return status;
}

int pnp_bios_set_dev_node(u8 nodenum, char boot, struct pnp_bios_node *data)
{
	int status;
	status =  __pnp_bios_set_dev_node( nodenum, boot, data );
	if ( status ) {
		pnpbios_print_status( "set_dev_node", status );
		return status;
	}
	if ( !boot ) { /* Update devlist */
		status =  pnp_bios_get_dev_node( &nodenum, boot, data );
		if ( status )
			return status;
	}
	return status;
}

#if needed
/*
 * Call PnP BIOS with function 0x03, "get event"
 */
static int pnp_bios_get_event(u16 *event)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_EVENT, 0, PNP_TS1, PNP_DS, 0, 0 ,0 ,0,
			       event, sizeof(u16), 0, 0);
	return status;
}
#endif

#if needed
/* 
 * Call PnP BIOS with function 0x04, "send message"
 */
static int pnp_bios_send_message(u16 message)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_SEND_MESSAGE, message, PNP_DS, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	return status;
}
#endif

#ifdef CONFIG_HOTPLUG
/*
 * Call PnP BIOS with function 0x05, "get docking station information"
 */
static int pnp_bios_dock_station_info(struct pnp_docking_station_info *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_DOCKING_STATION_INFORMATION, 0, PNP_TS1, PNP_DS, 0, 0, 0, 0,
			       data, sizeof(struct pnp_docking_station_info), 0, 0);
	return status;
}
#endif

#if needed
/*
 * Call PnP BIOS with function 0x09, "set statically allocated resource
 * information"
 */
static int pnp_bios_set_stat_res(char *info)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_SET_STATIC_ALLOCED_RES_INFO, 0, PNP_TS1, PNP_DS, 0, 0, 0, 0,
			       info, *((u16 *) info), 0, 0);
	return status;
}
#endif

/*
 * Call PnP BIOS with function 0x0a, "get statically allocated resource
 * information"
 */
static int __pnp_bios_get_stat_res(char *info)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_STATIC_ALLOCED_RES_INFO, 0, PNP_TS1, PNP_DS, 0, 0, 0, 0,
			       info, 65536, 0, 0);
	return status;
}

int pnp_bios_get_stat_res(char *info)
{
	int status;
	status = __pnp_bios_get_stat_res( info );
	if ( status )
		pnpbios_print_status( "get_stat_res", status );
	return status;
}

#if needed
/*
 * Call PnP BIOS with function 0x0b, "get APM id table"
 */
static int pnp_bios_apm_id_table(char *table, u16 *size)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_APM_ID_TABLE, 0, PNP_TS2, 0, PNP_TS1, PNP_DS, 0, 0,
			       table, *size, size, sizeof(u16));
	return status;
}
#endif

/*
 * Call PnP BIOS with function 0x40, "get isa pnp configuration structure"
 */
static int __pnp_bios_isapnp_config(struct pnp_isa_config_struc *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_PNP_ISA_CONFIG_STRUC, 0, PNP_TS1, PNP_DS, 0, 0, 0, 0,
			       data, sizeof(struct pnp_isa_config_struc), 0, 0);
	return status;
}

int pnp_bios_isapnp_config(struct pnp_isa_config_struc *data)
{
	int status;
	status = __pnp_bios_isapnp_config( data );
	if ( status )
		pnpbios_print_status( "isapnp_config", status );
	return status;
}

/*
 * Call PnP BIOS with function 0x41, "get ESCD info"
 */
static int __pnp_bios_escd_info(struct escd_info_struc *data)
{
	u16 status;
	if (!pnp_bios_present())
		return ESCD_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_ESCD_INFO, 0, PNP_TS1, 2, PNP_TS1, 4, PNP_TS1, PNP_DS,
			       data, sizeof(struct escd_info_struc), 0, 0);
	return status;
}

int pnp_bios_escd_info(struct escd_info_struc *data)
{
	int status;
	status = __pnp_bios_escd_info( data );
	if ( status )
		pnpbios_print_status( "escd_info", status );
	return status;
}

/*
 * Call PnP BIOS function 0x42, "read ESCD"
 * nvram_base is determined by calling escd_info
 */
static int __pnp_bios_read_escd(char *data, u32 nvram_base)
{
	u16 status;
	if (!pnp_bios_present())
		return ESCD_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_READ_ESCD, 0, PNP_TS1, PNP_TS2, PNP_DS, 0, 0, 0,
			       data, 65536, (void *)nvram_base, 65536);
	return status;
}

int pnp_bios_read_escd(char *data, u32 nvram_base)
{
	int status;
	status = __pnp_bios_read_escd( data, nvram_base );
	if ( status )
		pnpbios_print_status( "read_escd", status );
	return status;
}

#if needed
/*
 * Call PnP BIOS function 0x43, "write ESCD"
 */
static int pnp_bios_write_escd(char *data, u32 nvram_base)
{
	u16 status;
	if (!pnp_bios_present())
		return ESCD_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_WRITE_ESCD, 0, PNP_TS1, PNP_TS2, PNP_DS, 0, 0, 0,
			       data, 65536, nvram_base, 65536);
	return status;
}
#endif


/*
 *
 * DOCKING FUNCTIONS
 *
 */

#ifdef CONFIG_HOTPLUG

static int unloading = 0;
static struct completion unload_sem;

/*
 * (Much of this belongs in a shared routine somewhere)
 */
 
static int pnp_dock_event(int dock, struct pnp_docking_station_info *info)
{
	char *argv [3], **envp, *buf, *scratch;
	int i = 0, value;

	if (!hotplug_path [0])
		return -ENOENT;
	if (!current->fs->root) {
		return -EAGAIN;
	}
	if (!(envp = (char **) pnpbios_kmalloc (20 * sizeof (char *), GFP_KERNEL))) {
		return -ENOMEM;
	}
	if (!(buf = pnpbios_kmalloc (256, GFP_KERNEL))) {
		kfree (envp);
		return -ENOMEM;
	}

	/* only one standardized param to hotplug command: type */
	argv [0] = hotplug_path;
	argv [1] = "dock";
	argv [2] = 0;

	/* minimal command environment */
	envp [i++] = "HOME=/";
	envp [i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

#ifdef	DEBUG
	/* hint that policy agent should enter no-stdout debug mode */
	envp [i++] = "DEBUG=kernel";
#endif
	/* extensible set of named bus-specific parameters,
	 * supporting multiple driver selection algorithms.
	 */
	scratch = buf;

	/* action:  add, remove */
	envp [i++] = scratch;
	scratch += sprintf (scratch, "ACTION=%s", dock?"add":"remove") + 1;

	/* Report the ident for the dock */
	envp [i++] = scratch;
	scratch += sprintf (scratch, "DOCK=%x/%x/%x",
		info->location_id, info->serial, info->capabilities);
	envp[i] = 0;
	
	value = call_usermodehelper (argv [0], argv, envp, 0);
	kfree (buf);
	kfree (envp);
	return 0;
}

/*
 * Poll the PnP docking at regular intervals
 */
static int pnp_dock_thread(void * unused)
{
	static struct pnp_docking_station_info now;
	int docked = -1, d = 0;
	daemonize("kpnpbiosd");
	allow_signal(SIGKILL);
	while(!unloading && !signal_pending(current))
	{
		int status;
		
		/*
		 * Poll every 2 seconds
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ*2);
		if(signal_pending(current))
			break;

		status = pnp_bios_dock_station_info(&now);

		switch(status)
		{
			/*
			 * No dock to manage
			 */
			case PNP_FUNCTION_NOT_SUPPORTED:
				complete_and_exit(&unload_sem, 0);
			case PNP_SYSTEM_NOT_DOCKED:
				d = 0;
				break;
			case PNP_SUCCESS:
				d = 1;
				break;
			default:
				pnpbios_print_status( "pnp_dock_thread", status );
				continue;
		}
		if(d != docked)
		{
			if(pnp_dock_event(d, &now)==0)
			{
				docked = d;
#if 0
				printk(KERN_INFO "PnPBIOS: Docking station %stached\n", docked?"at":"de");
#endif
			}
		}
	}
	complete_and_exit(&unload_sem, 0);
}

#endif   /* CONFIG_HOTPLUG */

/* pnp EISA ids */

#define HEX(id,a) hex[((id)>>a) & 15]
#define CHAR(id,a) (0x40 + (((id)>>a) & 31))
//

static inline void pnpid32_to_pnpid(u32 id, char *str)
{
	const char *hex = "0123456789abcdef";

	id = be32_to_cpu(id);
	str[0] = CHAR(id, 26);
	str[1] = CHAR(id, 21);
	str[2] = CHAR(id,16);
	str[3] = HEX(id, 12);
	str[4] = HEX(id, 8);
	str[5] = HEX(id, 4);
	str[6] = HEX(id, 0);
	str[7] = '\0';

	return;
}
//
#undef CHAR
#undef HEX

static void node_id_data_to_dev(unsigned char *p, struct pnp_bios_node *node, struct pnp_dev *dev)
{
	int len;
	char id[8];
	struct pnp_id *dev_id;

	if ((char *)p == NULL)
		return;
        while ( (char *)p < ((char *)node->data + node->size )) {

                if( p[0] & 0x80 ) {
			len = (p[2] << 8) | p[1];
			if ((p[0] & 0x7f) == 0x02) /* human readable name */
			{
				int size = *(short *) &p[1];
				memcpy(dev->dev.name, p + 3, len >= 80 ? 79 : size);
				break;
			}
			p += len + 3;
			continue;
		}
		len = p[0] & 0x07;
		switch ((p[0]>>3) & 0x0f) {
		case 0x0f: /* end tag */
		{
        		return;
			break;
		}
		case 0x03: /* compatible ID */
		{
			if (len != 4)
				goto __skip;
			dev_id =  pnpbios_kmalloc(sizeof (struct pnp_id), GFP_KERNEL);
			if (!dev_id)
				return;
			memset(dev_id, 0, sizeof(struct pnp_id));
			pnpid32_to_pnpid(p[1] | p[2] << 8 | p[3] << 16 | p[4] << 24,id);
			memcpy(&dev_id->id, id, 7);
			pnp_add_id(dev_id, dev);
			break;
		}
		}
		__skip:
		p += len + 1;

	}
}

static int pnpbios_get_resources(struct pnp_dev * dev, struct pnp_resource_table * res)
{
	u8 nodenum = dev->number;
	struct pnp_bios_node * node;

	/* just in case */
	if(!pnpbios_is_dynamic(dev))
		return -EPERM;

	node = pnpbios_kmalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -1;
	if (pnp_bios_get_dev_node(&nodenum, (char )PNPMODE_DYNAMIC, node)) {
		kfree(node);
		return -ENODEV;
	}
	pnp_parse_current_resources((char *)node->data,(char *)node->data + node->size,res);
	dev->active = pnp_is_active(dev);
	kfree(node);
	return 0;
}

static int pnpbios_set_resources(struct pnp_dev * dev, struct pnp_resource_table * res)
{
	u8 nodenum = dev->number;
	struct pnp_bios_node * node;
	int ret;

	/* just in case */
	if (!pnpbios_is_dynamic(dev))
		return -EPERM;

	node = pnpbios_kmalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -1;
	if (pnp_bios_get_dev_node(&nodenum, (char )PNPMODE_STATIC, node))
		return -ENODEV;
	if(!pnp_write_resources((char *)node->data,(char *)node->data + node->size,res)){
		kfree(node);
		return -1;
	}
	ret = pnp_bios_set_dev_node(node->handle, (char)PNPMODE_DYNAMIC, node);
	kfree(node);
	if (ret > 0)
		ret = -1;
	return ret;
}

static int pnpbios_disable_resources(struct pnp_dev *dev)
{
	struct pnp_bios_node * node;
	int ret;
	
	/* just in case */
	if(dev->flags & PNPBIOS_NO_DISABLE || !pnpbios_is_dynamic(dev))
		return -EPERM;

	/* the value of this will be zero */
	node = pnpbios_kmalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	ret = pnp_bios_set_dev_node(dev->number, (char)PNPMODE_DYNAMIC, node);
	kfree(node);
	if (ret > 0)
		ret = -1;
	return ret;
}

/* PnP Layer support */

struct pnp_protocol pnpbios_protocol = {
	.name	= "Plug and Play BIOS",
	.get	= pnpbios_get_resources,
	.set	= pnpbios_set_resources,
	.disable = pnpbios_disable_resources,
};

static int insert_device(struct pnp_dev *dev, struct pnp_bios_node * node)
{
	struct list_head * pos;
	unsigned char * p;
	struct pnp_dev * pnp_dev;
	struct pnp_id *dev_id;
	char id[8];

	/* check if the device is already added */
	dev->number = node->handle;
	list_for_each (pos, &pnpbios_protocol.devices){
		pnp_dev = list_entry(pos, struct pnp_dev, protocol_list);
		if (dev->number == pnp_dev->number)
			return -1;
	}

	/* set the initial values for the PnP device */
	dev_id = pnpbios_kmalloc(sizeof(struct pnp_id), GFP_KERNEL);
	if (!dev_id)
		return -1;
	pnpid32_to_pnpid(node->eisa_id,id);
	memcpy(dev_id->id,id,7);
	pnp_add_id(dev_id, dev);
	p = pnp_parse_current_resources((char *)node->data,
		(char *)node->data + node->size,&dev->res);
	p = pnp_parse_possible_resources((char *)p,
		(char *)node->data + node->size,dev);
	node_id_data_to_dev(p,node,dev);
	dev->active = pnp_is_active(dev);
	dev->flags = node->flags;
	if (!(dev->flags & PNPBIOS_NO_CONFIG))
		dev->capabilities |= PNP_CONFIGURABLE;
	if (!(dev->flags & PNPBIOS_NO_DISABLE))
		dev->capabilities |= PNP_DISABLE;
	dev->capabilities |= PNP_READ;
	if (pnpbios_is_dynamic(dev))
		dev->capabilities |= PNP_WRITE;
	if (dev->flags & PNPBIOS_REMOVABLE)
		dev->capabilities |= PNP_REMOVABLE;
	dev->protocol = &pnpbios_protocol;

	pnp_add_device(dev);
	pnpbios_interface_attach_device(node);

	return 0;
}

static void __init build_devlist(void)
{
	u8 nodenum;
	unsigned int nodes_got = 0;
	unsigned int devs = 0;
	struct pnp_bios_node *node;
	struct pnp_dev_node_info node_info;
	struct pnp_dev *dev;

	if (!pnp_bios_present())
		return;

	if (pnp_bios_dev_node_info(&node_info) != 0)
		return;

	node = pnpbios_kmalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return;

	for(nodenum=0; nodenum<0xff; ) {
		u8 thisnodenum = nodenum;
		/* eventually we will want to use PNPMODE_STATIC here but for now
		 * dynamic will help us catch buggy bioses to add to the blacklist.
		 */
		if (!pnpbios_dont_use_current_config) {
			if (pnp_bios_get_dev_node(&nodenum, (char )PNPMODE_DYNAMIC, node))
				break;
		} else {
			if (pnp_bios_get_dev_node(&nodenum, (char )PNPMODE_STATIC, node))
				break;
		}
		nodes_got++;
		dev =  pnpbios_kmalloc(sizeof (struct pnp_dev), GFP_KERNEL);
		if (!dev)
			break;
		if(insert_device(dev,node)<0)
			kfree(dev);
		else
			devs++;
		if (nodenum <= thisnodenum) {
			printk(KERN_ERR "PnPBIOS: build_devlist: Node number 0x%x is out of sequence following node 0x%x. Aborting.\n", (unsigned int)nodenum, (unsigned int)thisnodenum);
			break;
		}
	}
	kfree(node);

	printk(KERN_INFO "PnPBIOS: %i node%s reported by PnP BIOS; %i recorded by driver\n",
		nodes_got, nodes_got != 1 ? "s" : "", devs);
}

/*
 *
 * INIT AND EXIT
 *
 */

static int pnpbios_disabled; /* = 0 */
int pnpbios_dont_use_current_config; /* = 0 */

#ifndef MODULE
static int __init pnpbios_setup(char *str)
{
	int invert;

	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "off", 3) == 0)
			pnpbios_disabled=1;
		if (strncmp(str, "on", 2) == 0)
			pnpbios_disabled=0;
		invert = (strncmp(str, "no-", 3) == 0);
		if (invert)
			str += 3;
		if (strncmp(str, "curr", 4) == 0)
			pnpbios_dont_use_current_config = invert;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}

	return 1;
}

__setup("pnpbios=", pnpbios_setup);
#endif

subsys_initcall(pnpbios_init);

int __init pnpbios_init(void)
{
	union pnp_bios_expansion_header *check;
	u8 sum;
	int i, length, r;

	spin_lock_init(&pnp_bios_lock);

	if(pnpbios_disabled || (dmi_broken & BROKEN_PNP_BIOS)) {
		printk(KERN_INFO "PnPBIOS: Disabled\n");
		return -ENODEV;
	} else
		printk(KERN_INFO "PnPBIOS: Scanning system for PnP BIOS support...\n");

	/*
 	 * Search the defined area (0xf0000-0xffff0) for a valid PnP BIOS
	 * structure and, if one is found, sets up the selectors and
	 * entry points
	 */
	for (check = (union pnp_bios_expansion_header *) __va(0xf0000);
	     check < (union pnp_bios_expansion_header *) __va(0xffff0);
	     ((void *) (check)) += 16) {
		if (check->fields.signature != PNP_SIGNATURE)
			continue;
		length = check->fields.length;
		if (!length)
			continue;
		for (sum = 0, i = 0; i < length; i++)
			sum += check->chars[i];
		if (sum)
			continue;
		if (check->fields.version < 0x10) {
			printk(KERN_WARNING "PnPBIOS: PnP BIOS version %d.%d is not supported\n",
			       check->fields.version >> 4,
			       check->fields.version & 15);
			continue;
		}
		printk(KERN_INFO "PnPBIOS: Found PnP BIOS installation structure at 0x%p\n", check);
		printk(KERN_INFO "PnPBIOS: PnP BIOS version %d.%d, entry 0x%x:0x%x, dseg 0x%x\n",
                       check->fields.version >> 4, check->fields.version & 15,
		       check->fields.pm16cseg, check->fields.pm16offset,
		       check->fields.pm16dseg);
		pnp_bios_callpoint.offset = check->fields.pm16offset;
		pnp_bios_callpoint.segment = PNP_CS16;
		pnp_bios_hdr = check;

		set_base(bad_bios_desc, __va((unsigned long)0x40 << 4));
		_set_limit((char *)&bad_bios_desc, 4095 - (0x40 << 4));
		for(i=0; i < NR_CPUS; i++)
		{
			Q2_SET_SEL(i, PNP_CS32, &pnp_bios_callfunc, 64 * 1024);
			Q_SET_SEL(i, PNP_CS16, check->fields.pm16cseg, 64 * 1024);
			Q_SET_SEL(i, PNP_DS, check->fields.pm16dseg, 64 * 1024);
		}
		break;
	}
	if (!pnp_bios_present()) {
		printk(KERN_INFO "PnPBIOS: A PnP BIOS was not detected.\n");
		return -ENODEV;
	}

	/*
	 * we found a pnpbios, now let's load the rest of the driver
	 */

	/* read the node info */
	if (pnp_bios_dev_node_info(&node_info)) {
		printk(KERN_ERR "PnPBIOS: Unable to get node info.  Aborting.\n");
		return -EIO;
	}

	/* register with the pnp layer */
	pnp_register_protocol(&pnpbios_protocol);

#ifdef CONFIG_PROC_FS
	/* start the proc interface */
	r = pnpbios_proc_init();
	if (r)
		return r;
#endif

	/* scan for pnpbios devices */
	build_devlist();

	return 0;
}

static int __init pnpbios_thread_init(void)
{
#ifdef CONFIG_HOTPLUG
	init_completion(&unload_sem);
	if (kernel_thread(pnp_dock_thread, NULL, CLONE_KERNEL) > 0)
		unloading = 0;
#endif
	return 0;
}

#ifndef MODULE

/* init/main.c calls pnpbios_init early */

/* Start the kernel thread later: */
module_init(pnpbios_thread_init);

#else

/*
 * N.B.: Building pnpbios as a module hasn't been fully implemented
 */

MODULE_LICENSE("GPL");

static int __init pnpbios_init_all(void)
{
	int r;

	r = pnpbios_init();
	if (r)
		return r;
	r = pnpbios_thread_init();
	if (r)
		return r;
	return 0;
}

static void __exit pnpbios_exit(void)
{
#ifdef CONFIG_HOTPLUG
	unloading = 1;
	wait_for_completion(&unload_sem);
#endif
	pnpbios_proc_exit();
	/* We ought to free resources here */
	return;
}

module_init(pnpbios_init_all);
module_exit(pnpbios_exit);

#endif
