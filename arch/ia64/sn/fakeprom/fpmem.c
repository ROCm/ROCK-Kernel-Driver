/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003 Silicon Graphics, Inc.  All rights reserved.
 */



/*
 * FPROM EFI memory descriptor build routines
 *
 * 	- Routines to build the EFI memory descriptor map
 * 	- Should also be usable by the SGI prom to convert
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

#ifdef SGI_SN2
#define PHYS_ADDRESS(_n, _x)		(((long)_n<<38) | (long)_x | 0x3000000000UL)
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

/* For SN, get the index th nasid */

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
	return  sn_memmap[cnode].cpuconfig & (1UL<<cpu);
}


/*
 * Made this into an explicit case statement so that
 * we can assign specific properties to banks like bank0
 * actually disabled etc.
 */

#ifdef SGI_SN2
int
IsBankPresent(int index, node_memmap_t nmemmap)
{
	switch (index) {
		case 0:return BankPresent(nmemmap.b0size);
		case 1:return BankPresent(nmemmap.b1size);
		case 2:return BankPresent(nmemmap.b2size);
		case 3:return BankPresent(nmemmap.b3size);
		default:return -1 ;
	}
}

int
GetBankSize(int index, node_memmap_t nmemmap)
{
	/*
	 * Add 2 because there are 4 dimms per bank.
	 */
        switch (index) {
                case 0:return 2 + ((long)nmemmap.b0size + nmemmap.b0dou);
                case 1:return 2 + ((long)nmemmap.b1size + nmemmap.b1dou);
                case 2:return 2 + ((long)nmemmap.b2size + nmemmap.b2dou);
                case 3:return 2 + ((long)nmemmap.b3size + nmemmap.b3dou);
                default:return -1 ;
        }
}

#endif

void
build_mem_desc(efi_memory_desc_t *md, int type, long paddr, long numbytes, long attr)
{
        md->type = type;
        md->phys_addr = paddr;
        md->virt_addr = 0;
        md->num_pages = numbytes >> 12;
        md->attribute = attr;
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
		for (bank=0;bank<MD_BANKS_PER_NODE;bank++) {
			if (IsBankPresent(bank, membank_info)) {
				bsize = GetBankSize(bank, membank_info) ;
                                paddr = PHYS_ADDRESS(nasid, (long)bank<<MD_BANK_SHFT);
                                numbytes = BankSizeBytes(bsize);
#ifdef SGI_SN2
				/* 
				 * Ignore directory.
				 * Shorten memory chunk by 1 page - makes a better
				 * testcase & is more like the real PROM.
				 */
				numbytes = numbytes * 31 / 32;
#endif
				/*
				 * Only emulate the memory prom grabs
				 * if we have lots of memory, to allow
				 * us to simulate smaller memory configs than
				 * we can actually run on h/w.  Otherwise,
				 * linux throws away a whole "granule".
				 */
				if (cnode == 0 && bank == 0 &&
				    numbytes > 128*1024*1024) {
					numbytes -= 1000;
				}

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
						hole = 2*1024*1024;
						build_mem_desc(md, EFI_PAL_CODE, paddr, hole, EFI_MEMORY_WB|EFI_MEMORY_WB);
						numbytes -= hole;
                                        	paddr += hole;
			        		count++ ;
                                        	md += mdsize;
						hole = 1*1024*1024;
						build_mem_desc(md, EFI_CONVENTIONAL_MEMORY, paddr, hole, EFI_MEMORY_UC);
						numbytes -= hole;
                                        	paddr += hole;
			        		count++ ;
                                        	md += mdsize;
						hole = 1*1024*1024;
						build_mem_desc(md, EFI_RUNTIME_SERVICES_DATA, paddr, hole, EFI_MEMORY_WB|EFI_MEMORY_WB);
						numbytes -= hole;
                                        	paddr += hole;
			        		count++ ;
                                        	md += mdsize;
					} else {
						hole = 2*1024*1024;
                                        	build_mem_desc(md, EFI_RUNTIME_SERVICES_DATA, paddr, hole, EFI_MEMORY_WB|EFI_MEMORY_WB);
						numbytes -= hole;
                                        	paddr += hole;
			        		count++ ;
                                        	md += mdsize;
						hole = 2*1024*1024;
                                        	build_mem_desc(md, EFI_RUNTIME_SERVICES_DATA, paddr, hole, EFI_MEMORY_UC);
						numbytes -= hole;
                                        	paddr += hole;
			        		count++ ;
                                        	md += mdsize;
					}
                                }
                                build_mem_desc(md, EFI_CONVENTIONAL_MEMORY, paddr, numbytes, EFI_MEMORY_WB|EFI_MEMORY_WB);

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
