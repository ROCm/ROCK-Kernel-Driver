/*
 * Copyright, 2000-2001, Silicon Graphics.
 * Copyright Srinivasa Thirumalachar (sprasad@engr.sgi.com)
 * Copyright 2000-2001 Kanoj Sarcar (kanoj@sgi.com)
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <asm/page.h>
#include <asm/efi.h>
#include <asm/sn/mmzone_sn1.h>

#define MIN(a,b)	((a) < (b) ? (a) : (b))
#define MAX(a,b)	((a) > (b) ? (a) : (b))

#define DONE_NOTHING	0
#define DONE_FINDING	1
#define DONE_BUILDING	2

struct nodemem_s {
        u64     start;	/* start of kernel usable memory */
        u64     end;	/* end of kernel usable memory */
	u64	mtot;	/* total kernel usable memory */
	u64	done;	/* state of bootmem initialization */
	u64	bstart;	/* where should the bootmem area be */
	u64	bsize;	/* bootmap size */
        u64 	hole[SN1_MAX_BANK_PER_NODE];
} nodemem[MAXNODES];

static int nodemem_valid = 0;

static int __init
free_unused_memmap_hole(int nid, unsigned long start, unsigned long end)
{
        struct page * page, *pageend;
        unsigned long count = 0;

	if (start >= end)
		return 0;

	/*
	 * Get the memmap ptrs to the start and end of the holes.
	 * virt_to_page(start) will panic, if start is in hole.
	 * Can we do virt_to_page(end), if end is on the next node?
	 */

	page = virt_to_page(start - 1);
	page++;
	pageend = virt_to_page(end);

	printk("hpage=0x%lx, hpageend=0x%lx\n", (u64)page, (u64)pageend) ;
	free_bootmem_node(NODE_DATA(nid), __pa(page), (u64)pageend - (u64)page);

	return count;
}

static void __init
free_unused_memmap_node(int nid)
{
	u64	i = 0;
	u64	holestart = -1;
	u64	start = nodemem[nid].start;

	start = ((start >> SN1_NODE_ADDR_SHIFT) << SN1_NODE_ADDR_SHIFT);
	do {
		holestart = nodemem[nid].hole[i];
		i++;
		while ((i < SN1_MAX_BANK_PER_NODE) && 
					(nodemem[nid].hole[i] == (u64)-1))
			i++;
		if (i < SN1_MAX_BANK_PER_NODE)
			free_unused_memmap_hole(nid, holestart, 
				start + (i<<SN1_BANK_ADDR_SHIFT));
	} while (i<SN1_MAX_BANK_PER_NODE);
}

/*
 * Since efi_memmap_walk merges contiguous banks, this code will need
 * to find all the nasid/banks covered by the input memory descriptor.
 */
static int __init
build_nodemem_map(unsigned long start, unsigned long end, void *arg)
{
	unsigned long vaddr = start;
	unsigned long nvaddr;
	int nasid = GetNasId(__pa(vaddr));
	int cnodeid, bankid;

	while (vaddr < end) {
		cnodeid = NASID_TO_CNODEID(nasid);
		bankid = GetBankId(__pa(vaddr));
		nodemem[cnodeid].start = MIN(nodemem[cnodeid].start, vaddr);
		nvaddr = (unsigned long)__va((unsigned long)(++nasid) << 
							SN1_NODE_ADDR_SHIFT);
		nodemem[cnodeid].end = MAX(nodemem[cnodeid].end, MIN(end, nvaddr));
		while ((bankid < SN1_MAX_BANK_PER_NODE) && 
					(vaddr < nodemem[cnodeid].end)) {
			nvaddr = nodemem[cnodeid].start + 
			  ((unsigned long)(bankid + 1) << SN1_BANK_ADDR_SHIFT);
			nodemem[cnodeid].hole[bankid++] = MIN(nvaddr, end);
			vaddr = nvaddr;
		}
	}

	return 0;
}

static int __init
pgtbl_size_ok(int nid)
{
	unsigned long numpfn, bank0size, nodesize ;
	unsigned long start = nodemem[nid].start;

	start = ((start >> SN1_NODE_ADDR_SHIFT) << SN1_NODE_ADDR_SHIFT);
	
	nodesize 	= nodemem[nid].end - start ;
	numpfn 		= nodesize >> PAGE_SHIFT;

	bank0size 	= nodemem[nid].hole[0] - start ;
	/* If nid == master node && no kernel text replication */
	bank0size      -= 0xA00000 ;	/* Kernel text + stuff */
	bank0size      -= ((numpfn + 7) >> 3);

	if ((numpfn * sizeof(mem_map_t)) > bank0size) {
		printk("nid = %d, ns=0x%lx, npfn=0x%lx, bank0size=0x%lx\n", 
			nid, nodesize, numpfn, bank0size) ;
		return 0 ;
	}

	return 1 ;
}

