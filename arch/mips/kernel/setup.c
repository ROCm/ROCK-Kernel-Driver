/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 Linus Torvalds
 * Copyright (C) 1995 Waldorf Electronics
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 01, 02, 03  Ralf Baechle
 * Copyright (C) 1996 Stoned Elipot
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000 2001, 2002  Maciej W. Rozycki
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/utsname.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/bootmem.h>
#include <linux/initrd.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/root_dev.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/sections.h>
#include <asm/system.h>

struct cpuinfo_mips cpu_data[NR_CPUS];

#ifdef CONFIG_VT
struct screen_info screen_info;
#endif

#if defined(CONFIG_BLK_DEV_FD) || defined(CONFIG_BLK_DEV_FD_MODULE)
extern struct fd_ops no_fd_ops;
struct fd_ops *fd_ops;
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
extern struct ide_ops no_ide_ops;
struct ide_ops *ide_ops;
#endif

extern void * __rd_start, * __rd_end;

extern struct rtc_ops no_rtc_ops;
struct rtc_ops *rtc_ops;

/*
 * Setup information
 *
 * These are initialized so they are in the .data section
 */
unsigned long mips_machtype = MACH_UNKNOWN;
unsigned long mips_machgroup = MACH_GROUP_UNKNOWN;

struct boot_mem_map boot_mem_map;

static char command_line[CL_SIZE];
       char saved_command_line[CL_SIZE];
extern char arcs_cmdline[CL_SIZE];

/*
 * mips_io_port_base is the begin of the address space to which x86 style
 * I/O ports are mapped.
 */
const unsigned long mips_io_port_base = -1;
EXPORT_SYMBOL(mips_io_port_base);

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is mapped
 * for the processor.
 */
unsigned long isa_slot_offset;
EXPORT_SYMBOL(isa_slot_offset);

extern void SetUpBootInfo(void);
extern void load_mmu(void);
extern ATTRIB_NORET asmlinkage void start_kernel(void);
extern void prom_init(int, char **, char **, int *);

static struct resource code_resource = { "Kernel code" };
static struct resource data_resource = { "Kernel data" };

asmlinkage void __init init_arch(int argc, char **argv, char **envp,
	int *prom_vec)
{
	/* Determine which MIPS variant we are running on. */
	cpu_probe();

	prom_init(argc, argv, envp, prom_vec);

	cpu_report();

	/*
	 * Determine the mmu/cache attached to this machine, then flush the
	 * tlb and caches.  On the r4xx0 variants this also sets CP0_WIRED to
	 * zero.
	 */
	load_mmu();

#ifdef CONFIG_MIPS32
	/* Disable coprocessors and set FPU for 16/32 FPR register model */
	clear_c0_status(ST0_CU1|ST0_CU2|ST0_CU3|ST0_KX|ST0_SX|ST0_FR);
	set_c0_status(ST0_CU0);
#endif
#ifdef CONFIG_MIPS64
	/*
	 * On IP27, I am seeing the TS bit set when the kernel is loaded.
	 * Maybe because the kernel is in ckseg0 and not xkphys? Clear it
	 * anyway ...
	 */
	clear_c0_status(ST0_BEV|ST0_TS|ST0_CU1|ST0_CU2|ST0_CU3);
	set_c0_status(ST0_CU0|ST0_KX|ST0_SX|ST0_FR);
#endif

	start_kernel();
}

void __init add_memory_region(phys_t start, phys_t size,
			      long type)
{
	int x = boot_mem_map.nr_map;

	if (x == BOOT_MEM_MAP_MAX) {
		printk("Ooops! Too many entries in the memory map!\n");
		return;
	}

	boot_mem_map.map[x].addr = start;
	boot_mem_map.map[x].size = size;
	boot_mem_map.map[x].type = type;
	boot_mem_map.nr_map++;
}

