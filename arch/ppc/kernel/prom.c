/*
 * $Id: prom.c,v 1.79 1999/10/08 01:56:32 paulus Exp $
 *
 * Procedures for interfacing to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * In particular, we are interested in the device tree
 * and in using some of its services (exit, write to stdout).
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <stdarg.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/threads.h>
#include <linux/spinlock.h>

#include <asm/init.h>
#include <asm/prom.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/bootx.h>
#include <asm/system.h>
#include <asm/gemini.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>

#ifdef CONFIG_FB
#include <asm/linux_logo.h>
#endif

/*
 * Properties whose value is longer than this get excluded from our
 * copy of the device tree.  This way we don't waste space storing
 * things like "driver,AAPL,MacOS,PowerPC" properties.
 */
#define MAX_PROPERTY_LENGTH	1024

struct prom_args {
	const char *service;
	int nargs;
	int nret;
	void *args[10];
};

struct pci_address {
	unsigned a_hi;
	unsigned a_mid;
	unsigned a_lo;
};

struct pci_reg_property {
	struct pci_address addr;
	unsigned size_hi;
	unsigned size_lo;
};

struct pci_range {
	struct pci_address addr;
	unsigned phys;
	unsigned size_hi;
	unsigned size_lo;
};

struct isa_reg_property {
	unsigned space;
	unsigned address;
	unsigned size;
};

struct pci_intr_map {
	struct pci_address addr;
	unsigned dunno;
	phandle int_ctrler;
	unsigned intr;
};

typedef unsigned long interpret_func(struct device_node *, unsigned long,
				     int, int);
static interpret_func interpret_pci_props;
static interpret_func interpret_dbdma_props;
static interpret_func interpret_isa_props;
static interpret_func interpret_macio_props;
static interpret_func interpret_root_props;

#ifndef FB_MAX			/* avoid pulling in all of the fb stuff */
#define FB_MAX	8
#endif
char *prom_display_paths[FB_MAX] __initdata = { 0, };
unsigned int prom_num_displays = 0;
char *of_stdout_device = 0;

prom_entry prom = 0;
ihandle prom_chosen = 0, prom_stdout = 0, prom_disp_node = 0;

extern char *klimit;
char *bootpath = 0;
char *bootdevice = 0;

unsigned int rtas_data = 0;   /* physical pointer */
unsigned int rtas_entry = 0;  /* physical pointer */
unsigned int rtas_size = 0;
unsigned int old_rtas = 0;

/* Set for a newworld machine */
int use_of_interrupt_tree = 0;
int pmac_newworld = 0;

static struct device_node *allnodes = 0;

#ifdef CONFIG_BOOTX_TEXT

#define NO_SCROLL

static void clearscreen(void);
static void flushscreen(void);

#ifndef NO_SCROLL
static void scrollscreen(void);
#endif

static void prepare_disp_BAT(void);

static void draw_byte(unsigned char c, long locX, long locY);
static void draw_byte_32(unsigned char *bits, unsigned long *base, int rb);
static void draw_byte_16(unsigned char *bits, unsigned long *base, int rb);
static void draw_byte_8(unsigned char *bits, unsigned long *base, int rb);

/* We want those in data, not BSS */
static long				g_loc_X = 0;
static long				g_loc_Y = 0;
static long				g_max_loc_X = 0;
static long				g_max_loc_Y = 0;

unsigned long disp_BAT[2] = {0, 0};

#define cmapsz	(16*256)

static unsigned char vga_font[cmapsz];

int bootx_text_mapped = 1;

#endif /* CONFIG_BOOTX_TEXT */


static void *call_prom(const char *service, int nargs, int nret, ...);
static void prom_exit(void);
static unsigned long copy_device_tree(unsigned long, unsigned long);
static unsigned long inspect_node(phandle, struct device_node *, unsigned long,
				  unsigned long, struct device_node ***);
static unsigned long finish_node(struct device_node *, unsigned long,
				 interpret_func *, int, int);
static unsigned long finish_node_interrupts(struct device_node *, unsigned long);
static unsigned long check_display(unsigned long);
static int prom_next_node(phandle *);
static void *early_get_property(unsigned long, unsigned long, char *);

#ifdef CONFIG_BOOTX_TEXT
static void setup_disp_fake_bi(ihandle dp);
static void prom_welcome(boot_infos_t* bi, unsigned long phys);
#endif

extern void enter_rtas(void *);
extern unsigned long reloc_offset(void);
void phys_call_rtas(int, int, int, ...);

extern char cmd_line[512];	/* XXX */
boot_infos_t *boot_infos = 0;	/* init it so it's in data segment not bss */
#ifdef CONFIG_BOOTX_TEXT
boot_infos_t *disp_bi = 0;
boot_infos_t fake_bi = {0,};
#endif
unsigned long dev_tree_size;

/*
 * prom_init() is called very early on, before the kernel text
 * and data have been mapped to KERNELBASE.  At this point the code
 * is running at whatever address it has been loaded at, so
 * references to extern and static variables must be relocated
 * explicitly.  The procedure reloc_offset() returns the address
 * we're currently running at minus the address we were linked at.
 * (Note that strings count as static variables.)
 *
 * Because OF may have mapped I/O devices into the area starting at
 * KERNELBASE, particularly on CHRP machines, we can't safely call
 * OF once the kernel has been mapped to KERNELBASE.  Therefore all
 * OF calls should be done within prom_init(), and prom_init()
 * and all routines called within it must be careful to relocate
 * references as necessary.
 *
 * Note that the bss is cleared *after* prom_init runs, so we have
 * to make sure that any static or extern variables it accesses
 * are put in the data segment.
 */
#define PTRRELOC(x)	((typeof(x))((unsigned long)(x) + offset))
#define PTRUNRELOC(x)	((typeof(x))((unsigned long)(x) - offset))
#define RELOC(x)	(*PTRRELOC(&(x)))

#define ALIGN(x) (((x) + sizeof(unsigned long)-1) & -sizeof(unsigned long))

/* Is boot-info compatible ? */
#define BOOT_INFO_IS_COMPATIBLE(bi)		((bi)->compatible_version <= BOOT_INFO_VERSION)
#define BOOT_INFO_IS_V2_COMPATIBLE(bi)	((bi)->version >= 2)
#define BOOT_INFO_IS_V4_COMPATIBLE(bi)	((bi)->version >= 4)

__init
static void
prom_exit()
{
	struct prom_args args;
	unsigned long offset = reloc_offset();

	args.service = "exit";
	args.nargs = 0;
	args.nret = 0;
	RELOC(prom)(&args);
	for (;;)			/* should never get here */
		;
}

__init
void
prom_enter(void)
{
	struct prom_args args;
	unsigned long offset = reloc_offset();

	args.service = RELOC("enter");
	args.nargs = 0;
	args.nret = 0;
	RELOC(prom)(&args);
}

__init
static void *
call_prom(const char *service, int nargs, int nret, ...)
{
	va_list list;
	int i;
	unsigned long offset = reloc_offset();
	struct prom_args prom_args;

	prom_args.service = service;
	prom_args.nargs = nargs;
	prom_args.nret = nret;
	va_start(list, nret);
	for (i = 0; i < nargs; ++i)
		prom_args.args[i] = va_arg(list, void *);
	va_end(list);
	for (i = 0; i < nret; ++i)
		prom_args.args[i + nargs] = 0;
	RELOC(prom)(&prom_args);
	return prom_args.args[nargs];
}

__init
void
prom_print(const char *msg)
{
	const char *p, *q;
	unsigned long offset = reloc_offset();

	if (RELOC(prom_stdout) == 0)
	{
#ifdef CONFIG_BOOTX_TEXT
		if (RELOC(disp_bi) != 0)
			prom_drawstring(msg);
#endif
		return;
	}

	for (p = msg; *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n'; ++q)
			;
		if (q > p)
			call_prom(RELOC("write"), 3, 1, RELOC(prom_stdout),
				  p, q - p);
		if (*q != 0) {
			++q;
			call_prom(RELOC("write"), 3, 1, RELOC(prom_stdout),
				  RELOC("\r\n"), 2);
		}
	}
}

void
prom_print_hex(unsigned int v)
{
	char buf[16];
	int i, c;

	for (i = 0; i < 8; ++i) {
		c = (v >> ((7-i)*4)) & 0xf;
		c += (c >= 10)? ('a' - 10): '0';
		buf[i] = c;
	}
	buf[i] = ' ';
	buf[i+1] = 0;
	prom_print(buf);
}

void
prom_print_nl(void)
{
	unsigned long offset = reloc_offset();
	prom_print(RELOC("\n"));
}

unsigned long smp_chrp_cpu_nr __initdata = 0;

#ifdef CONFIG_SMP
/*
 * With CHRP SMP we need to use the OF to start the other
 * processors so we can't wait until smp_boot_cpus (the OF is
 * trashed by then) so we have to put the processors into
 * a holding pattern controlled by the kernel (not OF) before
 * we destroy the OF.
 *
 * This uses a chunk of high memory, puts some holding pattern
 * code there and sends the other processors off to there until
 * smp_boot_cpus tells them to do something.  We do that by using
 * physical address 0x0.  The holding pattern checks that address
 * until its cpu # is there, when it is that cpu jumps to
 * __secondary_start().  smp_boot_cpus() takes care of setting those
 * values.
 *
 * We also use physical address 0x4 here to tell when a cpu
 * is in its holding pattern code.
 *
 * -- Cort
 */
static void
prom_hold_cpus(unsigned long mem)
{
	extern void __secondary_hold(void);
	unsigned long i;
	int cpu;
	phandle node;
	unsigned long offset = reloc_offset();
	char type[16], *path;
	unsigned int reg;

	/*
	 * XXX: hack to make sure we're chrp, assume that if we're
	 *      chrp we have a device_type property -- Cort
	 */
	node = call_prom(RELOC("finddevice"), 1, 1, RELOC("/"));
	if ( (int)call_prom(RELOC("getprop"), 4, 1, node,
			    RELOC("device_type"),type, sizeof(type)) <= 0)
		return;

	/* copy the holding pattern code to someplace safe (0) */
	/* the holding pattern is now within the first 0x100
	   bytes of the kernel image -- paulus */
	memcpy((void *)0, (void *)(KERNELBASE + offset), 0x100);
	flush_icache_range(0, 0x100);

	/* look for cpus */
	*(unsigned long *)(0x0) = 0;
	asm volatile("dcbf 0,%0": : "r" (0) : "memory");
	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("device_type"),
			  type, sizeof(type));
		if (strcmp(type, RELOC("cpu")) != 0)
			continue;
		path = (char *) mem;
		memset(path, 0, 256);
		if ((int) call_prom(RELOC("package-to-path"), 3, 1,
				    node, path, 255) < 0)
			continue;
		reg = -1;
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("reg"),
			  &reg, sizeof(reg));
		cpu = RELOC(smp_chrp_cpu_nr)++;
		RELOC(smp_hw_index)[cpu] = reg;
		/* XXX: hack - don't start cpu 0, this cpu -- Cort */
		if (cpu == 0)
			continue;
		prom_print(RELOC("starting cpu "));
		prom_print(path);
		*(ulong *)(0x4) = 0;
		call_prom(RELOC("start-cpu"), 3, 0, node,
			  __pa(__secondary_hold), cpu);
		prom_print(RELOC("..."));
		for ( i = 0 ; (i < 10000) && (*(ulong *)(0x4) == 0); i++ )
			;
		if (*(ulong *)(0x4) == cpu)
			prom_print(RELOC("ok\n"));
		else {
			prom_print(RELOC("failed: "));
			prom_print_hex(*(ulong *)0x4);
			prom_print_nl();
		}
	}
}
#endif /* CONFIG_SMP */

