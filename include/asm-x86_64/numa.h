#ifndef _ASM_X8664_NUMA_H 
#define _ASM_X8664_NUMA_H 1

#define MAXNODE 8 
#define NODEMASK 0xff

struct node { 
	u64 start,end; 
};

#define for_all_nodes(x) for ((x) = 0; (x) <= maxnode; (x)++) \
				if ((1UL << (x)) & nodes_present)

#define early_for_all_nodes(n) \
	for (n=0; n<MAXNODE;n++) if (nodes[n].start!=nodes[n].end)

extern int compute_hash_shift(struct node *nodes, int numnodes, u64 maxmem);
extern unsigned long nodes_present;

#define ZONE_ALIGN (1UL << (MAX_ORDER+PAGE_SHIFT))

#endif
