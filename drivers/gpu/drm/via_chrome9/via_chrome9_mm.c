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

#include "drmP.h"
#include "via_chrome9_drm.h"
#include "via_chrome9_drv.h"
#include "drm_sman.h"
#include "via_chrome9_mm.h"

#define VIA_CHROME9_MM_GRANULARITY 4
#define VIA_CHROME9_MM_GRANULARITY_MASK ((1 << VIA_CHROME9_MM_GRANULARITY) - 1)


int via_chrome9_map_init(struct drm_device *dev,
	struct drm_via_chrome9_init *init)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;

	dev_priv->sarea = drm_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("could not find sarea!\n");
		goto error;
	}
	dev_priv->sarea_priv =
		(struct drm_via_chrome9_sarea *)((unsigned char *)dev_priv->
		sarea->handle + init->sarea_priv_offset);

	dev_priv->fb = drm_core_findmap(dev, init->fb_handle);
	if (!dev_priv->fb) {
		DRM_ERROR("could not find framebuffer!\n");
		goto error;
	}
	/* Frame buffer physical base address */
	dev_priv->fb_base_address = init->fb_base_address;

	if (init->shadow_size) {
		/* find apg shadow region mappings */
	dev_priv->shadow_map.shadow = drm_core_findmap(dev, init->
	shadow_handle);
		if (!dev_priv->shadow_map.shadow) {
			DRM_ERROR("could not shadow map!\n");
			goto error;
			}
		dev_priv->shadow_map.shadow_size = init->shadow_size;
		dev_priv->shadow_map.shadow_handle = (unsigned int *)dev_priv->
			shadow_map.shadow->handle;
		init->shadow_handle = dev_priv->shadow_map.shadow->offset;
		}
	if (init->agp_tex_size && init->chip_agp != CHIP_PCIE) {
		/* find apg texture buffer mappings */
		dev_priv->agp_tex = drm_core_findmap(dev, init->agp_tex_handle);
		dev_priv->agp_size = init->agp_tex_size;
		dev_priv->agp_offset = init->agp_tex_handle;
		if (!dev_priv->agp_tex) {
			DRM_ERROR("could not find agp texture map !\n");
			goto error;
		}
	}
	/* find mmio/dma mappings */
	dev_priv->mmio = drm_core_findmap(dev, init->mmio_handle);
	if (!dev_priv->mmio) {
		DRM_ERROR("failed to find mmio region!\n");
		goto error;
		}

	dev_priv->hostBlt = drm_core_findmap(dev, init->hostBlt_handle);
	if (!dev_priv->hostBlt) {
		DRM_ERROR("failed to find host bitblt region!\n");
		goto error;
		}

	dev_priv->drm_agp_type = init->agp_type;
	if (init->agp_type != AGP_DISABLED && init->chip_agp != CHIP_PCIE) {
		dev->agp_buffer_map = drm_core_findmap(dev, init->dma_handle);
		if (!dev->agp_buffer_map) {
			DRM_ERROR("failed to find dma buffer region!\n");
			goto error;
			}
		}

	dev_priv->bci  = (char *)dev_priv->mmio->handle + 0x10000;

	return 0;

error:
	/* do cleanup here, refine_later */
	return -EINVAL;
}

int via_chrome9_heap_management_init(struct drm_device *dev,
	struct drm_via_chrome9_init *init)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	int ret = 0;

	/* video memory management. range: 0 ---- video_whole_size */
	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_set_range(&dev_priv->sman, VIA_CHROME9_MEM_VIDEO,
		0, dev_priv->available_fb_size >> VIA_CHROME9_MM_GRANULARITY);
	if (ret) {
		DRM_ERROR("VRAM memory manager initialization ******ERROR\
			!******\n");
		mutex_unlock(&dev->struct_mutex);
		goto error;
	}
	dev_priv->vram_initialized = 1;
	/* agp/pcie heap management.
	note:because agp is contradict with pcie, so only one is enough
	for managing both of them.*/
	init->agp_type = dev_priv->drm_agp_type;
	if (init->agp_type != AGP_DISABLED && dev_priv->agp_size) {
		ret = drm_sman_set_range(&dev_priv->sman, VIA_CHROME9_MEM_AGP,
		0, dev_priv->agp_size >> VIA_CHROME9_MM_GRANULARITY);
	if (ret) {
		DRM_ERROR("AGP/PCIE memory manager initialization ******ERROR\
			!******\n");
		mutex_unlock(&dev->struct_mutex);
		goto error;
		}
	dev_priv->agp_initialized = 1;
	}
	mutex_unlock(&dev->struct_mutex);
	return 0;

error:
	/* Do error recover here, refine_later */
	return -EINVAL;
}


void via_chrome9_memory_destroy_heap(struct drm_device *dev,
	struct drm_via_chrome9_private *dev_priv)
{
	mutex_lock(&dev->struct_mutex);
	drm_sman_cleanup(&dev_priv->sman);
	dev_priv->vram_initialized = 0;
	dev_priv->agp_initialized = 0;
	mutex_unlock(&dev->struct_mutex);
}

