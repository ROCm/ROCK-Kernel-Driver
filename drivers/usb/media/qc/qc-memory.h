#ifndef _LINUX_QC_MEMORY_H
#define _LINUX_QC_MEMORY_H

#include <linux/mm.h>

void *qc_mm_rvmalloc(unsigned long size);
void qc_mm_rvfree(void *mem, unsigned long size);
int qc_mm_remap(struct vm_area_struct *vma, void *src, unsigned long src_size, const void *dst, unsigned long dst_size);

#endif
