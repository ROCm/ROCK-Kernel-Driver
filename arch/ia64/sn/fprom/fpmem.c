/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */


/*
 * FPROM EFI memory descriptor build routines
 *
 * 	- Routines to build the EFI memory descriptor map
 * 	- Should also be usable by the SGI SN1 prom to convert
 * 	  klconfig to efi_memmap
 */

#include <asm/efi.h>
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
#define MD_BANK_SHFT 30

#define TO_NODE(_n, _x)		(((long)_n<<33L) | (long)_x)

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
		for (bank=0;bank<SN1_MAX_BANK_PER_NODE;bank++) {
			if (IsBankPresent(bank, membank_info)) {
				bsize = GetBankSize(bank, membank_info) ;
                                paddr = TO_NODE(nasid, (long)bank<<MD_BANK_SHFT);
                                numbytes = BankSizeBytes(bsize);

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
                                 * So lets just reserve 0-12MB.
                                 */
                                if (bank == 0) {
					hole = (cnode == 0) ? KERNEL_SIZE : PROMRESERVED_SIZE;
					numbytes -= hole;
                                        build_mem_desc(md, EFI_RUNTIME_SERVICES_DATA, paddr, hole);
                                        paddr += hole;
			        	count++ ;
                                        md += mdsize;
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