void
bootx_init(unsigned long r4, unsigned long phys)
{
	boot_infos_t *bi = (boot_infos_t *) r4;
	unsigned long space;
	unsigned long ptr, x;
	char *model;
	unsigned long offset = reloc_offset();

	RELOC(boot_infos) = PTRUNRELOC(bi);
	if (!BOOT_INFO_IS_V2_COMPATIBLE(bi))
		bi->logicalDisplayBase = 0;

#ifdef CONFIG_BOOTX_TEXT
	RELOC(g_loc_X) = 0;
	RELOC(g_loc_Y) = 0;
	RELOC(g_max_loc_X) = (bi->dispDeviceRect[2] - bi->dispDeviceRect[0]) / 8;
	RELOC(g_max_loc_Y) = (bi->dispDeviceRect[3] - bi->dispDeviceRect[1]) / 16;
	RELOC(disp_bi) = PTRUNRELOC(bi);
		
	clearscreen();

	/* Test if boot-info is compatible. Done only in config CONFIG_BOOTX_TEXT since
	   there is nothing much we can do with an incompatible version, except display
	   a message and eventually hang the processor...
		   
	   I'll try to keep enough of boot-info compatible in the future to always allow
	   display of this message;
	*/
	if (!BOOT_INFO_IS_COMPATIBLE(bi))
		prom_print(RELOC(" !!! WARNING - Incompatible version of BootX !!!\n\n\n"));
		
	prom_welcome(bi, phys);
	flushscreen();
#endif	/* CONFIG_BOOTX_TEXT */	
		
	/* New BootX enters kernel with MMU off, i/os are not allowed
	   here. This hack will have been done by the boostrap anyway.
	*/
	if (bi->version < 4) {
		/*
		 * XXX If this is an iMac, turn off the USB controller.
		 */
		model = (char *) early_get_property
			(r4 + bi->deviceTreeOffset, 4, RELOC("model"));
		if (model
		    && (strcmp(model, RELOC("iMac,1")) == 0
			|| strcmp(model, RELOC("PowerMac1,1")) == 0)) {
			out_le32((unsigned *)0x80880008, 1);	/* XXX */
		}
	}
		
	/* Move klimit to enclose device tree, args, ramdisk, etc... */
	if (bi->version < 5) {
		space = bi->deviceTreeOffset + bi->deviceTreeSize;
		if (bi->ramDisk)
			space = bi->ramDisk + bi->ramDiskSize;
	} else
		space = bi->totalParamsSize;
	RELOC(klimit) = PTRUNRELOC((char *) bi + space);

	/* New BootX will have flushed all TLBs and enters kernel with
	   MMU switched OFF, so this should not be useful anymore.
	*/
	if (bi->version < 4) {
		/*
		 * Touch each page to make sure the PTEs for them
		 * are in the hash table - the aim is to try to avoid
		 * getting DSI exceptions while copying the kernel image.
		 */
		for (ptr = (KERNELBASE + offset) & PAGE_MASK;
		     ptr < (unsigned long)bi + space; ptr += PAGE_SIZE)
			x = *(volatile unsigned long *)ptr;
	}
		
#ifdef CONFIG_BOOTX_TEXT
	prepare_disp_BAT();
	prom_drawstring(RELOC("booting...\n"));
	flushscreen();
	RELOC(bootx_text_mapped) = 1;
#endif
}

#ifdef CONFIG_PPC64BRIDGE
/*
 * Set up a hash table with a set of entries in it to map the
 * first 64MB of RAM.  This is used on 64-bit machines since
 * some of them don't have BATs.
 * We assume the PTE will fit in the primary PTEG.
 */

static inline void make_pte(unsigned long htab, unsigned int hsize,
			    unsigned int va, unsigned int pa, int mode)
{
	unsigned int *pteg;
	unsigned int hash, i;

	hash = ((va >> 5) ^ (va >> 21)) & 0x7fff80;
	pteg = (unsigned int *)(htab + (hash & (hsize - 1)));
	for (i = 0; i < 8; ++i, pteg += 4) {
		if ((pteg[1] & 1) == 0) {
			pteg[1] = ((va >> 16) & 0xff80) | 1;
			pteg[3] = pa | mode;
			break;
		}
	}
}

extern unsigned long _SDR1;
extern PTE *Hash;
extern unsigned long Hash_size;

void
prom_alloc_htab(void)
{
	unsigned int hsize;
	unsigned long htab;
	unsigned int addr;
	unsigned long offset = reloc_offset();

	/*
	 * Because of OF bugs we can't use the "claim" client
	 * interface to allocate memory for the hash table.
	 * This code is only used on 64-bit PPCs, and the only
	 * 64-bit PPCs at the moment are RS/6000s, and their
	 * OF is based at 0xc00000 (the 12M point), so we just
	 * arbitrarily use the 0x800000 - 0xc00000 region for the
	 * hash table.
	 *  -- paulus.
	 */
#ifdef CONFIG_POWER4
	hsize = 4 << 20;	/* POWER4 has no BATs */
#else
	hsize = 2 << 20;
#endif /* CONFIG_POWER4 */
	htab = (8 << 20);
	RELOC(Hash) = (void *)(htab + KERNELBASE);
	RELOC(Hash_size) = hsize;
	RELOC(_SDR1) = htab + __ilog2(hsize) - 18;

	/*
	 * Put in PTEs for the first 64MB of RAM
	 */
	cacheable_memzero((void *)htab, hsize);
	for (addr = 0; addr < 0x4000000; addr += 0x1000)
		make_pte(htab, hsize, addr + KERNELBASE, addr,
			 _PAGE_ACCESSED | _PAGE_COHERENT | PP_RWXX);
}
#endif /* CONFIG_PPC64BRIDGE */

static __init void
prom_instantiate_rtas(void)
{
	ihandle prom_rtas;
	unsigned int i;
	struct prom_args prom_args;
	unsigned long offset = reloc_offset();

	prom_rtas = call_prom(RELOC("finddevice"), 1, 1, RELOC("/rtas"));
	if (prom_rtas == (void *) -1)
		return;

	RELOC(rtas_size) = 0;
	call_prom(RELOC("getprop"), 4, 1, prom_rtas,
		  RELOC("rtas-size"), &RELOC(rtas_size), sizeof(rtas_size));
	prom_print(RELOC("instantiating rtas"));
	if (RELOC(rtas_size) == 0) {
		RELOC(rtas_data) = 0;
	} else {
		/*
		 * Ask OF for some space for RTAS.
		 * Actually OF has bugs so we just arbitrarily
		 * use memory at the 6MB point.
		 */
		RELOC(rtas_data) = 6 << 20;
		prom_print(RELOC(" at "));
		prom_print_hex(RELOC(rtas_data));
	}

	prom_rtas = call_prom(RELOC("open"), 1, 1, RELOC("/rtas"));
	prom_print(RELOC("..."));
	prom_args.service = RELOC("call-method");
	prom_args.nargs = 3;
	prom_args.nret = 2;
	prom_args.args[0] = RELOC("instantiate-rtas");
	prom_args.args[1] = prom_rtas;
	prom_args.args[2] = (void *) RELOC(rtas_data);
	RELOC(prom)(&prom_args);
	i = 0;
	if (prom_args.args[3] == 0)
		i = (unsigned int)prom_args.args[4];
	RELOC(rtas_entry) = i;
	if ((RELOC(rtas_entry) == -1) || (RELOC(rtas_entry) == 0))
		prom_print(RELOC(" failed\n"));
	else
		prom_print(RELOC(" done\n"));
}

/*
 * We enter here early on, when the Open Firmware prom is still
 * handling exceptions and the MMU hash table for us.
 */
__init
unsigned long
prom_init(int r3, int r4, prom_entry pp)
{
	int chrp = 0;
	unsigned long mem;
	ihandle prom_mmu, prom_op;
	unsigned long offset = reloc_offset();
	int l;
	char *p, *d;
	int prom_version = 0;
 	unsigned long phys;

 	/* Default */
 	phys = offset + KERNELBASE;

	/* check if we're apus, return if we are */
	if ( r3 == 0x61707573 )
		return phys;

	/* If we came here from BootX, clear the screen,
	 * set up some pointers and return. */
	if (r3 == 0x426f6f58 && pp == NULL) {
		bootx_init(r4, phys);
		return phys;
	}

	/* check if we're prep, return if we are */
	if ( *(unsigned long *)(0) == 0xdeadc0de )
		return phys;

	/* First get a handle for the stdout device */
	RELOC(prom) = pp;
	RELOC(prom_chosen) = call_prom(RELOC("finddevice"), 1, 1,
				       RELOC("/chosen"));
	if (RELOC(prom_chosen) == (void *)-1)
		prom_exit();
	if ((int) call_prom(RELOC("getprop"), 4, 1, RELOC(prom_chosen),
			    RELOC("stdout"), &RELOC(prom_stdout),
			    sizeof(prom_stdout)) <= 0)
		prom_exit();

	/* Get the full OF pathname of the stdout device */
	mem = (unsigned long) RELOC(klimit) + offset;
	p = (char *) mem;
	memset(p, 0, 256);
	call_prom(RELOC("instance-to-path"), 3, 1, RELOC(prom_stdout), p, 255);
	RELOC(of_stdout_device) = PTRUNRELOC(p);
	mem += strlen(p) + 1;

	/* Find the OF version */
	prom_op = call_prom(RELOC("finddevice"), 1, 1, RELOC("/openprom"));
	prom_version = 0;
	if (prom_op != (void*)-1) {
		char model[64];
		int sz;
		sz = (int)call_prom(RELOC("getprop"), 4, 1, prom_op,
				    RELOC("model"), model, 64);
		if (sz > 0) {
			char *c;
			/* hack to skip the ibm chrp firmware # */
			if ( strncmp(model,RELOC("IBM"),3) ) {
				for (c = model; *c; c++)
					if (*c >= '0' && *c <= '9') {
						prom_version = *c - '0';
						break;
					}
			}
			else
				chrp = 1;
		}
	}
	if (prom_version >= 3)
		prom_print(RELOC("OF Version 3 detected.\n"));

	/* Get the boot device and translate it to a full OF pathname. */
	p = (char *) mem;
	l = (int) call_prom(RELOC("getprop"), 4, 1, RELOC(prom_chosen),
			    RELOC("bootpath"), p, 1<<20);
	if (l > 0) {
		p[l] = 0;	/* should already be null-terminated */
		RELOC(bootpath) = PTRUNRELOC(p);
		mem += l + 1;
		d = (char *) mem;
		*d = 0;
		call_prom(RELOC("canon"), 3, 1, p, d, 1<<20);
		RELOC(bootdevice) = PTRUNRELOC(d);
		mem = ALIGN(mem + strlen(d) + 1);
	}

	prom_instantiate_rtas();

#ifdef CONFIG_PPC64BRIDGE
	/*
	 * Find out how much memory we have and allocate a
	 * suitably-sized hash table.
	 */
	prom_alloc_htab();
#endif

#ifdef CONFIG_SMP
	prom_hold_cpus(mem);
#endif

	mem = check_display(mem);

	prom_print(RELOC("copying OF device tree..."));
	mem = copy_device_tree(mem, mem + (1<<20));
	prom_print(RELOC("done\n"));

	RELOC(klimit) = (char *) (mem - offset);

	/* If we are already running at 0xc0000000, we assume we were loaded by
	 * an OF bootloader which did set a BAT for us. This breaks OF translate
	 * so we force phys to be 0
	 */
	if (offset == 0)
		phys = 0;
	else {
 	    if ((int) call_prom(RELOC("getprop"), 4, 1, RELOC(prom_chosen),
			    RELOC("mmu"), &prom_mmu, sizeof(prom_mmu)) <= 0) {	
		prom_print(RELOC(" no MMU found\n"));
	    } else {
		int nargs;
		struct prom_args prom_args;
		nargs = 4;
		prom_args.service = RELOC("call-method");
		prom_args.nargs = nargs;
		prom_args.nret = 4;
		prom_args.args[0] = RELOC("translate");
		prom_args.args[1] = prom_mmu;
		prom_args.args[2] = (void *)(offset + KERNELBASE);
		prom_args.args[3] = (void *)1;
		RELOC(prom)(&prom_args);

		/* We assume the phys. address size is 3 cells */
		if (prom_args.args[nargs] != 0)
			prom_print(RELOC(" (translate failed)\n"));
		else
			phys = (unsigned long)prom_args.args[nargs+3];
	    }
	}

#ifdef CONFIG_BOOTX_TEXT
	if (RELOC(prom_disp_node) != 0)
		setup_disp_fake_bi(RELOC(prom_disp_node));
#endif

	/* If OpenFirmware version >= 3, then use quiesce call */
	if (prom_version >= 3) {
		prom_print(RELOC("Calling quiesce ...\n"));
		call_prom(RELOC("quiesce"), 0, 0);
	}

#ifdef CONFIG_BOOTX_TEXT
	if (!chrp && RELOC(disp_bi)) {
		RELOC(prom_stdout) = 0; /* stop OF output */
		clearscreen();
		prepare_disp_BAT();
		prom_welcome(PTRRELOC(RELOC(disp_bi)), phys);
		prom_drawstring(RELOC("booting...\n"));
		RELOC(bootx_text_mapped) = 1;
	} else {
		RELOC(bootx_text_mapped) = 0;
	}
#endif

	prom_print(RELOC("returning "));
	prom_print_hex(phys);
	prom_print(RELOC(" from prom_init\n"));
	RELOC(prom_stdout) = 0;
	return phys;
}

