/*
 * BK Id: SCCS/s.prom.c 1.42 09/08/01 15:47:42 paulus
 */
/*
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

#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/bootx.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/btext.h>
#include "open_pic.h"

#ifdef CONFIG_FB
#include <asm/linux_logo.h>
#endif

/*
 * Properties whose value is longer than this get excluded from our
 * copy of the device tree.  This way we don't waste space storing
 * things like "driver,AAPL,MacOS,PowerPC" properties.  But this value
 * does need to be big enough to ensure that we don't lose things
 * like the interrupt-map property on a PCI-PCI bridge.
 */
#define MAX_PROPERTY_LENGTH	4096

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
phandle prom_display_nodes[FB_MAX] __initdata;
unsigned int prom_num_displays __initdata = 0;
char *of_stdout_device __initdata = 0;
ihandle prom_disp_node __initdata = 0;

prom_entry prom __initdata = 0;
ihandle prom_chosen __initdata = 0;
ihandle prom_stdout __initdata = 0;

extern char *klimit;
char *bootpath;
char *bootdevice;

unsigned int rtas_data;   /* physical pointer */
unsigned int rtas_entry;  /* physical pointer */
unsigned int rtas_size;
unsigned int old_rtas;

/* Set for a newworld or CHRP machine */
int use_of_interrupt_tree;
struct device_node *dflt_interrupt_controller;
int num_interrupt_controllers;

int pmac_newworld;

static struct device_node *allnodes;

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
static struct device_node *find_phandle(phandle);

#ifdef CONFIG_BOOTX_TEXT
static void setup_disp_fake_bi(ihandle dp);
#endif

extern void enter_rtas(void *);
void phys_call_rtas(int, int, int, ...);

extern char cmd_line[512];	/* XXX */
boot_infos_t *boot_infos;
unsigned long dev_tree_size;

#define ALIGN(x) (((x) + sizeof(unsigned long)-1) & -sizeof(unsigned long))

/* Is boot-info compatible ? */
#define BOOT_INFO_IS_COMPATIBLE(bi)		((bi)->compatible_version <= BOOT_INFO_VERSION)
#define BOOT_INFO_IS_V2_COMPATIBLE(bi)	((bi)->version >= 2)
#define BOOT_INFO_IS_V4_COMPATIBLE(bi)	((bi)->version >= 4)

/*
 * Note that prom_init() and anything called from prom_init() must
 * use the RELOC/PTRRELOC macros to access any static data in
 * memory, since the kernel may be running at an address that is
 * different from the address that it was linked at.
 * (Note that strings count as static variables.)
 */

static void __init
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

void __init
prom_enter(void)
{
	struct prom_args args;
	unsigned long offset = reloc_offset();

	args.service = RELOC("enter");
	args.nargs = 0;
	args.nret = 0;
	RELOC(prom)(&args);
}

static void * __init
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

void __init
prom_print(const char *msg)
{
	const char *p, *q;
	unsigned long offset = reloc_offset();

	if (RELOC(prom_stdout) == 0)
		return;

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

static void __init
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
static void __init
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
			prom_print(RELOC("\n"));
		}
	}
}
#endif /* CONFIG_SMP */

void __init
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
	btext_init(bi);

	/*
	 * Test if boot-info is compatible.  Done only in config
	 * CONFIG_BOOTX_TEXT since there is nothing much we can do
	 * with an incompatible version, except display a message
	 * and eventually hang the processor...
	 *
	 * I'll try to keep enough of boot-info compatible in the
	 * future to always allow display of this message;
	 */
	if (!BOOT_INFO_IS_COMPATIBLE(bi)) {
		btext_drawstring(RELOC(" !!! WARNING - Incompatible version of BootX !!!\n\n\n"));
		btext_flushscreen();
	}
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
	/*
	 * Note that after we call prepare_disp_BAT, we can't do
	 * prom_draw*, flushscreen or clearscreen until we turn the MMU
	 * on, since prepare_disp_BAT sets disp_bi->logicalDisplayBase
	 * to a virtual address.
	 */
	btext_prepare_BAT();
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
	unsigned int hash, i, vsid;

	vsid = ((va >> 28) * 0x111) << 12;
	hash = ((va ^ vsid) >> 5) & 0x7fff80;
	pteg = (unsigned int *)(htab + (hash & (hsize - 1)));
	for (i = 0; i < 8; ++i, pteg += 4) {
		if ((pteg[1] & 1) == 0) {
			pteg[1] = vsid | ((va >> 16) & 0xf80) | 1;
			pteg[3] = pa | mode;
			break;
		}
	}
}

