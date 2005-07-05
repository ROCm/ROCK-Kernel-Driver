/**
 * \file drm_compat.h
 * Backward compatability definitions for Direct Rendering Manager
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_COMPAT_H_
#define _DRM_COMPAT_H_

#ifndef minor
#define minor(x) MINOR((x))
#endif

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(x)
#endif

#ifndef preempt_disable
#define preempt_disable()
#define preempt_enable()
#endif

#ifndef pte_offset_map
#define pte_offset_map pte_offset
#define pte_unmap(pte)
#endif

#ifndef module_param
#define module_param(name, type, perm)
#endif

#ifndef list_for_each_safe
#define list_for_each_safe(pos, n, head)				\
	for (pos = (head)->next, n = pos->next; pos != (head);		\
		pos = n, n = pos->next)
#endif

#ifndef list_for_each_entry
#define list_for_each_entry(pos, head, member)				\
       for (pos = list_entry((head)->next, typeof(*pos), member),	\
                    prefetch(pos->member.next);				\
            &pos->member != (head);					\
            pos = list_entry(pos->member.next, typeof(*pos), member),	\
                    prefetch(pos->member.next))
#endif

#ifndef list_for_each_entry_safe
#define list_for_each_entry_safe(pos, n, head, member)                  \
        for (pos = list_entry((head)->next, typeof(*pos), member),      \
                n = list_entry(pos->member.next, typeof(*pos), member); \
             &pos->member != (head);                                    \
             pos = n, n = list_entry(n->member.next, typeof(*n), member))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,19)
static inline struct page *vmalloc_to_page(void *vmalloc_addr)
{
	unsigned long addr = (unsigned long)vmalloc_addr;
	struct page *page = NULL;
	pgd_t *pgd = pgd_offset_k(addr);
	pmd_t *pmd;
	pte_t *ptep, pte;

	if (!pgd_none(*pgd)) {
		pmd = pmd_offset(pgd, addr);
		if (!pmd_none(*pmd)) {
			preempt_disable();
			ptep = pte_offset_map(pmd, addr);
			pte = *ptep;
			if (pte_present(pte))
				page = pte_page(pte);
			pte_unmap(ptep);
			preempt_enable();
		}
	}
	return page;
}
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,2)
#define down_write down
#define up_write up
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define DRM_PCI_DEV(pdev) &pdev->dev
#else
#define DRM_PCI_DEV(pdev) NULL
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
static inline unsigned iminor(struct inode *inode)
{
	return MINOR(inode->i_rdev);
}

#define old_encode_dev(x) (x)

struct drm_sysfs_class;
struct class_simple;
struct device;

#define pci_dev_put(x) do {} while (0)
#define pci_get_subsys pci_find_subsys

static inline struct class_device *DRM(sysfs_device_add) (struct drm_sysfs_class
							  * cs, dev_t dev,
							  struct device *
							  device,
							  const char *fmt,
							  ...) {
	return NULL;
}

static inline void DRM(sysfs_device_remove) (dev_t dev) {
}

static inline void DRM(sysfs_destroy) (struct drm_sysfs_class * cs) {
}

static inline struct drm_sysfs_class *DRM(sysfs_create) (struct module * owner,
							 char *name) {
	return NULL;
}

#ifndef pci_pretty_name
#define pci_pretty_name(x) x->name
#endif

struct drm_device;
static inline int radeon_create_i2c_busses(struct drm_device *dev)
{
	return 0;
};
static inline void radeon_delete_i2c_busses(struct drm_device *dev)
{
};

#endif

#ifndef __user
#define __user
#endif

#ifndef __put_page
#define __put_page(p)           atomic_dec(&(p)->count)
#endif

#ifndef REMAP_PAGE_RANGE_5_ARGS
#define DRM_RPR_ARG(vma)
#else
#define DRM_RPR_ARG(vma) vma,
#endif

#define VM_OFFSET(vma) ((vma)->vm_pgoff << PAGE_SHIFT)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
static inline int remap_pfn_range(struct vm_area_struct *vma, unsigned long from, unsigned long pfn, unsigned long size, pgprot_t pgprot)
{
  return remap_page_range(DRM_RPR_ARG(vma) from, 
			  pfn << PAGE_SHIFT,
			  size,
			  pgprot);
}
#endif

/* old architectures */
#ifdef __AMD64__
#define __x86_64__
#endif

#endif
