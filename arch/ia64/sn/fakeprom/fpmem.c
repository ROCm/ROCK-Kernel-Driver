/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2002 Silicon Graphics, Inc.  All rights reserved.
 */



/*
 * FPROM EFI memory descriptor build routines
 *
 * 	- Routines to build the EFI memory descriptor map
 * 	- Should also be usable by the SGI SN1 prom to convert
 * 	  klconfig to efi_memmap
 */

#include <linux/config.h>
#include <linux/efi.h>
#include "fpmem.h"

/*
 * args points to a layout in memory like this
 *
 *		32 bit		32 bit
 *
 * 		numnodes	numcpus
 *
 *		16 bit   16 bit		   32 bit
 *		nasid0	cpuconf		membankdesc0
 *		nasid1	cpuconf		membankdesc1
 *			   .
 *			   .
 *			   .
 *			   .
 *			   .
 */

sn_memmap_t	*sn_memmap ;
sn_config_t	*sn_config ;

/*
 * There is a hole in the node 0 address space. Dont put it
 * in the memory map
 */
#define NODE0_HOLE_SIZE         (20*MB)
#define NODE0_HOLE_END          (4UL*GB)

#define	MB			(1024*1024)
#define GB			(1024*MB)
#define KERNEL_SIZE		(4*MB)
#define PROMRESERVED_SIZE	(1*MB)

#ifdef CONFIG_IA64_SGI_SN1
#define PHYS_ADDRESS(_n, _x)		(((long)_n<<33L) | (long)_x)
#define MD_BANK_SHFT 30
#else
#define PHYS_ADDRESS(_n, _x)		(((long)_n<<38L) | (long)_x | 0x3000000000UL)
#define MD_BANK_SHFT 34
#endif

/*
 * For SN, this may not take an arg and gets the numnodes from 
 * the prom variable or by traversing klcfg or promcfg
 */
int
GetNumNodes(void)
{
	return sn_config->nodes;
}

int
GetNumCpus(void)
{
	return sn_config->cpus;
}

/* For SN1, get the index th nasid */

int
GetNasid(int index)
{
	return sn_memmap[index].nasid ;
}

node_memmap_t
GetMemBankInfo(int index)
{
	return sn_memmap[index].node_memmap ;
}

int
IsCpuPresent(int cnode, int cpu)
{
	return  sn_memmap[cnode].cpuconfig & (1<<cpu);
}


/*
 * Made this into an explicit case statement so that
 * we can assign specific properties to banks like bank0
 * actually disabled etc.
 */

#ifdef CONFIG_IA64_SGI_SN1
int
IsBankPresent(int index, node_memmap_t nmemmap)
{
	switch (index) {
		case 0:return nmemmap.b0;
		case 1:return nmemmap.b1;
		case 2:return nmemmap.b2;
		case 3:return nmemmap.b3;
		case 4:return nmemmap.b4;
		case 5:return nmemmap.b5;
		case 6:return nmemmap.b6;
		case 7:return nmemmap.b7;
		default:return -1 ;
	}
}

int
GetBankSize(int index, node_memmap_t nmemmap)
{
        switch (index) {
                case 0:
                case 1:return nmemmap.b01size;
                case 2:
                case 3:return nmemmap.b23size;
                case 4:
                case 5:return nmemmap.b45size;
                case 6:
                case 7:return nmemmap.b67size;
                default:return -1 ;
        }
}

#else
int
IsBankPresent(int index, node_memmap_t nmemmap)
{
	switch (index) {
		case 0:return nmemmap.ena0;
		case 1:return nmemmap.ena1;
		case 2:return nmemmap.ena2;
		case 3:return nmemmap.ena3;
		default:return -1 ;
	}
}

int
GetBankSize(int index, node_memmap_t nmemmap)
{
        switch (index) {
                case 0:return (long)nmemmap.b0size + nmemmap.b0dou;
                case 1:return (long)nmemmap.b1size + nmemmap.b1dou;
                case 2:return (long)nmemmap.b2size + nmemmap.b2dou;
                case 3:return (long)nmemmap.b3size + nmemmap.b3dou;
                default:return -1 ;
        }
}

#endif

void
build_mem_desc(efi_memory_desc_t *md, int type, long paddr, long numbytes)
{
        md->type = type;
        md->phys_addr = paddr;
        md->virt_addr = 0;
        md->num_pages = numbytes >> 12;
        md->attribute = EFI_MEMORY_WB;
}

int
build_efi_memmap(void *md, int mdsize)
{
	int		numnodes = GetNumNodes() ;
	int		cnode,bank ;
	int		nasid ;
	node_memmap_t	membank_info ;
	int		bsize;
	int		count = 0 ;
	long		paddr, hole, numbytes;


	for (cnode=0;cnode<numnodes;cnode++) {
		nasid = GetNasid(cnode) ;
		membank_info = GetMemBankInfo(cnode) ;
		for (bank=0;bank<PLAT_CLUMPS_PER_NODE;bank++) {
			if (IsBankPresent(bank, membank_info)) {
				bsize = GetBankSize(bank, membank_info) ;
                                paddr = PHYS_ADDRESS(nasid, (long)bank<<MD_BANK_SHFT);
                                numbytes = BankSizeBytes(bsize);
#ifdef CONFIG_IA64_SGI_SN2
				numbytes = numbytes * 31 / 32;
#endif

                                /*
                                 * Check for the node 0 hole. Since banks cant
                                 * span the hole, we only need to check if the end of
                                 * the range is the end of the hole.
                                 */
                                if (paddr+numbytes == NODE0_HOLE_END)
                                        numbytes -= NODE0_HOLE_SIZE;
                                /*
                                 * UGLY hack - we must skip overr the kernel and
                                 * PROM runtime services but we dont exactly where it is.
                                 * So lets just reserve:
				 *	node 0
				 *		0-1MB for PAL
				 *		1-4MB for SAL
				 *	node 1-N
				 *		0-1 for SAL
                                 */
                                if (bank == 0) {
					if (cnode == 0) {
						hole = 1*1024*1024;
						build_mem_desc(md, EFI_PAL_CODE, paddr, hole);
						numbytes -= hole;
                                        	paddr += hole;
			        		count++ ;
                                        	md += mdsize;
						hole = 3*1024*1024;
						build_mem_desc(md, EFI_RUNTIME_SERVICES_DATA, paddr, hole);
						numbytes -= hole;
                                        	paddr += hole;
			        		count++ ;
                                        	md += mdsize;
					} else {
						hole = PROMRESERVED_SIZE;
                                        	build_mem_desc(md, EFI_RUNTIME_SERVICES_DATA, paddr, hole);
						numbytes -= hole;
                                        	paddr += hole;
			        		count++ ;
                                        	md += mdsize;
					}
                                }
                                build_mem_desc(md, EFI_CONVENTIONAL_MEMORY, paddr, numbytes);

			        md += mdsize ;
			        count++ ;
			}
		}
	}
	return count ;
}

void
build_init(unsigned long args)
{
	sn_config = (sn_config_t *) (args);	
	sn_memmap = (sn_memmap_t *)(args + 8) ; /* SN equiv for this is */
						/* init to klconfig start */
}
