/* $Id: memory.c,v 1.5 2000/01/27 23:21:57 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by David S. Miller
 * Copyright (C) 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 by Silicon Graphics, Inc.
 *
 * PROM library functions for acquiring/using memory descriptors given to us
 * from the ARCS firmware.  This is only used when CONFIG_ARC_MEMORY is set
 * because on some machines like SGI IP27 the ARC memory configuration data
 * completly bogus and alternate easier to use mechanisms are available.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/swap.h>

#include <asm/sgialib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>

#undef DEBUG

struct linux_mdesc * __init
ArcGetMemoryDescriptor(struct linux_mdesc *Current)
{
	return (struct linux_mdesc *) ARC_CALL1(get_mdesc, Current);
}

#ifdef DEBUG /* convenient for debugging */
static char *arcs_mtypes[8] = {
	"Exception Block",
	"ARCS Romvec Page",
	"Free/Contig RAM",
	"Generic Free RAM",
	"Bad Memory",
	"Standlong Program Pages",
	"ARCS Temp Storage Area",
	"ARCS Permanent Storage Area"
};

static char *arc_mtypes[8] = {
	"Exception Block",
	"SystemParameterBlock",
	"FreeMemory",
	"Bad Memory",
	"LoadedProgram",
	"FirmwareTemporary",
	"FirmwarePermanent",
	"FreeContigiuous"
};
#define mtypes(a) (prom_flags & PROM_FLAG_ARCS) ? arcs_mtypes[a.arcs] : arc_mtypes[a.arc]
#endif

static struct prom_pmemblock pblocks[PROM_MAX_PMEMBLOCKS];

#define MEMTYPE_DONTUSE   0
#define MEMTYPE_PROM      1
#define MEMTYPE_FREE      2

static inline int memtype_classify_arcs (union linux_memtypes type)
{
	switch (type.arcs) {
	case arcs_fcontig:
	case arcs_free:
		return MEMTYPE_FREE;
	case arcs_atmp:
		return MEMTYPE_PROM;
	case arcs_eblock:
	case arcs_rvpage:
	case arcs_bmem:
	case arcs_prog:
	case arcs_aperm:
		return MEMTYPE_DONTUSE;
	default:
		BUG();
	}
	while(1);				/* Nuke warning.  */
}

static inline int memtype_classify_arc (union linux_memtypes type)
{
	switch (type.arc) {
	case arc_free:
	case arc_fcontig:
		return MEMTYPE_FREE;
	case arc_atmp:
		return MEMTYPE_PROM;
	case arc_eblock:
	case arc_rvpage:
	case arc_bmem:
	case arc_prog:
	case arc_aperm:
		return MEMTYPE_DONTUSE;
	default:
		BUG();
	}
	while(1);				/* Nuke warning.  */
}

static int __init prom_memtype_classify (union linux_memtypes type)
{
	if (prom_flags & PROM_FLAG_ARCS)	/* SGI is ``different'' ...  */
		return memtype_classify_arcs(type);

	return memtype_classify_arc(type);
}

static inline unsigned long find_max_low_pfn(void)
{
	struct prom_pmemblock *p, *highest;
	unsigned long pfn;

	p = pblocks;
	highest = 0;
	while (p->size != 0) {
		if (!highest || p->base > highest->base)
			highest = p;
		p++;
	}

	pfn = (highest->base + highest->size) >> PAGE_SHIFT;
#ifdef DEBUG
	prom_printf("find_max_low_pfn: 0x%lx pfns.\n", pfn);
#endif
	return pfn;
}

static inline struct prom_pmemblock *find_largest_memblock(void)
{
	struct prom_pmemblock *p, *largest;

	p = pblocks;
	largest = 0;
	while (p->size != 0) {
		if (!largest || p->size > largest->size)
			largest = p;
		p++;
	}

	return largest;
}

void __init prom_meminit(void)
{
	struct prom_pmemblock *largest;
	unsigned long bootmap_size;
	struct linux_mdesc *p;
	int totram;
	int i = 0;

#ifdef DEBUG
	prom_printf("ARCS MEMORY DESCRIPTOR dump:\n");
	p = ArcGetMemoryDescriptor(PROM_NULL_MDESC);
	while(p) {
		prom_printf("[%d,%p]: base<%08lx> pages<%08lx> type<%s>\n",
			    i, p, p->base, p->pages, mtypes(p->type));
		p = ArcGetMemoryDescriptor(p);
		i++;
	}
#endif

	totram = 0;
	i = 0;
	p = PROM_NULL_MDESC;
	while ((p = ArcGetMemoryDescriptor(p))) {
		pblocks[i].type = prom_memtype_classify(p->type);
		pblocks[i].base = p->base << PAGE_SHIFT;
		pblocks[i].size = p->pages << PAGE_SHIFT;

		switch (pblocks[i].type) {
		case MEMTYPE_FREE:
			totram += pblocks[i].size;
#ifdef DEBUG
			prom_printf("free_chunk[%d]: base=%08lx size=%x\n",
				    i, pblocks[i].base,
				    pblocks[i].size);
#endif
			i++;
			break;
		case MEMTYPE_PROM:
#ifdef DEBUG
			prom_printf("prom_chunk[%d]: base=%08lx size=%x\n",
				    i, pblocks[i].base,
				    pblocks[i].size);
#endif
			i++;
			break;
		default:
			break;
		}
	}
	pblocks[i].size = 0;

	max_low_pfn = find_max_low_pfn();
	largest = find_largest_memblock();
	bootmap_size = init_bootmem(largest->base >> PAGE_SHIFT, max_low_pfn);

	for (i = 0; pblocks[i].size; i++)
		if (pblocks[i].type == MEMTYPE_FREE)
			free_bootmem(pblocks[i].base, pblocks[i].size);

	/* This test is simpleminded.  It will fail if the bootmem bitmap
	   falls into multiple adjacent ARC memory areas.  */
	if (bootmap_size > largest->size) {
		prom_printf("CRITIAL: overwriting PROM data.\n");
		BUG();
	}
	reserve_bootmem(largest->base, bootmap_size);

	printk("PROMLIB: Total free ram %dK / %dMB.\n",
	       totram >> 10, totram >> 20);
}

void __init
prom_free_prom_memory (void)
{
	struct prom_pmemblock *p;
	unsigned long freed = 0;
	unsigned long addr, end;

	for (p = pblocks; p->size != 0; p++) {
		if (p->type != MEMTYPE_PROM)
			continue;

		addr = PAGE_OFFSET + (unsigned long) (long) p->base;
		end = addr + (unsigned long) (long) p->size;
		while (addr < end) {
			ClearPageReserved(virt_to_page(addr));
			set_page_count(virt_to_page(addr), 1);
			free_page(addr);
			addr += PAGE_SIZE;
			freed += PAGE_SIZE;
		}
	}
	printk("Freeing prom memory: %ldkb freed\n", freed >> 10);
}