void phys_call_rtas(int service, int nargs, int nret, ...)
{
	va_list list;
	union {
		unsigned long words[16];
		double align;
	} u;
	unsigned long offset = reloc_offset();
	void (*rtas)(void *, unsigned long);
	int i;

	u.words[0] = service;
	u.words[1] = nargs;
	u.words[2] = nret;
	va_start(list, nret);
	for (i = 0; i < nargs; ++i)
		u.words[i+3] = va_arg(list, unsigned long);
	va_end(list);

	rtas = (void (*)(void *, unsigned long)) RELOC(rtas_entry);
	rtas(&u, RELOC(rtas_data));
}

#ifdef CONFIG_BOOTX_TEXT
__init static void
prom_welcome(boot_infos_t* bi, unsigned long phys)
{
	unsigned long offset = reloc_offset();
	unsigned long flags;
	unsigned long pvr;
	
	prom_drawstring(RELOC("Welcome to Linux, kernel " UTS_RELEASE "\n"));
	prom_drawstring(RELOC("\nstarted at       : 0x"));
	prom_drawhex(phys);
	prom_drawstring(RELOC("\nlinked at        : 0x"));
	prom_drawhex(KERNELBASE);
	prom_drawstring(RELOC("\nframe buffer at  : 0x"));
	prom_drawhex((unsigned long)bi->dispDeviceBase);
	prom_drawstring(RELOC(" (phys), 0x"));
	prom_drawhex((unsigned long)bi->logicalDisplayBase);
	prom_drawstring(RELOC(" (log)"));
	prom_drawstring(RELOC("\nklimit           : 0x"));
	prom_drawhex((unsigned long)RELOC(klimit));
	prom_drawstring(RELOC("\nMSR              : 0x"));
	__asm__ __volatile__ ("mfmsr %0" : "=r" (flags));
	prom_drawhex(flags);
	__asm__ __volatile__ ("mfspr %0, 287" : "=r" (pvr));
	pvr >>= 16;
	if (pvr > 1) {
	    prom_drawstring(RELOC("\nHID0             : 0x"));
	    __asm__ __volatile__ ("mfspr %0, 1008" : "=r" (flags));
	    prom_drawhex(flags);
	}
	if (pvr == 8 || pvr == 12) {
	    prom_drawstring(RELOC("\nICTC             : 0x"));
	    __asm__ __volatile__ ("mfspr %0, 1019" : "=r" (flags));
	    prom_drawhex(flags);
	}
	prom_drawstring(RELOC("\n\n"));
}

/* Calc BAT values for mapping the display and store them
 * in disp_BAT.  Those values are then used from head.S to map
 * the display during identify_machine() and MMU_Init()
 * 
 * For now, the display is mapped in place (1:1). This should
 * be changed if the display physical address overlaps
 * KERNELBASE, which is fortunately not the case on any machine
 * I know of. This mapping is temporary and will disappear as
 * soon as the setup done by MMU_Init() is applied
 * 
 * For now, we align the BAT and then map 8Mb on 601 and 16Mb
 * on other PPCs. This may cause trouble if the framebuffer
 * is really badly aligned, but I didn't encounter this case
 * yet.
 */
__init
static void
prepare_disp_BAT(void)
{
	unsigned long offset = reloc_offset();
	boot_infos_t* bi = PTRRELOC(RELOC(disp_bi));
	unsigned long addr = (unsigned long)bi->dispDeviceBase;
	
	if ((_get_PVR() >> 16) != 1) {
		/* 603, 604, G3, G4, ... */
		addr &= 0xFF000000UL;
		RELOC(disp_BAT[0]) = addr | (BL_16M<<2) | 2;
		RELOC(disp_BAT[1]) = addr | (_PAGE_NO_CACHE | _PAGE_GUARDED | BPP_RW);		
	} else {
		/* 601 */
		addr &= 0xFF800000UL;
		RELOC(disp_BAT[0]) = addr | (_PAGE_NO_CACHE | PP_RWXX) | 4;
		RELOC(disp_BAT[1]) = addr | BL_8M | 0x40;
	}
	bi->logicalDisplayBase = bi->dispDeviceBase;
}

#endif

static int prom_set_color(ihandle ih, int i, int r, int g, int b)
{
	struct prom_args prom_args;
	unsigned long offset = reloc_offset();

	prom_args.service = RELOC("call-method");
	prom_args.nargs = 6;
	prom_args.nret = 1;
	prom_args.args[0] = RELOC("color!");
	prom_args.args[1] = ih;
	prom_args.args[2] = (void *) i;
	prom_args.args[3] = (void *) b;
	prom_args.args[4] = (void *) g;
	prom_args.args[5] = (void *) r;
	RELOC(prom)(&prom_args);
	return (int) prom_args.args[6];
}

/*
 * If we have a display that we don't know how to drive,
 * we will want to try to execute OF's open method for it
 * later.  However, OF will probably fall over if we do that
 * we've taken over the MMU.
 * So we check whether we will need to open the display,
 * and if so, open it now.
 */
__init
static unsigned long
check_display(unsigned long mem)
{
	phandle node;
	ihandle ih;
	int i;
	unsigned long offset = reloc_offset();
	char type[16], *path;
	static unsigned char default_colors[] = {
		0x00, 0x00, 0x00,
		0x00, 0x00, 0xaa,
		0x00, 0xaa, 0x00,
		0x00, 0xaa, 0xaa,
		0xaa, 0x00, 0x00,
		0xaa, 0x00, 0xaa,
		0xaa, 0xaa, 0x00,
		0xaa, 0xaa, 0xaa,
		0x55, 0x55, 0x55,
		0x55, 0x55, 0xff,
		0x55, 0xff, 0x55,
		0x55, 0xff, 0xff,
		0xff, 0x55, 0x55,
		0xff, 0x55, 0xff,
		0xff, 0xff, 0x55,
		0xff, 0xff, 0xff
	};

	RELOC(prom_disp_node) = 0;

	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		call_prom(RELOC("getprop"), 4, 1, node, RELOC("device_type"),
			  type, sizeof(type));
		if (strcmp(type, RELOC("display")) != 0)
			continue;
		/* It seems OF doesn't null-terminate the path :-( */
		path = (char *) mem;
		memset(path, 0, 256);
		if ((int) call_prom(RELOC("package-to-path"), 3, 1,
				    node, path, 255) < 0)
			continue;
		prom_print(RELOC("opening display "));
		prom_print(path);
		ih = call_prom(RELOC("open"), 1, 1, path);
		if (ih == 0 || ih == (ihandle) -1) {
			prom_print(RELOC("... failed\n"));
			continue;
		}
		prom_print(RELOC("... ok\n"));

		if (RELOC(prom_disp_node) == 0)
			RELOC(prom_disp_node) = node;
			
		/* Setup a useable color table when the appropriate
		 * method is available. Should update this to set-colors */
		for (i = 0; i < 32; i++)
			if (prom_set_color(ih, i, RELOC(default_colors)[i*3],
					   RELOC(default_colors)[i*3+1],
					   RELOC(default_colors)[i*3+2]) != 0)
				break;

#ifdef CONFIG_FB
		for (i = 0; i < LINUX_LOGO_COLORS; i++)
			if (prom_set_color(ih, i + 32,
					   RELOC(linux_logo_red)[i],
					   RELOC(linux_logo_green)[i],
					   RELOC(linux_logo_blue)[i]) != 0)
				break;
#endif /* CONFIG_FB */

		/*
		 * If this display is the device that OF is using for stdout,
		 * move it to the front of the list.
		 */
		mem += strlen(path) + 1;
		i = RELOC(prom_num_displays)++;
		if (RELOC(of_stdout_device) != 0 && i > 0
		    && strcmp(PTRRELOC(RELOC(of_stdout_device)), path) == 0) {
			for (; i > 0; --i)
				RELOC(prom_display_paths[i])
					= RELOC(prom_display_paths[i-1]);
		}
		RELOC(prom_display_paths[i]) = PTRUNRELOC(path);
		if (RELOC(prom_num_displays) >= FB_MAX)
			break;
	}
	return ALIGN(mem);
}

/* This function will enable the early boot text when doing OF booting. This
 * way, xmon output should work too
 */
