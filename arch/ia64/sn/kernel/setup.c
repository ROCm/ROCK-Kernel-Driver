/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999,2001-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/config.h>
#include <linux/module.h>
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
#include <linux/acpi.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/root_dev.h>

#include <asm/io.h>
#include <asm/sal.h>
#include <asm/machvec.h>
#include <asm/system.h>
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
#include <asm/sn/clksupport.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn2/shub.h>

DEFINE_PER_CPU(struct pda_s, pda_percpu);

#define MAX_PHYS_MEMORY		(1UL << 49)     /* 1 TB */

extern void bte_init_node (nodepda_t *, cnodeid_t);
extern void bte_init_cpu (void);
extern void sn_timer_init(void);
extern unsigned long last_time_offset;
extern void init_platform_hubinfo(nodepda_t **nodepdaindr);
extern void (*ia64_mark_idle)(int);
extern void snidle(int);
extern unsigned char acpi_kbd_controller_present;


unsigned long sn_rtc_cycles_per_second;   

partid_t sn_partid = -1;
char sn_system_serial_number_string[128];
u64 sn_partition_serial_number;

short physical_node_map[MAX_PHYSNODE_ID];

EXPORT_SYMBOL(physical_node_map);

int	numionodes;
/*
 * This is the address of the RRegs in the HSpace of the global
 * master.  It is used by a hack in serial.c (serial_[in|out],
 * printk.c (early_printk), and kdb_io.c to put console output on that
 * node's Bedrock UART.  It is initialized here to 0, so that
 * early_printk won't try to access the UART before
 * master_node_bedrock_address is properly calculated.
 */
u64 master_node_bedrock_address;

static void sn_init_pdas(char **);
static void scan_for_ionodes(void);


static nodepda_t	*nodepdaindr[MAX_COMPACT_NODES];

irqpda_t		*irqpdaindr;


/*
 * The format of "screen_info" is strange, and due to early i386-setup
 * code. This is just enough to make the console code think we're on a
 * VGA color display.
 */
struct screen_info sn_screen_info = {
	.orig_x			= 0,
	.orig_y			= 0,
	.orig_video_mode	= 3,
	.orig_video_cols	= 80,
	.orig_video_ega_bx	= 3,
	.orig_video_lines	= 25,
	.orig_video_isVGA	= 1,
	.orig_video_points	= 16
};

/*
 * This is here so we can use the CMOS detection in ide-probe.c to
 * determine what drives are present.  In theory, we don't need this
 * as the auto-detection could be done via ide-probe.c:do_probe() but
 * in practice that would be much slower, which is painful when
 * running in the simulator.  Note that passing zeroes in DRIVE_INFO
 * is sufficient (the IDE driver will autodetect the drive geometry).
 */
#ifdef CONFIG_IA64_GENERIC
extern char drive_info[4*16];
#else
char drive_info[4*16];
#endif

/*
 * This routine can only be used during init, since
 * smp_boot_data is an init data structure.
 * We have to use smp_boot_data.cpu_phys_id to find
 * the physical id of the processor because the normal
 * cpu_physical_id() relies on data structures that
 * may not be initialized yet.
 */

static int __init
pxm_to_nasid(int pxm)
{
	int i;
	int nid;

	nid = pxm_to_nid_map[pxm];
	for (i = 0; i < num_node_memblks; i++) {
		if (node_memblk[i].nid == nid) {
			return NASID_GET(node_memblk[i].start_paddr);
		}
	}
	return -1;
}
/**
 * early_sn_setup - early setup routine for SN platforms
 *
 * Sets up an initial console to aid debugging.  Intended primarily
 * for bringup.  See start_kernel() in init/main.c.
 */

