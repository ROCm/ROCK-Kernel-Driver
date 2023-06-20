/*
 * Copyright (c) 2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef AMDKCL_DRM_VMA_MANAGER_H
#define AMDKCL_DRM_VMA_MANAGER_H

/* We make up offsets for buffer objects so we can recognize them at
 * mmap time. pgoff in mmap is an unsigned long, so we need to make sure
 * that the faked up offset will fit
 */
#include <drm/drm_vma_manager.h>
#include <drm/drm_file.h>

/* Copied from include/drm/drm_vma_manager.h */
#if (BITS_PER_LONG == 64)
#ifdef DRM_FILE_PAGE_OFFSET_START
#undef DRM_FILE_PAGE_OFFSET_START
#endif
#ifdef DRM_FILE_PAGE_OFFSET_SIZE
#undef DRM_FILE_PAGE_OFFSET_SIZE
#endif

#define DRM_FILE_PAGE_OFFSET_START ((0xFFFFFFFFULL >> PAGE_SHIFT) + 1)
#define DRM_FILE_PAGE_OFFSET_SIZE ((0xFFFFFFFFULL >> PAGE_SHIFT) * 4096)

static inline void
kcl_drm_vma_offset_manager_adjust(struct drm_vma_offset_manager *mgr)
{
	u64 size;

	BUG_ON(!mgr);

	size = mgr->vm_addr_space_mm.head_node.hole_size;
	if (size < DRM_FILE_PAGE_OFFSET_SIZE)
		drm_vma_offset_manager_destroy(mgr);
	else
		return;

	drm_vma_offset_manager_init(mgr,
		DRM_FILE_PAGE_OFFSET_START,
		DRM_FILE_PAGE_OFFSET_SIZE);
}
#else
static inline void
kcl_drm_vma_offset_manager_adjust(struct drm_vma_offset_manager *mgr)
{
}
#endif

#endif