void via_chrome9_reclaim_buffers_locked(struct drm_device *dev,
				struct drm_file *file_priv)
{
	return;
}

int via_chrome9_ioctl_allocate_aperture(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	return 0;
}

int via_chrome9_ioctl_free_aperture(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	return 0;
}


/* Allocate memory from DRM module for video playing */
int via_chrome9_ioctl_allocate_mem_base(struct drm_device *dev,
void *data, struct drm_file *file_priv)
{
	struct drm_via_chrome9_mem *mem = data;
	struct drm_memblock_item *item;
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	unsigned long tmpSize = 0, offset = 0, alignment = 0;
	/* modify heap_type to agp for pcie, since we treat pcie/agp heap
	no difference in heap management */
	if (mem->type == memory_heap_pcie) {
		if (dev_priv->chip_agp != CHIP_PCIE) {
			DRM_ERROR("User want to alloc memory from pcie heap \
			but via_chrome9.ko has no this heap exist.\n");
			return -EINVAL;
		}
	mem->type = memory_heap_agp;
	}

	if (mem->type > VIA_CHROME9_MEM_AGP) {
		DRM_ERROR("Unknown memory type allocation\n");
		return -EINVAL;
	}
	mutex_lock(&dev->struct_mutex);
	if (0 == ((mem->type == VIA_CHROME9_MEM_VIDEO) ?
		dev_priv->vram_initialized : dev_priv->agp_initialized)) {
		DRM_ERROR("Attempt to allocate from uninitialized\
			memory manager.\n");
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
		}
	tmpSize = (mem->size + VIA_CHROME9_MM_GRANULARITY_MASK) >>
		VIA_CHROME9_MM_GRANULARITY;
	mem->size = tmpSize << VIA_CHROME9_MM_GRANULARITY;
	alignment = (dev_priv->alignment & 0x80000000) ? dev_priv->
	alignment & 0x7FFFFFFF : 0;
	alignment /= (1 << VIA_CHROME9_MM_GRANULARITY);
	item = drm_sman_alloc(&dev_priv->sman, mem->type, tmpSize, alignment,
	(unsigned long)file_priv);
	mutex_unlock(&dev->struct_mutex);
	/* alloc failed */
	if (!item) {
		DRM_ERROR("Allocate memory failed ******ERROR******.\n");
		return -ENOMEM;
	}
	/* Till here every thing is ok, we check the memory type allocated
	and return appropriate value to user mode  Here the value return to
	user is very difficult to operate. BE CAREFULLY!!! */
	/* offset is used by user mode ap to calculate the virtual address
	which is used to access the memory allocated */
	mem->index = item->user_hash.key;
	offset = item->mm->offset(item->mm, item->mm_info) <<
	VIA_CHROME9_MM_GRANULARITY;
	switch (mem->type) {
	case VIA_CHROME9_MEM_VIDEO:
		mem->offset = offset + dev_priv->back_offset;
		break;
	case VIA_CHROME9_MEM_AGP:
	/* return different value to user according to the chip type */
		if (dev_priv->chip_agp == CHIP_PCIE) {
			mem->offset = offset +
			((struct drm_via_chrome9_DMA_manager *)dev_priv->
				dma_manager)->DMASize * sizeof(unsigned long);
		} else {
		mem->offset = offset;
		}
		break;
	default:
	/* Strange thing happen! Faint. Code bug! */
		DRM_ERROR("Enter here is impossible ******\
		ERROR******.\n");
		return -EINVAL;
	}
	/*DONE. Need we call function copy_to_user ?NO. We can't even
	touch user's space.But we are lucky, since kernel drm:drm_ioctl
	will to the job for us.  */
	return 0;
}

