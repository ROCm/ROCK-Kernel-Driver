/*
 * Copyright, 2000, Silicon Graphics.
 * Copyright Srinivasa Thirumalachar (sprasad@engr.sgi.com)
 * Copyright 2000 Kanoj Sarcar (kanoj@sgi.com)
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <asm/page.h>
#include <asm/efi.h>
#include <asm/sn/mmzone_sn1.h>

#       define MIN(a,b)         ((a) < (b) ? (a) : (b))
#       define MAX(a,b)         ((a) > (b) ? (a) : (b))

/*
 * Note that the nodemem[] data structure does not support arbitrary
 * memory types and memory descriptors inside the node. For example, 
 * you can not have multiple efi-mem-type segments in the node and
 * expect the OS not to use specific mem-types. Currently, the 
 * assumption is that "start" is the start of virtual/physical memory 
 * on the node. PROM can reserve some memory _only_ at the beginning. 
 * This is tracked via the "usable" field, that maintains where the 
 * os can start using memory from on a node (ie end of PROM memory).
 * setup_node_bootmem() is passed the above "usable" value, and is
 * expected to make bootmem calls that ensure lower memory is not used.
 * Note that the bootmem for a node is initialized on the entire node, 
 * without regards to any holes - then we reserve the holes in 
 * setup_sn1_bootmem(), to make sure the holes are not handed out by
 * alloc_bootmem, as well as the corresponding mem_map entries are not
 * considered allocatable by the page_alloc routines.
 */
struct nodemem_s {
        u64     start ;
        u64     end   ;
        u64 	hole[SN1_MAX_BANK_PER_NODE] ;
	u64	usable;
} nodemem[MAXNODES] ;
static int nodemem_valid = 0;

static int __init
free_unused_memmap_hole(int nid, unsigned long start, unsigned long end)
{
        struct page * page, *pageend;
        unsigned long count = 0;

	if (start >= end)
		return 0 ;

	/*
	 * Get the memmap ptrs to the start and end of the holes.
	 * virt_to_page(start) will panic, if start is in hole.
	 * Can we do virt_to_page(end), if end is on the next node?
	 */

	page = virt_to_page(start-1);
	page++ ;
	pageend = virt_to_page(end) ;

	printk("hpage=0x%lx, hpageend=0x%lx\n", (u64)page, (u64)pageend) ;
	free_bootmem_node(NODE_DATA(nid), __pa(page), (u64)pageend - (u64)page);

	return count ;
}

