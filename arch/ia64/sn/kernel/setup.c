/*
 * Copyright (C) 1999,2001-2002 Silicon Graphics, Inc. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/serial.h>
#include <linux/irq.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/interrupt.h>
#include <linux/root_dev.h>

#include <asm/io.h>
#include <asm/sal.h>
#include <asm/machvec.h>
#include <asm/system.h>
#ifdef CONFIG_IA64_MCA
#include <asm/acpi-ext.h>
#endif
#include <asm/processor.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/arch.h>
#include <asm/sn/addrs.h>
#include <asm/sn/pda.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/simulator.h>
#include <asm/sn/leds.h>
#include <asm/sn/bte.h>

#ifdef CONFIG_IA64_SGI_SN2
#include <asm/sn/sn2/shub.h>
#endif

extern void bte_init_node (nodepda_t *, cnodeid_t);
extern void bte_init_cpu (void);

long sn_rtc_cycles_per_second;   

/*
 * This is the address of the RRegs in the HSpace of the global
 * master.  It is used by a hack in serial.c (serial_[in|out],
 * printk.c (early_printk), and kdb_io.c to put console output on that
 * node's Bedrock UART.  It is initialized here to 0, so that
 * early_printk won't try to access the UART before
 * master_node_bedrock_address is properly calculated.
 */
u64 master_node_bedrock_address = 0UL;

static void sn_init_pdas(void);

extern struct irq_desc *_sn1_irq_desc[];

#if defined(CONFIG_IA64_SGI_SN1)
extern synergy_da_t	*Synergy_da_indr[];
#endif

static nodepda_t	*nodepdaindr[MAX_COMPACT_NODES];

#ifdef CONFIG_IA64_SGI_SN2
irqpda_t		*irqpdaindr[NR_CPUS];
#endif /* CONFIG_IA64_SGI_SN2 */


/*
 * The format of "screen_info" is strange, and due to early i386-setup
 * code. This is just enough to make the console code think we're on a
 * VGA color display.
 */
struct screen_info sn1_screen_info = {
	.orig_x =		 0,
	.orig_y =		 0,
	.orig_video_mode =	 3,
	.orig_video_cols =	80,
	.orig_video_ega_bx =	 3,
	.orig_video_lines =	25,
	.orig_video_isVGA =	 1,
	.orig_video_points =	16
};

/*
 * This is here so we can use the CMOS detection in ide-probe.c to
 * determine what drives are present.  In theory, we don't need this
 * as the auto-detection could be done via ide-probe.c:do_probe() but
 * in practice that would be much slower, which is painful when
 * running in the simulator.  Note that passing zeroes in DRIVE_INFO
 * is sufficient (the IDE driver will autodetect the drive geometry).
 */
char drive_info[4*16];

/**
 * sn1_map_nr - return the mem_map entry for a given kernel address
 * @addr: kernel address to query
 *
 * Finds the mem_map entry for the kernel address given.  Used by
 * virt_to_page() (asm-ia64/page.h), among other things.
 */
unsigned long
sn1_map_nr (unsigned long addr)
{
	return MAP_NR_DISCONTIG(addr);
}

/**
 * early_sn1_setup - early setup routine for SN platforms
 *
 * Sets up an intial console to aid debugging.  Intended primarily
 * for bringup, it's only called if %BRINGUP and %CONFIG_IA64_EARLY_PRINTK
 * are turned on.  See start_kernel() in init/main.c.
 */
#if defined(CONFIG_IA64_EARLY_PRINTK)
void __init
early_sn1_setup(void)
{
#if defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
	if ( IS_RUNNING_ON_SIMULATOR() )
#endif
	{
#ifdef CONFIG_IA64_SGI_SN2
		master_node_bedrock_address = (u64)REMOTE_HUB(get_nasid(), SH_JUNK_BUS_UART0);
#else
		master_node_bedrock_address = (u64)REMOTE_HSPEC_ADDR(get_nasid(), 0);
#endif
		printk(KERN_DEBUG "early_sn1_setup: setting master_node_bedrock_address to 0x%lx\n", master_node_bedrock_address);
	}
}
#endif /* CONFIG_IA64_EARLY_PRINTK */

#ifdef NOT_YET_CONFIG_IA64_MCA
extern void ia64_mca_cpe_int_handler (int cpe_irq, void *arg, struct pt_regs *ptregs);
static struct irqaction mca_cpe_irqaction = { 
	.handler =  ia64_mca_cpe_int_handler,
	.flags =    SA_INTERRUPT,
	.name =     "cpe_hndlr"
};
#endif
#ifdef CONFIG_IA64_MCA
extern int platform_irq_list[];
#endif

