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
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/memblk.h>
#include <linux/mm.h>
#include <linux/node.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <asm/numa.h>

static struct memblk *sysfs_memblks;
static struct node *sysfs_nodes;
static struct cpu *sysfs_cpus;

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
u8 numa_slit[MAX_NUMNODES * MAX_NUMNODES];

/* Identify which cnode a physical address resides on */
int
paddr_to_nid(unsigned long paddr)
{
	int	i;

	for (i = 0; i < num_memblks; i++)
		if (paddr >= node_memblk[i].start_paddr &&
		    paddr < node_memblk[i].start_paddr + node_memblk[i].size)
			break;

	return (i < num_memblks) ? node_memblk[i].nid : (num_memblks ? -1 : 0);
}

static int __init topology_init(void)
{
	int i, err = 0;

	sysfs_nodes = kmalloc(sizeof(struct node) * numnodes, GFP_KERNEL);
	if (!sysfs_nodes) {
		err = -ENOMEM;
		goto out;
	}
	memset(sysfs_nodes, 0, sizeof(struct node) * numnodes);

	sysfs_memblks = kmalloc(sizeof(struct memblk) * num_memblks,
				GFP_KERNEL);
	if (!sysfs_memblks) {
		kfree(sysfs_nodes);
		err = -ENOMEM;
		goto out;
	}
	memset(sysfs_memblks, 0, sizeof(struct memblk) * num_memblks);

	sysfs_cpus = kmalloc(sizeof(struct cpu) * NR_CPUS, GFP_KERNEL);
	if (!sysfs_cpus) {
		kfree(sysfs_memblks);
		kfree(sysfs_nodes);
		err = -ENOMEM;
		goto out;
	}
	memset(sysfs_cpus, 0, sizeof(struct cpu) * NR_CPUS);

	for (i = 0; i < numnodes; i++)
		if ((err = register_node(&sysfs_nodes[i], i, 0)))
			goto out;

	for (i = 0; i < num_memblks; i++)
		if ((err = register_memblk(&sysfs_memblks[i], i,
					   &sysfs_nodes[memblk_to_node(i)])))
			goto out;

	for (i = 0; i < NR_CPUS; i++)
		if (cpu_online(i))
			if((err = register_cpu(&sysfs_cpus[i], i,
					       &sysfs_nodes[cpu_to_node(i)])))
				goto out;
 out:
	return err;
}

__initcall(topology_init);