#ifdef CONFIG_BOOTX_TEXT
__init
static void
setup_disp_fake_bi(ihandle dp)
{
	int width = 640, height = 480, depth = 8, pitch;
	unsigned address;
	boot_infos_t* bi;
	unsigned long offset = reloc_offset();
	struct pci_reg_property addrs[8];
	int i, naddrs;
	char name[32];
	char *getprop = RELOC("getprop");

	prom_print(RELOC("Initializing fake screen: "));

	memset(name, 0, sizeof(name));
	call_prom(getprop, 4, 1, dp, RELOC("name"), name, sizeof(name));
	name[sizeof(name)-1] = 0;
	prom_print(name);
	prom_print(RELOC("\n"));
	call_prom(getprop, 4, 1, dp, RELOC("width"), &width, sizeof(width));
	call_prom(getprop, 4, 1, dp, RELOC("height"), &height, sizeof(height));
	call_prom(getprop, 4, 1, dp, RELOC("depth"), &depth, sizeof(depth));
	pitch = width * ((depth + 7) / 8);
	call_prom(getprop, 4, 1, dp, RELOC("linebytes"),
		  &pitch, sizeof(pitch));
	if (pitch == 1)
		pitch = 0x1000;		/* for strange IBM display */
	address = 0;
	call_prom(getprop, 4, 1, dp, RELOC("address"),
		  &address, sizeof(address));
	if (address == 0) {
		/* look for an assigned address with a size of >= 1MB */
		naddrs = (int) call_prom(getprop, 4, 1, dp,
				RELOC("assigned-addresses"),
				addrs, sizeof(addrs));
		naddrs /= sizeof(struct pci_reg_property);
		for (i = 0; i < naddrs; ++i) {
			if (addrs[i].size_lo >= (1 << 20)) {
				address = addrs[i].addr.a_lo;
				/* use the BE aperture if possible */
				if (addrs[i].size_lo >= (16 << 20))
					address += (8 << 20);
				break;
			}
		}
		if (address == 0) {
			prom_print(RELOC("Failed to get address\n"));
			return;
		}
	}
	/* kludge for valkyrie */
	if (strcmp(name, RELOC("valkyrie")) == 0) 
		address += 0x1000;
 
	RELOC(disp_bi) = &fake_bi;
	bi = PTRRELOC((&fake_bi));
	RELOC(g_loc_X) = 0;
	RELOC(g_loc_Y) = 0;
	RELOC(g_max_loc_X) = width / 8;
	RELOC(g_max_loc_Y) = height / 16;
	bi->logicalDisplayBase = (unsigned char *)address;
	bi->dispDeviceBase = (unsigned char *)address;
	bi->dispDeviceRowBytes = pitch;
	bi->dispDeviceDepth = depth;
	bi->dispDeviceRect[0] = bi->dispDeviceRect[1] = 0;
	bi->dispDeviceRect[2] = width;
	bi->dispDeviceRect[3] = height;
}
#endif

__init
static int
prom_next_node(phandle *nodep)
{
	phandle node;
	unsigned long offset = reloc_offset();

	if ((node = *nodep) != 0
	    && (*nodep = call_prom(RELOC("child"), 1, 1, node)) != 0)
		return 1;
	if ((*nodep = call_prom(RELOC("peer"), 1, 1, node)) != 0)
		return 1;
	for (;;) {
		if ((node = call_prom(RELOC("parent"), 1, 1, node)) == 0)
			return 0;
		if ((*nodep = call_prom(RELOC("peer"), 1, 1, node)) != 0)
			return 1;
	}
}

/*
 * Make a copy of the device tree from the PROM.
 */
__init
static unsigned long
copy_device_tree(unsigned long mem_start, unsigned long mem_end)
{
	phandle root;
	unsigned long new_start;
	struct device_node **allnextp;
	unsigned long offset = reloc_offset();

	root = call_prom(RELOC("peer"), 1, 1, (phandle)0);
	if (root == (phandle)0) {
		prom_print(RELOC("couldn't get device tree root\n"));
		prom_exit();
	}
	allnextp = &RELOC(allnodes);
	mem_start = ALIGN(mem_start);
	new_start = inspect_node(root, 0, mem_start, mem_end, &allnextp);
	*allnextp = 0;
	return new_start;
}

__init
static unsigned long
inspect_node(phandle node, struct device_node *dad,
	     unsigned long mem_start, unsigned long mem_end,
	     struct device_node ***allnextpp)
{
	int l;
	phandle child;
	struct device_node *np;
	struct property *pp, **prev_propp;
	char *prev_name, *namep;
	unsigned char *valp;
	unsigned long offset = reloc_offset();

	np = (struct device_node *) mem_start;
	mem_start += sizeof(struct device_node);
	memset(np, 0, sizeof(*np));
	np->node = node;
	**allnextpp = PTRUNRELOC(np);
	*allnextpp = &np->allnext;
	if (dad != 0) {
		np->parent = PTRUNRELOC(dad);
		/* we temporarily use the `next' field as `last_child'. */
		if (dad->next == 0)
			dad->child = PTRUNRELOC(np);
		else
			dad->next->sibling = PTRUNRELOC(np);
		dad->next = np;
	}

	/* get and store all properties */
	prev_propp = &np->properties;
	prev_name = RELOC("");
	for (;;) {
		pp = (struct property *) mem_start;
		namep = (char *) (pp + 1);
		pp->name = PTRUNRELOC(namep);
		if ((int) call_prom(RELOC("nextprop"), 3, 1, node, prev_name,
				    namep) <= 0)
			break;
		mem_start = ALIGN((unsigned long)namep + strlen(namep) + 1);
		prev_name = namep;
		valp = (unsigned char *) mem_start;
		pp->value = PTRUNRELOC(valp);
		pp->length = (int)
			call_prom(RELOC("getprop"), 4, 1, node, namep,
				  valp, mem_end - mem_start);
		if (pp->length < 0)
			continue;
#ifdef MAX_PROPERTY_LENGTH
		if (pp->length > MAX_PROPERTY_LENGTH)
			continue; /* ignore this property */
#endif
		mem_start = ALIGN(mem_start + pp->length);
		*prev_propp = PTRUNRELOC(pp);
		prev_propp = &pp->next;
	}
	*prev_propp = 0;

	/* get the node's full name */
	l = (int) call_prom(RELOC("package-to-path"), 3, 1, node,
			    (char *) mem_start, mem_end - mem_start);
	if (l >= 0) {
		np->full_name = PTRUNRELOC((char *) mem_start);
		*(char *)(mem_start + l) = 0;
		mem_start = ALIGN(mem_start + l + 1);
	}

	/* do all our children */
	child = call_prom(RELOC("child"), 1, 1, node);
	while (child != (void *)0) {
		mem_start = inspect_node(child, np, mem_start, mem_end,
					 allnextpp);
		child = call_prom(RELOC("peer"), 1, 1, child);
	}

	return mem_start;
}

/*
 * finish_device_tree is called once things are running normally
 * (i.e. with text and data mapped to the address they were linked at).
 * It traverses the device tree and fills in the name, type,
 * {n_}addrs and {n_}intrs fields of each node.
 */
__init
void
finish_device_tree(void)
{
	unsigned long mem = (unsigned long) klimit;

	/* All newworld machines now use the interrupt tree */
	struct device_node *np = allnodes;

	while(np && (_machine == _MACH_Pmac)) {
		if (get_property(np, "interrupt-parent", 0)) {
			pmac_newworld = 1;
			break;
		}
		np = np->allnext;
	}
	if ((_machine == _MACH_chrp) || (boot_infos == 0 && pmac_newworld))
		use_of_interrupt_tree = 1;

	mem = finish_node(allnodes, mem, NULL, 0, 0);
	dev_tree_size = mem - (unsigned long) allnodes;
	klimit = (char *) mem;
}

/*
 * early_get_property is used to access the device tree image prepared
 * by BootX very early on, before the pointers in it have been relocated.
 */
__init void *
early_get_property(unsigned long base, unsigned long node, char *prop)
{
	struct device_node *np = (struct device_node *)(base + node);
	struct property *pp;

	for (pp = np->properties; pp != 0; pp = pp->next) {
		pp = (struct property *) (base + (unsigned long)pp);
		if (strcmp((char *)((unsigned long)pp->name + base),
			   prop) == 0) {
			return (void *)((unsigned long)pp->value + base);
		}
	}
	return 0;
}

__init
static unsigned long
finish_node(struct device_node *np, unsigned long mem_start,
	    interpret_func *ifunc, int naddrc, int nsizec)
{
	struct device_node *child;
	int *ip;

	np->name = get_property(np, "name", 0);
	np->type = get_property(np, "device_type", 0);

	/* get the device addresses and interrupts */
	if (ifunc != NULL) {
		mem_start = ifunc(np, mem_start, naddrc, nsizec);
	}
	if (use_of_interrupt_tree) {
		mem_start = finish_node_interrupts(np, mem_start);
	}

	/* Look for #address-cells and #size-cells properties. */
	ip = (int *) get_property(np, "#address-cells", 0);
	if (ip != NULL)
		naddrc = *ip;
	ip = (int *) get_property(np, "#size-cells", 0);
	if (ip != NULL)
		nsizec = *ip;

	/* the f50 sets the name to 'display' and 'compatible' to what we
	 * expect for the name -- Cort
	 */
	if (!strcmp(np->name, "display"))
		np->name = get_property(np, "compatible", 0);

	if (!strcmp(np->name, "device-tree"))
		ifunc = interpret_root_props;
	else if (np->type == 0)
		ifunc = NULL;
	else if (!strcmp(np->type, "pci") || !strcmp(np->type, "vci"))
		ifunc = interpret_pci_props;
	else if (!strcmp(np->type, "dbdma"))
		ifunc = interpret_dbdma_props;
	else if (!strcmp(np->type, "mac-io")
		 || ifunc == interpret_macio_props)
		ifunc = interpret_macio_props;
	else if (!strcmp(np->type, "isa"))
		ifunc = interpret_isa_props;
	else if (!((ifunc == interpret_dbdma_props
		    || ifunc == interpret_macio_props)
		   && (!strcmp(np->type, "escc")
		       || !strcmp(np->type, "media-bay"))))
		ifunc = NULL;

	/* if we were booted from BootX, convert the full name */
	if (boot_infos
	    && strncmp(np->full_name, "Devices:device-tree", 19) == 0) {
		if (np->full_name[19] == 0) {
			strcpy(np->full_name, "/");
		} else if (np->full_name[19] == ':') {
			char *p = np->full_name + 19;
			np->full_name = p;
			for (; *p; ++p)
				if (*p == ':')
					*p = '/';
		}
	}

	for (child = np->child; child != NULL; child = child->sibling)
		mem_start = finish_node(child, mem_start, ifunc,
					naddrc, nsizec);

	return mem_start;
}

/* This routine walks the interrupt tree for a given device node and gather 
 * all necessary informations according to the draft interrupt mapping
 * for CHRP. The current version was only tested on Apple "Core99" machines
 * and may not handle cascaded controllers correctly.
 */