extern nasid_t master_nasid;

/**
 * sn1_setup - SN platform setup routine
 * @cmdline_p: kernel command line
 *
 * Handles platform setup for SN machines.  This includes determining
 * the RTC frequency (via a SAL call), initializing secondary CPUs, and
 * setting up per-node data areas.  The console is also initialized here.
 */
void __init
sn1_setup(char **cmdline_p)
{
	long status, ticks_per_sec, drift;
	int i;

#if defined(CONFIG_SERIAL) && !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
	struct serial_struct req;
#endif

	master_nasid = get_nasid();
	(void)get_console_nasid();

	status = ia64_sal_freq_base(SAL_FREQ_BASE_REALTIME_CLOCK, &ticks_per_sec, &drift);
	if (status != 0 || ticks_per_sec < 100000)
		printk(KERN_WARNING "unable to determine platform RTC clock frequency\n");
	else
		sn_rtc_cycles_per_second = ticks_per_sec;
		
	for (i=0;i<NR_CPUS;i++)
		_sn1_irq_desc[i] = _irq_desc;

#ifdef CONFIG_IA64_MCA
	platform_irq_list[ACPI20_ENTRY_PIS_CPEI] = IA64_PCE_VECTOR;
#endif


#if defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
	if ( IS_RUNNING_ON_SIMULATOR() )
#endif
	{
#ifdef CONFIG_IA64_SGI_SN2
		master_node_bedrock_address = (u64)REMOTE_HUB(get_nasid(), SH_JUNK_BUS_UART0);
#else
		master_node_bedrock_address = (u64)REMOTE_HSPEC_ADDR(get_nasid(), 0);
#endif
		printk(KERN_DEBUG "sn1_setup: setting master_node_bedrock_address to 0x%lx\n",
		       master_node_bedrock_address);
	}

#if defined(CONFIG_SERIAL) && !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
	/*
	 * We do early_serial_setup() to clean out the rs-table[] from the
	 * statically compiled in version.
	 */
	memset(&req, 0, sizeof(struct serial_struct));
	req.line = 0;
	req.baud_base = 124800;
	req.port = 0;
	req.port_high = 0;
	req.irq = 0;
	req.flags = (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST);
	req.io_type = SERIAL_IO_MEM;
	req.hub6 = 0;
#ifdef CONFIG_IA64_SGI_SN2
	req.iomem_base = (u8 *)(master_node_bedrock_address);
#else
	req.iomem_base = (u8 *)(master_node_bedrock_address + 0x80);
#endif
	req.iomem_reg_shift = 3;
	req.type = 0;
	req.xmit_fifo_size = 0;
	req.custom_divisor = 0;
	req.closing_wait = 0;
	early_serial_setup(&req);
#endif /* CONFIG_SERIAL && !CONFIG_SERIAL_SGI_L1_PROTOCOL */

	/*
	 * we set the default root device to /dev/hda
	 * to make simulation easy
	 */
	ROOT_DEV = Root_HDA1;

	/*
	 * Create the PDAs and NODEPDAs for all the cpus.
	 */
	sn_init_pdas();


	/* 
	 * For the bootcpu, we do this here. All other cpus will make the
	 * call as part of cpu_init in slave cpu initialization.
	 */
	sn_cpu_init();


#ifdef CONFIG_SMP
	init_smp_config();
#endif
	screen_info = sn1_screen_info;

	/*
	 * Turn off "floating-point assist fault" warnings by default.
	 */
	current->thread.flags |= IA64_THREAD_FPEMU_NOPRINT;
}

/**
 * sn_init_pdas - setup node data areas
 *
 * One time setup for Node Data Area.  Called by sn1_setup().
 */
void
sn_init_pdas(void)
{
	cnodeid_t	cnode;

	/*
	 * Make sure that the PDA fits entirely in the same page as the 
	 * cpu_data area.
	 */
	if ((PDAADDR&~PAGE_MASK)+sizeof(pda_t) > PAGE_SIZE)
		panic("overflow of cpu_data page");

        /*
         * Allocate & initalize the nodepda for each node.
         */
        for (cnode=0; cnode < numnodes; cnode++) {
		nodepdaindr[cnode] = alloc_bootmem_node(NODE_DATA(cnode), sizeof(nodepda_t));
		memset(nodepdaindr[cnode], 0, sizeof(nodepda_t));

#if defined(CONFIG_IA64_SGI_SN1)
		Synergy_da_indr[cnode * 2] = (synergy_da_t *) alloc_bootmem_node(NODE_DATA(cnode), sizeof(synergy_da_t));
		Synergy_da_indr[cnode * 2 + 1] = (synergy_da_t *) alloc_bootmem_node(NODE_DATA(cnode), sizeof(synergy_da_t));
		memset(Synergy_da_indr[cnode * 2], 0, sizeof(synergy_da_t));
		memset(Synergy_da_indr[cnode * 2 + 1], 0, sizeof(synergy_da_t));
#endif
        }

	/*
	 * Now copy the array of nodepda pointers to each nodepda.
	 */
        for (cnode=0; cnode < numnodes; cnode++)
		memcpy(nodepdaindr[cnode]->pernode_pdaindr, nodepdaindr, sizeof(nodepdaindr));


	/*
	 * Set up IO related platform-dependent nodepda fields.
	 * The following routine actually sets up the hubinfo struct
	 * in nodepda.
	 */
	for (cnode = 0; cnode < numnodes; cnode++) {
		init_platform_nodepda(nodepdaindr[cnode], cnode);
		bte_init_node (nodepdaindr[cnode], cnode);
	}
}

