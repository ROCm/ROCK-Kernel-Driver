/*
 * pSeries NUMA support
 *
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/threads.h>
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <asm/lmb.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>

#if 1
#define dbg(args...) printk(KERN_INFO args)
#else
#define dbg(args...)
#endif

#ifdef DEBUG_NUMA
#define ARRAY_INITIALISER -1
#else
#define ARRAY_INITIALISER 0
#endif

int numa_cpu_lookup_table[NR_CPUS] = { [ 0 ... (NR_CPUS - 1)] =
	ARRAY_INITIALISER};
char *numa_memory_lookup_table;
cpumask_t numa_cpumask_lookup_table[MAX_NUMNODES];
int nr_cpus_in_node[MAX_NUMNODES] = { [0 ... (MAX_NUMNODES -1)] = 0};

struct pglist_data node_data[MAX_NUMNODES];
bootmem_data_t plat_node_bdata[MAX_NUMNODES];
static unsigned long node0_io_hole_size;
static int common_depth;

EXPORT_SYMBOL(node_data);
EXPORT_SYMBOL(numa_cpu_lookup_table);
EXPORT_SYMBOL(numa_memory_lookup_table);
EXPORT_SYMBOL(numa_cpumask_lookup_table);
EXPORT_SYMBOL(nr_cpus_in_node);

static inline void map_cpu_to_node(int cpu, int node)
{
	dbg("cpu %d maps to domain %d\n", cpu, node);
	numa_cpu_lookup_table[cpu] = node;
	if (!(cpu_isset(cpu, numa_cpumask_lookup_table[node]))) {
		cpu_set(cpu, numa_cpumask_lookup_table[node]);
		nr_cpus_in_node[node]++;
	}
}

#ifdef CONFIG_HOTPLUG_CPU
static void unmap_cpu_from_node(unsigned long cpu)
{
	int node = numa_cpu_lookup_table[cpu];

	dbg("removing cpu %lu from node %d\n", cpu, node);

	if (cpu_isset(cpu, numa_cpumask_lookup_table[node])) {
		cpu_clear(cpu, numa_cpumask_lookup_table[node]);
		nr_cpus_in_node[node]--;
	} else {
		printk(KERN_ERR "WARNING: cpu %lu not found in node %d\n",
		       cpu, node);
	}
}
#endif /* CONFIG_HOTPLUG_CPU */

static struct device_node * __devinit find_cpu_node(unsigned int cpu)
{
	unsigned int hw_cpuid = get_hard_smp_processor_id(cpu);
	struct device_node *cpu_node = NULL;
	unsigned int *interrupt_server, *reg;
	int len;

	while ((cpu_node = of_find_node_by_type(cpu_node, "cpu")) != NULL) {
		/* Try interrupt server first */
		interrupt_server = (unsigned int *)get_property(cpu_node,
					"ibm,ppc-interrupt-server#s", &len);

		len = len / sizeof(u32);

		if (interrupt_server && (len > 0)) {
			while (len--) {
				if (interrupt_server[len] == hw_cpuid)
					return cpu_node;
			}
		} else {
			reg = (unsigned int *)get_property(cpu_node,
							   "reg", &len);
			if (reg && (len > 0) && (reg[0] == hw_cpuid))
				return cpu_node;
		}
	}

	return NULL;
}

/*
 * Figure out to which domain a cpu belongs and stick it there.
 * Return the id of the domain used.
 */
