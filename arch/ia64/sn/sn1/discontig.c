/*
 * Copyright 2000, Silicon Graphics, sprasad@engr.sgi.com
 * Copyright 2000, Kanoj Sarcar, kanoj@sgi.com
 */

/*
 * Contains common definitions and globals for NUMA platform
 * support. For now, SN-IA64 and SN-MIPS are the NUMA platforms.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <asm/sn/mmzone.h>
#include <asm/efi.h>

extern int numnodes ;

plat_pg_data_t plat_node_data[MAXNODES];
bootmem_data_t bdata[MAXNODES];
int chunktonid[MAXCHUNKS];
int nasid_map[MAXNASIDS];

void __init
init_chunktonid(void)
{
	memset(chunktonid, -1, sizeof(chunktonid)) ;
}

void __init
init_nodeidmap(void)
{
	memset(nasid_map, -1, sizeof(nasid_map)) ;
}

int		cnodeid_map[MAXNODES] ;
void __init
init_cnodeidmap(void)
{
	memset(cnodeid_map, -1, sizeof(cnodeid_map)) ;
}

int
numa_debug(void)
{
       panic("NUMA debug\n");
       return(0);
}

int __init
build_cnodeid_map(void)
{
	int	i,j ;

	for (i=0,j=0;i<MAXNASIDS;i++) {
		if (nasid_map[i] >= 0)
			cnodeid_map[j++] = i ;
	}
	return j ;
}

/*
 * Since efi_memmap_walk merges contiguous banks, this code will need
 * to find all the nasids covered by the input memory descriptor.
 */
static int __init
build_nasid_map(unsigned long start, unsigned long end, void *arg)
{
	unsigned long vaddr = start;
	int nasid = GetNasId(__pa(vaddr));

	while (vaddr < end) {
		if (nasid < MAXNASIDS)
			nasid_map[nasid] = 0;
		else
			panic("build_nasid_map");
		vaddr = (unsigned long)__va((unsigned long)(++nasid) << 
							SN1_NODE_ADDR_SHIFT);
	}
	return 0;
}

void __init
fix_nasid_map(void)
{
	int	i ;
	int		j ;

	/* For every nasid */
	for (j=0;j<MAXNASIDS;j++) {
		for (i=0;i<MAXNODES;i++) {
			if (CNODEID_TO_NASID(i) == j)
				break ;
		}
		if (i<MAXNODES)
			nasid_map[j] = i ;
	}
}

static void __init
dump_bootmem_info(void)
{
        int     i;
        struct bootmem_data *bdata ;

	printk("CNODE INFO ....\n") ;
        for (i=0;i<numnodes;i++) {
		printk("%d ", CNODEID_TO_NASID(i)) ;
	}
	printk("\n") ;

	printk("BOOT MEM INFO ....\n") ;
        printk("Node   Start                LowPfn               BootmemMap\n") ;
        for (i=0;i<numnodes;i++) {
                bdata = NODE_DATA(i)->bdata ;
                printk("%d      0x%016lx   0x%016lx   0x%016lx\n", i,
                        bdata->node_boot_start, bdata->node_low_pfn,
                        (unsigned long)bdata->node_bootmem_map) ;
        }
}

void __init
discontig_mem_init(void)
{
	extern void setup_sn1_bootmem(int);
	int		maxnodes ;

        init_chunktonid() ;
	init_nodeidmap() ;
	init_cnodeidmap() ;
	efi_memmap_walk(build_nasid_map, 0) ;
	maxnodes = build_cnodeid_map() ;
	fix_nasid_map() ;
#ifdef CONFIG_DISCONTIGMEM
	setup_sn1_bootmem(maxnodes) ;
#endif
	numnodes = maxnodes;
	dump_bootmem_info() ;
}

void __init
discontig_paging_init(void)
{
	int i;
	unsigned long max_dma, zones_size[MAX_NR_ZONES];
	void dump_node_data(void);

        max_dma = virt_to_phys((void *) MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	for (i = 0; i < numnodes; i++) {
	       extern void free_unused_memmap_node(int);
               unsigned long startpfn = __pa((void *)NODE_START(i)) >> PAGE_SHIFT;
               unsigned long numpfn = NODE_SIZE(i) >> PAGE_SHIFT;
               memset(zones_size, 0, sizeof(zones_size));

               if ((startpfn + numpfn) < max_dma) {
                       zones_size[ZONE_DMA] = numpfn;
               } else if (startpfn > max_dma) {
                       zones_size[ZONE_NORMAL] = numpfn;
               } else {
                       zones_size[ZONE_DMA] = (max_dma - startpfn);
                       zones_size[ZONE_NORMAL] = numpfn - zones_size[ZONE_DMA];
               }
               free_area_init_node(i, NODE_DATA(i), NULL, zones_size, startpfn<<PAGE_SHIFT, 0);
	       free_unused_memmap_node(i);
	}
	dump_node_data();
}


void
dump_node_data(void)
{
        int     i;

	printk("NODE DATA ....\n") ;
	printk("Node, Start, Size, MemMap, BitMap, StartP, Mapnr, Size, Id\n") ;
        for (i=0;i<numnodes;i++) {
		printk("%d, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, %d\n", 
			CNODEID_TO_NASID(i), NODE_START(i), NODE_SIZE(i), 
			(long)NODE_MEM_MAP(i), (long)NODE_DATA(i)->valid_addr_bitmap, 
			NODE_DATA(i)->node_start_paddr, 
			NODE_DATA(i)->node_start_mapnr,
			NODE_DATA(i)->node_size,
			NODE_DATA(i)->node_id)  ;
	}
}

