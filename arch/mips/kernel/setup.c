/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995  Linus Torvalds
 * Copyright (C) 1995, 1996, 1997, 1998  Ralf Baechle
 * Copyright (C) 1996  Stoned Elipot
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/utsname.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/bootmem.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <linux/ide.h>
#ifdef CONFIG_RTC
#include <linux/timex.h>
#endif

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/io.h>
#include <asm/stackframe.h>
#include <asm/system.h>
#include <asm/cpu.h>
#ifdef CONFIG_SGI_IP22
#include <asm/sgialib.h>
#endif

struct mips_cpuinfo boot_cpu_data = { NULL, NULL, 0 };

/*
 * Not all of the MIPS CPUs have the "wait" instruction available. Moreover,
 * the implementation of the "wait" feature differs between CPU families. This
 * points to the function that implements CPU specific wait. 
 * The wait instruction stops the pipeline and reduces the power consumption of
 * the CPU very much.
 */
void (*cpu_wait)(void) = NULL;

/*
 * Do we have a cyclecounter available?
 */
char cyclecounter_available;

/*
 * There are several bus types available for MIPS machines.  "RISC PC"
 * type machines have ISA, EISA, VLB or PCI available, DECstations
 * have Turbochannel or Q-Bus, SGI has GIO, there are lots of VME
 * boxes ...
 * This flag is set if a EISA slots are available.
 */
int EISA_bus = 0;

struct screen_info screen_info;

#ifdef CONFIG_BLK_DEV_FD
extern struct fd_ops no_fd_ops;
struct fd_ops *fd_ops;
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
extern struct ide_ops no_ide_ops;
struct ide_ops *ide_ops;
#endif

extern struct rtc_ops no_rtc_ops;
struct rtc_ops *rtc_ops;

#ifdef CONFIG_PC_KEYB
extern struct kbd_ops no_kbd_ops;
struct kbd_ops *kbd_ops;
#endif

/*
 * Setup information
 *
 * These are initialized so they are in the .data section
 */
unsigned long mips_memory_upper = KSEG0; /* this is set by kernel_entry() */
unsigned long mips_cputype = CPU_UNKNOWN;
unsigned long mips_machtype = MACH_UNKNOWN;
unsigned long mips_machgroup = MACH_GROUP_UNKNOWN;

unsigned char aux_device_present;
extern int _end;

static char command_line[CL_SIZE] = { 0, };
       char saved_command_line[CL_SIZE];
extern char arcs_cmdline[CL_SIZE];

/*
 * The board specific setup routine sets irq_setup to point to a board
 * specific setup routine.
 */
void (*irq_setup)(void);

/*
 * mips_io_port_base is the begin of the address space to which x86 style
 * I/O ports are mapped.
 */
unsigned long mips_io_port_base;

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is is mapped
 * for the processor.
 */
unsigned long isa_slot_offset;

extern void sgi_sysinit(void);
extern void SetUpBootInfo(void);
extern void loadmmu(void);
extern asmlinkage void start_kernel(void);
extern int prom_init(int, char **, char **, int *);

/*
 * Probe whether cpu has config register by trying to play with
 * alternate cache bit and see whether it matters.
 * It's used by cpu_probe to distinguish between R3000A and R3081.
 */
static inline int cpu_has_confreg(void)
{
#ifdef CONFIG_CPU_R3000
	extern unsigned long r3k_cache_size(unsigned long);
	unsigned long size1, size2; 
	unsigned long cfg = read_32bit_cp0_register(CP0_CONF);

	size1 = r3k_cache_size(ST0_DE);
	write_32bit_cp0_register(CP0_CONF, cfg^CONF_AC);
	size2 = r3k_cache_size(ST0_DE);
	write_32bit_cp0_register(CP0_CONF, cfg);
	return size1 != size2;
#else
	return 0;
#endif
}

static inline void cpu_probe(void)
{
	unsigned int prid = read_32bit_cp0_register(CP0_PRID);
	switch(prid & 0xff00) {
	case PRID_IMP_R2000:
		mips_cputype = CPU_R2000;
		break;
	case PRID_IMP_R3000:
		if((prid & 0xff) == PRID_REV_R3000A)
			if(cpu_has_confreg())
				mips_cputype = CPU_R3081E;
			else
				mips_cputype = CPU_R3000A;
		else
			 mips_cputype = CPU_R3000;
		break;
	case PRID_IMP_R4000:
		if((prid & 0xff) == PRID_REV_R4400)
			mips_cputype = CPU_R4400SC;
		else
			mips_cputype = CPU_R4000SC;
		break;
	case PRID_IMP_R4600:
		mips_cputype = CPU_R4600;
		break;
	case PRID_IMP_R4650:
		mips_cputype = CPU_R4650;
		break;
	case PRID_IMP_R4700:
		mips_cputype = CPU_R4700;
		break;
	case PRID_IMP_R5000:
		mips_cputype = CPU_R5000;
		break;
	case PRID_IMP_NEVADA:
		mips_cputype = CPU_NEVADA;
		break;
	case PRID_IMP_R6000:
		mips_cputype = CPU_R6000;
		break;
	case PRID_IMP_R6000A:
		mips_cputype = CPU_R6000A;
		break;
	case PRID_IMP_R8000:
		mips_cputype = CPU_R8000;
		break;
	case PRID_IMP_R10000:
		mips_cputype = CPU_R10000;
		break;
	case PRID_IMP_RM7000:
		mips_cputype = CPU_R5000;
		break;
	default:
		mips_cputype = CPU_UNKNOWN;
	}
}

