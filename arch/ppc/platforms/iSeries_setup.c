/*
 *
 *
 *    Copyright (c) 2000 Mike Corrigan <mikejc@us.ibm.com>
 *    Copyright (c) 1999-2000 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: iSeries_setup.c
 *
 *    Description:
 *      Architecture- / platform-specific boot-time initialization code for
 *      the IBM iSeries LPAR.  Adapted from original code by Grant Erickson and
 *      code by Gary Thomas, Cort Dougan <cort@fsmlabs.com>, and Dan Malek
 *      <dan@net4x.com>.
 *
 */

#include <linux/pci.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/blk.h>
#include <linux/ide.h>
#include <linux/root_dev.h>
#include <linux/seq_file.h>

#include <asm/processor.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/bootinfo.h>

#include <asm/time.h>
#include "iSeries_setup.h"
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/HvCallHpt.h>
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/HvCallEvent.h>
#include <asm/iSeries/HvCallSm.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/IoHriMainStore.h>
#include <asm/iSeries/iSeries_proc.h>
#include <asm/iSeries/pmc_proc.h>
#include <asm/iSeries/mf.h>
#include <asm/pci-bridge.h>
#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/iSeries_fixup.h>
#include <asm/iSeries/HvReleaseData.h>

/* Function Prototypes */

extern void abort(void);
static void build_iSeries_Memory_Map( void );
static void setup_iSeries_cache_sizes( void );
extern void iSeries_pci_Initialize(void);
static int iSeries_show_cpuinfo(struct seq_file *m);
static int iSeries_show_percpuinfo(struct seq_file *m, int i);
extern struct pci_ops iSeries_pci_ops;

/* Global Variables */

unsigned short iSeries_icache_line_size = 0;
unsigned short iSeries_dcache_line_size = 0;
unsigned short iSeries_icache_lines_per_page = 0;
unsigned short iSeries_dcache_lines_per_page = 0;
unsigned short iSeries_log_icache_line_size = 0;
unsigned short iSeries_log_dcache_line_size = 0;

unsigned long procFreqHz = 0;
unsigned long procFreqMhz = 0;
unsigned long procFreqMhzHundreths = 0;

unsigned long tbFreqHz = 0;
unsigned long tbFreqMhz = 0;
unsigned long tbFreqMhzHundreths = 0;

unsigned long decr_overclock = 8;
unsigned long decr_overclock_proc0 = 8;
unsigned long decr_overclock_set = 0;
unsigned long decr_overclock_proc0_set = 0;

extern unsigned long embedded_sysmap_start;
extern unsigned long embedded_sysmap_end;

extern unsigned long sysmap;
extern unsigned long sysmap_size;
extern unsigned long end_of_DRAM;	// Defined in ppc/mm/init.c

#ifdef CONFIG_SMP
extern struct smp_ops_t iSeries_smp_ops;
#endif /* CONFIG_SMP */

/* XXX for now... */
#ifndef CONFIG_PCI
unsigned long isa_io_base;
#endif

/*
 * void __init platform_init()
 *
 * Description:
 *   This routine...
 *
 * Input(s):
 *   r3 - Optional pointer to a board information structure.
 *   r4 - Optional pointer to the physical starting address of the init RAM
 *        disk.
 *   r5 - Optional pointer to the physical ending address of the init RAM
 *        disk.
 *   r6 - Optional pointer to the physical starting address of any kernel
 *        command-line parameters.
 *   r7 - Optional pointer to the physical ending address of any kernel
 *        command-line parameters.
 *
 * Output(s):
 *   N/A
 *
 * Returns:
 *   N/A
 *
 */

extern int rd_size;		// Defined in drivers/block/rd.c
extern u64 next_jiffy_update_tb[];
extern u64 get_tb64(void);