extern unsigned long _SDR1;
extern PTE *Hash;
extern unsigned long Hash_size;

static void __init
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

static void __init
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
unsigned long __init
prom_init(int r3, int r4, prom_entry pp)
{
	unsigned long mem;
	ihandle prom_mmu;
	unsigned long offset = reloc_offset();
	int l;
	char *p, *d;
 	unsigned long phys;

 	/* Default */
 	phys = offset + KERNELBASE;

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

	mem = check_display(mem);

	prom_print(RELOC("copying OF device tree..."));
	mem = copy_device_tree(mem, mem + (1<<20));
	prom_print(RELOC("done\n"));

#ifdef CONFIG_SMP
	prom_hold_cpus(mem);
#endif

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

	/* Use quiesce call to get OF to shut down any devices it's using */
	prom_print(RELOC("Calling quiesce ...\n"));
	call_prom(RELOC("quiesce"), 0, 0);

#ifdef CONFIG_BOOTX_TEXT
	btext_prepare_BAT();
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

static int __init
prom_set_color(ihandle ih, int i, int r, int g, int b)
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
static unsigned long __init
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

		/*
		 * If this display is the device that OF is using for stdout,
		 * move it to the front of the list.
		 */
		mem += strlen(path) + 1;
		i = RELOC(prom_num_displays)++;
		if (RELOC(of_stdout_device) != 0 && i > 0
		    && strcmp(PTRRELOC(RELOC(of_stdout_device)), path) == 0) {
			for (; i > 0; --i) {
				RELOC(prom_display_paths[i])
					= RELOC(prom_display_paths[i-1]);
				RELOC(prom_display_nodes[i])
					= RELOC(prom_display_nodes[i-1]);
			}
		}
		RELOC(prom_display_paths[i]) = PTRUNRELOC(path);
		RELOC(prom_display_nodes[i]) = node;
		if (i == 0)
			RELOC(prom_disp_node) = node;
		if (RELOC(prom_num_displays) >= FB_MAX)
			break;
	}

try_again:
	/*
	 * Open the first display and set its colormap.
	 */
	if (RELOC(prom_num_displays) > 0) {
		path = PTRRELOC(RELOC(prom_display_paths[0]));
		prom_print(RELOC("opening display "));
		prom_print(path);
		ih = call_prom(RELOC("open"), 1, 1, path);
		if (ih == 0 || ih == (ihandle) -1) {
			prom_print(RELOC("... failed\n"));
			for (i=1; i<RELOC(prom_num_displays); i++) {
				RELOC(prom_display_paths[i-1]) = RELOC(prom_display_paths[i]);
				RELOC(prom_display_nodes[i-1]) = RELOC(prom_display_nodes[i]);
			}	
			if (--RELOC(prom_num_displays) > 0)
				RELOC(prom_disp_node) = RELOC(prom_display_nodes[0]);
			else
				RELOC(prom_disp_node) = NULL;
			goto try_again;
		} else {
			prom_print(RELOC("... ok\n"));
			/*
			 * Setup a usable color table when the appropriate
			 * method is available.
			 * Should update this to use set-colors.
			 */
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
		}
	}

	return ALIGN(mem);
}

/* This function will enable the early boot text when doing OF booting. This
 * way, xmon output should work too
 */
#ifdef CONFIG_BOOTX_TEXT
static void __init
setup_disp_fake_bi(ihandle dp)
{
	int width = 640, height = 480, depth = 8, pitch;
	unsigned address;
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

	btext_setup_display(width, height, depth, pitch, address);
}
#endif

static int __init
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
static unsigned long __init
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