__init
static unsigned long
finish_node_interrupts(struct device_node *np, unsigned long mem_start)
{
	/* Finish this node */
	unsigned int *isizep, *asizep, *interrupts, *map, *map_mask, *reg;
	phandle *parent;
	struct device_node *node, *parent_node;
	int l, isize, ipsize, asize, map_size, regpsize;

	/* Currently, we don't look at all nodes with no "interrupts" property */
	interrupts = (unsigned int *)get_property(np, "interrupts", &l);
	if (interrupts == NULL)
		return mem_start;
	ipsize = l>>2;

	reg = (unsigned int *)get_property(np, "reg", &l);
	regpsize = l>>2;

	/* We assume default interrupt cell size is 1 (bugus ?) */
	isize = 1;
	node = np;
	
	do {
	    /* We adjust the cell size if the current parent contains an #interrupt-cells
	     * property */
	    isizep = (unsigned int *)get_property(node, "#interrupt-cells", &l);
	    if (isizep)
	    	isize = *isizep;

	    /* We don't do interrupt cascade (ISA) for now, we stop on the first 
	     * controller found
	     */
	    if (get_property(node, "interrupt-controller", &l)) {
	    	int i,j;
		int cvt_irq;

		/* XXX on chrp, offset interrupt numbers for the
		   8259 by 0, those for the openpic by 16 */
		cvt_irq = _machine == _MACH_chrp
			&& get_property(node, "interrupt-parent", NULL) == 0;
	    	np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = ipsize / isize;
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
		    np->intrs[i].line = *interrupts++;
		    if (cvt_irq)
			np->intrs[i].line = openpic_to_irq(np->intrs[i].line);
		    np->intrs[i].sense = 0;
		    if (isize > 1)
		        np->intrs[i].sense = *interrupts++;
		    for (j=2; j<isize; j++)
		    	interrupts++;
		}
		return mem_start;
	    }
	    /* We lookup for an interrupt-map. This code can only handle one interrupt
	     * per device in the map. We also don't handle #address-cells in the parent
	     * I skip the pci node itself here, may not be necessary but I don't like it's
	     * reg property.
	     */
	    if (np != node)
	        map = (unsigned int *)get_property(node, "interrupt-map", &l);
	     else
	     	map = NULL;
	    if (map && l) {
	    	int i, found, temp_isize;
	        map_size = l>>2;
	        map_mask = (unsigned int *)get_property(node, "interrupt-map-mask", &l);
	        asizep = (unsigned int *)get_property(node, "#address-cells", &l);
	        if (asizep && l == sizeof(unsigned int))
	            asize = *asizep;
	        else
	            asize = 0;
	        found = 0;
	        while(map_size>0 && !found) {
	            found = 1;
	            for (i=0; i<asize; i++) {
	            	unsigned int mask = map_mask ? map_mask[i] : 0xffffffff;
	            	if (!reg || (i>=regpsize) || ((mask & *map) != (mask & reg[i])))
	           	    found = 0;
	           	map++;
	           	map_size--;
	            }
	            for (i=0; i<isize; i++) {
	            	unsigned int mask = map_mask ? map_mask[i+asize] : 0xffffffff;
	            	if ((mask & *map) != (mask & interrupts[i]))
	            	    found = 0;
	            	map++;
	            	map_size--;
	            }
	            parent = *((phandle *)(map));
	            map+=1; map_size-=1;
	            parent_node = find_phandle(parent);
	            temp_isize = isize;
	            if (parent_node) {
			isizep = (unsigned int *)get_property(parent_node, "#interrupt-cells", &l);
	    		if (isizep)
	    		    temp_isize = *isizep;
	            }
	            if (!found) {
	            	map += temp_isize;
	            	map_size-=temp_isize;
	            }
	        }
	        if (found) {
	            node = parent_node;
	            reg = NULL;
	            regpsize = 0;
	            interrupts = (unsigned int *)map;
	            ipsize = temp_isize*1;
		    continue;
	        }
	    }
	    /* We look for an explicit interrupt-parent.
	     */
	    parent = (phandle *)get_property(node, "interrupt-parent", &l);
	    if (parent && (l == sizeof(phandle)) &&
	    	(parent_node = find_phandle(*parent))) {
	    	node = parent_node;
	    	continue;
	    }
	    /* Default, get real parent */
	    node = node->parent;
	} while(node);

	return mem_start;
}


/*
 * When BootX makes a copy of the device tree from the MacOS
 * Name Registry, it is in the format we use but all of the pointers
 * are offsets from the start of the tree.
 * This procedure updates the pointers.
 */
__init
void relocate_nodes(void)
{
	unsigned long base;
	struct device_node *np;
	struct property *pp;

#define ADDBASE(x)	(x = (x)? ((typeof (x))((unsigned long)(x) + base)): 0)

	base = (unsigned long) boot_infos + boot_infos->deviceTreeOffset;
	allnodes = (struct device_node *)(base + 4);
	for (np = allnodes; np != 0; np = np->allnext) {
		ADDBASE(np->full_name);
		ADDBASE(np->properties);
		ADDBASE(np->parent);
		ADDBASE(np->child);
		ADDBASE(np->sibling);
		ADDBASE(np->allnext);
		for (pp = np->properties; pp != 0; pp = pp->next) {
			ADDBASE(pp->name);
			ADDBASE(pp->value);
			ADDBASE(pp->next);
		}
	}
}

__init
static unsigned long
interpret_pci_props(struct device_node *np, unsigned long mem_start,
		    int naddrc, int nsizec)
{
	struct address_range *adr;
	struct pci_reg_property *pci_addrs;
	int i, l, *ip, ml;
	struct pci_intr_map *imp;

	pci_addrs = (struct pci_reg_property *)
		get_property(np, "assigned-addresses", &l);
	if (pci_addrs != 0 && l >= sizeof(struct pci_reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct pci_reg_property)) >= 0) {
			/* XXX assumes PCI addresses mapped 1-1 to physical */
			adr[i].space = pci_addrs[i].addr.a_hi;
			adr[i].address = pci_addrs[i].addr.a_lo;
			adr[i].size = pci_addrs[i].size_lo;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;

	/*
	 * If the pci host bridge has an interrupt-map property,
	 * look for our node in it.
	 */
	if (np->parent != 0 && pci_addrs != 0
	    && (imp = (struct pci_intr_map *)
		get_property(np->parent, "interrupt-map", &ml)) != 0
	    && (ip = (int *) get_property(np, "interrupts", &l)) != 0) {
		unsigned int devfn = pci_addrs[0].addr.a_hi & 0xff00;
		unsigned int cell_size;
		struct device_node* np2;
		/* This is hackish, but is only used for BootX booting */
		cell_size = sizeof(struct pci_intr_map);
		np2 = np->parent;
		while(np2) {
			if (device_is_compatible(np2, "uni-north")) {
				cell_size += 4;
				break;
			}
			np2 = np2->parent;
		}
		np->n_intrs = 0;
		np->intrs = (struct interrupt_info *) mem_start;
		for (i = 0; (ml -= cell_size) >= 0; ++i) {
			if (imp->addr.a_hi == devfn) {
				np->intrs[np->n_intrs].line = imp->intr;
				np->intrs[np->n_intrs].sense = 0; /* FIXME */
				++np->n_intrs;
			}
			imp = (struct pci_intr_map *)(((unsigned int)imp)
				+ cell_size);
		}
		if (np->n_intrs == 0)
			np->intrs = 0;
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		return mem_start;
	}

	ip = (int *) get_property(np, "AAPL,interrupts", &l);
	if (ip == 0)
		ip = (int *) get_property(np, "interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / sizeof(int);
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = 0;
		}
	}

	return mem_start;
}

__init
static unsigned long
interpret_dbdma_props(struct device_node *np, unsigned long mem_start,
		      int naddrc, int nsizec)
{
	struct reg_property *rp;
	struct address_range *adr;
	unsigned long base_address;
	int i, l, *ip;
	struct device_node *db;

	base_address = 0;
	for (db = np->parent; db != NULL; db = db->parent) {
		if (!strcmp(db->type, "dbdma") && db->n_addrs != 0) {
			base_address = db->addrs[0].address;
			break;
		}
	}

	rp = (struct reg_property *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct reg_property)) >= 0) {
			adr[i].space = 0;
			adr[i].address = rp[i].address + base_address;
			adr[i].size = rp[i].size;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;

	ip = (int *) get_property(np, "AAPL,interrupts", &l);
	if (ip == 0)
		ip = (int *) get_property(np, "interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / sizeof(int);
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = 0;
		}
	}

	return mem_start;
}

__init
static unsigned long
interpret_macio_props(struct device_node *np, unsigned long mem_start,
		      int naddrc, int nsizec)
{
	struct reg_property *rp;
	struct address_range *adr;
	unsigned long base_address;
	int i, l, keylargo, *ip;
	struct device_node *db;

	base_address = 0;
	for (db = np->parent; db != NULL; db = db->parent) {
		if (!strcmp(db->type, "mac-io") && db->n_addrs != 0) {
			base_address = db->addrs[0].address;
			keylargo = device_is_compatible(db, "Keylargo");
			break;
		}
	}

	rp = (struct reg_property *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct reg_property)) >= 0) {
			adr[i].space = 0;
			adr[i].address = rp[i].address + base_address;
			adr[i].size = rp[i].size;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;

	ip = (int *) get_property(np, "interrupts", &l);
	if (ip == 0)
		ip = (int *) get_property(np, "AAPL,interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		if (_machine == _MACH_Pmac) {
			/* for the iMac */
			np->n_intrs = l / sizeof(int);
			/* Hack for BootX on Core99 */
			if (keylargo)
				np->n_intrs = np->n_intrs/2;
			for (i = 0; i < np->n_intrs; ++i) {
				np->intrs[i].line = *ip++;
				if (keylargo)
					np->intrs[i].sense = *ip++;
				else
					np->intrs[i].sense = 0;
			}
		} else {
			/* CHRP machines */
			np->n_intrs = l / (2 * sizeof(int));
			for (i = 0; i < np->n_intrs; ++i) {
				np->intrs[i].line = openpic_to_irq(*ip++);
				np->intrs[i].sense = *ip++;
			}
		}
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
	}

	return mem_start;
}

__init
static unsigned long
interpret_isa_props(struct device_node *np, unsigned long mem_start,
		    int naddrc, int nsizec)
{
	struct isa_reg_property *rp;
	struct address_range *adr;
	int i, l, *ip;

	rp = (struct isa_reg_property *) get_property(np, "reg", &l);
	if (rp != 0 && l >= sizeof(struct isa_reg_property)) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= sizeof(struct reg_property)) >= 0) {
			adr[i].space = rp[i].space;
			adr[i].address = rp[i].address
				+ (adr[i].space? 0: _ISA_MEM_BASE);
			adr[i].size = rp[i].size;
			++i;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;
 
	ip = (int *) get_property(np, "interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / (2 * sizeof(int));
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = *ip++;
		}
	}

	return mem_start;
}

__init
static unsigned long
interpret_root_props(struct device_node *np, unsigned long mem_start,
		     int naddrc, int nsizec)
{
	struct address_range *adr;
	int i, l, *ip;
	unsigned int *rp;
	int rpsize = (naddrc + nsizec) * sizeof(unsigned int);

	rp = (unsigned int *) get_property(np, "reg", &l);
	if (rp != 0 && l >= rpsize) {
		i = 0;
		adr = (struct address_range *) mem_start;
		while ((l -= rpsize) >= 0) {
			adr[i].space = 0;
			adr[i].address = rp[naddrc - 1];
			adr[i].size = rp[naddrc + nsizec - 1];
			++i;
			rp += naddrc + nsizec;
		}
		np->addrs = adr;
		np->n_addrs = i;
		mem_start += i * sizeof(struct address_range);
	}

	if (use_of_interrupt_tree)
		return mem_start;

	ip = (int *) get_property(np, "AAPL,interrupts", &l);
	if (ip == 0)
		ip = (int *) get_property(np, "interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / sizeof(int);
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = 0;
		}
	}

	return mem_start;
}

/*
 * Construct and return a list of the device_nodes with a given name.
 */