unsigned long __init iSeries_find_end_of_memory(void)
{
	/* totalLpChunks contains the size of memory (in units of 256K) */
	unsigned long memory_end = (totalLpChunks << 18);

	#ifndef CONFIG_HIGHMEM
	/* Max memory if highmem is not configured is 768 MB */
	if (memory_end > (768 << 20))
		memory_end = 768 << 20;
	#endif /* CONFIG_HIGHMEM */

	return memory_end;
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured and there is
	 * a non-zero starting address for it, set it up
	 */
	if ( xNaca.xRamDisk ) {
		initrd_start = xNaca.xRamDisk + KERNELBASE;
		initrd_end   = initrd_start + xNaca.xRamDiskSize * PAGE_SIZE;
		initrd_below_start_ok = 1;	// ramdisk in kernel space
		ROOT_DEV = Root_RAM0;

		if ( ((rd_size*1024)/PAGE_SIZE) < xNaca.xRamDiskSize )
			rd_size = (xNaca.xRamDiskSize*PAGE_SIZE)/1024;
	} else

#endif /* CONFIG_BLK_DEV_INITRD */
#if CONFIG_VIODASD_IDE
	  {
		ROOT_DEV = Root_HDA1;
	  }
#elif defined(CONFIG_VIODASD)
	  {
		ROOT_DEV = MKDEV( VIODASD_MAJOR, 1 );
	  }
#endif /* CONFIG_VIODASD_IDE */

	/* If an embedded System.map has been added to the kernel,
	 * set it up.
	 */
	if ( embedded_sysmap_start ) {
		sysmap = embedded_sysmap_start + KERNELBASE;
		sysmap_size = embedded_sysmap_end - embedded_sysmap_start;
	}

	/* Copy the kernel command line arguments to a safe place. */

	if (r6) {
 		*(char *)(r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6 + KERNELBASE));
	}

	/* Initialize the table which translate Linux physical addresses to
	 * iSeries absolute addresses
	 */

	build_iSeries_Memory_Map();

	setup_iSeries_cache_sizes();

	/* Initialize machine-dependency vectors */

	ppc_md.setup_arch	 	= iSeries_setup_arch;
	ppc_md.show_cpuinfo	 	= iSeries_show_cpuinfo;
	ppc_md.show_percpuinfo	 	= iSeries_show_percpuinfo;
	ppc_md.irq_cannonicalize 	= NULL;
	ppc_md.init_IRQ		 	= iSeries_init_IRQ;
	ppc_md.get_irq		 	= iSeries_get_irq;
	ppc_md.init		 	= NULL;

	ppc_md.restart		 	= iSeries_restart;
	ppc_md.power_off	 	= iSeries_power_off;
	ppc_md.halt		 	= iSeries_halt;

	ppc_md.time_init	 	= NULL;
	ppc_md.set_rtc_time	 	= iSeries_set_rtc_time;
	ppc_md.get_rtc_time	 	= iSeries_get_rtc_time;
	ppc_md.calibrate_decr	 	= iSeries_calibrate_decr;
	ppc_md.progress			= iSeries_progress;
	ppc_md.find_end_of_memory	= iSeries_find_end_of_memory;

#ifdef CONFIG_PCI
	ppc_md.pcibios_fixup_bus        = iSeries_fixup_bus;
	ppc_md.pcibios_fixup            = iSeries_fixup;
#else
	ppc_md.pcibios_fixup_bus        = NULL;
	ppc_md.pcibios_fixup            = NULL;
#endif /* CONFIG_PCI         */

#ifdef CONFIG_SMP
	ppc_md.smp_ops			= &iSeries_smp_ops;
#endif /* CONFIG_SMP */

	// Associate Lp Event Queue 0 with processor 0
	HvCallEvent_setLpEventQueueInterruptProc( 0, 0 );

	{
	// copy the command line parameter from the primary VSP
	char *p, *q;
	HvCallEvent_dmaToSp( cmd_line,
			     2*64*1024,
			     256,
			     HvLpDma_Direction_RemoteToLocal );
	p = q = cmd_line + 255;
	while( p > cmd_line ) {
		if ((*p == 0) || (*p == ' ') || (*p == '\n'))
			--p;
		else
			break;
	}
	if ( p < q )
		*(p+1) = 0;
	}

	next_jiffy_update_tb[0] = get_tb64();

	iSeries_proc_early_init();

	mf_init();

	iSeries_proc_callback( &pmc_proc_init );

	return;
}