static unsigned long __init
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
	if (np->node != NULL) {
		/* Add a "linux,phandle" property" */
		pp = (struct property *) mem_start;
		*prev_propp = PTRUNRELOC(pp);
		prev_propp = &pp->next;
		namep = (char *) (pp + 1);
		pp->name = PTRUNRELOC(namep);
		strcpy(namep, RELOC("linux,phandle"));
		mem_start = ALIGN((unsigned long)namep + strlen(namep) + 1);
		pp->value = (unsigned char *) PTRUNRELOC(&np->node);
		pp->length = sizeof(np->node);
	}
	*prev_propp = NULL;

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
void __init
finish_device_tree(void)
{
	unsigned long mem = (unsigned long) klimit;
	struct device_node *np;

	/* All newworld pmac machines and CHRPs now use the interrupt tree */
	for (np = allnodes; np != NULL; np = np->allnext) {
		if (get_property(np, "interrupt-parent", 0)) {
			use_of_interrupt_tree = 1;
			break;
		}
	}
	if (_machine == _MACH_Pmac && use_of_interrupt_tree)
		pmac_newworld = 1;

#ifdef CONFIG_BOOTX_TEXT
	if (boot_infos && pmac_newworld) {
		prom_print("WARNING ! BootX/miBoot booting is not supported on this machine\n");
		prom_print("          You should use an Open Firmware bootloader\n");
	}
#endif /* CONFIG_BOOTX_TEXT */

	if (use_of_interrupt_tree) {
		/*
		 * We want to find out here how many interrupt-controller
		 * nodes there are, and if we are booted from BootX,
		 * we need a pointer to the first (and hopefully only)
		 * such node.  But we can't use find_devices here since
		 * np->name has not been set yet.  -- paulus
		 */
		int n = 0;
		char *name, *ic;
		int iclen;

		for (np = allnodes; np != NULL; np = np->allnext) {
			ic = get_property(np, "interrupt-controller", &iclen);
			name = get_property(np, "name", NULL);
			/* checking iclen makes sure we don't get a false
			   match on /chosen.interrupt_controller */
			if ((name != NULL
			     && strcmp(name, "interrupt-controller") == 0)
			    || (ic != NULL && iclen == 0)) {
				if (n == 0)
					dflt_interrupt_controller = np;
				++n;
			}
		}
		num_interrupt_controllers = n;
	}

	mem = finish_node(allnodes, mem, NULL, 1, 1);
	dev_tree_size = mem - (unsigned long) allnodes;
	klimit = (char *) mem;
}

/*
 * early_get_property is used to access the device tree image prepared
 * by BootX very early on, before the pointers in it have been relocated.
 */
static void * __init
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

static unsigned long __init
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
	if (use_of_interrupt_tree)
		mem_start = finish_node_interrupts(np, mem_start);

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

	if (np->parent == NULL)
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

/*
 * Find the interrupt parent of a node.
 */
static struct device_node * __init
intr_parent(struct device_node *p)
{
	phandle *parp;

	parp = (phandle *) get_property(p, "interrupt-parent", NULL);
	if (parp == NULL)
		return p->parent;
	p = find_phandle(*parp);
	if (p != NULL)
		return p;
	/*
	 * On a powermac booted with BootX, we don't get to know the
	 * phandles for any nodes, so find_phandle will return NULL.
	 * Fortunately these machines only have one interrupt controller
	 * so there isn't in fact any ambiguity.  -- paulus
	 */
	if (num_interrupt_controllers == 1)
		p = dflt_interrupt_controller;
	return p;
}

/*
 * Find out the size of each entry of the interrupts property
 * for a node.
 */
static int __init
prom_n_intr_cells(struct device_node *np)
{
	struct device_node *p;
	unsigned int *icp;

	for (p = np; (p = intr_parent(p)) != NULL; ) {
		icp = (unsigned int *)
			get_property(p, "#interrupt-cells", NULL);
		if (icp != NULL)
			return *icp;
		if (get_property(p, "interrupt-controller", NULL) != NULL
		    || get_property(p, "interrupt-map", NULL) != NULL) {
			printk("oops, node %s doesn't have #interrupt-cells\n",
			       p->full_name);
			return 1;
		}
	}
	printk("prom_n_intr_cells failed for %s\n", np->full_name);
	return 1;
}

/*
 * Map an interrupt from a device up to the platform interrupt
 * descriptor.
 */
static int __init
map_interrupt(unsigned int **irq, struct device_node **ictrler,
	      struct device_node *np, unsigned int *ints, int nintrc)
{
	struct device_node *p, *ipar;
	unsigned int *imap, *imask, *ip;
	int i, imaplen, match;
	int newintrc, newaddrc;
	unsigned int *reg;
	int naddrc;

