/* sbuslib.c: Helper library for SBUS framebuffer drivers.
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>

#include <asm/oplib.h>

#include "sbuslib.h"

void sbusfb_fill_var(struct fb_var_screeninfo *var, int prom_node, int bpp)
{
	memset(var, 0, sizeof(*var));

	var->xres = prom_getintdefault(prom_node, "width", 1152);
	var->yres = prom_getintdefault(prom_node, "height", 900);
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->bits_per_pixel = bpp;
}

EXPORT_SYMBOL(sbusfb_fill_var);

static unsigned long sbusfb_mmapsize(long size, unsigned long fbsize)
{
	if (size == SBUS_MMAP_EMPTY) return 0;
	if (size >= 0) return size;
	return fbsize * (-size);
}

int sbusfb_mmap_helper(struct sbus_mmap_map *map,
		       unsigned long physbase,
		       unsigned long fbsize,
		       unsigned long iospace,
		       struct vm_area_struct *vma)
{
	unsigned int size, page, r, map_size;
	unsigned long map_offset = 0;
	unsigned long off;
	int i;
                                        
	size = vma->vm_end - vma->vm_start;
	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	off = vma->vm_pgoff << PAGE_SHIFT;

	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= (VM_SHM | VM_IO | VM_LOCKED);
	
	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		map_size = 0;
		for (i = 0; map[i].size; i++)
			if (map[i].voff == off+page) {
				map_size = sbusfb_mmapsize(map[i].size, fbsize);
#ifdef __sparc_v9__
#define POFF_MASK	(PAGE_MASK|0x1UL)
#else
#define POFF_MASK	(PAGE_MASK)
#endif				
				map_offset = (physbase + map[i].poff) & POFF_MASK;
				break;
			}
		if (!map_size){
			page += PAGE_SIZE;
			continue;
		}
		if (page + map_size > size)
			map_size = size - page;
		r = io_remap_page_range(vma,
					vma->vm_start + page,
					map_offset, map_size,
					vma->vm_page_prot, iospace);
		if (r)
			return -EAGAIN;
		page += map_size;
	}

	return 0;
}
