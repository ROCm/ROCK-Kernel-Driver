/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file contains NUMA specific variables and functions which can
 * be split away from DISCONTIGMEM and are used on NUMA machines with
 * contiguous memory.
 * 
 *                         2002/08/07 Erich Focht <efocht@ess.nec.de>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <asm/numa.h>

/*
 * The following structures are usually initialized by ACPI or
 * similar mechanisms and describe the NUMA characteristics of the machine.
 */
int num_memblks = 0;
struct node_memblk_s node_memblk[NR_MEMBLKS];
struct node_cpuid_s node_cpuid[NR_CPUS];
/*
 * This is a matrix with "distances" between nodes, they should be
 * proportional to the memory access latency ratios.
 */
u8 numa_slit[NR_NODES * NR_NODES];

/* Identify which cnode a physical address resides on */
int
paddr_to_nid(unsigned long paddr)
{
	int	i;

	for (i = 0; i < num_memblks; i++)
		if (paddr >= node_memblk[i].start_paddr &&
		    paddr < node_memblk[i].start_paddr + node_memblk[i].size)
			break;

	return (i < num_memblks) ? node_memblk[i].nid : -1;
}