	reg = (unsigned int *) get_property(np, "reg", NULL);
	naddrc = prom_n_addr_cells(np);
	p = intr_parent(np);
	while (p != NULL) {
		if (get_property(p, "interrupt-controller", NULL) != NULL)
			/* this node is an interrupt controller, stop here */
			break;
		imap = (unsigned int *)
			get_property(p, "interrupt-map", &imaplen);
		if (imap == NULL) {
			p = intr_parent(p);
			continue;
		}
		imask = (unsigned int *)
			get_property(p, "interrupt-map-mask", NULL);
		if (imask == NULL) {
			printk("oops, %s has interrupt-map but no mask\n",
			       p->full_name);
			return 0;
		}
		imaplen /= sizeof(unsigned int);
		match = 0;
		ipar = NULL;
		while (imaplen > 0 && !match) {
			/* check the child-interrupt field */
			match = 1;
			for (i = 0; i < naddrc && match; ++i)
				match = ((reg[i] ^ imap[i]) & imask[i]) == 0;
			for (; i < naddrc + nintrc && match; ++i)
				match = ((ints[i-naddrc] ^ imap[i]) & imask[i]) == 0;
			imap += naddrc + nintrc;
			imaplen -= naddrc + nintrc;
			/* grab the interrupt parent */
			ipar = find_phandle((phandle) *imap++);
			--imaplen;
			if (ipar == NULL && num_interrupt_controllers == 1)
				/* cope with BootX not giving us phandles */
				ipar = dflt_interrupt_controller;
			if (ipar == NULL) {
				printk("oops, no int parent %x in map of %s\n",
				       imap[-1], p->full_name);
				return 0;
			}
			/* find the parent's # addr and intr cells */
			ip = (unsigned int *)
				get_property(ipar, "#interrupt-cells", NULL);
			if (ip == NULL) {
				printk("oops, no #interrupt-cells on %s\n",
				       ipar->full_name);
				return 0;
			}
			newintrc = *ip;
			ip = (unsigned int *)
				get_property(ipar, "#address-cells", NULL);
			newaddrc = (ip == NULL)? 0: *ip;
			imap += newaddrc + newintrc;
			imaplen -= newaddrc + newintrc;
		}
		if (imaplen < 0) {
			printk("oops, error decoding int-map on %s, len=%d\n",
			       p->full_name, imaplen);
			return 0;
		}
		if (!match) {
			printk("oops, no match in %s int-map for %s\n",
			       p->full_name, np->full_name);
			return 0;
		}
		p = ipar;
		naddrc = newaddrc;
		nintrc = newintrc;
		ints = imap - nintrc;
		reg = ints - naddrc;
	}
	if (p == NULL)
		printk("hmmm, int tree for %s doesn't have ctrler\n",
		       np->full_name);
	*irq = ints;
	*ictrler = p;
	return nintrc;
}

/*
 * New version of finish_node_interrupts.
 */
static unsigned long __init
finish_node_interrupts(struct device_node *np, unsigned long mem_start)
{
	unsigned int *ints;
	int intlen, intrcells;
	int i, j, n, offset;
	unsigned int *irq;
	struct device_node *ic;

	ints = (unsigned int *) get_property(np, "interrupts", &intlen);
	if (ints == NULL)
		return mem_start;
	intrcells = prom_n_intr_cells(np);
	intlen /= intrcells * sizeof(unsigned int);
	np->n_intrs = intlen;
	np->intrs = (struct interrupt_info *) mem_start;
	mem_start += intlen * sizeof(struct interrupt_info);

	for (i = 0; i < intlen; ++i) {
		np->intrs[i].line = 0;
		np->intrs[i].sense = 1;
		n = map_interrupt(&irq, &ic, np, ints, intrcells);
		if (n <= 0)
			continue;
		offset = 0;
		/*
		 * On a CHRP we have an 8259 which is subordinate to
		 * the openpic in the interrupt tree, but we want the
		 * openpic's interrupt numbers offsetted, not the 8259's.
		 * So we apply the offset if the controller is at the
		 * root of the interrupt tree, i.e. has no interrupt-parent.
		 * This doesn't cope with the general case of multiple
		 * cascaded interrupt controllers, but then neither will
		 * irq.c at the moment either.  -- paulus
		 */
		if (num_interrupt_controllers > 1 && ic != NULL
		    && get_property(ic, "interrupt-parent", NULL) == NULL)
			offset = 16;
		np->intrs[i].line = irq[0] + offset;
		if (n > 1)
			np->intrs[i].sense = irq[1];
		if (n > 2) {
			printk("hmmm, got %d intr cells for %s:", n,
			       np->full_name);
			for (j = 0; j < n; ++j)
				printk(" %d", irq[j]);
			printk("\n");
		}
		ints += intrcells;
	}

	return mem_start;
}

/*
 * When BootX makes a copy of the device tree from the MacOS
 * Name Registry, it is in the format we use but all of the pointers
 * are offsets from the start of the tree.
 * This procedure updates the pointers.
 */
