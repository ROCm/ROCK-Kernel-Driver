/*
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Vijay Chander(vijay@engr.sgi.com)
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

#include <asm/sn/mmzone_sn1.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/machvec.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm-ia64/sn/arch.h>
#include <asm-ia64/sn/addrs.h>


/*
 * This is the address of the RRegs in the HSpace of the global
 * master.  It is used by a hack in serial.c (serial_[in|out],
 * printk.c (early_printk), and kdb_io.c to put console output on that
 * node's Bedrock UART.  It is initialized here to 0, so that
 * early_printk won't try to access the UART before
 * master_node_bedrock_address is properly calculated.
 */
u64 master_node_bedrock_address = 0UL;

static void sn_fix_ivt_for_partitioned_system(void);


/*
 * The format of "screen_info" is strange, and due to early i386-setup
 * code. This is just enough to make the console code think we're on a
 * VGA color display.
 */
struct screen_info sn1_screen_info = {
	orig_x:			 0,
	orig_y:			 0,
	orig_video_mode:	 3,
	orig_video_cols:	80,
	orig_video_ega_bx:	 3,
	orig_video_lines:	25,
	orig_video_isVGA:	 1,
	orig_video_points:	16
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

unsigned long
sn1_map_nr (unsigned long addr)
{
#ifdef CONFIG_DISCONTIGMEM
	return MAP_NR_SN1(addr);
#else
	return MAP_NR_DENSE(addr);
#endif
}

#if defined(BRINGUP) && defined(CONFIG_IA64_EARLY_PRINTK)
void __init
early_sn1_setup(void)
{
	master_node_bedrock_address = 
		(u64)REMOTE_HSPEC_ADDR(get_nasid(), 0);
	printk("early_sn1_setup: setting master_node_bedrock_address to 0x%lx\n", master_node_bedrock_address);
}
#endif /* BRINGUP && CONFIG_IA64_EARLY_PRINTK */

void __init
sn1_setup(char **cmdline_p)
{
#if defined(CONFIG_SERIAL) && !defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
	struct serial_struct req;
#endif

	MAX_DMA_ADDRESS = PAGE_OFFSET + 0x10000000000UL;
	master_node_bedrock_address = 
		(u64)REMOTE_HSPEC_ADDR(get_nasid(), 0);
	printk("sn1_setup: setting master_node_bedrock_address to 0x%lx\n",
		   master_node_bedrock_address);

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
	req.iomem_base = (u8 *)(master_node_bedrock_address + 0x80);
	req.iomem_reg_shift = 3;
	req.type = 0;
	req.xmit_fifo_size = 0;
	req.custom_divisor = 0;
	req.closing_wait = 0;
	early_serial_setup(&req);
#endif /* CONFIG_SERIAL && !CONFIG_SERIAL_SGI_L1_PROTOCOL */

	ROOT_DEV = to_kdev_t(0x0301);		/* default to first IDE drive */
	sn_fix_ivt_for_partitioned_system();

#ifdef CONFIG_SMP
	init_smp_config();
#endif
	screen_info = sn1_screen_info;
}


/* 
 * sn_fix_ivt_for_partitioned_system
 *
 * This is an ugly hack that is needed for partitioned systems.
 *
 * On a partitioned system, most partitions do NOT have a physical address 0.
 * Unfortunately, the exception handling code in ivt.S has a couple of physical
 * addresses of kernel structures hardcoded into "movl" instructions.
 * These addresses are correct on partition 0 only. On all other partitions,
 * the addresses must be changed to reference the correct address.
 *
 * This routine scans the ivt code and replaces the hardcoded addresses with 
 * the correct address.
 *
 * Note that we could have made the ivt.S code dynamically determine the correct
 * address but this would add code to performance critical pathes. This option
 * was rejected.
 */

#define TEMP_mlx	4		/* template type that contains movl instruction */
#define TEMP_mlX	5		/* template type that contains movl instruction */

typedef union {				/* Instruction encoding for movl instruction */
	struct {
		unsigned long	qp:6;
		unsigned long	r1:7;
		unsigned long	imm7b:7;
		unsigned long	vc:1;
		unsigned long	ic:1;
		unsigned long	imm5c:5;
		unsigned long	imm9d:9;
		unsigned long	i:1;
		unsigned long	op:4;
		unsigned long	fill:23;
	} b;
	unsigned long	l;
} movl_instruction_t;

#define MOVL_OPCODE	6
#define MOVL_ARG(a,b)	(((long)a.i<<63) | ((long)b<<22) | ((long)a.ic<<21) | \
			((long)a.imm5c<<16) | ((long)a.imm9d<<7) | ((long)a.imm7b))

typedef struct {				/* Instruction bundle */
	unsigned long template:5;
	unsigned long ins2:41;
	unsigned long ins1l:18;
	unsigned long ins1u:23;
	unsigned long ins0:41;
} instruction_bundle_t;


static void __init
sn_fix_ivt_for_partitioned_system(void)
{
	extern int		ia64_ivt;
	instruction_bundle_t	*p, *pend;
	movl_instruction_t	ins0, ins1, ins2;
	long			new_ins1, phys_offset;
	unsigned long		val;

	/*
	 * Setup to scan the ivt code.
	 */
	p = (instruction_bundle_t*)&ia64_ivt;
	pend = p + 0x8000/sizeof(instruction_bundle_t);
	phys_offset = __pa(p) & ~0x1ffffffffUL;

	/*
	 * Hunt for movl instructions that contain the node 0 physical address 
	 * of "SWAPPER_PGD_ADDR".  These addresses must be relocated to reference the 
	 * actual node that the kernel is loaded on.
	 */
	for (; p < pend; p++) {
		if (p->template != TEMP_mlx && p->template != TEMP_mlX)
			continue;
		ins0.l =  p->ins0;
		if (ins0.b.op != MOVL_OPCODE)
			continue;
		ins1.l =  ((long)p->ins1u<<18) | p->ins1l;
		ins2.l =  p->ins2;
		val = MOVL_ARG(ins0.b, ins1.l);

		/*
		 * Test for correct address. SWAPPER_PGD_ADDR will
		 * always be a node 0 virtual address. Note that we cant
		 * use the __pa or __va macros here since they may contain
		 * debug code that gets fooled here.
		 */
		if ((PAGE_OFFSET | val)  != SWAPPER_PGD_ADDR)
			continue;

		/*
		 * We found an instruction that needs to be fixed. The following
		 * inserts the NASID of the ivt into the movl instruction.
		 */
		new_ins1 = ins1.l | (phys_offset>>22);
		p->ins1l = new_ins1 & 0x3ffff;
		p->ins1u = (new_ins1>>18) & 0x7fffff;
		ia64_fc(p);
	}

	/*
	 * Do necessary serialization.
	 */
	ia64_sync_i();
	ia64_srlz_i();

}

int
IS_RUNNING_ON_SIMULATOR(void)
{
#ifdef CONFIG_IA64_SGI_SN1_SIM
	long sn;
	asm("mov %0=cpuid[%1]" : "=r"(sn) : "r"(2));
	return(sn == SNMAGIC);
#else
	return(0);
#endif
}