/* Allocate video/AGP/PCIE memory from heap management */
int via_chrome9_ioctl_allocate_mem_wrapper(struct drm_device
	*dev, void *data, struct drm_file *file_priv)
{
	struct drm_via_chrome9_memory_alloc *memory_alloc =
		(struct drm_via_chrome9_memory_alloc *)data;
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_mem mem;

	mem.size = memory_alloc->size;
	mem.type = memory_alloc->heap_type;
	dev_priv->alignment = memory_alloc->align | 0x80000000;
	if (via_chrome9_ioctl_allocate_mem_base(dev, &mem, file_priv)) {
		DRM_ERROR("Allocate memory error!.\n");
		return -ENOMEM;
		}
	dev_priv->alignment = 0;
	/* Till here every thing is ok, we check the memory type allocated and
	return appropriate value to user mode Here the value return to user is
	very difficult to operate. BE CAREFULLY!!!*/
	/* offset is used by user mode ap to calculate the virtual address
	which is used to access the memory allocated */
	memory_alloc->offset = mem.offset;
	memory_alloc->heap_info.lpL1Node = (void *)mem.index;
	memory_alloc->size = mem.size;
	switch (memory_alloc->heap_type) {
	case VIA_CHROME9_MEM_VIDEO:
		memory_alloc->physaddress = memory_alloc->offset +
		dev_priv->fb_base_address;
		memory_alloc->linearaddress = (void *)memory_alloc->physaddress;
		break;
	case VIA_CHROME9_MEM_AGP:
		/* return different value to user according to the chip type */
		if (dev_priv->chip_agp == CHIP_PCIE) {
			memory_alloc->physaddress = memory_alloc->offset;
			memory_alloc->linearaddress = (void *)memory_alloc->
			physaddress;
		} else {
			memory_alloc->physaddress = dev->agp->base +
			memory_alloc->offset +
			((struct drm_via_chrome9_DMA_manager *)
			dev_priv->dma_manager)->DMASize * sizeof(unsigned long);
			memory_alloc->linearaddress =
			(void *)memory_alloc->physaddress;
		}
		break;
	default:
		/* Strange thing happen! Faint. Code bug! */
		DRM_ERROR("Enter here is impossible ******ERROR******.\n");
		return -EINVAL;
	}
	return 0;
}

int via_chrome9_ioctl_free_mem_wrapper(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	struct drm_via_chrome9_memory_alloc *memory_alloc = data;
	struct drm_via_chrome9_mem mem;

	mem.index = (unsigned long)memory_alloc->heap_info.lpL1Node;
	if (via_chrome9_ioctl_freemem_base(dev, &mem, file_priv)) {
		DRM_ERROR("function free_mem_wrapper error.\n");
		return -EINVAL;
	}

	return 0;
}

int via_chrome9_ioctl_freemem_base(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	struct drm_via_chrome9_private *dev_priv = dev->dev_private;
	struct drm_via_chrome9_mem *mem = data;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_free_key(&dev_priv->sman, mem->index);
	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("free = 0x%lx\n", mem->index);

	return ret;
}

int via_chrome9_ioctl_check_vidmem_size(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	return 0;
}

int via_chrome9_ioctl_pciemem_ctrl(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	int result = 0;
	struct drm_via_chrome9_private *dev_priv = dev->dev_private;
	struct drm_via_chrome9_pciemem_ctrl *pcie_memory_ctrl = data;
	switch (pcie_memory_ctrl->ctrl_type) {
	case pciemem_copy_from_user:
		result = copy_from_user((void *)(
		dev_priv->pcie_vmalloc_nocache+
		pcie_memory_ctrl->pcieoffset),
		pcie_memory_ctrl->usermode_data,
		pcie_memory_ctrl->size);
		break;
	case pciemem_copy_to_user:
		result = copy_to_user(pcie_memory_ctrl->usermode_data,
		(void *)(dev_priv->pcie_vmalloc_nocache+
		pcie_memory_ctrl->pcieoffset),
		pcie_memory_ctrl->size);
		break;
	case pciemem_memset:
		memset((void *)(dev_priv->pcie_vmalloc_nocache +
		pcie_memory_ctrl->pcieoffset),
		pcie_memory_ctrl->memsetdata,
		pcie_memory_ctrl->size);
		break;
	default:
		break;
	}
	return 0;
}


int via_fb_alloc(struct drm_via_chrome9_mem *mem)
{
	struct drm_device *dev = (struct drm_device *)via_chrome9_dev_v4l;
	struct drm_via_chrome9_private *dev_priv;

	if (!dev || !dev->dev_private || !via_chrome9_filepriv_v4l) {
		DRM_ERROR("V4L work before X initialize DRM module !!!\n");
		return -EINVAL;
	}

	dev_priv = (struct drm_via_chrome9_private *)dev->dev_private;
	if (!dev_priv->vram_initialized ||
		mem->type != VIA_CHROME9_MEM_VIDEO) {
		DRM_ERROR("the memory type from V4L is error !!!\n");
		return -EINVAL;
	}

	if (via_chrome9_ioctl_allocate_mem_base(dev,
		mem, via_chrome9_filepriv_v4l)) {
		DRM_ERROR("DRM module allocate memory error for V4L!!!\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(via_fb_alloc);

int via_fb_free(struct drm_via_chrome9_mem *mem)
{
	struct drm_device *dev = (struct drm_device *)via_chrome9_dev_v4l;
	struct drm_via_chrome9_private *dev_priv;

	if (!dev || !dev->dev_private || !via_chrome9_filepriv_v4l)
		return -EINVAL;

	dev_priv = (struct drm_via_chrome9_private *)dev->dev_private;
	if (!dev_priv->vram_initialized ||
		mem->type != VIA_CHROME9_MEM_VIDEO)
		return -EINVAL;

	if (via_chrome9_ioctl_freemem_base(dev, mem, via_chrome9_filepriv_v4l))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(via_fb_free);