/**
 * sn_cpu_init - initialize per-cpu data areas
 * @cpuid: cpuid of the caller
 *
 * Called during cpu initialization on each cpu as it starts.
 * Currently, initializes the per-cpu data area for SNIA.
 * Also sets up a few fields in the nodepda.  Also known as
 * platform_cpu_init() by the ia64 machvec code.
 */
void __init
sn_cpu_init(void)
{
	int	cpuid;
	int	cpuphyid;
	int	nasid;
	int	slice;
	int	cnode;

	/*
	 * The boot cpu makes this call again after platform initialization is
	 * complete.
	 */
	if (nodepdaindr[0] == NULL)
		return;

	cpuid = smp_processor_id();
	cpuphyid = ((ia64_get_lid() >> 16) & 0xffff);
	nasid = cpu_physical_id_to_nasid(cpuphyid);
	cnode = nasid_to_cnodeid(nasid);
	slice = cpu_physical_id_to_slice(cpuphyid);

	pda.p_nodepda = nodepdaindr[cnode];
	pda.led_address = (long*) (LED0 + (slice<<LED_CPU_SHIFT));
	pda.led_state = 0;
	pda.hb_count = HZ/2;
	pda.hb_state = 0;
	pda.idle_flag = 0;
	
	if (local_node_data->active_cpu_count == 1)
		nodepda->node_first_cpu = cpuid;

#ifdef CONFIG_IA64_SGI_SN1
	{
		int	synergy;
		synergy = cpu_physical_id_to_synergy(cpuphyid);
		pda.p_subnodepda = &nodepdaindr[cnode]->snpda[synergy];
	}
#endif

#ifdef CONFIG_IA64_SGI_SN2

	/*
	 * We must use different memory allocators for first cpu (bootmem 
	 * allocator) than for the other cpus (regular allocator).
	 */
	if (cpuid == 0)
		irqpdaindr[cpuid] = alloc_bootmem_node(NODE_DATA(cpuid_to_cnodeid(cpuid)),sizeof(irqpda_t));
	else
		irqpdaindr[cpuid] = page_address(alloc_pages_node(local_cnodeid(), GFP_KERNEL, get_order(sizeof(irqpda_t))));
	memset(irqpdaindr[cpuid], 0, sizeof(irqpda_t));
	pda.p_irqpda = irqpdaindr[cpuid];
	pda.pio_write_status_addr = (volatile unsigned long *)LOCAL_MMR_ADDR((slice < 2 ? SH_PIO_WRITE_STATUS_0 : SH_PIO_WRITE_STATUS_1 ) );
#endif

#ifdef CONFIG_IA64_SGI_SN1
	pda.bedrock_rev_id = (volatile unsigned long *) LOCAL_HUB(LB_REV_ID);
	if (cpuid_to_synergy(cpuid))
		/* CPU B */
		pda.pio_write_status_addr = (volatile unsigned long *) GBL_PERF_B_ADDR;
	else
		/* CPU A */
		pda.pio_write_status_addr = (volatile unsigned long *) GBL_PERF_A_ADDR;
#endif


	bte_init_cpu();
}


/**
 * cnodeid_to_cpuid - convert a cnode to a cpuid of a cpu on the node.
 * @cnode: node to get a cpuid from
 *	
 * Returns -1 if no cpus exist on the node.
 * NOTE:BRINGUP ZZZ This is NOT a good way to find cpus on the node.
 * Need a better way!!
 */
int
cnodeid_to_cpuid(int cnode) {
	int cpu;

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (!cpu_online(cpu)) continue;
		if (cpuid_to_cnodeid(cpu) == cnode)
			break;
	}

	if (cpu == NR_CPUS) 
		cpu = -1;

	return cpu;
}