/*
 * The iSeries may have very large memories ( > 128 GB ) and a partition
 * may get memory in "chunks" that may be anywhere in the 2**52 real
 * address space.  The chunks are 256K in size.  To map this to the
 * memory model Linux expects, the iSeries specific code builds a
 * translation table to translate what Linux thinks are "physical"
 * addresses to the actual real addresses.  This allows us to make
 * it appear to Linux that we have contiguous memory starting at
 * physical address zero while in fact this could be far from the truth.
 * To avoid confusion, I'll let the words physical and/or real address
 * apply to the Linux addresses while I'll use "absolute address" to
 * refer to the actual hardware real address.
 *
 * build_iSeries_Memory_Map gets information from the Hypervisor and
 * looks at the Main Store VPD to determine the absolute addresses
 * of the memory that has been assigned to our partition and builds
 * a table used to translate Linux's physical addresses to these
 * absolute addresses.  Absolute addresses are needed when
 * communicating with the hypervisor (e.g. to build HPT entries)
 */

static void __init build_iSeries_Memory_Map(void)
{
	u32 loadAreaFirstChunk, loadAreaLastChunk, loadAreaSize;
	u32 hptFirstChunk, hptLastChunk, hptSizeChunks;
	u32 absAddrHi, absAddrLo;
	u32 nextPhysChunk;
	u32 holeFirstChunk, holeSizeChunks;
	u32 totalChunks,moreChunks;
	u32 currChunk, thisChunk, absChunk;
	u32 currDword;
	u32 chunkBit;
	u64 holeStart, holeEnd, holeSize;
	u64 map;
	struct IoHriMainStoreSegment4 * msVpd;

	// Get absolute address of our load area
	// and map it to physical address 0
	// This guarantees that the loadarea ends up at physical 0
	// otherwise, it might not be returned by PLIC as the first
	// chunks

	loadAreaFirstChunk = (u32)(itLpNaca.xLoadAreaAddr >> 18);
	loadAreaSize =  itLpNaca.xLoadAreaChunks;

	loadAreaLastChunk = loadAreaFirstChunk + loadAreaSize - 1;

	// Get absolute address of our HPT and remember it so
	// we won't map it to any physical address

	hptFirstChunk = (u32)(HvCallHpt_getHptAddress() >> 18 );
	hptSizeChunks = (u32)(HvCallHpt_getHptPages() >> 6 );
	hptLastChunk = hptFirstChunk + hptSizeChunks - 1;

	loadAreaLastChunk = loadAreaFirstChunk + loadAreaSize - 1;

	absAddrLo = loadAreaFirstChunk << 18;
	absAddrHi = loadAreaFirstChunk >> 14;

	printk( "Mapping load area - physical addr = 0, absolute addr = %08x%08x\n",
			absAddrHi, absAddrLo );
	printk( "Load area size %dK\n", loadAreaSize*256 );

	nextPhysChunk = 0;

	for ( absChunk = loadAreaFirstChunk; absChunk <= loadAreaLastChunk; ++absChunk ) {
		if ( ( absChunk < hptFirstChunk ) ||
		     ( absChunk > hptLastChunk ) ) {
			msChunks[nextPhysChunk] = absChunk;
			++nextPhysChunk;
		}
	}
	// Get absolute address of our HPT and remember it so
	// we won't map it to any physical address
	hptFirstChunk = (u32)(HvCallHpt_getHptAddress() >> 18 );
	hptSizeChunks = (u32)(HvCallHpt_getHptPages() >> 6 );
	hptLastChunk = hptFirstChunk + hptSizeChunks - 1;
	absAddrLo = hptFirstChunk << 18;
	absAddrHi = hptFirstChunk >> 14;

	printk( "HPT absolute addr = %08x%08x, size = %dK\n",
			absAddrHi, absAddrLo, hptSizeChunks*256 );

	// Determine if absolute memory has any
	// holes so that we can interpret the
	// access map we get back from the hypervisor
	// correctly.

	msVpd = (struct IoHriMainStoreSegment4 *)xMsVpd;
	holeStart = msVpd->nonInterleavedBlocksStartAdr;
	holeEnd   = msVpd->nonInterleavedBlocksEndAdr;
	holeSize = holeEnd - holeStart;

	if ( holeSize ) {
		holeStart = holeStart & 0x000fffffffffffff;
		holeStart = holeStart >> 18;
		holeFirstChunk = (u32)holeStart;
		holeSize = holeSize >> 18;
		holeSizeChunks = (u32)holeSize;
		printk( "Main store hole: start chunk = %0x, size = %0x chunks\n",
				holeFirstChunk, holeSizeChunks );
	}
	else {
		holeFirstChunk = 0xffffffff;
		holeSizeChunks = 0;
	}

	// Process the main store access map from the hypervisor
	// to build up our physical -> absolute translation table

	totalChunks = (u32)HvLpConfig_getMsChunks();

	if ((totalChunks-hptSizeChunks) > 16384) {
		panic("More than 4GB of memory assigned to this partition");
	}

	currChunk = 0;
	currDword = 0;
	moreChunks = totalChunks;

	while ( moreChunks ) {
		map = HvCallSm_get64BitsOfAccessMap( itLpNaca.xLpIndex,
						     currDword );
		thisChunk = currChunk;
		while ( map ) {
			chunkBit = map >> 63;
			map <<= 1;
			if ( chunkBit ) {
				--moreChunks;
				absChunk = thisChunk;
				if ( absChunk >= holeFirstChunk )
					absChunk += holeSizeChunks;
				if ( ( ( absChunk < hptFirstChunk ) ||
				       ( absChunk > hptLastChunk ) ) &&
				     ( ( absChunk < loadAreaFirstChunk ) ||
				       ( absChunk > loadAreaLastChunk ) ) ) {
//					printk( "Mapping physical = %0x to absolute %0x for 256K\n", nextPhysChunk << 18, absChunk << 18 );
					msChunks[nextPhysChunk] = absChunk;
					++nextPhysChunk;
				}
			}
			++thisChunk;
		}
		++currDword;
		currChunk += 64;
	}

	// main store size (in chunks) is
	//   totalChunks - hptSizeChunks
	// which should be equal to
	//   nextPhysChunk

	totalLpChunks = nextPhysChunk;

}