static void __init
check_pgtbl_size(int nid)
{
	int	bank = SN1_MAX_BANK_PER_NODE - 1 ;

	/* Find highest bank with valid memory */
        while ((nodemem[nid].hole[bank] == -1) && (bank))
               bank-- ;

	while (!pgtbl_size_ok(nid)) {
		/* Remove that bank of memory */
		/* Collect some numbers later */
		printk("Ignoring node %d bank %d\n", nid, bank) ;
		nodemem[nid].hole[bank--] = -1 ;
		/* Get to the next populated bank */
		while ((nodemem[nid].hole[bank] == -1) && (bank))
			bank-- ;
		printk("Using only upto bank %d on node %d\n", bank,nid) ;
		nodemem[nid].end = nodemem[nid].hole[bank] ; 
		if (!bank) break ;
	}
}

void dump_nodemem_map(int) ;

#ifdef CONFIG_DISCONTIGMEM

extern bootmem_data_t bdata[];

/*
 * This assumes there will be a hole in kernel-usable memory between nodes
 * (due to prom). The memory descriptors invoked via efi_memmap_walk are 
 * in increasing order. It tries to identify first suitable free area to 
 * put the bootmem for the node in. When presented with the md holding
 * the kernel, it only searches at the end of the kernel area.
 */
static int __init
find_node_bootmem(unsigned long start, unsigned long end, void *arg)
{
	int nasid = GetNasId(__pa(start));
	int cnodeid = NASID_TO_CNODEID(nasid);
	unsigned long nodesize;
	extern char _end;
	unsigned long kaddr = (unsigned long)&_end;

	/*
	 * Track memory available to kernel.
	 */
	nodemem[cnodeid].mtot += ((end - start) >> PAGE_SHIFT);
	if (nodemem[cnodeid].done != DONE_NOTHING)
		return(0);
	nodesize = nodemem[cnodeid].end - ((nodemem[cnodeid].start >> 
				SN1_NODE_ADDR_SHIFT) << SN1_NODE_ADDR_SHIFT);
	nodesize >>= PAGE_SHIFT;

	/*
	 * Adjust limits for the md holding the kernel.
	 */
	if ((start < kaddr) && (end > kaddr))
		start = PAGE_ALIGN(kaddr);

	/*
	 * We need space for mem_map, bootmem map plus a few more pages
	 * to satisfy alloc_bootmems out of node 0.
	 */
	if ((end - start) > ((nodesize * sizeof(struct page)) + (nodesize/8)
						+ (10 * PAGE_SIZE))) {
		nodemem[cnodeid].bstart = start;
		nodemem[cnodeid].done = DONE_FINDING;
	}
	return(0);
}

/*
 * This assumes there will be a hole in kernel-usable memory between nodes
 * (due to prom). The memory descriptors invoked via efi_memmap_walk are 
 * in increasing order.
 */
static int __init
build_node_bootmem(unsigned long start, unsigned long end, void *arg)
{
	int nasid = GetNasId(__pa(start));
	int curnodeid = NASID_TO_CNODEID(nasid);
	int i;
	unsigned long pstart, pend;
	extern char _end, _stext;
	unsigned long kaddr = (unsigned long)&_end;

	if (nodemem[curnodeid].done == DONE_FINDING) {
		/*
		 * This is where we come to know the node is present.
		 * Do node wide tasks.
		 */
		nodemem[curnodeid].done = DONE_BUILDING;
		NODE_DATA(curnodeid)->bdata = &(bdata[curnodeid]);

		/*
	 	 * Update the chunktonid array as a node wide task. There
		 * are too many smalls mds on first node to do this per md.
	 	 */
		pstart = __pa(nodemem[curnodeid].start);
		pend = __pa(nodemem[curnodeid].end);
		pstart &= CHUNKMASK;
		pend = (pend + CHUNKSZ - 1) & CHUNKMASK;
		/* Possible check point to enforce minimum node size */
		if (nodemem[curnodeid].bstart == -1) {
			printk("No valid bootmem area on node %d\n", curnodeid);
			while(1);
		}
		for (i = PCHUNKNUM(pstart); i <= PCHUNKNUM(pend - 1); i++)
			chunktonid[i] = curnodeid;
		if ((CHUNKTONID(PCHUNKNUM(pend)) > MAXCHUNKS) || 
				(PCHUNKNUM(pstart) >= PCHUNKNUM(pend))) {
			printk("Ign 0x%lx-0x%lx, ", __pa(start), __pa(end));
			return(0);
		}

		/*
		 * NODE_START and NODE_SIZE determine the physical range
		 * on the node that mem_map array needs to be set up for.
		 */
		NODE_START(curnodeid) = ((nodemem[curnodeid].start >> 
				SN1_NODE_ADDR_SHIFT) << SN1_NODE_ADDR_SHIFT);
		NODE_SIZE(curnodeid) = (nodemem[curnodeid].end - 
							NODE_START(curnodeid));

        	nodemem[curnodeid].bsize = 
			init_bootmem_node(NODE_DATA(curnodeid),
			(__pa(nodemem[curnodeid].bstart) >> PAGE_SHIFT),
			(__pa((nodemem[curnodeid].start >> SN1_NODE_ADDR_SHIFT)
			<< SN1_NODE_ADDR_SHIFT) >> PAGE_SHIFT),
			(__pa(nodemem[curnodeid].end) >> PAGE_SHIFT));

	} else if (nodemem[curnodeid].done == DONE_NOTHING) {
		printk("build_node_bootmem: node %d weirdness\n", curnodeid);
		while(1);		/* Paranoia */
	}

	/*
	 * Free the entire md.
	 */
	free_bootmem_node(NODE_DATA(curnodeid), __pa(start), (end - start));

	/*
	 * Reclaim back the bootmap and kernel areas.
	 */
	if ((start <= nodemem[curnodeid].bstart) && (end >
						nodemem[curnodeid].bstart))
		reserve_bootmem_node(NODE_DATA(curnodeid),
		    __pa(nodemem[curnodeid].bstart), nodemem[curnodeid].bsize);
	if ((start <= kaddr) && (end > kaddr))
		reserve_bootmem_node(NODE_DATA(curnodeid),
		    __pa(&_stext), (&_end - &_stext));

	return(0);
}

