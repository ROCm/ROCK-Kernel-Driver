/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * PROM library functions for acquiring/using memory descriptors given to 
 * us from the YAMON.
 * 
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/bootmem.h>

#include <asm/bootinfo.h>
#include <asm/page.h>

#include <asm/mips-boards/prom.h>

/*#define DEBUG*/

enum yamon_memtypes {
	yamon_dontuse,
	yamon_prom,
	yamon_free,
};
struct prom_pmemblock mdesc[PROM_MAX_PMEMBLOCKS];

#define MEMTYPE_DONTUSE 0
#define MEMTYPE_PROM    1
#define MEMTYPE_FREE    2

#ifdef DEBUG
static char *mtypes[3] = {
	"Dont use memory",
	"YAMON PROM memory",
	"Free memmory",
};
#endif

/* References to section boundaries */
extern char _end;

#define PFN_ALIGN(x)    (((unsigned long)(x) + (PAGE_SIZE - 1)) & PAGE_MASK)

struct prom_pmemblock * __init prom_getmdesc(void)
{
	char *memsize_str;
	unsigned int memsize;

	memsize_str = prom_getenv("memsize");
	if (!memsize_str) {
		prom_printf("memsize not set in boot prom, set to default (32Mb)\n");
		memsize = 0x02000000;
	} else {
#ifdef DEBUG
		prom_printf("prom_memsize = %s\n", memsize_str);
#endif
		memsize = simple_strtol(memsize_str, NULL, 0);
	}

	memset(mdesc, 0, sizeof(mdesc));

	mdesc[0].type = yamon_dontuse;
	mdesc[0].base = 0x00000000;
	mdesc[0].size = 0x00001000;

	mdesc[1].type = yamon_prom;
	mdesc[1].base = 0x00001000;
	mdesc[1].size = 0x000ef000;

#if (CONFIG_MIPS_MALTA)
	/* 
	 * The area 0x000f0000-0x000fffff is allocated for BIOS memory by the
	 * south bridge and PCI access always forwarded to the ISA Bus and 
	 * BIOSCS# is always generated.
	 * This mean that this area can't be used as DMA memory for PCI 
	 * devices.
	 */
	mdesc[2].type = yamon_dontuse;
	mdesc[2].base = 0x000f0000;
	mdesc[2].size = 0x00010000;
#else
	mdesc[2].type = yamon_prom;
	mdesc[2].base = 0x000f0000;
	mdesc[2].size = 0x00010000;
#endif

	mdesc[3].type = yamon_dontuse;
	mdesc[3].base = 0x00100000;
	mdesc[3].size = CPHYSADDR(PFN_ALIGN(&_end)) - mdesc[3].base;

	mdesc[4].type = yamon_free;
	mdesc[4].base = CPHYSADDR(PFN_ALIGN(&_end));
	mdesc[4].size = memsize - mdesc[4].base;

	return &mdesc[0];
}

int __init page_is_ram(unsigned long pagenr)
{
	if ((pagenr << PAGE_SHIFT) < mdesc[4].base + mdesc[4].size)
		return 1;

	return 0;
}

static struct prom_pmemblock pblocks[PROM_MAX_PMEMBLOCKS];

static int __init prom_memtype_classify (unsigned int type)
{
	switch (type) {
	case yamon_free:
		return MEMTYPE_FREE;
	case yamon_prom:
		return MEMTYPE_PROM;
	default:
		return MEMTYPE_DONTUSE;
	}
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
	struct prom_pmemblock *largest, *p;
	unsigned long bootmap_size;
	int totram;
	int i = 0;

#ifdef DEBUG
	prom_printf("YAMON MEMORY DESCRIPTOR dump:\n");
	p = prom_getmdesc();
	while (p->size) {
		prom_printf("[%d,%p]: base<%08lx> size<%08lx> type<%s>\n",
			    i, p, p->base, p->size, mtypes[p->type]);
		p++;
		i++;
	}
#endif
	totram = 0;
	i = 0;
	p = prom_getmdesc();

	while (p->size) {
		pblocks[i].type = prom_memtype_classify (p->type);
		pblocks[i].base = p->base;
		pblocks[i].size = p->size;
		switch (pblocks[i].type) {
		case MEMTYPE_FREE:
			totram += pblocks[i].size;
#ifdef DEBUG
			prom_printf("free_chunk[%d]: base=%08lx size=%d\n",
				    i, pblocks[i].base, pblocks[i].size);
#endif
			i++;
			break;
		case MEMTYPE_PROM:
#ifdef DEBUG
		        prom_printf("prom_chunk[%d]: base=%08lx size=%d\n",
				    i, pblocks[i].base, pblocks[i].size);
#endif
			i++;
			break;
		default:
			break;
		}
		p++;
	}
	pblocks[i].base = 0xdeadbeef;
	pblocks[i].size = 0; /* indicates last elem. of array */

	max_low_pfn = find_max_low_pfn();
	largest = find_largest_memblock();
	bootmap_size = init_bootmem(largest->base >> PAGE_SHIFT, max_low_pfn);

	for (i = 0; pblocks[i].size; i++)
		if (pblocks[i].type == MEMTYPE_FREE)
			free_bootmem(pblocks[i].base, pblocks[i].size);

	/* This test is simpleminded.  It will fail if the bootmem bitmap
	   falls into multiple adjacent PROM memory areas.  */
	if (bootmap_size > largest->size) {
		prom_printf("CRITIAL: overwriting PROM data.\n");
		BUG();
	}

	/* Reserve the memory bootmap itself */
	reserve_bootmem(largest->base, bootmap_size);
	printk("PROMLIB: Total free ram %d bytes (%dK,%dMB)\n",
	       totram, (totram/1024), (totram/1024/1024));
}

void prom_free_prom_memory (void)
{
	struct prom_pmemblock *p;
	unsigned long freed = 0;
	unsigned long addr;

	for (p = pblocks; p->size != 0; p++) {
		if (p->type != MEMTYPE_PROM)
			continue;

		addr = p->base;
		while (addr < p->base + p->size) {
		        ClearPageReserved(virt_to_page(__va(addr)));
			set_page_count(virt_to_page(__va(addr)), 1);
			free_page(__va(addr));
			addr += PAGE_SIZE;
			freed += PAGE_SIZE;
		}
	}
	printk("Freeing prom memory: %ldkb freed\n", freed >> 10);
}