void __init
early_sn_setup(void)
{
	void ia64_sal_handler_init (void *entry_point, void *gpval);
	efi_system_table_t			*efi_systab;
	efi_config_table_t 			*config_tables;
	struct ia64_sal_systab			*sal_systab;
	struct ia64_sal_desc_entry_point	*ep;
	char					*p;
	int					i;

	/*
	 * Parse enough of the SAL tables to locate the SAL entry point. Since, console
	 * IO on SN2 is done via SAL calls, early_printk won't work without this.
	 *
	 * This code duplicates some of the ACPI table parsing that is in efi.c & sal.c.
	 * Any changes to those file may have to be made hereas well.
	 */
	efi_systab = (efi_system_table_t*)__va(ia64_boot_param->efi_systab);
	config_tables = __va(efi_systab->tables);
	for (i = 0; i < efi_systab->nr_tables; i++) {
		if (efi_guidcmp(config_tables[i].guid, SAL_SYSTEM_TABLE_GUID) == 0) {
			sal_systab = __va(config_tables[i].table);
			p = (char*)(sal_systab+1);
			for (i = 0; i < sal_systab->entry_count; i++) {
				if (*p == SAL_DESC_ENTRY_POINT) {
					ep = (struct ia64_sal_desc_entry_point *) p;
					ia64_sal_handler_init(__va(ep->sal_proc), __va(ep->gp));
					break;
				}
				p += SAL_DESC_SIZE(*p);
			}
		}
	}

	if ( IS_RUNNING_ON_SIMULATOR() ) {
		master_node_bedrock_address = (u64)REMOTE_HUB(get_nasid(), SH_JUNK_BUS_UART0);
		printk(KERN_DEBUG "early_sn_setup: setting master_node_bedrock_address to 0x%lx\n", master_node_bedrock_address);
	}
}

extern int platform_intr_list[];
extern nasid_t master_nasid;
static int shub_1_1_found __initdata;


/*
 * sn_check_for_wars
 *
 * Set flag for enabling shub specific wars
 */

static inline int __init
is_shub_1_1(int nasid)
{
	unsigned long id;
	int	rev;

	id = REMOTE_HUB_L(nasid, SH_SHUB_ID);
	rev =  (id & SH_SHUB_ID_REVISION_MASK) >> SH_SHUB_ID_REVISION_SHFT;
	return rev <= 2;
}

static void __init
sn_check_for_wars(void)
{
	int	cnode;

	for (cnode=0; cnode< numnodes; cnode++)
		if (is_shub_1_1(cnodeid_to_nasid(cnode)))
			shub_1_1_found = 1;
}



/**
 * sn_setup - SN platform setup routine
 * @cmdline_p: kernel command line
 *
 * Handles platform setup for SN machines.  This includes determining
 * the RTC frequency (via a SAL call), initializing secondary CPUs, and
 * setting up per-node data areas.  The console is also initialized here.
 */