void __init
setup_sn1_bootmem(int maxnodes)
{
        int     i;

        for (i = 0; i < MAXNODES; i++) {
                nodemem[i].start = nodemem[i].bstart = -1;
                nodemem[i].end = nodemem[i].bsize = nodemem[i].mtot = 0;
		nodemem[i].done = DONE_NOTHING;
		memset(&nodemem[i].hole, -1, sizeof(nodemem[i].hole));
        }
        efi_memmap_walk(build_nodemem_map, 0);

	nodemem_valid = 1;

	/* 
	 * After building the nodemem map, check if the node memmap
	 * will fit in the first bank of each node. If not change
	 * the node end addr till it fits.
 	 */

        for (i = 0; i < maxnodes; i++)
		check_pgtbl_size(i);

	dump_nodemem_map(maxnodes);

	efi_memmap_walk(find_node_bootmem, 0);
	efi_memmap_walk(build_node_bootmem, 0);
}
#endif

void __init
discontig_paging_init(void)
{
	int i;
	unsigned long max_dma, zones_size[MAX_NR_ZONES], holes_size[MAX_NR_ZONES];
	extern void dump_node_data(void);

	max_dma = virt_to_phys((void *) MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	for (i = 0; i < numnodes; i++) {
		unsigned long startpfn = __pa((void *)NODE_START(i)) >> PAGE_SHIFT;
		unsigned long numpfn = NODE_SIZE(i) >> PAGE_SHIFT;
		memset(zones_size, 0, sizeof(zones_size));
		memset(holes_size, 0, sizeof(holes_size));
		holes_size[ZONE_DMA] = numpfn - nodemem[i].mtot;

		if ((startpfn + numpfn) < max_dma) {
			zones_size[ZONE_DMA] = numpfn;
		} else if (startpfn > max_dma) {
			zones_size[ZONE_NORMAL] = numpfn;
			panic("discontig_paging_init: %d\n", i);
		} else {
			zones_size[ZONE_DMA] = (max_dma - startpfn);
			zones_size[ZONE_NORMAL] = numpfn - zones_size[ZONE_DMA];
			panic("discontig_paging_init: %d\n", i);
		}
		free_area_init_node(i, NODE_DATA(i), NULL, zones_size, startpfn<<PAGE_SHIFT, holes_size);
		free_unused_memmap_node(i);
	}
	dump_node_data();
}

/*
 * This used to be invoked from an SN1 specific hack in efi_memmap_walk.
 * It tries to ignore banks which the kernel is ignoring because bank 0 
 * is too small to hold the memmap entries for this bank.
 * The current SN1 efi_memmap_walk callbacks do not need this. That 
 * leaves the generic ia64 callbacks find_max_pfn, count_pages and
 * count_reserved_pages, of which the first can probably get by without
 * this, the last two probably need this, although they also can probably
 * get by. 
 */
int
sn1_bank_ignore(u64 start, u64 end)
{
	int 	nid = NASID_TO_CNODEID(GetNasId(__pa(end))) ;
	int	bank = GetBankId(__pa(end)) ;

	if (!nodemem_valid)
		return 0 ;

	if (nodemem[nid].hole[bank] == -1)
		return 1 ;
	else
		return 0 ;
}

void
dump_nodemem_map(int maxnodes)
{
	int	i,j;

        printk("NODEMEM_S info ....\n") ;
        printk("Node         start                end\n");
        for (i=0;i<maxnodes;i++) {
                printk("%d      0x%lx   0x%lx\n",
                       i, nodemem[i].start, nodemem[i].end);
                printk("Holes -> ") ;
                for (j=0;j<SN1_MAX_BANK_PER_NODE;j++)
                        printk("0x%lx ", nodemem[i].hole[j]) ;
		printk("\n");
        }
}