/*
 * Set up the variables that describe the cache line sizes
 * for this machine.
 */

static void __init setup_iSeries_cache_sizes(void)
{
	unsigned i,n;
	iSeries_icache_line_size = xIoHriProcessorVpd[0].xInstCacheOperandSize;
	iSeries_dcache_line_size = xIoHriProcessorVpd[0].xDataCacheOperandSize;
	iSeries_icache_lines_per_page = PAGE_SIZE / iSeries_icache_line_size;
	iSeries_dcache_lines_per_page = PAGE_SIZE / iSeries_dcache_line_size;
	i = iSeries_icache_line_size;
	n = 0;
	while ((i=(i/2))) ++n;
	iSeries_log_icache_line_size = n;
	i = iSeries_dcache_line_size;
	n = 0;
	while ((i=(i/2))) ++n;
	iSeries_log_dcache_line_size = n;
	printk( "D-cache line size = %d  (log = %d)\n",
			(unsigned)iSeries_dcache_line_size,
			(unsigned)iSeries_log_dcache_line_size );
	printk( "I-cache line size = %d  (log = %d)\n",
			(unsigned)iSeries_icache_line_size,
			(unsigned)iSeries_log_icache_line_size );

}


int piranha_simulator = 0;

/*
 * Document me.
 */
