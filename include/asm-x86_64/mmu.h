#ifndef __x86_64_MMU_H
#define __x86_64_MMU_H

/*
 * The x86_64 doesn't have a mmu context, but
 * we put the segment information here.
 */
typedef struct { 
	void *segments;
	unsigned long cpuvalid;
} mm_context_t;

#endif