static void __init print_memory_map(void)
{
	int i;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		printk(" memory: %0*Lx @ %0*Lx ",
		       sizeof(long) * 2, (u64) boot_mem_map.map[i].size,
		       sizeof(long) * 2, (u64) boot_mem_map.map[i].addr);

		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
			printk("(usable)\n");
			break;
		case BOOT_MEM_ROM_DATA:
			printk("(ROM data)\n");
			break;
		case BOOT_MEM_RESERVED:
			printk("(reserved)\n");
			break;
		default:
			printk("type %lu\n", boot_mem_map.map[i].type);
			break;
		}
	}
}

static inline void parse_cmdline_early(void)
{
	char c = ' ', *to = command_line, *from = saved_command_line;
	unsigned long start_at, mem_size;
	int len = 0;
	int usermem = 0;

	printk("Determined physical RAM map:\n");
	print_memory_map();

	for (;;) {
		/*
		 * "mem=XXX[kKmM]" defines a memory region from
		 * 0 to <XXX>, overriding the determined size.
		 * "mem=XXX[KkmM]@YYY[KkmM]" defines a memory region from
		 * <YYY> to <YYY>+<XXX>, overriding the determined size.
		 */
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			if (to != command_line)
				to--;
			/*
			 * If a user specifies memory size, we
			 * blow away any automatically generated
			 * size.
			 */
			if (usermem == 0) {
				boot_mem_map.nr_map = 0;
				usermem = 1;
			}
			mem_size = memparse(from + 4, &from);
			if (*from == '@')
				start_at = memparse(from + 1, &from);
			else
				start_at = 0;
			add_memory_region(start_at, mem_size, BOOT_MEM_RAM);
		}
		c = *(from++);
		if (!c)
			break;
		if (CL_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';

	if (usermem) {
		printk("User-defined physical RAM map:\n");
		print_memory_map();
	}
}


#define PFN_UP(x)	(((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((x) << PAGE_SHIFT)

#define MAXMEM		HIGHMEM_START
#define MAXMEM_PFN	PFN_DOWN(MAXMEM)

static inline void bootmem_init(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long tmp;
	unsigned long *initrd_header;
#endif
	unsigned long bootmap_size;
	unsigned long start_pfn, max_low_pfn, first_usable_pfn;
	int i;

#ifdef CONFIG_BLK_DEV_INITRD
	tmp = (((unsigned long)&_end + PAGE_SIZE-1) & PAGE_MASK) - 8;
	if (tmp < (unsigned long)&_end)
		tmp += PAGE_SIZE;
	initrd_header = (unsigned long *)tmp;
	if (initrd_header[0] == 0x494E5244) {
		initrd_start = (unsigned long)&initrd_header[2];
		initrd_end = initrd_start + initrd_header[1];
	}
	start_pfn = PFN_UP(CPHYSADDR((&_end)+(initrd_end - initrd_start) + PAGE_SIZE));
#else
	/*
	 * Partially used pages are not usable - thus
	 * we are rounding upwards.
	 */
	start_pfn = PFN_UP(CPHYSADDR(&_end));
#endif	/* CONFIG_BLK_DEV_INITRD */

#ifndef CONFIG_SGI_IP27
	/* Find the highest page frame number we have available.  */
	max_pfn = 0;
	first_usable_pfn = -1UL;
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long start, end;

		if (boot_mem_map.map[i].type != BOOT_MEM_RAM)
			continue;

		start = PFN_UP(boot_mem_map.map[i].addr);
		end = PFN_DOWN(boot_mem_map.map[i].addr
		      + boot_mem_map.map[i].size);

		if (start >= end)
			continue;
		if (end > max_pfn)
			max_pfn = end;
		if (start < first_usable_pfn) {
			if (start > start_pfn) {
				first_usable_pfn = start;
			} else if (end > start_pfn) {
				first_usable_pfn = start_pfn;
			}
		}
	}

	/*
	 * Determine low and high memory ranges
	 */
	max_low_pfn = max_pfn;
	if (max_low_pfn > MAXMEM_PFN) {
		max_low_pfn = MAXMEM_PFN;
#ifndef CONFIG_HIGHMEM
		/* Maximum memory usable is what is directly addressable */
		printk(KERN_WARNING "Warning only %ldMB will be used.\n",
		       MAXMEM >> 20);
		printk(KERN_WARNING "Use a HIGHMEM enabled kernel.\n");
#endif
	}

#ifdef CONFIG_HIGHMEM
	/*
	 * Crude, we really should make a better attempt at detecting
	 * highstart_pfn
	 */
	highstart_pfn = highend_pfn = max_pfn;
	if (max_pfn > MAXMEM_PFN) {
		highstart_pfn = MAXMEM_PFN;
		printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		       (highend_pfn - highstart_pfn) >> (20 - PAGE_SHIFT));
	}