__openfirmware
struct device_node *
find_devices(const char *name)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (np->name != 0 && strcasecmp(np->name, name) == 0) {
			*prevp = np;
			prevp = &np->next;
		}
	}
	*prevp = 0;
	return head;
}

/*
 * Construct and return a list of the device_nodes with a given type.
 */
__openfirmware
struct device_node *
find_type_devices(const char *type)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (np->type != 0 && strcasecmp(np->type, type) == 0) {
			*prevp = np;
			prevp = &np->next;
		}
	}
	*prevp = 0;
	return head;
}

/* Finds a device node given its PCI bus number, device number
 * and function number
 */
__openfirmware
struct device_node *
find_pci_device_OFnode(unsigned char bus, unsigned char dev_fn)
{
	struct device_node* np;
	unsigned int *reg;
	int l;
	
	for (np = allnodes; np != 0; np = np->allnext) {
		int in_macio = 0;
		struct device_node* parent = np->parent;
		while(parent) {
			char *pname = (char *)get_property(parent, "name", &l);
			if (pname && strcmp(pname, "mac-io") == 0) {
				in_macio = 1;
				break;
			}
			parent = parent->parent;
		}
		if (in_macio)
			continue;
		reg = (unsigned int *) get_property(np, "reg", &l);
		if (reg == 0 || l < sizeof(struct reg_property))
			continue;
		if (((reg[0] >> 8) & 0xff) == dev_fn && ((reg[0] >> 16) & 0xff) == bus)
			break;
	}
	return np;
}

/*
 * Returns all nodes linked together
 */
__openfirmware
struct device_node *
find_all_nodes(void)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		*prevp = np;
		prevp = &np->next;
	}
	*prevp = 0;
	return head;
}

/* Checks if the given "compat" string matches one of the strings in
 * the device's "compatible" property
 */
__openfirmware
int
device_is_compatible(struct device_node *device, const char *compat)
{
	const char* cp;
	int cplen, l;

	cp = (char *) get_property(device, "compatible", &cplen);
	if (cp == NULL)
		return 0;
	while (cplen > 0) {
		if (strncasecmp(cp, compat, strlen(compat)) == 0)
			return 1;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
	}

	return 0;
}


/*
 * Indicates whether the root node has a given value in its
 * compatible property.
 */
__openfirmware
int
machine_is_compatible(const char *compat)
{
	struct device_node *root;
	
	root = find_path_device("/");
	if (root == 0)
		return 0;
	return device_is_compatible(root, compat);
}

/*
 * Construct and return a list of the device_nodes with a given type
 * and compatible property.
 */
__openfirmware
struct device_node *
find_compatible_devices(const char *type, const char *compat)
{
	struct device_node *head, **prevp, *np;

	prevp = &head;
	for (np = allnodes; np != 0; np = np->allnext) {
		if (type != NULL
		    && !(np->type != 0 && strcasecmp(np->type, type) == 0))
			continue;
		if (device_is_compatible(np, compat)) {
			*prevp = np;
			prevp = &np->next;
		}
	}
	*prevp = 0;
	return head;
}

/*
 * Find the device_node with a given full_name.
 */
__openfirmware
struct device_node *
find_path_device(const char *path)
{
	struct device_node *np;

	for (np = allnodes; np != 0; np = np->allnext)
		if (np->full_name != 0 && strcasecmp(np->full_name, path) == 0)
			return np;
	return NULL;
}

/*
 * Find the device_node with a given phandle.
 */
__openfirmware
struct device_node *
find_phandle(phandle ph)
{
	struct device_node *np;

	for (np = allnodes; np != 0; np = np->allnext)
		if (np->node == ph)
			return np;
	return NULL;
}

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
__openfirmware
unsigned char *
get_property(struct device_node *np, const char *name, int *lenp)
{
	struct property *pp;

	for (pp = np->properties; pp != 0; pp = pp->next)
		if (strcmp(pp->name, name) == 0) {
			if (lenp != 0)
				*lenp = pp->length;
			return pp->value;
		}
	return 0;
}

#if 0
__openfirmware
void
print_properties(struct device_node *np)
{
	struct property *pp;
	char *cp;
	int i, n;

	for (pp = np->properties; pp != 0; pp = pp->next) {
		printk(KERN_INFO "%s", pp->name);
		for (i = strlen(pp->name); i < 16; ++i)
			printk(" ");
		cp = (char *) pp->value;
		for (i = pp->length; i > 0; --i, ++cp)
			if ((i > 1 && (*cp < 0x20 || *cp > 0x7e))
			    || (i == 1 && *cp != 0))
				break;
		if (i == 0 && pp->length > 1) {
			/* looks like a string */
			printk(" %s\n", (char *) pp->value);
		} else {
			/* dump it in hex */
			n = pp->length;
			if (n > 64)
				n = 64;
			if (pp->length % 4 == 0) {
				unsigned int *p = (unsigned int *) pp->value;

				n /= 4;
				for (i = 0; i < n; ++i) {
					if (i != 0 && (i % 4) == 0)
						printk("\n                ");
					printk(" %08x", *p++);
				}
			} else {
				unsigned char *bp = pp->value;

				for (i = 0; i < n; ++i) {
					if (i != 0 && (i % 16) == 0)
						printk("\n                ");
					printk(" %02x", *bp++);
				}
			}
			printk("\n");
			if (pp->length > 64)
				printk("                 ... (length = %d)\n",
				       pp->length);
		}
	}
}
#endif

spinlock_t rtas_lock = SPIN_LOCK_UNLOCKED;

/* this can be called after setup -- Cort */
__openfirmware
int
call_rtas(const char *service, int nargs, int nret,
	  unsigned long *outputs, ...)
{
	va_list list;
	int i;
	unsigned long s;
	struct device_node *rtas;
	int *tokp;
	union {
		unsigned long words[16];
		double align;
	} u;

	rtas = find_devices("rtas");
	if (rtas == NULL)
		return -1;
	tokp = (int *) get_property(rtas, service, NULL);
	if (tokp == NULL) {
		printk(KERN_ERR "No RTAS service called %s\n", service);
		return -1;
	}
	u.words[0] = *tokp;
	u.words[1] = nargs;
	u.words[2] = nret;
	va_start(list, outputs);
	for (i = 0; i < nargs; ++i)
		u.words[i+3] = va_arg(list, unsigned long);
	va_end(list);

	spin_lock_irqsave(&rtas_lock, s);
	enter_rtas((void *)__pa(&u));
	spin_unlock_irqrestore(&rtas_lock, s);

	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = u.words[i+nargs+4];
	return u.words[nargs+3];
}

__init
void
abort()
{
#ifdef CONFIG_XMON
	xmon(NULL);
#endif
	for (;;)
		prom_exit();
}

#ifdef CONFIG_BOOTX_TEXT

/* Here's a small text engine to use during early boot or for debugging purposes
 * 
 * todo:
 * 
 *  - build some kind of vgacon with it to enable early printk
 *  - move to a separate file
 *  - add a few video driver hooks to keep in sync with display
 *    changes.
 */

void
map_bootx_text(void)
{
	unsigned long base, offset, size;
	if (disp_bi == 0)
		return;
	base = ((unsigned long) disp_bi->dispDeviceBase) & 0xFFFFF000UL;
	offset = ((unsigned long) disp_bi->dispDeviceBase) - base;
	size = disp_bi->dispDeviceRowBytes * disp_bi->dispDeviceRect[3] + offset
		+ disp_bi->dispDeviceRect[0];
	disp_bi->logicalDisplayBase = ioremap(base, size);
	if (disp_bi->logicalDisplayBase == 0)
		return;
	disp_bi->logicalDisplayBase += offset;
	bootx_text_mapped = 1;
}

/* Calc the base address of a given point (x,y) */
__pmac
static unsigned char *
calc_base(boot_infos_t *bi, int x, int y)
{
	unsigned char *base;

	base = bi->logicalDisplayBase;
	if (base == 0)
		base = bi->dispDeviceBase;
	base += (x + bi->dispDeviceRect[0]) * (bi->dispDeviceDepth >> 3);
	base += (y + bi->dispDeviceRect[1]) * bi->dispDeviceRowBytes;
	return base;
}

/* Adjust the display to a new resolution */
void
bootx_update_display(unsigned long phys, int width, int height,
		     int depth, int pitch)
{
	if (disp_bi == 0)
		return;
	/* check it's the same frame buffer (within 16MB) */
	if ((phys ^ (unsigned long)disp_bi->dispDeviceBase) & 0xff000000)
		return;

	disp_bi->dispDeviceBase = (__u8 *) phys;
	disp_bi->dispDeviceRect[0] = 0;
	disp_bi->dispDeviceRect[1] = 0;
	disp_bi->dispDeviceRect[2] = width;
	disp_bi->dispDeviceRect[3] = height;
	disp_bi->dispDeviceDepth = depth;
	disp_bi->dispDeviceRowBytes = pitch;
	if (bootx_text_mapped) {
		iounmap(disp_bi->logicalDisplayBase);
		bootx_text_mapped = 0;
	}
	map_bootx_text();
	g_loc_X = 0;
	g_loc_Y = 0;
	g_max_loc_X = width / 8;
	g_max_loc_Y = height / 16;
}

__pmac
static void
clearscreen(void)
{
	unsigned long offset	= reloc_offset();
	boot_infos_t* bi	= PTRRELOC(RELOC(disp_bi));
	unsigned long *base	= (unsigned long *)calc_base(bi, 0, 0);
	unsigned long width 	= ((bi->dispDeviceRect[2] - bi->dispDeviceRect[0]) *
					(bi->dispDeviceDepth >> 3)) >> 2;
	int i,j;
	
	for (i=0; i<(bi->dispDeviceRect[3] - bi->dispDeviceRect[1]); i++)
	{
		unsigned long *ptr = base;
		for(j=width; j; --j)
			*(ptr++) = 0;
		base += (bi->dispDeviceRowBytes >> 2);
	}
}

__inline__ void dcbst(const void* addr)
{
	__asm__ __volatile__ ("dcbst 0,%0" :: "r" (addr));
}

__pmac
static void
flushscreen(void)
{
	unsigned long offset	= reloc_offset();
	boot_infos_t* bi	= PTRRELOC(RELOC(disp_bi));
	unsigned long *base	= (unsigned long *)calc_base(bi, 0, 0);
	unsigned long width 	= ((bi->dispDeviceRect[2] - bi->dispDeviceRect[0]) *
					(bi->dispDeviceDepth >> 3)) >> 2;
	int i,j;
	
	for (i=0; i<(bi->dispDeviceRect[3] - bi->dispDeviceRect[1]); i++)
	{
		unsigned long *ptr = base;
		for(j=width; j>0; j-=8) {
			dcbst(ptr);
			ptr += 8;
		}
		base += (bi->dispDeviceRowBytes >> 2);
	}
}