void __init
iSeries_setup_arch(void)
{
	void *	eventStack;
	u32	procFreq;
	u32	tbFreq;
//	u32	evStackContigReal;
//	u64	evStackReal;

	/* Setup the Lp Event Queue */

	/* Associate Lp Event Queue 0 with processor 0 */

//	HvCallEvent_setLpEventQueueInterruptProc( 0, 0 );

	/* Allocate a page for the Event Stack
	 * The hypervisor wants the absolute real address, so
	 * we subtract out the KERNELBASE and add in the
	 * absolute real address of the kernel load area
	 */

	eventStack = alloc_bootmem_pages( LpEventStackSize );

	memset( eventStack, 0, LpEventStackSize );

	/* Invoke the hypervisor to initialize the event stack */

	HvCallEvent_setLpEventStack( 0, eventStack, LpEventStackSize );

	/* Initialize fields in our Lp Event Queue */

	xItLpQueue.xHSlicEventStackPtr = 0;
	xItLpQueue.xSlicEventStackPtr = (char *)eventStack;
	xItLpQueue.xHSlicCurEventPtr = 0;
	xItLpQueue.xSlicCurEventPtr = (char *)eventStack;
	xItLpQueue.xHSlicLastValidEventPtr = 0;
	xItLpQueue.xSlicLastValidEventPtr = (char *)eventStack +
					(LpEventStackSize - LpEventMaxSize);
	xItLpQueue.xIndex = 0;

	if ( itLpNaca.xPirEnvironMode == 0 ) {
		printk("Running on Piranha simulator\n");
		piranha_simulator = 1;
	}
	// Compute processor frequency
	procFreq = ( 0x40000000 / (xIoHriProcessorVpd[0].xProcFreq / 1600) );
	procFreqHz = procFreq * 10000;
	procFreqMhz = procFreq / 100;
	procFreqMhzHundreths = procFreq - (procFreqMhz * 100 );

	// Compute time base frequency
	tbFreq = ( 0x40000000 / (xIoHriProcessorVpd[0].xTimeBaseFreq / 400) );
	tbFreqHz = tbFreq * 10000;
	tbFreqMhz = tbFreq / 100;
	tbFreqMhzHundreths = tbFreq - (tbFreqMhz * 100 );

	printk("Max  logical processors = %d\n",
			itVpdAreas.xSlicMaxLogicalProcs );
	printk("Max physical processors = %d\n",
			itVpdAreas.xSlicMaxPhysicalProcs );
	printk("Processor frequency = %lu.%02lu\n",
			procFreqMhz,
			procFreqMhzHundreths );
	printk("Time base frequency = %lu.%02lu\n",
			tbFreqMhz,
			tbFreqMhzHundreths );
	printk("Processor version = %x\n",
			xIoHriProcessorVpd[0].xPVR );

#ifdef CONFIG_PCI
	/* Initialize the flight recorder, global bus map and pci memory table */
	iSeries_pci_Initialize();

	/* Setup the PCI controller list */
	iSeries_build_hose_list();
#endif /* CONFIG_PCI         */
/*
	// copy the command line parameter from the primary VSP
	HvCallEvent_dmaToSp( cmd_line,
			     2*64*1024,
			     256,
			     HvLpDma_Direction_RemoteToLocal );

	mf_init();
	viopath_init();
*/
}

/*
 * int iSeries_show_percpuinfo()
 *
 * Description:
 *   This routine pretty-prints CPU information gathered from the VPD
 *   for use in /proc/cpuinfo
 *
 * Input(s):
 *  *buffer - Buffer into which CPU data is to be printed.
 *
 * Output(s):
 *  *buffer - Buffer with CPU data.
 *
 * Returns:
 *   The number of bytes copied into 'buffer' if OK, otherwise zero or less
 *   on error.
 */