void __init
relocate_nodes(void)
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

int
prom_n_addr_cells(struct device_node* np)
{
	int* ip;
	do {
		if (np->parent)
			np = np->parent;
		ip = (int *) get_property(np, "#address-cells", 0);
		if (ip != NULL)
			return *ip;
	} while (np->parent);
	/* No #address-cells property for the root node, default to 1 */
	return 1;
}

int
prom_n_size_cells(struct device_node* np)
{
	int* ip;
	do {
		if (np->parent)
			np = np->parent;
		ip = (int *) get_property(np, "#size-cells", 0);
		if (ip != NULL)
			return *ip;
	} while (np->parent);
	/* No #size-cells property for the root node, default to 1 */
	return 1;
}

static unsigned long __init
interpret_pci_props(struct device_node *np, unsigned long mem_start,
		    int naddrc, int nsizec)
{
	struct address_range *adr;
	struct pci_reg_property *pci_addrs;
	int i, l, *ip;

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

	ip = (int *) get_property(np, "AAPL,interrupts", &l);
	if (ip == 0 && np->parent)
		ip = (int *) get_property(np->parent, "AAPL,interrupts", &l);
	if (ip == 0)
		ip = (int *) get_property(np, "interrupts", &l);
	if (ip != 0) {
		np->intrs = (struct interrupt_info *) mem_start;
		np->n_intrs = l / sizeof(int);
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = 1;
		}
	}

	return mem_start;
}

static unsigned long __init
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
			np->intrs[i].sense = 1;
		}
	}

	return mem_start;
}

static unsigned long __init
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
		np->n_intrs = l / sizeof(int);
		for (i = 0; i < np->n_intrs; ++i) {
			np->intrs[i].line = *ip++;
			np->intrs[i].sense = 1;
		}
		mem_start += np->n_intrs * sizeof(struct interrupt_info);
	}

	return mem_start;
}

static unsigned long __init
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

static unsigned long __init
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
			adr[i].space = (naddrc >= 2? rp[naddrc-2]: 0);
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
			np->intrs[i].sense = 1;
		}
	}

	return mem_start;
}

/*
 * Work out the sense (active-low level / active-high edge)
 * of each interrupt from the device tree.
 */
void __init
prom_get_irq_senses(unsigned char *senses, int off, int max)
{
	struct device_node *np;
	int i, j;

	/* default to level-triggered */
	memset(senses, 1, max - off);
	if (!use_of_interrupt_tree)
		return;

	for (np = allnodes; np != 0; np = np->allnext) {
		for (j = 0; j < np->n_intrs; j++) {
			i = np->intrs[j].line;
			if (i >= off && i < max)
				senses[i-off] = np->intrs[j].sense;
		}
	}
}

/*
 * Construct and return a list of the device_nodes with a given name.
 */
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

/*
 * Returns all nodes linked together
 */
struct device_node * __openfirmware
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
static struct device_node * __init
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
unsigned char *
get_property(struct device_node *np, const char *name, int *lenp)
{
	struct property *pp;

	for (pp = np->properties; pp != 0; pp = pp->next)
		if (pp->name != NULL && strcmp(pp->name, name) == 0) {
			if (lenp != 0)
				*lenp = pp->length;
			return pp->value;
		}
	return 0;
}

/*
 * Add a property to a node
 */
void __openfirmware
prom_add_property(struct device_node* np, struct property* prop)
{
	struct property **next = &np->properties;

	prop->next = NULL;	
	while (*next)
		next = &(*next)->next;
	*next = prop;
}

#if 0
void __openfirmware
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

static spinlock_t rtas_lock = SPIN_LOCK_UNLOCKED;

/* this can be called after setup -- Cort */
int __openfirmware
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

	/* Shouldn't we enable kernel FP here ? enter_rtas will play
	 * with MSR_FE0|MSR_FE1|MSR_FP so I assume rtas might use
	 * floating points. If that's the case, then we need to make
	 * sure any lazy FP context is backed up
	 * --BenH
	 */
	spin_lock_irqsave(&rtas_lock, s);
	enter_rtas((void *)__pa(&u));
	spin_unlock_irqrestore(&rtas_lock, s);

	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = u.words[i+nargs+4];
	return u.words[nargs+3];
}

void __init
abort()
{
#ifdef CONFIG_XMON
	xmon(NULL);
#endif
	for (;;)
		prom_exit();
}