#ifndef NO_SCROLL
__pmac
static void
scrollscreen(void)
{
	unsigned long offset		= reloc_offset();
	boot_infos_t* bi		= PTRRELOC(RELOC(disp_bi));
	unsigned long *src		= (unsigned long *)calc_base(bi,0,16);
	unsigned long *dst		= (unsigned long *)calc_base(bi,0,0);
	unsigned long width		= ((bi->dispDeviceRect[2] - bi->dispDeviceRect[0]) *
						(bi->dispDeviceDepth >> 3)) >> 2;
	int i,j;
	
#ifdef CONFIG_ADB_PMU
	pmu_suspend();	/* PMU will not shut us down ! */
#endif
	for (i=0; i<(bi->dispDeviceRect[3] - bi->dispDeviceRect[1] - 16); i++)
	{
		unsigned long *src_ptr = src;
		unsigned long *dst_ptr = dst;
		for(j=width; j; --j)
			*(dst_ptr++) = *(src_ptr++);
		src += (bi->dispDeviceRowBytes >> 2);
		dst += (bi->dispDeviceRowBytes >> 2);
	}
	for (i=0; i<16; i++)
	{
		unsigned long *dst_ptr = dst;
		for(j=width; j; --j)
			*(dst_ptr++) = 0;
		dst += (bi->dispDeviceRowBytes >> 2);
	}
#ifdef CONFIG_ADB_PMU
	pmu_resume();	/* PMU will not shut us down ! */
#endif
}
#endif /* ndef NO_SCROLL */

__pmac
void
prom_drawchar(char c)
{
	unsigned long offset = reloc_offset();
	int cline = 0, x;

	if (!RELOC(bootx_text_mapped))
		return;

	switch (c) {
	case '\b':
		if (RELOC(g_loc_X) > 0)
			--RELOC(g_loc_X);
		break;
	case '\t':
		RELOC(g_loc_X) = (RELOC(g_loc_X) & -8) + 8;
		break;
	case '\r':
		RELOC(g_loc_X) = 0;
		break;
	case '\n':
		RELOC(g_loc_X) = 0;
		RELOC(g_loc_Y)++;
		cline = 1;
		break;
	default:
		draw_byte(c, RELOC(g_loc_X)++, RELOC(g_loc_Y));
	}
	if (RELOC(g_loc_X) >= RELOC(g_max_loc_X)) {
		RELOC(g_loc_X) = 0;
		RELOC(g_loc_Y)++;
		cline = 1;
	}
#ifndef NO_SCROLL
	while (RELOC(g_loc_Y) >= RELOC(g_max_loc_Y)) {
		scrollscreen();
		RELOC(g_loc_Y)--;
	}
#else
	/* wrap around from bottom to top of screen so we don't
	   waste time scrolling each line.  -- paulus. */
	if (RELOC(g_loc_Y) >= RELOC(g_max_loc_Y))
		RELOC(g_loc_Y) = 0;
	if (cline) {
		for (x = 0; x < RELOC(g_max_loc_X); ++x)
			draw_byte(' ', x, RELOC(g_loc_Y));
	}
#endif
}

__pmac
void
prom_drawstring(const char *c)
{
	unsigned long offset	= reloc_offset();

	if (!RELOC(bootx_text_mapped))
		return;
	while (*c)
		prom_drawchar(*c++);
}

__pmac
void
prom_drawhex(unsigned long v)
{
	static char hex_table[] = "0123456789abcdef";
	unsigned long offset	= reloc_offset();
	
	if (!RELOC(bootx_text_mapped))
		return;
	prom_drawchar(RELOC(hex_table)[(v >> 28) & 0x0000000FUL]);
	prom_drawchar(RELOC(hex_table)[(v >> 24) & 0x0000000FUL]);
	prom_drawchar(RELOC(hex_table)[(v >> 20) & 0x0000000FUL]);
	prom_drawchar(RELOC(hex_table)[(v >> 16) & 0x0000000FUL]);
	prom_drawchar(RELOC(hex_table)[(v >> 12) & 0x0000000FUL]);
	prom_drawchar(RELOC(hex_table)[(v >>  8) & 0x0000000FUL]);
	prom_drawchar(RELOC(hex_table)[(v >>  4) & 0x0000000FUL]);
	prom_drawchar(RELOC(hex_table)[(v >>  0) & 0x0000000FUL]);
}


__pmac
static void
draw_byte(unsigned char c, long locX, long locY)
{
	unsigned long offset	= reloc_offset();
	boot_infos_t* bi	= PTRRELOC(RELOC(disp_bi));
	unsigned char *base	= calc_base(bi, locX << 3, locY << 4);
	unsigned char *font	= &RELOC(vga_font)[((unsigned long)c) * 16];
	int rb			= bi->dispDeviceRowBytes;
	
	switch(bi->dispDeviceDepth) {
		case 32:
			draw_byte_32(font, (unsigned long *)base, rb);
			break;
		case 16:
			draw_byte_16(font, (unsigned long *)base, rb);
			break;
		case 8:
			draw_byte_8(font, (unsigned long *)base, rb);
			break;
		default:
			break;
	}
}

__pmac
static unsigned long expand_bits_8[16] = {
	0x00000000,
	0x000000ff,
	0x0000ff00,
	0x0000ffff,
	0x00ff0000,
	0x00ff00ff,
	0x00ffff00,
	0x00ffffff,
	0xff000000,
	0xff0000ff,
	0xff00ff00,
	0xff00ffff,
	0xffff0000,
	0xffff00ff,
	0xffffff00,
	0xffffffff
};

__pmac
static unsigned long expand_bits_16[4] = {
	0x00000000,
	0x0000ffff,
	0xffff0000,
	0xffffffff
};


__pmac
static void
draw_byte_32(unsigned char *font, unsigned long *base, int rb)
{
	int l, bits;	
	int fg = 0xFFFFFFFFUL;
	int bg = 0x00000000UL;
	
	for (l = 0; l < 16; ++l)
	{
		bits = *font++;
		base[0] = (-(bits >> 7) & fg) ^ bg;
		base[1] = (-((bits >> 6) & 1) & fg) ^ bg;
		base[2] = (-((bits >> 5) & 1) & fg) ^ bg;
		base[3] = (-((bits >> 4) & 1) & fg) ^ bg;
		base[4] = (-((bits >> 3) & 1) & fg) ^ bg;
		base[5] = (-((bits >> 2) & 1) & fg) ^ bg;
		base[6] = (-((bits >> 1) & 1) & fg) ^ bg;
		base[7] = (-(bits & 1) & fg) ^ bg;
		base = (unsigned long *) ((char *)base + rb);
	}
}

__pmac
static void
draw_byte_16(unsigned char *font, unsigned long *base, int rb)
{
	int l, bits;	
	int fg = 0xFFFFFFFFUL;
	int bg = 0x00000000UL;
	unsigned long offset = reloc_offset();
	unsigned long *eb = RELOC(expand_bits_16);

	for (l = 0; l < 16; ++l)
	{
		bits = *font++;
		base[0] = (eb[bits >> 6] & fg) ^ bg;
		base[1] = (eb[(bits >> 4) & 3] & fg) ^ bg;
		base[2] = (eb[(bits >> 2) & 3] & fg) ^ bg;
		base[3] = (eb[bits & 3] & fg) ^ bg;
		base = (unsigned long *) ((char *)base + rb);
	}
}

__pmac
static void
draw_byte_8(unsigned char *font, unsigned long *base, int rb)
{
	int l, bits;	
	int fg = 0x0F0F0F0FUL;
	int bg = 0x00000000UL;
	unsigned long offset = reloc_offset();
	unsigned long *eb = RELOC(expand_bits_8);

	for (l = 0; l < 16; ++l)
	{
		bits = *font++;
		base[0] = (eb[bits >> 4] & fg) ^ bg;
		base[1] = (eb[bits & 0xf] & fg) ^ bg;
		base = (unsigned long *) ((char *)base + rb);
	}
}

