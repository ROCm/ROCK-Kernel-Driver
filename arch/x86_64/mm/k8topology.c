/* 
 * AMD K8 NUMA support.
 * Discover the memory map and associated nodes.
 * 
 * Doesn't use the ACPI SRAT table because it has a questionable license.
 * Instead the northbridge registers are read directly. 
 * XXX in 2.5 we could use the generic SRAT code
 * 
 * Copyright 2002,2003 Andi Kleen, SuSE Labs.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/module.h>
#include <asm/io.h>
#include <linux/pci_ids.h>
#include <asm/types.h>
#include <asm/mmzone.h>
#include <asm/proto.h>
#include <asm/e820.h>
#include <asm/pci-direct.h>
#include <asm/numa.h>

static __init int find_northbridge(void)
{
	int num; 

	for (num = 0; num < 32; num++) { 
		u32 header;
		
		header = read_pci_config(0, num, 0, 0x00);  
		if (header != (PCI_VENDOR_ID_AMD | (0x1100<<16)))
			continue; 	

		header = read_pci_config(0, num, 1, 0x00); 
		if (header != (PCI_VENDOR_ID_AMD | (0x1101<<16)))
			continue;	
		return num; 
	} 

	return -1; 	
}

int __init k8_scan_nodes(unsigned long start, unsigned long end)
{ 
	unsigned long prevbase;
	struct node nodes[MAXNODE];
	int nodeid, i, nb; 
	int found = 0;
	u32 reg;

	nb = find_northbridge(); 
	if (nb < 0) 
		return nb;

	printk(KERN_INFO "Scanning NUMA topology in Northbridge %d\n", nb); 

	reg = read_pci_config(0, nb, 0, 0x60); 
	numnodes =  ((reg >> 4) & 7) + 1; 

	printk(KERN_INFO "Number of nodes %d (%x)\n", numnodes, reg);

	memset(&nodes,0,sizeof(nodes)); 
	prevbase = 0;
	for (i = 0; i < 8; i++) { 
		unsigned long base,limit; 

		base = read_pci_config(0, nb, 1, 0x40 + i*8);
		limit = read_pci_config(0, nb, 1, 0x44 + i*8);

		nodeid = limit & 7; 
		if ((base & 3) == 0) { 
			if (i < numnodes) 
				printk("Skipping disabled node %d\n", i); 
			continue;
		} 
		if (nodeid >= numnodes) { 
			printk("Ignoring excess node %d (%lx:%lx)\n", nodeid,
			       base, limit); 
			continue;
		} 

		if (!limit) { 
			printk(KERN_INFO "Skipping node entry %d (base %lx)\n", i,
			       base);
			continue;
		}
		if ((base >> 8) & 3 || (limit >> 8) & 3) {
			printk(KERN_ERR "Node %d using interleaving mode %lx/%lx\n", 
			       nodeid, (base>>8)&3, (limit>>8) & 3); 
			return -1; 
		}	
		if (node_online(nodeid)) { 
			printk(KERN_INFO "Node %d already present. Skipping\n", 
			       nodeid);
			continue;
		}

		limit >>= 16; 
		limit <<= 24; 
		limit |= (1<<24)-1;

		if (limit > end_pfn_map << PAGE_SHIFT) 
			limit = end_pfn_map << PAGE_SHIFT; 
		if (limit <= base)
			continue; 
			
		base >>= 16;
		base <<= 24; 

		if (base < start) 
			base = start; 
		if (limit > end) 
			limit = end; 
		if (limit == base) { 
			printk(KERN_ERR "Empty node %d\n", nodeid); 
			continue; 
		}
		if (limit < base) { 
			printk(KERN_ERR "Node %d bogus settings %lx-%lx.\n",
			       nodeid, base, limit); 			       
			continue;
		} 
		
		/* Could sort here, but pun for now. Should not happen anyroads. */
		if (prevbase > base) { 
			printk(KERN_ERR "Node map not sorted %lx,%lx\n",
			       prevbase,base);
			return -1;
		}
			
		printk(KERN_INFO "Node %d MemBase %016lx Limit %016lx\n", 
		       nodeid, base, limit); 
		
		found++;
		
		nodes[nodeid].start = base; 
		nodes[nodeid].end = limit;

		prevbase = base;
	} 

	if (!found)
		return -1; 

	memnode_shift = compute_hash_shift(nodes);
	if (memnode_shift < 0) { 
		printk(KERN_ERR "No NUMA node hash function found. Contact maintainer\n"); 
		return -1; 
	} 
	printk(KERN_INFO "Using node hash shift of %d\n", memnode_shift); 

	for (i = 0; i < MAXNODE; i++) { 
		if (nodes[i].start != nodes[i].end) { 
			/* assume 1:1 NODE:CPU */
			cpu_to_node[i] = i; 
		setup_node_bootmem(i, nodes[i].start, nodes[i].end); 
	} 
	}

	numa_init_array();
	return 0;
} 