asmlinkage void __init init_arch(int argc, char **argv, char **envp, int *prom_vec)
{
	unsigned int s;

	/* Determine which MIPS variant we are running on. */
	cpu_probe();

	prom_init(argc, argv, envp, prom_vec);

#ifdef CONFIG_SGI_IP22
	sgi_sysinit();
#endif
#ifdef CONFIG_COBALT_MICRO_SERVER
	SetUpBootInfo();
#endif

	/*
	 * Determine the mmu/cache attached to this machine,
	 * then flush the tlb and caches.  On the r4xx0
	 * variants this also sets CP0_WIRED to zero.
	 */
	loadmmu();

	/* Disable coprocessors */
	s = read_32bit_cp0_register(CP0_STATUS);
	s &= ~(ST0_CU1|ST0_CU2|ST0_CU3|ST0_KX|ST0_SX);
	s |= ST0_CU0;
	write_32bit_cp0_register(CP0_STATUS, s);

	/*
	 * Main should never return here, but
	 * just in case, we know what happens.
	 */
	for(;;)
		start_kernel();
}

static void __init default_irq_setup(void)
{
	panic("Unknown machtype in init_IRQ");
}

void __init setup_arch(char **cmdline_p)
{
	void baget_setup(void);
	void cobalt_setup(void);
	void decstation_setup(void);
	void deskstation_setup(void);
	void jazz_setup(void);
	void sni_rm200_pci_setup(void);
	void sgi_setup(void);
	void ddb_setup(void);
	void orion_setup(void);

	/* Save defaults for configuration-dependent routines.  */
	irq_setup = default_irq_setup;

#ifdef CONFIG_BLK_DEV_FD
	fd_ops = &no_fd_ops;
#endif

#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &no_ide_ops;
#endif

#ifdef CONFIG_PC_KEYB
	kbd_ops = &no_kbd_ops;
#endif
	
	rtc_ops = &no_rtc_ops;

	switch(mips_machgroup)
	{
#ifdef CONFIG_BAGET_MIPS
	case MACH_GROUP_BAGET: 
		baget_setup();
		break;
#endif
#ifdef CONFIG_COBALT_MICRO_SERVER
	case MACH_GROUP_COBALT:
		cobalt_setup();
		break;
#endif
#ifdef CONFIG_DECSTATION
	case MACH_GROUP_DEC:
		decstation_setup();
		break;
#endif
#ifdef CONFIG_MIPS_JAZZ
	case MACH_GROUP_JAZZ:
		jazz_setup();
		break;
#endif
#ifdef CONFIG_SGI_IP22
	/* As of now this is only IP22.  */
	case MACH_GROUP_SGI:
		sgi_setup();
		break;
#endif
#ifdef CONFIG_SNI_RM200_PCI
	case MACH_GROUP_SNI_RM:
		sni_rm200_pci_setup();
		break;
#endif
#ifdef CONFIG_DDB5074
	case MACH_GROUP_NEC_DDB:
		ddb_setup();
		break;
#endif
#ifdef CONFIG_ORION
	case MACH_GROUP_ORION:
		orion_setup();
		break;
#endif
	default:
		panic("Unsupported architecture");
	}

        strncpy (command_line, arcs_cmdline, CL_SIZE);
	memcpy(saved_command_line, command_line, CL_SIZE);
	saved_command_line[CL_SIZE-1] = '\0';

	*cmdline_p = command_line;

#ifdef CONFIG_BLK_DEV_INITRD
#error "Fixme, I'm broken."
	tmp = (((unsigned long)&_end + PAGE_SIZE-1) & PAGE_MASK) - 8;
	if (tmp < (unsigned long)&_end)
		tmp += PAGE_SIZE;
	initrd_header = (unsigned long *)tmp;
	if (initrd_header[0] == 0x494E5244) {
		initrd_start = (unsigned long)&initrd_header[2];
		initrd_end = initrd_start + initrd_header[1];
		initrd_below_start_ok = 1;
		if (initrd_end > memory_end) {
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_end,memory_end);
			initrd_start = 0;
		} else
			*memory_start_p = initrd_end;
	}
#endif

	paging_init();
}

void r3081_wait(void) 
{
	unsigned long cfg = read_32bit_cp0_register(CP0_CONF);
	write_32bit_cp0_register(CP0_CONF, cfg|CONF_HALT);
}

void r4k_wait(void)
{
	__asm__(".set\tmips3\n\t"
		"wait\n\t"
		".set\tmips0");
}