void
free_unused_memmap_node(int nid)
{
	u64	i = 0 ;
	u64	holestart = -1 ;

	do {
		holestart = nodemem[nid].hole[i] ;
		i++ ;
		while ((i < SN1_MAX_BANK_PER_NODE) && 
			(nodemem[nid].hole[i] == (u64)-1))
			i++ ;
		if (i < SN1_MAX_BANK_PER_NODE)
			free_unused_memmap_hole(nid, holestart, 
				nodemem[nid].start + (i<<SN1_BANK_ADDR_SHIFT));
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
		nodemem[cnodeid].usable = MIN(nodemem[cnodeid].usable, vaddr);
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
	
	nodesize 	= nodemem[nid].end - nodemem[nid].start ;
	numpfn 		= nodesize >> PAGE_SHIFT;

	bank0size 	= nodemem[nid].hole[0] - nodemem[nid].start ;
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

extern bootmem_data_t 	bdata[] ;
static int	 	curnodeid ;

static int __init
setup_node_bootmem(unsigned long start, unsigned long end, unsigned long nodefree)
{
	extern char _end;
	int i;
	unsigned long kernelend = PAGE_ALIGN((unsigned long)(&_end));
	unsigned long pkernelend = __pa(kernelend);
	unsigned long bootmap_start, bootmap_size;
	unsigned long pstart, pend;

	pstart = __pa(start) ;
	pend   = __pa(end) ;

	/* If we are past a node mem boundary, on simulated dig numa
	 * increment current node id. */

	curnodeid = NASID_TO_CNODEID(GetNasId(pstart)) ;

       /*
        * Make sure we are being passed page aligned addresses.
        */
	if ((start & (PAGE_SIZE - 1)) || (end & (PAGE_SIZE - 1)))
               panic("setup_node_bootmem:align");


	/* For now, just go to the lower CHUNK alignment so that 
	 * chunktonid of 0-8MB and other lower mem pages get initted. */

	pstart &= CHUNKMASK ;
	pend = (pend+CHUNKSZ-1) & CHUNKMASK;

	/* If pend == 0, both addrs below 8 MB, special case it
	 * FIX: CHUNKNUM(pend-1) broken if pend == 0 
	 * both addrs within 8MB */

	if (pend == 0) {
		chunktonid[0] = 0;
		return 0;
	}

	/* Fill up the chunktonid array first. */

        for (i = PCHUNKNUM(pstart); i <= PCHUNKNUM(pend-1); i++)
               chunktonid[i] = curnodeid;

	/* This check is bogus for now till MAXCHUNKS is properly
	 * defined to say if it includes holes or not. */

	if ((CHUNKTONID(PCHUNKNUM(pend)) > MAXCHUNKS) || 
		(PCHUNKNUM(pstart) >= PCHUNKNUM(pend))) {
		printk("Ign 0x%lx-0x%lx, ", __pa(start), __pa(end));
		return(0);
	}

	/* This routine gets called many times in node 0.
	 * The first one to reach here would be the one after
	 * kernelend to end of first node. */

	NODE_DATA(curnodeid)->bdata = &(bdata[curnodeid]);

	if (curnodeid == 0) {
		/* for master node, forcibly assign these values
		 * This gets called many times on dig but we
		 * want these exact values 
		 * Also on softsdv, the memdesc for 0 is missing */
		NODE_START(curnodeid) = PAGE_OFFSET;
		NODE_SIZE(curnodeid) = (end - PAGE_OFFSET);
	} else {
		/* This gets called only once for non zero nodes
		 * If it does not, then NODE_STARt should be 
		 * LOCAL_BASE(nid) */

		NODE_START(curnodeid) = start;
		NODE_SIZE(curnodeid) = (end - start);
	}

	/* if end < kernelend do not do anything below this */
	if (pend < pkernelend)
		return 0 ;

       /*
        * Handle the node that contains kernel text/data. It would
        * be nice if the loader loads the kernel at a "chunk", ie
        * not in memory that the kernel will ignore (else free_initmem
        * has to worry about not freeing memory that the kernel ignores).
        * Note that we assume the space from the node start to
        * KERNEL_START can not hold all the bootmem data, but from kernel
        * end to node end can.
        */

	/* TBD: This may be bogus in light of the above check. */

	if ((pstart < pkernelend) && (pend >= pkernelend)) {
               bootmap_start = pkernelend;
	} else {
               bootmap_start = __pa(start);    /* chunk & page aligned */
	}

	/*
	 * Low memory is reserved for PROM use on SN1. The current node
	 * memory model is [PROM mem ... kernel ... free], where the 
	 * first two components are optional on a node.
	 */
	if (bootmap_start < __pa(nodefree))
		bootmap_start = __pa(nodefree);

/* XXX TBD */
/* For curnodeid of 0, this gets called many times because of many
 * < 8MB segments. start gets bumped each time. We want to fix it
 * to 0 now. 
 */
	if (curnodeid == 0)
		start=PAGE_OFFSET;
/*
 * This makes sure that in free_area_init_core - paging_init
 * idx is the entire node page range and for loop goes thro
 * all pages. test_bit for kernel pages should remain reserved
 * because free available mem takes care of kernel_start and end
 */

        bootmap_size = init_bootmem_node(NODE_DATA(curnodeid),
			(bootmap_start >> PAGE_SHIFT),
			(__pa(start) >> PAGE_SHIFT), (__pa(end) >> PAGE_SHIFT));

	free_bootmem_node(NODE_DATA(curnodeid), bootmap_start + bootmap_size,
				__pa(end) - (bootmap_start + bootmap_size));

	return(0);
}

void
setup_sn1_bootmem(int maxnodes)
{
        int     i;

        for (i=0;i<MAXNODES;i++) {
                nodemem[i].usable = nodemem[i].start = -1 ;
                nodemem[i].end   = 0 ;
		memset(&nodemem[i].hole, -1, sizeof(nodemem[i].hole)) ;
        }
        efi_memmap_walk(build_nodemem_map, 0) ;

	/*
	 * Run thru all the nodes, adjusting their starts. This is needed
	 * because efi_memmap_walk() might not process certain mds that 
	 * are marked reserved for PROM at node low memory.
	 */
	for (i = 0; i < maxnodes; i++)
		nodemem[i].start = ((nodemem[i].start >> SN1_NODE_ADDR_SHIFT) <<
					SN1_NODE_ADDR_SHIFT);
	nodemem_valid = 1 ;

	/* After building the nodemem map, check if the page table
	 * will fit in the first bank of each node. If not change
	 * the node end addr till it fits. We dont want to do this
	 * in mm/page_alloc.c
 	 */

        for (i=0;i<maxnodes;i++)
		check_pgtbl_size(i) ;

        for (i=0;i<maxnodes;i++)
                setup_node_bootmem(nodemem[i].start, nodemem[i].end, nodemem[i].usable);

	/*
	 * Mark the holes as reserved, so the corresponding mem_map
	 * entries will not be marked allocatable in free_all_bootmem*().
	 */
	for (i = 0; i < maxnodes; i++) {
		int j = 0 ;
		u64 holestart = -1 ;

		do {
			holestart = nodemem[i].hole[j++];
			while ((j < SN1_MAX_BANK_PER_NODE) && 
					(nodemem[i].hole[j] == (u64)-1))
				j++;
			if (j < SN1_MAX_BANK_PER_NODE)
				reserve_bootmem_node(NODE_DATA(i), 
					__pa(holestart), (nodemem[i].start + 
					((long)j <<  SN1_BANK_ADDR_SHIFT) - 
					 holestart));
		} while (j < SN1_MAX_BANK_PER_NODE);
	}

	dump_nodemem_map(maxnodes) ;
}
#endif

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
        printk("Node         start                end                 usable\n");
        for (i=0;i<maxnodes;i++) {
                printk("%d      0x%lx   0x%lx   0x%lx\n",
                       i, nodemem[i].start, nodemem[i].end, nodemem[i].usable);
                printk("Holes -> ") ;
                for (j=0;j<SN1_MAX_BANK_PER_NODE;j++)
                        printk("0x%lx ", nodemem[i].hole[j]) ;
		printk("\n");
        }
}