static int
iSeries_show_percpuinfo(struct seq_file *m, int i)
{
	seq_printf(m, "clock\t\t: %lu.%02luMhz\n",
		procFreqMhz, procFreqMhzHundreths );
//	seq_printf(m, "  processor clock\t\t: %ldMHz\n",
//		((unsigned long)xIoHriProcessorVpd[0].xProcFreq)/1000000);
	seq_printf(m, "time base\t: %lu.%02luMHz\n",
		tbFreqMhz, tbFreqMhzHundreths );
//	seq_printf(m, "  time base freq\t\t: %ldMHz\n",
//		((unsigned long)xIoHriProcessorVpd[0].xTimeBaseFreq)/1000000);
	seq_printf(m, "i-cache\t\t: %d\n",
		iSeries_icache_line_size);
	seq_printf(m, "d-cache\t\t: %d\n",
		iSeries_dcache_line_size);

	return 0;
}

static int iSeries_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: iSeries Logical Partition\n");

	return 0;
}

#ifndef CONFIG_PCI
/*
 * Document me.
 * and Implement me.
 * If no Native I/O, do nothing routine.
 */
 void __init
iSeries_init_IRQ(void)
{
	return;
}
#endif

/*
 * Document me.
 * and Implement me.
 */
int
iSeries_get_irq(struct pt_regs *regs)
{
/*
	return (ppc4xx_pic_get_irq(regs));
*/
	/* -2 means ignore this interrupt */
	return -2;
}

/*
 * Document me.
 */
void
iSeries_restart(char *cmd)
{
	mf_reboot();
}

/*
 * Document me.
 */
void
iSeries_power_off(void)
{
	mf_powerOff();
}

/*
 * Document me.
 */
void
iSeries_halt(void)
{
	mf_powerOff();
}

/*
 * Nothing to do here.
 */
void __init
iSeries_time_init(void)
{
	/* Nothing to do */
}

/*
 * Set the RTC in the virtual service processor
 * This requires flowing LpEvents to the primary partition
 */
int iSeries_set_rtc_time(unsigned long time)
{
    mf_setRtcTime(time);
    return 0;
}

/*
 * Get the RTC from the virtual service processor
 * This requires flowing LpEvents to the primary partition
 */
unsigned long iSeries_get_rtc_time(void)
{
    /* XXX - Implement me */
    unsigned long time;
    mf_getRtcTime(&time);
    return (time);
}

/*
 * void __init iSeries_calibrate_decr()
 *
 * Description:
 *   This routine retrieves the internal processor frequency from the VPD,
 *   and sets up the kernel timer decrementer based on that value.
 *
 */

void __init
iSeries_calibrate_decr(void)
{
	u32	freq;
	u32	tbf;
	struct Paca * paca;

	/* Compute decrementer (and TB) frequency
	 * in cycles/sec
	 */

	tbf = xIoHriProcessorVpd[0].xTimeBaseFreq / 16;

	freq = 0x010000000;
	freq = freq / tbf;		/* cycles / usec */
	freq = freq * 1000000;		/* now in cycles/sec */

	/* Set the amount to refresh the decrementer by.  This
	 * is the number of decrementer ticks it takes for
	 * 1/HZ seconds.
	 */

	/* decrementer_count = freq / HZ;
	 * count_period_num = 1;
	 * count_period_den = freq; */

	if ( decr_overclock_set && !decr_overclock_proc0_set )
		decr_overclock_proc0 = decr_overclock;

	tb_ticks_per_jiffy = freq / HZ;
	paca = (struct Paca *)mfspr(SPRG1);
	paca->default_decr = tb_ticks_per_jiffy / decr_overclock_proc0;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);
}
void __init
iSeries_progress( char * st, unsigned short code )
{
	printk( "Progress: [%04x] - %s\n", (unsigned)code, st );
	if (code != 0xffff)
	    mf_displayProgress( code );
	else
	    mf_clearSrc();
}