static int numa_setup_cpu(unsigned long lcpu)
{
	int numa_domain;
	struct device_node *cpu;

	cpu = find_cpu_node(lcpu);

	if (cpu) {
		int *tmp, len;

		tmp = (int *)get_property(cpu, "ibm,associativity",
					  &len);
		if (tmp && (len >= common_depth)) {
			numa_domain = tmp[common_depth];

		} else {
			printk(KERN_ERR "WARNING: no NUMA "
			       "information for cpu %ld\n", lcpu);
			numa_domain = 0;
		}

		if (numa_domain >= MAX_NUMNODES) {
			/*
			 * POWER4 LPAR uses 0xffff as invalid node,
			 * dont warn in this case.
			 */
			if (numa_domain != 0xffff)
				printk(KERN_ERR "WARNING: cpu %ld "
				       "maps to invalid NUMA node %d\n",
				       lcpu, numa_domain);
			numa_domain = 0;
		}
	} else {
		printk(KERN_ERR "WARNING: no NUMA information for "
		       "cpu %ld\n", lcpu);
		numa_domain = 0;
	}

	/* This can be moved to callers if all domains are onlined at
	 * boot.
	 */
	node_set_online(numa_domain);

	map_cpu_to_node(lcpu, numa_domain);

	of_node_put(cpu);

	return numa_domain;
}

#ifdef CONFIG_HOTPLUG_CPU

static int cpu_numa_callback(struct notifier_block *nfb,
			     unsigned long action,
			     void *hcpu)
{
	if (action == CPU_UP_PREPARE) {
		dbg("Running %s for new cpu %lu\n", __FUNCTION__,
		    (unsigned long)hcpu);

		numa_setup_cpu((unsigned long)hcpu);

	} else if (action == CPU_DEAD || action == CPU_UP_CANCELED) {
		dbg("Running %s for dead cpu %lu\n", __FUNCTION__,
		    (unsigned long)hcpu);

		unmap_cpu_from_node((unsigned long)hcpu);

	}
	return NOTIFY_OK;
}


#endif /* CONFIG_HOTPLUG_CPU */

static int __init parse_numa_properties(void)
{
	struct device_node *cpu = NULL;
	struct device_node *memory = NULL;
	unsigned int *tmp;
	int cpu_associativity;
	int memory_associativity;
	int len;
	int max_domain = 0;
	long entries = lmb_end_of_DRAM() >> MEMORY_INCREMENT_SHIFT;
	unsigned long i;

	if (strstr(saved_command_line, "numa=off")) {
		printk(KERN_WARNING "NUMA disabled by user\n");
		return -1;
	}

	numa_memory_lookup_table =
		(char *)abs_to_virt(lmb_alloc(entries * sizeof(char), 1));

	for (i = 0; i < entries ; i++)
		numa_memory_lookup_table[i] = ARRAY_INITIALISER;

	/* Check that both memory and cpu have associativity information */
	cpu = of_find_node_by_type(NULL, "cpu");
	if (!cpu)
		return -1;

	tmp = (int *)get_property(cpu, "ibm,associativity", &len);
	if (!tmp || len <= 0) {
		of_node_put(cpu);
		return -1;
	}
	cpu_associativity = tmp[0];
	of_node_put(cpu);

	memory = of_find_node_by_type(NULL, "memory");
	if (!memory)
		return -1;

	tmp = (int *)get_property(memory, "ibm,associativity", &len);
	if (!tmp || len <= 0) {
		of_node_put(memory);
		return -1;
	}
	memory_associativity = tmp[0];
	of_node_put(memory);

	/* find common depth */
	if (cpu_associativity < memory_associativity)
		common_depth = cpu_associativity;
	else
		common_depth = memory_associativity;

	for_each_cpu(i) {
		int numa_domain = numa_setup_cpu(i);

		if (max_domain < numa_domain)
			max_domain = numa_domain;
	}

	memory = NULL;
	while ((memory = of_find_node_by_type(memory, "memory")) != NULL) {
		unsigned long start = 0;
		unsigned long size = 0;
		int numa_domain;
		int ranges;

		tmp = (unsigned int *)get_property(memory, "reg", &len);
		if (!tmp || len <= 0)
			continue;

		ranges = memory->n_addrs;
new_range:

		i = prom_n_size_cells(memory);
		while (i--) {
			start = (start << 32) | *tmp;
			tmp++;
		}

		i = prom_n_size_cells(memory);
		while (i--) {
			size = (size << 32) | *tmp;
			tmp++;
		}

		start = _ALIGN_DOWN(start, MEMORY_INCREMENT);
		size = _ALIGN_UP(size, MEMORY_INCREMENT);

		tmp = (unsigned int *)get_property(memory, "ibm,associativity",
					   &len);
		if (tmp && len >= common_depth) {
			numa_domain = tmp[common_depth];
		} else {
			printk(KERN_ERR "WARNING: no NUMA information for "
			       "memory at %lx\n", start);
			numa_domain = 0;
		}

		if (numa_domain >= MAX_NUMNODES) {
			if (numa_domain != 0xffff)
				printk(KERN_ERR "WARNING: memory at %lx maps "
				       "to invalid NUMA node %d\n", start,
				       numa_domain);
			numa_domain = 0;
		}

		node_set_online(numa_domain);

		if (max_domain < numa_domain)
			max_domain = numa_domain;

		/* 
		 * Coalesce memory regions. We dont handle holes within
		 * NUMA nodes.
		 */
		if (node_data[numa_domain].node_spanned_pages) {
			unsigned long shouldstart =
				node_data[numa_domain].node_start_pfn + 
				node_data[numa_domain].node_spanned_pages;
			if (shouldstart != (start / PAGE_SIZE)) {
				printk(KERN_ERR "Hole in node, disabling "
						"region start %lx length %lx\n",
						start, size);
				continue;
			}
			node_data[numa_domain].node_spanned_pages +=
				size / PAGE_SIZE;
		} else {
			node_data[numa_domain].node_start_pfn =
				start / PAGE_SIZE;
			node_data[numa_domain].node_spanned_pages =
				size / PAGE_SIZE;
		}

		for (i = start ; i < (start+size); i += MEMORY_INCREMENT)
			numa_memory_lookup_table[i >> MEMORY_INCREMENT_SHIFT] =
				numa_domain;

		dbg("memory region %lx to %lx maps to domain %d\n",
		    start, start+size, numa_domain);

		ranges--;
		if (ranges)
			goto new_range;
	}

	numnodes = max_domain + 1;

	/*
	 * This must run before the sched domains notifier in
	 * arch/ppc64/kernel/smp.c.
	 */
	hotcpu_notifier(cpu_numa_callback, 1);

	return 0;
}