void __init
sn_setup(char **cmdline_p)
{
	long status, ticks_per_sec, drift;
	int pxm;
	int major = sn_sal_rev_major(), minor = sn_sal_rev_minor();
	extern nasid_t snia_get_master_baseio_nasid(void);
	extern void sn_cpu_init(void);
	extern nasid_t snia_get_console_nasid(void);

	/*
	 * If the generic code has enabled vga console support - lets
	 * get rid of it again. This is a kludge for the fact that ACPI
	 * currtently has no way of informing us if legacy VGA is available
	 * or not.
	 */
#if defined(CONFIG_VT) && defined(CONFIG_VGA_CONSOLE)
	if (conswitchp == &vga_con) {
		printk(KERN_DEBUG "SGI: Disabling VGA console\n");
#ifdef CONFIG_DUMMY_CONSOLE
		conswitchp = &dummy_con;
#else
		conswitchp = NULL;
#endif /* CONFIG_DUMMY_CONSOLE */
	}
#endif /* def(CONFIG_VT) && def(CONFIG_VGA_CONSOLE) */

	MAX_DMA_ADDRESS = PAGE_OFFSET + MAX_PHYS_MEMORY;

	memset(physical_node_map, -1, sizeof(physical_node_map));
	for (pxm=0; pxm<MAX_PXM_DOMAINS; pxm++)
		if (pxm_to_nid_map[pxm] != -1)
			physical_node_map[pxm_to_nasid(pxm)] = pxm_to_nid_map[pxm];


	/*
	 * Old PROMs do not provide an ACPI FADT. Disable legacy keyboard
	 * support here so we don't have to listen to failed keyboard probe
	 * messages.
	 */
	if ((major < 2 || (major == 2 && minor <= 9)) &&
	    acpi_kbd_controller_present) {
		printk(KERN_INFO "Disabling legacy keyboard support as prom "
		       "is too old and doesn't provide FADT\n");
		acpi_kbd_controller_present = 0;
	}

	printk("SGI SAL version %x.%02x\n", major, minor);

	/*
	 * Confirm the SAL we're running on is recent enough...
	 */
	if ((major < SN_SAL_MIN_MAJOR) || (major == SN_SAL_MIN_MAJOR &&
					   minor < SN_SAL_MIN_MINOR)) {
		printk(KERN_ERR "This kernel needs SGI SAL version >= "
		       "%x.%02x\n", SN_SAL_MIN_MAJOR, SN_SAL_MIN_MINOR);
		panic("PROM version too old\n");
	}

	master_nasid = get_nasid();
	(void)snia_get_console_nasid();
	(void)snia_get_master_baseio_nasid();

	status = ia64_sal_freq_base(SAL_FREQ_BASE_REALTIME_CLOCK, &ticks_per_sec, &drift);
	if (status != 0 || ticks_per_sec < 100000) {
		printk(KERN_WARNING "unable to determine platform RTC clock frequency, guessing.\n");
		/* PROM gives wrong value for clock freq. so guess */
		sn_rtc_cycles_per_second = 1000000000000UL/30000UL;
	}
	else
		sn_rtc_cycles_per_second = ticks_per_sec;

	platform_intr_list[ACPI_INTERRUPT_CPEI] = IA64_CPE_VECTOR;


	if ( IS_RUNNING_ON_SIMULATOR() )
	{
		master_node_bedrock_address = (u64)REMOTE_HUB(get_nasid(), SH_JUNK_BUS_UART0);
		printk(KERN_DEBUG "sn_setup: setting master_node_bedrock_address to 0x%lx\n",
		       master_node_bedrock_address);
	}

	/*
	 * we set the default root device to /dev/hda
	 * to make simulation easy
	 */
	ROOT_DEV = Root_HDA1;

	/*
	 * Create the PDAs and NODEPDAs for all the cpus.
	 */
	sn_init_pdas(cmdline_p);

	ia64_mark_idle = &snidle;

	/* 
	 * For the bootcpu, we do this here. All other cpus will make the
	 * call as part of cpu_init in slave cpu initialization.
	 */
	sn_cpu_init();

	/*
	 * Setup hubinfo stuff. Has to happen AFTER sn_cpu_init(),
	 * because it uses the cnode to nasid tables.
	 */
	init_platform_hubinfo(nodepdaindr);

#ifdef CONFIG_SMP
	init_smp_config();
#endif
	screen_info = sn_screen_info;

	sn_timer_init();
}

/**
 * sn_init_pdas - setup node data areas
 *
 * One time setup for Node Data Area.  Called by sn_setup().
 */
