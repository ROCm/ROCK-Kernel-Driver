/*
 * linux/include/asm-arm/arch-sa1100/mmzone.h
 *
 * (C) 1999-2000, Nicolas Pitre <nico@cam.org>
 * (inspired by Kanoj Sarcar's code)
 *
 * Because of the wide memory address space between physical RAM banks on the 
 * SA1100, it's much convenient to use Linux's NUMA support to implement our 
 * memory map representation.  Assuming all memory nodes have equal access 
 * characteristics, we then have generic discontigous memory support.
 *
 * Of course, all this isn't mandatory for SA1100 implementations with only
 * one used memory bank.  For those, simply undefine CONFIG_DISCONTIGMEM.
 *
 * The nodes are matched with the physical memory bank addresses which are 
 * incidentally the same as virtual addresses.
 * 
 * 	node 0:  0xc0000000 - 0xc7ffffff
 * 	node 1:  0xc8000000 - 0xcfffffff
 * 	node 2:  0xd0000000 - 0xd7ffffff
 * 	node 3:  0xd8000000 - 0xdfffffff
 */

#define NR_NODES	4

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define KVADDR_TO_NID(addr) \
		(((unsigned long)(addr) - 0xc0000000) >> 27)

/*
 * Given a kaddr, ADDR_TO_MAPBASE finds the owning node of the memory
 * and returns the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr) \
			NODE_MEM_MAP(KVADDR_TO_NID((unsigned long)(kaddr)))

/*
 * Given a kaddr, LOCAL_MEM_MAP finds the owning node of the memory
 * and returns the index corresponding to the appropriate page in the
 * node's mem_map.
 */
#define LOCAL_MAP_NR(kvaddr) \
	(((unsigned long)(kvaddr) & 0x07ffffff) >> PAGE_SHIFT)

/*
 * Given a kaddr, virt_to_page returns a pointer to the corresponding 
 * mem_map entry.
 */
#define virt_to_page(kaddr) \
	(ADDR_TO_MAPBASE(kaddr) + LOCAL_MAP_NR(kaddr))

/*
 * VALID_PAGE returns a non-zero value if given page pointer is valid.
 * This assumes all node's mem_maps are stored within the node they refer to.
 */
#define VALID_PAGE(page) \
({ unsigned int node = KVADDR_TO_NID(page); \
   ( (node < NR_NODES) && \
     ((unsigned)((page) - NODE_MEM_MAP(node)) < NODE_DATA(node)->node_size) ); \
})