static void __init setup_nonnuma(void)
{
	unsigned long top_of_ram = lmb_end_of_DRAM();
	unsigned long total_ram = lmb_phys_mem_size();
	unsigned long i;

	printk(KERN_INFO "Top of RAM: 0x%lx, Total RAM: 0x%lx\n",
	       top_of_ram, total_ram);
	printk(KERN_INFO "Memory hole size: %ldMB\n",
	       (top_of_ram - total_ram) >> 20);

	if (!numa_memory_lookup_table) {
		long entries = top_of_ram >> MEMORY_INCREMENT_SHIFT;
		numa_memory_lookup_table =
			(char *)abs_to_virt(lmb_alloc(entries * sizeof(char), 1));
		for (i = 0; i < entries ; i++)
			numa_memory_lookup_table[i] = ARRAY_INITIALISER;
	}

	for (i = 0; i < NR_CPUS; i++)
		map_cpu_to_node(i, 0);

	node_set_online(0);

	node_data[0].node_start_pfn = 0;
	node_data[0].node_spanned_pages = lmb_end_of_DRAM() / PAGE_SIZE;

	for (i = 0 ; i < top_of_ram; i += MEMORY_INCREMENT)
		numa_memory_lookup_table[i >> MEMORY_INCREMENT_SHIFT] = 0;

	node0_io_hole_size = top_of_ram - total_ram;
}