void __init
sn_init_pdas(char **cmdline_p)
{
	cnodeid_t	cnode;

	memset(pda->cnodeid_to_nasid_table, -1, sizeof(pda->cnodeid_to_nasid_table));
	for (cnode=0; cnode<numnodes; cnode++)
		pda->cnodeid_to_nasid_table[cnode] = pxm_to_nasid(nid_to_pxm_map[cnode]);

	numionodes = numnodes;
	scan_for_ionodes();

        /*
         * Allocate & initalize the nodepda for each node.
         */
        for (cnode=0; cnode < numnodes; cnode++) {
		nodepdaindr[cnode] = alloc_bootmem_node(NODE_DATA(cnode), sizeof(nodepda_t));
		memset(nodepdaindr[cnode], 0, sizeof(nodepda_t));
        }

	/*
	 * Now copy the array of nodepda pointers to each nodepda.
	 */
        for (cnode=0; cnode < numionodes; cnode++)
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
	static int	wars_have_been_checked;

	/*
	 * The boot cpu makes this call again after platform initialization is
	 * complete.
	 */
	if (nodepdaindr[0] == NULL)
		return;

	cpuid = smp_processor_id();
	cpuphyid = ((ia64_getreg(_IA64_REG_CR_LID) >> 16) & 0xffff);
	nasid = cpu_physical_id_to_nasid(cpuphyid);
	cnode = nasid_to_cnodeid(nasid);
	slice = cpu_physical_id_to_slice(cpuphyid);

	memset(pda, 0, sizeof(pda));
	pda->p_nodepda = nodepdaindr[cnode];
	pda->led_address = (typeof(pda->led_address)) (LED0 + (slice<<LED_CPU_SHIFT));
	pda->led_state = LED_ALWAYS_SET;
	pda->hb_count = HZ/2;
	pda->hb_state = 0;
	pda->idle_flag = 0;

	if (cpuid != 0){
		memcpy(pda->cnodeid_to_nasid_table, pdacpu(0)->cnodeid_to_nasid_table,
				sizeof(pda->cnodeid_to_nasid_table));
	}

	/*
	 * Check for WARs.
	 * Only needs to be done once, on BSP.
	 * Has to be done after loop above, because it uses pda.cnodeid_to_nasid_table[i].
	 * Has to be done before assignment below.
	 */
	if (!wars_have_been_checked) {
		sn_check_for_wars();
		wars_have_been_checked = 1;
	}
	pda->shub_1_1_found = shub_1_1_found;
	

	/*
	 * We must use different memory allocators for first cpu (bootmem 
	 * allocator) than for the other cpus (regular allocator).
	 */
	if (cpuid == 0)
		irqpdaindr = alloc_bootmem_node(NODE_DATA(cpuid_to_cnodeid(cpuid)),sizeof(irqpda_t));

	memset(irqpdaindr, 0, sizeof(irqpda_t));
	irqpdaindr->irq_flags[SGI_PCIBR_ERROR] = SN2_IRQ_SHARED;
	irqpdaindr->irq_flags[SGI_PCIBR_ERROR] |= SN2_IRQ_RESERVED;
	irqpdaindr->irq_flags[SGI_II_ERROR] = SN2_IRQ_SHARED;
	irqpdaindr->irq_flags[SGI_II_ERROR] |= SN2_IRQ_RESERVED;

	pda->pio_write_status_addr = (volatile unsigned long *)
			LOCAL_MMR_ADDR((slice < 2 ? SH_PIO_WRITE_STATUS_0 : SH_PIO_WRITE_STATUS_1 ) );
	pda->mem_write_status_addr = (volatile u64 *)
			LOCAL_MMR_ADDR((slice < 2 ? SH_MEMORY_WRITE_STATUS_0 : SH_MEMORY_WRITE_STATUS_1 ) );

	if (local_node_data->active_cpu_count++ == 0) {
		int	buddy_nasid;
		buddy_nasid = cnodeid_to_nasid(numa_node_id() == numnodes-1 ? 0 : numa_node_id()+ 1);
		pda->pio_shub_war_cam_addr = (volatile unsigned long*)GLOBAL_MMR_ADDR(nasid, SH_PI_CAM_CONTROL);
	}

	bte_init_cpu();
}

/*
 * Scan klconfig for ionodes.  Add the nasids to the
 * physical_node_map and the pda and increment numionodes.
 */

static void __init
scan_for_ionodes(void)
{
	int nasid = 0;
	lboard_t *brd;

	/* Setup ionodes with memory */
	for (nasid = 0; nasid < MAX_PHYSNODE_ID; nasid +=2) {
		u64 klgraph_header;
		cnodeid_t cnodeid;

		if (physical_node_map[nasid] == -1) 
			continue;

		klgraph_header = cnodeid = -1;
		klgraph_header = ia64_sn_get_klconfig_addr(nasid);
		if (klgraph_header <= 0) {
			if ( IS_RUNNING_ON_SIMULATOR() )
				continue;
			BUG(); /* All nodes must have klconfig tables! */
		}
		cnodeid = nasid_to_cnodeid(nasid);
		root_lboard[cnodeid] = (lboard_t *)
					NODE_OFFSET_TO_LBOARD( (nasid),
					((kl_config_hdr_t *)(klgraph_header))->
					ch_board_info);
	}

	/* Scan headless/memless IO Nodes. */
	for (nasid = 0; nasid < MAX_PHYSNODE_ID; nasid +=2) {
		/* if there's no nasid, don't try to read the klconfig on the node */
		if (physical_node_map[nasid] == -1) continue;
		brd = find_lboard_any((lboard_t *)root_lboard[nasid_to_cnodeid(nasid)], KLTYPE_SNIA);
		if (brd) {
			brd = KLCF_NEXT_ANY(brd); /* Skip this node's lboard */
			if (!brd)
				continue;
		}

		brd = find_lboard_any(brd, KLTYPE_SNIA);
		while (brd) {
			pda->cnodeid_to_nasid_table[numionodes] = brd->brd_nasid;
			physical_node_map[brd->brd_nasid] = numionodes;
			root_lboard[numionodes] = brd;
			numionodes++;
			brd = KLCF_NEXT_ANY(brd);
			if (!brd)
				break;

			brd = find_lboard_any(brd, KLTYPE_SNIA);
		}
	}
}