#endif

	/* Initialize the boot-time allocator with low memory only.  */
	bootmap_size = init_bootmem(first_usable_pfn, max_low_pfn);

	/*
	 * Register fully available low RAM pages with the bootmem allocator.
	 */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long curr_pfn, last_pfn, size;

		/*
		 * Reserve usable memory.
		 */
		if (boot_mem_map.map[i].type != BOOT_MEM_RAM)
			continue;

		/*
		 * We are rounding up the start address of usable memory:
		 */
		curr_pfn = PFN_UP(boot_mem_map.map[i].addr);
		if (curr_pfn >= max_low_pfn)
			continue;
		if (curr_pfn < start_pfn)
			curr_pfn = start_pfn;

		/*
		 * ... and at the end of the usable range downwards:
		 */
		last_pfn = PFN_DOWN(boot_mem_map.map[i].addr
				    + boot_mem_map.map[i].size);

		if (last_pfn > max_low_pfn)
			last_pfn = max_low_pfn;

		/*
		 * Only register lowmem part of lowmem segment with bootmem.
		 */
		size = last_pfn - curr_pfn;
		if (curr_pfn > PFN_DOWN(HIGHMEM_START))
			continue;
		if (curr_pfn + size - 1 > PFN_DOWN(HIGHMEM_START))
			size = PFN_DOWN(HIGHMEM_START) - curr_pfn;
		if (!size)
			continue;

		/*
		 * ... finally, did all the rounding and playing
		 * around just make the area go away?
		 */
		if (last_pfn <= curr_pfn)
			continue;

		/* Register lowmem ranges */
		free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(size));
	}

	/* Reserve the bootmap memory.  */
	reserve_bootmem(PFN_PHYS(first_usable_pfn), bootmap_size);
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	/* Board specific code should have set up initrd_start and initrd_end */
	ROOT_DEV = Root_RAM0;
	if (&__rd_start != &__rd_end) {
		initrd_start = (unsigned long)&__rd_start;
		initrd_end = (unsigned long)&__rd_end;
	}
	initrd_below_start_ok = 1;
	if (initrd_start) {
		unsigned long initrd_size = ((unsigned char *)initrd_end) - ((unsigned char *)initrd_start);
		printk("Initial ramdisk at: 0x%p (%lu bytes)\n",
		       (void *)initrd_start,
		       initrd_size);
/* FIXME: is this right? */
#ifndef CONFIG_SGI_IP27
		if (CPHYSADDR(initrd_end) > PFN_PHYS(max_low_pfn)) {
			printk("initrd extends beyond end of memory "
			       "(0x%0*Lx > 0x%0*Lx)\ndisabling initrd\n",
			       sizeof(long) * 2, CPHYSADDR(initrd_end),
			       sizeof(long) * 2, PFN_PHYS(max_low_pfn));
			initrd_start = initrd_end = 0;
		}
#endif /* !CONFIG_SGI_IP27 */
	}
#endif /* CONFIG_BLK_DEV_INITRD  */
}