#ifdef CONFIG_PCI
/*
 * unsigned int __init iSeries_build_hose_list()
 *
 * Description:
 *   This routine builds a list of the PCI host bridges that
 *   connect PCI buses either partially or fully owned by
 *   this guest partition
 *
 */
unsigned int __init iSeries_build_hose_list ( ) {
    struct pci_controller* hose;
    struct iSeries_hose_arch_data* hose_data;
    u64 hvRc;
    u16 hvbusnum;
    int LxBusNumber = 0;		/* Linux Bus number for grins */

    /* Check to make sure the device probing will work on this iSeries Release. */
    if(hvReleaseData.xVrmIndex !=3) {
	printk("PCI: iSeries Lpar and Linux native PCI I/O code is incompatible.\n");
	printk("PCI: A newer version of the Linux kernel is need for this iSeries release.\n");
	return 0;
    }


    for (hvbusnum = 0; hvbusnum < 256; hvbusnum++) {  /* All PCI buses which could be owned by this guest partition will be numbered by the hypervisor between 1 & 255 */
	hvRc = HvCallXm_testBus (hvbusnum);  /* Call the system hypervisor to query guest partition ownership status of this bus */
	if (hvRc == 0) {  /* This bus is partially/fully owned by this guest partition */
	    hose = (struct pci_controller*)pcibios_alloc_controller();  // Create the hose for this PCI bus
	    hose->first_busno = LxBusNumber;	/* This just for debug.   pcibios will */
	    hose->last_busno  = 0xff;		/* assign the bus numbers.             */
	    hose->ops = &iSeries_pci_ops;
	    /* Create the iSeries_arch_data for the hose and cache the HV bus number in it so that pci bios can build the global bus map */
	    hose_data = (struct iSeries_hose_arch_data *) alloc_bootmem(sizeof(struct iSeries_hose_arch_data));
	    memset(hose_data, 0, sizeof(*hose_data));
	    hose->arch_data = (void *) hose_data;
	    ((struct iSeries_hose_arch_data *)(hose->arch_data))->hvBusNumber = hvbusnum;
	    LxBusNumber += 1;			/* Keep track for debug */
	}
    }
    pci_assign_all_busses = 1;          /* Let Linux assign the bus numbers in pcibios_init */
    return 0;
}
#endif /* CONFIG_PCI         */

int iSeries_spread_lpevents( char * str )
{
	/* The parameter is the number of processors to share in processing lp events */
	unsigned long i;
	unsigned long val = simple_strtoul(str, NULL, 0 );
	if ( ( val > 0 ) && ( val <= maxPacas ) ) {
		for( i=1; i<val; ++i )
			xPaca[i].lpQueuePtr = xPaca[0].lpQueuePtr;
	}
	else
		printk("invalid spread_lpevents %ld\n", val);
	return 1;
}

int iSeries_decr_overclock_proc0( char * str )
{
	unsigned long val = simple_strtoul(str, NULL, 0 );
	if ( ( val >= 1 ) && ( val <= 48 ) ) {
		decr_overclock_proc0_set = 1;
		decr_overclock_proc0 = val;
		printk("proc 0 decrementer overclock factor of %ld\n", val);
	}
	else {
		printk("invalid proc 0 decrementer overclock factor of %ld\n", val);
	}
	return 1;
}

int iSeries_decr_overclock( char * str )
{
	unsigned long val = simple_strtoul( str, NULL, 0 );
	if ( ( val >= 1 ) && ( val <= 48 ) ) {
		decr_overclock_set = 1;
		decr_overclock = val;
		printk("decrementer overclock factor of %ld\n", val);
	}
	else {
		printk("invalid decrementer overclock factor of %ld\n", val);
	}
	return 1;
}

__setup("spread_lpevents=", iSeries_spread_lpevents );
__setup("decr_overclock_proc0=", iSeries_decr_overclock_proc0 );
__setup("decr_overclock=", iSeries_decr_overclock );