void __init do_init_bootmem(void)
{
	int nid;

	min_low_pfn = 0;
	max_low_pfn = lmb_end_of_DRAM() >> PAGE_SHIFT;

	if (parse_numa_properties())
		setup_nonnuma();

	for (nid = 0; nid < numnodes; nid++) {
		unsigned long start_paddr, end_paddr;
		int i;
		unsigned long bootmem_paddr;
		unsigned long bootmap_pages;

		if (node_data[nid].node_spanned_pages == 0)
			continue;

		start_paddr = node_data[nid].node_start_pfn * PAGE_SIZE;
		end_paddr = start_paddr + 
				(node_data[nid].node_spanned_pages * PAGE_SIZE);

		dbg("node %d\n", nid);
		dbg("start_paddr = %lx\n", start_paddr);
		dbg("end_paddr = %lx\n", end_paddr);

		NODE_DATA(nid)->bdata = &plat_node_bdata[nid];

		bootmap_pages = bootmem_bootmap_pages((end_paddr - start_paddr) >> PAGE_SHIFT);
		dbg("bootmap_pages = %lx\n", bootmap_pages);

		bootmem_paddr = lmb_alloc_base(bootmap_pages << PAGE_SHIFT,
				PAGE_SIZE, end_paddr);
		dbg("bootmap_paddr = %lx\n", bootmem_paddr);

		init_bootmem_node(NODE_DATA(nid), bootmem_paddr >> PAGE_SHIFT,
				  start_paddr >> PAGE_SHIFT,
				  end_paddr >> PAGE_SHIFT);

		for (i = 0; i < lmb.memory.cnt; i++) {
			unsigned long physbase, size;

			physbase = lmb.memory.region[i].physbase;
			size = lmb.memory.region[i].size;

			if (physbase < end_paddr &&
			    (physbase+size) > start_paddr) {
				/* overlaps */
				if (physbase < start_paddr) {
					size -= start_paddr - physbase;
					physbase = start_paddr;
				}

				if (size > end_paddr - physbase)
					size = end_paddr - physbase;

				dbg("free_bootmem %lx %lx\n", physbase, size);
				free_bootmem_node(NODE_DATA(nid), physbase,
						  size);
			}
		}

		for (i = 0; i < lmb.reserved.cnt; i++) {
			unsigned long physbase = lmb.reserved.region[i].physbase;
			unsigned long size = lmb.reserved.region[i].size;

			if (physbase < end_paddr &&
			    (physbase+size) > start_paddr) {
				/* overlaps */
				if (physbase < start_paddr) {
					size -= start_paddr - physbase;
					physbase = start_paddr;
				}

				if (size > end_paddr - physbase)
					size = end_paddr - physbase;

				dbg("reserve_bootmem %lx %lx\n", physbase,
				    size);
				reserve_bootmem_node(NODE_DATA(nid), physbase,
						     size);
			}
		}
	}
}

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
	unsigned long zholes_size[MAX_NR_ZONES];
	struct page *node_mem_map; 
	int nid;

	memset(zones_size, 0, sizeof(zones_size));
	memset(zholes_size, 0, sizeof(zholes_size));

	for (nid = 0; nid < numnodes; nid++) {
		unsigned long start_pfn;
		unsigned long end_pfn;

		start_pfn = plat_node_bdata[nid].node_boot_start >> PAGE_SHIFT;
		end_pfn = plat_node_bdata[nid].node_low_pfn;

		zones_size[ZONE_DMA] = end_pfn - start_pfn;
		zholes_size[ZONE_DMA] = 0;
		if (nid == 0)
			zholes_size[ZONE_DMA] = node0_io_hole_size;

		dbg("free_area_init node %d %lx %lx (hole: %lx)\n", nid,
		    zones_size[ZONE_DMA], start_pfn, zholes_size[ZONE_DMA]);

		/* 
		 * Give this empty node a dummy struct page to avoid
		 * us from trying to allocate a node local mem_map
		 * in free_area_init_node (which will fail).
		 */
		if (!node_data[nid].node_spanned_pages)
			node_mem_map = alloc_bootmem(sizeof(struct page));
		else
			node_mem_map = NULL;

		free_area_init_node(nid, NODE_DATA(nid), node_mem_map,
				    zones_size, start_pfn, zholes_size);
	}
}