static inline void resource_init(void)
{
	int i;

	code_resource.start = virt_to_phys(&_text);
	code_resource.end = virt_to_phys(&_etext) - 1;
	data_resource.start = virt_to_phys(&_etext);
	data_resource.end = virt_to_phys(&_edata) - 1;

	/*
	 * Request address space for all standard RAM.
	 */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		struct resource *res;
		unsigned long start, end;

		start = boot_mem_map.map[i].addr;
		end = boot_mem_map.map[i].addr + boot_mem_map.map[i].size - 1;
		if (start >= MAXMEM)
			continue;
		if (end >= MAXMEM)
			end = MAXMEM - 1;

		res = alloc_bootmem(sizeof(struct resource));
		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
		case BOOT_MEM_ROM_DATA:
			res->name = "System RAM";
			break;
		case BOOT_MEM_RESERVED:
		default:
			res->name = "reserved";
		}

		res->start = start;
		res->end = end;

		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		request_resource(&iomem_resource, res);

		/*
		 *  We don't know which RAM region contains kernel data,
		 *  so we try it repeatedly and let the resource manager
		 *  test it.
		 */
		request_resource(res, &code_resource);
		request_resource(res, &data_resource);
	}
}

#undef PFN_UP
#undef PFN_DOWN
#undef PFN_PHYS

#undef MAXMEM
#undef MAXMEM_PFN

