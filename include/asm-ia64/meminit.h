#ifndef meminit_h
#define meminit_h

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

/*
 * Entries defined so far:
 * 	- boot param structure itself
 * 	- memory map
 * 	- initrd (optional)
 * 	- command line string
 * 	- kernel code & data
 *
 * More could be added if necessary
 */
#define IA64_MAX_RSVD_REGIONS 5

struct rsvd_region {
	unsigned long start;	/* virtual address of beginning of element */
	unsigned long end;	/* virtual address of end of element + 1 */
};

extern struct rsvd_region rsvd_region[IA64_MAX_RSVD_REGIONS + 1];
extern int num_rsvd_regions;

extern void find_memory (void);
extern void reserve_memory (void);
extern void find_initrd (void);
extern int filter_rsvd_memory (unsigned long start, unsigned long end, void *arg);

#ifdef CONFIG_DISCONTIGMEM
extern void call_pernode_memory (unsigned long start, unsigned long end, void *arg);
#endif

#define IGNORE_PFN0	1	/* XXX fix me: ignore pfn 0 until TLB miss handler is updated... */

#endif /* meminit_h */
