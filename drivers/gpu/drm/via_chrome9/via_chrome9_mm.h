/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice
 * (including the next paragraph) shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL VIA, S3 GRAPHICS, AND/OR
 * ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef _VIA_CHROME9_MM_H_
#define _VIA_CHROME9_MM_H_
struct drm_via_chrome9_pciemem_ctrl {
	enum {
		pciemem_copy_from_user = 0,
		pciemem_copy_to_user,
		pciemem_memset,
		} ctrl_type;
	unsigned int pcieoffset;
	unsigned int size;/*in Byte*/
	unsigned char memsetdata;/*for memset*/
	void *usermode_data;/*user mode data pointer*/
};

extern int via_chrome9_map_init(struct drm_device *dev,
	struct drm_via_chrome9_init *init);
extern int via_chrome9_heap_management_init(struct drm_device
	*dev, struct drm_via_chrome9_init *init);
extern void via_chrome9_memory_destroy_heap(struct drm_device
	*dev, struct drm_via_chrome9_private *dev_priv);
extern int via_chrome9_ioctl_check_vidmem_size(struct drm_device
	*dev, void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_pciemem_ctrl(struct drm_device *dev,
	void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_allocate_aperture(struct drm_device
	*dev, void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_free_aperture(struct drm_device *dev,
	void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_allocate_mem_base(struct drm_device
	*dev, void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_allocate_mem_wrapper(
	struct drm_device *dev,	void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_freemem_base(struct drm_device
	*dev, void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_free_mem_wrapper(struct drm_device
	*dev, void *data, struct drm_file *file_priv);
extern void via_chrome9_reclaim_buffers_locked(struct drm_device
	*dev, struct drm_file *file_priv);

#endif