void __init setup_arch(char **cmdline_p)
{
	extern void atlas_setup(void);
	extern void baget_setup(void);
	extern void cobalt_setup(void);
	extern void lasat_setup(void);
	extern void ddb_setup(void);
	extern void decstation_setup(void);
	extern void deskstation_setup(void);
	extern void jazz_setup(void);
	extern void sni_rm200_pci_setup(void);
	extern void ip22_setup(void);
	extern void ip27_setup(void);
	extern void ip32_setup(void);
	extern void ev96100_setup(void);
	extern void malta_setup(void);
	extern void sead_setup(void);
	extern void ikos_setup(void);
	extern void momenco_ocelot_setup(void);
	extern void momenco_ocelot_g_setup(void);
	extern void momenco_ocelot_c_setup(void);
	extern void nec_osprey_setup(void);
	extern void nec_eagle_setup(void);
	extern void zao_capcella_setup(void);
	extern void victor_mpc30x_setup(void);
	extern void ibm_workpad_setup(void);
	extern void casio_e55_setup(void);
	extern void jmr3927_setup(void);
	extern void it8172_setup(void);
	extern void swarm_setup(void);
	extern void hp_setup(void);
	extern void au1x00_setup(void);
	extern void frame_info_init(void);

	frame_info_init();

#ifdef CONFIG_BLK_DEV_FD
	fd_ops = &no_fd_ops;
#endif

#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &no_ide_ops;
#endif

	rtc_ops = &no_rtc_ops;

	switch (mips_machgroup) {
#ifdef CONFIG_BAGET_MIPS
	case MACH_GROUP_BAGET:
		baget_setup();
		break;
#endif
#ifdef CONFIG_MIPS_COBALT
	case MACH_GROUP_COBALT:
		cobalt_setup();
		break;
#endif
#ifdef CONFIG_DECSTATION
	case MACH_GROUP_DEC:
		decstation_setup();
		break;
#endif
#ifdef CONFIG_MIPS_ATLAS
	case MACH_GROUP_UNKNOWN:
		atlas_setup();
		break;
#endif
#ifdef CONFIG_MIPS_JAZZ
	case MACH_GROUP_JAZZ:
		jazz_setup();
		break;
#endif
#ifdef CONFIG_MIPS_MALTA
	case MACH_GROUP_UNKNOWN:
		malta_setup();
		break;
#endif
#ifdef CONFIG_MOMENCO_OCELOT
	case MACH_GROUP_MOMENCO:
		momenco_ocelot_setup();
		break;
#endif
#ifdef CONFIG_MOMENCO_OCELOT_G
	case MACH_GROUP_MOMENCO:
		momenco_ocelot_g_setup();
		break;
#endif
#ifdef CONFIG_MOMENCO_OCELOT_C
	case MACH_GROUP_MOMENCO:
		momenco_ocelot_c_setup();
		break;
#endif
#ifdef CONFIG_MIPS_SEAD
	case MACH_GROUP_UNKNOWN:
		sead_setup();
		break;
#endif
#ifdef CONFIG_SGI_IP22
	/* As of now this is only IP22.  */
	case MACH_GROUP_SGI:
		ip22_setup();
		break;
#endif
#ifdef CONFIG_SGI_IP27
	case MACH_GROUP_SGI:
		ip27_setup();
		break;
#endif
#ifdef CONFIG_SGI_IP32
	case MACH_GROUP_SGI:
		ip32_setup();
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
#ifdef CONFIG_DDB5476
       case MACH_GROUP_NEC_DDB:
               ddb_setup();
               break;
#endif
#ifdef CONFIG_DDB5477
       case MACH_GROUP_NEC_DDB:
               ddb_setup();
               break;
#endif
#ifdef CONFIG_CPU_VR41XX
	case MACH_GROUP_NEC_VR41XX:
		switch (mips_machtype) {
#ifdef CONFIG_NEC_OSPREY
		case MACH_NEC_OSPREY:
			nec_osprey_setup();
			break;
#endif
#ifdef CONFIG_NEC_EAGLE
		case MACH_NEC_EAGLE:
			nec_eagle_setup();
			break;
#endif
#ifdef CONFIG_ZAO_CAPCELLA
		case MACH_ZAO_CAPCELLA:
			zao_capcella_setup();
			break;
#endif
#ifdef CONFIG_VICTOR_MPC30X
		case MACH_VICTOR_MPC30X:
			victor_mpc30x_setup();
			break;
#endif
#ifdef CONFIG_IBM_WORKPAD
		case MACH_IBM_WORKPAD:
			ibm_workpad_setup();
			break;
#endif
#ifdef CONFIG_CASIO_E55
		case MACH_CASIO_E55:
			casio_e55_setup();
			break;
#endif
#ifdef CONFIG_TANBAC_TB0229
		case MACH_TANBAC_TB0229:
			tanbac_tb0229_setup();
			break;
#endif
		}
		break;
#endif
#ifdef CONFIG_MIPS_EV96100
	case MACH_GROUP_GALILEO:
		ev96100_setup();
		break;
#endif
#ifdef CONFIG_MIPS_EV64120
	case MACH_GROUP_GALILEO:
		ev64120_setup();
		break;
#endif
#if defined(CONFIG_MIPS_IVR) || defined(CONFIG_MIPS_ITE8172)
	case  MACH_GROUP_ITE:
	case  MACH_GROUP_GLOBESPAN:
		it8172_setup();
		break;
#endif
#ifdef CONFIG_LASAT
        case MACH_GROUP_LASAT:
                lasat_setup();
                break;
#endif
#ifdef CONFIG_SOC_AU1X00
	case MACH_GROUP_ALCHEMY:
		au1x00_setup();
		break;
#endif
#ifdef CONFIG_TOSHIBA_JMR3927
	case MACH_GROUP_TOSHIBA:
		jmr3927_setup();
		break;
#endif
#ifdef CONFIG_TOSHIBA_RBTX4927
	case MACH_GROUP_TOSHIBA:
		tx4927_setup();
		break;
#endif
#ifdef CONFIG_SIBYTE_BOARD
	case MACH_GROUP_SIBYTE:
		swarm_setup();
		break;
#endif
#ifdef CONFIG_HP_LASERJET
        case MACH_GROUP_HP_LJ:
                hp_setup();
                break;
#endif
	default:
		panic("Unsupported architecture");
	}

	strlcpy(command_line, arcs_cmdline, sizeof(command_line));
	strlcpy(saved_command_line, command_line, sizeof(saved_command_line));

	*cmdline_p = command_line;

	parse_cmdline_early();

	bootmem_init();

	paging_init();

	resource_init();
}

int __init fpu_disable(char *s)
{
	cpu_data[0].options &= ~MIPS_CPU_FPU;

	return 1;
}

__setup("nofpu", fpu_disable);