__pmac
static unsigned char vga_font[cmapsz] = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x81, 0xa5, 0x81, 0x81, 0xbd, 
0x99, 0x81, 0x81, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0xff, 
0xdb, 0xff, 0xff, 0xc3, 0xe7, 0xff, 0xff, 0x7e, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x6c, 0xfe, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x10, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x7c, 0xfe, 
0x7c, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 
0x3c, 0x3c, 0xe7, 0xe7, 0xe7, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x18, 0x3c, 0x7e, 0xff, 0xff, 0x7e, 0x18, 0x18, 0x3c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3c, 
0x3c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xe7, 0xc3, 0xc3, 0xe7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x66, 0x3c, 0x00, 
0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc3, 0x99, 0xbd, 
0xbd, 0x99, 0xc3, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x1e, 0x0e, 
0x1a, 0x32, 0x78, 0xcc, 0xcc, 0xcc, 0xcc, 0x78, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x3c, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x7e, 0x18, 0x18, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x33, 0x3f, 0x30, 0x30, 0x30, 
0x30, 0x70, 0xf0, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x63, 
0x7f, 0x63, 0x63, 0x63, 0x63, 0x67, 0xe7, 0xe6, 0xc0, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x18, 0x18, 0xdb, 0x3c, 0xe7, 0x3c, 0xdb, 0x18, 0x18, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfe, 0xf8, 
0xf0, 0xe0, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x06, 0x0e, 
0x1e, 0x3e, 0xfe, 0x3e, 0x1e, 0x0e, 0x06, 0x02, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x18, 0x3c, 0x7e, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 
0x66, 0x00, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xdb, 
0xdb, 0xdb, 0x7b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x7c, 0xc6, 0x60, 0x38, 0x6c, 0xc6, 0xc6, 0x6c, 0x38, 0x0c, 0xc6, 
0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0xfe, 0xfe, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3c, 
0x7e, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18, 0x7e, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x18, 0x3c, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x7e, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x18, 0x0c, 0xfe, 0x0c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x60, 0xfe, 0x60, 0x30, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xc0, 
0xc0, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x24, 0x66, 0xff, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x38, 0x7c, 0x7c, 0xfe, 0xfe, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0x7c, 0x7c, 
0x38, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x18, 0x3c, 0x3c, 0x3c, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x24, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6c, 
0x6c, 0xfe, 0x6c, 0x6c, 0x6c, 0xfe, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00, 
0x18, 0x18, 0x7c, 0xc6, 0xc2, 0xc0, 0x7c, 0x06, 0x06, 0x86, 0xc6, 0x7c, 
0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc2, 0xc6, 0x0c, 0x18, 
0x30, 0x60, 0xc6, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x6c, 
0x6c, 0x38, 0x76, 0xdc, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x30, 0x30, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x30, 
0x30, 0x30, 0x18, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x18, 
0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x3c, 0xff, 0x3c, 0x66, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x7e, 
0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x02, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xce, 0xde, 0xf6, 0xe6, 0xc6, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x38, 0x78, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 
0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0xc6, 0xfe, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x7c, 0xc6, 0x06, 0x06, 0x3c, 0x06, 0x06, 0x06, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x1c, 0x3c, 0x6c, 0xcc, 0xfe, 
0x0c, 0x0c, 0x0c, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xc0, 
0xc0, 0xc0, 0xfc, 0x06, 0x06, 0x06, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x38, 0x60, 0xc0, 0xc0, 0xfc, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xc6, 0x06, 0x06, 0x0c, 0x18, 
0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 
0xc6, 0xc6, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0x7e, 0x06, 0x06, 0x06, 0x0c, 0x78, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 
0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0c, 0x06, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 
0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 
0x30, 0x18, 0x0c, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x7c, 0xc6, 0xc6, 0x0c, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xde, 0xde, 
0xde, 0xdc, 0xc0, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 
0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xfc, 0x66, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x66, 0x66, 0xfc, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x66, 0xc2, 0xc0, 0xc0, 0xc0, 
0xc0, 0xc2, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x6c, 
0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x6c, 0xf8, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xfe, 0x66, 0x62, 0x68, 0x78, 0x68, 0x60, 0x62, 0x66, 0xfe, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x66, 0x62, 0x68, 0x78, 0x68, 
0x60, 0x60, 0x60, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x66, 
0xc2, 0xc0, 0xc0, 0xde, 0xc6, 0xc6, 0x66, 0x3a, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x0c, 
0x0c, 0x0c, 0x0c, 0x0c, 0xcc, 0xcc, 0xcc, 0x78, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xe6, 0x66, 0x66, 0x6c, 0x78, 0x78, 0x6c, 0x66, 0x66, 0xe6, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x60, 0x60, 0x60, 0x60, 0x60, 
0x60, 0x62, 0x66, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0xe7, 
0xff, 0xff, 0xdb, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xc6, 0xe6, 0xf6, 0xfe, 0xde, 0xce, 0xc6, 0xc6, 0xc6, 0xc6, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 
0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x66, 
0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0x60, 0xf0, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xd6, 0xde, 0x7c, 
0x0c, 0x0e, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x66, 0x66, 0x66, 0x7c, 0x6c, 
0x66, 0x66, 0x66, 0xe6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 
0xc6, 0x60, 0x38, 0x0c, 0x06, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xff, 0xdb, 0x99, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 
0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0xc3, 
0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xdb, 0xdb, 0xff, 0x66, 0x66, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0xc3, 0x66, 0x3c, 0x18, 0x18, 
0x3c, 0x66, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0xc3, 
0xc3, 0x66, 0x3c, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xff, 0xc3, 0x86, 0x0c, 0x18, 0x30, 0x60, 0xc1, 0xc3, 0xff, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x30, 0x30, 0x30, 0x30, 0x30, 
0x30, 0x30, 0x30, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 
0xc0, 0xe0, 0x70, 0x38, 0x1c, 0x0e, 0x06, 0x02, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3c, 
0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x6c, 0xc6, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 
0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x0c, 0x7c, 
0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x60, 
0x60, 0x78, 0x6c, 0x66, 0x66, 0x66, 0x66, 0x7c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 0xc0, 0xc0, 0xc0, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x0c, 0x0c, 0x3c, 0x6c, 0xcc, 
0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x7c, 0xc6, 0xfe, 0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x38, 0x6c, 0x64, 0x60, 0xf0, 0x60, 0x60, 0x60, 0x60, 0xf0, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xcc, 0xcc, 
0xcc, 0xcc, 0xcc, 0x7c, 0x0c, 0xcc, 0x78, 0x00, 0x00, 0x00, 0xe0, 0x60, 
0x60, 0x6c, 0x76, 0x66, 0x66, 0x66, 0x66, 0xe6, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x18, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06, 0x00, 0x0e, 0x06, 0x06, 
0x06, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3c, 0x00, 0x00, 0x00, 0xe0, 0x60, 
0x60, 0x66, 0x6c, 0x78, 0x78, 0x6c, 0x66, 0xe6, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0xff, 0xdb, 
0xdb, 0xdb, 0xdb, 0xdb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0xdc, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdc, 0x66, 0x66, 
0x66, 0x66, 0x66, 0x7c, 0x60, 0x60, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x76, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x7c, 0x0c, 0x0c, 0x1e, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0xdc, 0x76, 0x66, 0x60, 0x60, 0x60, 0xf0, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 0x60, 
0x38, 0x0c, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x30, 
0x30, 0xfc, 0x30, 0x30, 0x30, 0x30, 0x36, 0x1c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0xc3, 0xc3, 
0xc3, 0x66, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0xc3, 0xc3, 0xc3, 0xdb, 0xdb, 0xff, 0x66, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0xc3, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc6, 0xc6, 0xc6, 
0xc6, 0xc6, 0xc6, 0x7e, 0x06, 0x0c, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0xfe, 0xcc, 0x18, 0x30, 0x60, 0xc6, 0xfe, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x0e, 0x18, 0x18, 0x18, 0x70, 0x18, 0x18, 0x18, 0x18, 0x0e, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x18, 
0x18, 0x18, 0x0e, 0x18, 0x18, 0x18, 0x18, 0x70, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x76, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x6c, 0xc6, 
0xc6, 0xc6, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x66, 
0xc2, 0xc0, 0xc0, 0xc0, 0xc2, 0x66, 0x3c, 0x0c, 0x06, 0x7c, 0x00, 0x00, 
0x00, 0x00, 0xcc, 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x18, 0x30, 0x00, 0x7c, 0xc6, 0xfe, 
0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x6c, 
0x00, 0x78, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xcc, 0x00, 0x00, 0x78, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x76, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x30, 0x18, 0x00, 0x78, 0x0c, 0x7c, 
0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x6c, 0x38, 
0x00, 0x78, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x3c, 0x66, 0x60, 0x60, 0x66, 0x3c, 0x0c, 0x06, 
0x3c, 0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x6c, 0x00, 0x7c, 0xc6, 0xfe, 
0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc6, 0x00, 
0x00, 0x7c, 0xc6, 0xfe, 0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x60, 0x30, 0x18, 0x00, 0x7c, 0xc6, 0xfe, 0xc0, 0xc0, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x00, 0x00, 0x38, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3c, 0x66, 
0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x60, 0x30, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0xc6, 0x00, 0x10, 0x38, 0x6c, 0xc6, 0xc6, 
0xfe, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00, 0x38, 0x6c, 0x38, 0x00, 
0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00, 
0x18, 0x30, 0x60, 0x00, 0xfe, 0x66, 0x60, 0x7c, 0x60, 0x60, 0x66, 0xfe, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6e, 0x3b, 0x1b, 
0x7e, 0xd8, 0xdc, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x6c, 
0xcc, 0xcc, 0xfe, 0xcc, 0xcc, 0xcc, 0xcc, 0xce, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x10, 0x38, 0x6c, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc6, 0x00, 0x00, 0x7c, 0xc6, 0xc6, 
0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x30, 0x18, 
0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x30, 0x78, 0xcc, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x30, 0x18, 0x00, 0xcc, 0xcc, 0xcc, 
0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc6, 0x00, 
0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7e, 0x06, 0x0c, 0x78, 0x00, 
0x00, 0xc6, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0xc6, 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 
0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x7e, 
0xc3, 0xc0, 0xc0, 0xc0, 0xc3, 0x7e, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x38, 0x6c, 0x64, 0x60, 0xf0, 0x60, 0x60, 0x60, 0x60, 0xe6, 0xfc, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x66, 0x3c, 0x18, 0xff, 0x18, 
0xff, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x66, 0x66, 
0x7c, 0x62, 0x66, 0x6f, 0x66, 0x66, 0x66, 0xf3, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x0e, 0x1b, 0x18, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 
0xd8, 0x70, 0x00, 0x00, 0x00, 0x18, 0x30, 0x60, 0x00, 0x78, 0x0c, 0x7c, 
0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x18, 0x30, 
0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x18, 0x30, 0x60, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x30, 0x60, 0x00, 0xcc, 0xcc, 0xcc, 
0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xdc, 
0x00, 0xdc, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 
0x76, 0xdc, 0x00, 0xc6, 0xe6, 0xf6, 0xfe, 0xde, 0xce, 0xc6, 0xc6, 0xc6, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x6c, 0x6c, 0x3e, 0x00, 0x7e, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x6c, 0x6c, 
0x38, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x60, 0xc0, 0xc6, 0xc6, 0x7c, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xc0, 
0xc0, 0xc0, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0xfe, 0x06, 0x06, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0xc0, 0xc0, 0xc2, 0xc6, 0xcc, 0x18, 0x30, 0x60, 0xce, 0x9b, 0x06, 
0x0c, 0x1f, 0x00, 0x00, 0x00, 0xc0, 0xc0, 0xc2, 0xc6, 0xcc, 0x18, 0x30, 
0x66, 0xce, 0x96, 0x3e, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 
0x00, 0x18, 0x18, 0x18, 0x3c, 0x3c, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x6c, 0xd8, 0x6c, 0x36, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd8, 0x6c, 0x36, 
0x6c, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x44, 0x11, 0x44, 
0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 
0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 
0x55, 0xaa, 0x55, 0xaa, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 
0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x18, 0xf8, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x36, 0x36, 0xf6, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x36, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x18, 0xf8, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x36, 0x36, 0x36, 0x36, 
0x36, 0xf6, 0x06, 0xf6, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x36, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x06, 0xf6, 
0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
0x36, 0xf6, 0x06, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0xfe, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x18, 0xf8, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0xf8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x18, 0x1f, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x37, 
0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x37, 0x30, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x30, 0x37, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0xf7, 0x00, 0xff, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0xff, 0x00, 0xf7, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x36, 0x36, 0x36, 0x36, 0x37, 0x30, 0x37, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x36, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x36, 0x36, 0x36, 
0x36, 0xf7, 0x00, 0xf7, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0xff, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0xff, 0x00, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x3f, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x1f, 0x18, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x18, 0x1f, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 
0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
0x36, 0x36, 0x36, 0xff, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x18, 0xff, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xf0, 0xf0, 0xf0, 
0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 
0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 
0x0f, 0x0f, 0x0f, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x76, 0xdc, 0xd8, 0xd8, 0xd8, 0xdc, 0x76, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x78, 0xcc, 0xcc, 0xcc, 0xd8, 0xcc, 0xc6, 0xc6, 0xc6, 0xcc, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xc6, 0xc6, 0xc0, 0xc0, 0xc0, 
0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0xfe, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0xfe, 0xc6, 0x60, 0x30, 0x18, 0x30, 0x60, 0xc6, 0xfe, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0xd8, 0xd8, 
0xd8, 0xd8, 0xd8, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x66, 0x66, 0x66, 0x66, 0x66, 0x7c, 0x60, 0x60, 0xc0, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x76, 0xdc, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x18, 0x3c, 0x66, 0x66, 
0x66, 0x3c, 0x18, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 
0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0x6c, 0x38, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x38, 0x6c, 0xc6, 0xc6, 0xc6, 0x6c, 0x6c, 0x6c, 0x6c, 0xee, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x30, 0x18, 0x0c, 0x3e, 0x66, 
0x66, 0x66, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x7e, 0xdb, 0xdb, 0xdb, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x03, 0x06, 0x7e, 0xdb, 0xdb, 0xf3, 0x7e, 0x60, 0xc0, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x30, 0x60, 0x60, 0x7c, 0x60, 
0x60, 0x60, 0x30, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 
0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0xfe, 0x00, 0x00, 0xfe, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x7e, 0x18, 
0x18, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 
0x18, 0x0c, 0x06, 0x0c, 0x18, 0x30, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x0c, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0c, 0x00, 0x7e, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x1b, 0x1b, 0x1b, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
0x18, 0x18, 0x18, 0x18, 0xd8, 0xd8, 0xd8, 0x70, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x7e, 0x00, 0x18, 0x18, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xdc, 0x00, 
0x76, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x6c, 0x6c, 
0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x0c, 0x0c, 
0x0c, 0x0c, 0x0c, 0xec, 0x6c, 0x6c, 0x3c, 0x1c, 0x00, 0x00, 0x00, 0x00, 
0x00, 0xd8, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0xd8, 0x30, 0x60, 0xc8, 0xf8, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x7c, 0x7c, 0x7c, 0x7c, 0x7c, 0x7c, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x00, 
};

#endif /* CONFIG_BOOTX_TEXT */

