#ifndef __MIPS_UMAP_H
#define __MIPS_UMAP_H

void remove_mapping (struct task_struct *task, unsigned long start,
unsigned long end);

#endif 

