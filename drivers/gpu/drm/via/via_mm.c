/*
 * Copyright 2006 Tungsten Graphics Inc., Bismarck, ND., USA.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include "drmP.h"
#include "via_drm.h"
#include "via_drv.h"
#include "drm_sman.h"

struct drm_via_video_save_head *via_video_save_head;
#define VIA_MM_ALIGN_SHIFT 4
#define VIA_MM_ALIGN_MASK ( (1 << VIA_MM_ALIGN_SHIFT) - 1)

int via_agp_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_agp_t *agp = data;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_set_range(&dev_priv->sman, VIA_MEM_AGP, 0,
				 agp->size >> VIA_MM_ALIGN_SHIFT);

	if (ret) {
		DRM_ERROR("AGP memory manager initialisation error\n");
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	dev_priv->agp_initialized = 1;
	dev_priv->agp_offset = agp->offset;
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("offset = %u, size = %u\n", agp->offset, agp->size);
	return 0;
}
static void *global_dev;
static void *global_file_priv;

int via_fb_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_fb_t *fb = data;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_set_range(&dev_priv->sman, VIA_MEM_VIDEO, 0,
				 fb->size >> VIA_MM_ALIGN_SHIFT);

	if (ret) {
		DRM_ERROR("VRAM memory manager initialisation error\n");
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	dev_priv->vram_initialized = 1;
	dev_priv->vram_offset = fb->offset;

	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("offset = %u, size = %u\n", fb->offset, fb->size);
	global_dev = dev;
	global_file_priv = file_priv;

	return 0;

}

int via_final_context(struct drm_device *dev, int context)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	via_release_futex(dev_priv, context);

	/* Linux specific until context tracking code gets ported to BSD */
	/* Last context, perform cleanup */
	if (dev->ctx_count == 1 && dev->dev_private) {
		DRM_DEBUG("Last Context\n");
		if (dev->irq)
			drm_irq_uninstall(dev);
		via_cleanup_futex(dev_priv);
		via_do_cleanup_map(dev);
	}
	return 1;
}

void via_lastclose(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	if (!dev_priv)
		return;

	mutex_lock(&dev->struct_mutex);
	drm_sman_cleanup(&dev_priv->sman);
	dev_priv->vram_initialized = 0;
	dev_priv->agp_initialized = 0;
	mutex_unlock(&dev->struct_mutex);
}

static int via_videomem_presave_ok(drm_via_private_t *dev_priv,
	drm_via_mem_t *mem)
{
	void *pvideomem = 0, *psystemmem = 0;
	struct drm_via_video_save_head *pnode = 0;

	if (!mem || !mem->size || (mem->type != VIA_MEM_VIDEO_SAVE))
		return 0;

	/* here the mem->offset is the absolute address,
	 * not the offset within videomem
	 */
	pvideomem = (void *)ioremap(dev_priv->fb->offset + mem->offset,
		mem->size);
	if (!pvideomem)
		return 0;

	psystemmem = kmalloc(mem->size, GFP_KERNEL);
	if (!psystemmem) {
		iounmap(pvideomem);
		return 0;
	}

	/* map success, then save this information into
	 * a data structure for later saving usage
	 */
	pnode = kmalloc(
		sizeof(struct drm_via_video_save_head), GFP_KERNEL);
	if (!pnode) {
		iounmap(pvideomem);
		kfree(psystemmem);
		return 0;
	}

	pnode->next = 0;
	pnode->psystemmem = psystemmem;
	pnode->pvideomem = pvideomem;
	pnode->size = mem->size;
	pnode->token = mem->offset;

	/* insert this node into list */
	if (!via_video_save_head) {
		via_video_save_head = pnode;
	} else {
		pnode->next = via_video_save_head;
		via_video_save_head = pnode;
	}

	return 1;
}

static int via_videomem_housekeep_ok(drm_via_mem_t *mem)
{
	struct drm_via_video_save_head **ppnode = 0;
	struct drm_via_video_save_head *tmpnode = 0;
	/* if this mem's token match with one node of the list */
	for (ppnode = &via_video_save_head; *ppnode;
	ppnode = (struct drm_via_video_save_head **)(&((*ppnode)->next))) {
		if ((*ppnode)->token == mem->offset)
			break;
	}

	if (*ppnode == 0) {
		/* not found, the user may specify the wrong mem node to free */
		return 0;
	}

	/* delete this node from the list and then
	*free all the mem to avoid memory leak
	*/
	tmpnode = *ppnode;
	*ppnode = (*ppnode)->next;
	iounmap(tmpnode->pvideomem);
	kfree(tmpnode->psystemmem);
	kfree(tmpnode);

	return 1;
}
int via_mem_alloc(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	drm_via_mem_t *mem = data;
	int retval = 0;
	struct drm_memblock_item *item;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	unsigned long tmpSize;

	if (mem->type > VIA_MEM_VIDEO_SAVE) {
		DRM_ERROR("Unknown memory type allocation\n");
		return -EINVAL;
	}
	mutex_lock(&dev->struct_mutex);
	if (0 == ((mem->type == VIA_MEM_VIDEO ||
		mem->type == VIA_MEM_VIDEO_SAVE) ? dev_priv->vram_initialized :
		      dev_priv->agp_initialized)) {
		DRM_ERROR
		    ("Attempt to allocate from uninitialized memory manager.\n");
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	tmpSize = (mem->size + VIA_MM_ALIGN_MASK) >> VIA_MM_ALIGN_SHIFT;
	item = drm_sman_alloc(&dev_priv->sman,
		(mem->type == VIA_MEM_VIDEO_SAVE ? VIA_MEM_VIDEO : mem->type),
			tmpSize, 0, (unsigned long)file_priv);
	mutex_unlock(&dev->struct_mutex);
	if (item) {
		mem->offset = ((mem->type == VIA_MEM_VIDEO ||
			mem->type == VIA_MEM_VIDEO_SAVE) ?
			dev_priv->vram_offset : dev_priv->agp_offset) +
			(item->mm->offset(item->mm, item->mm_info) <<
			VIA_MM_ALIGN_SHIFT);
		mem->index = item->user_hash.key;
		if (mem->type == VIA_MEM_VIDEO_SAVE) {
			if (!via_videomem_presave_ok(dev_priv, mem)) {
				mutex_lock(&dev->struct_mutex);
				drm_sman_free_key(&dev_priv->sman, mem->index);
				mutex_unlock(&dev->struct_mutex);
				retval = -ENOMEM;
			}
		}
	} else {
		mem->offset = 0;
		mem->size = 0;
		mem->index = 0;
		DRM_DEBUG("Video memory allocation failed\n");
		retval = -ENOMEM;
	}

	return retval;
}

int via_mem_free(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	drm_via_mem_t *mem = data;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm_sman_free_key(&dev_priv->sman, mem->index);
	if (mem->type == VIA_MEM_VIDEO_SAVE) {
		if (!via_videomem_housekeep_ok(mem))
			ret = -EINVAL;
	}
	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("free = 0x%lx\n", mem->index);

	return ret;
}


void via_reclaim_buffers_locked(struct drm_device * dev,
				struct drm_file *file_priv)
{
	drm_via_private_t *dev_priv = dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	if (drm_sman_owner_clean(&dev_priv->sman, (unsigned long)file_priv)) {
		mutex_unlock(&dev->struct_mutex);
		return;
	}

	if (dev->driver->dma_quiescent) {
		dev->driver->dma_quiescent(dev);
	}

	drm_sman_owner_cleanup(&dev_priv->sman, (unsigned long)file_priv);
	mutex_unlock(&dev->struct_mutex);
	return;
}
static int via_fb_alloc(drm_via_mem_t *mem)
{
	struct drm_device *dev = global_dev;
	struct drm_file *file_priv = global_file_priv;

	if (dev && file_priv)
		return via_mem_alloc(dev, mem, file_priv);
	else
		return -EINVAL;
}
EXPORT_SYMBOL(via_fb_alloc);

static int via_fb_free(drm_via_mem_t *mem)
{
	struct drm_device *dev = global_dev;
	struct drm_file *file_priv = global_file_priv;

	if (dev && file_priv)
		return via_mem_free(dev, mem, file_priv);
	else
		return -EINVAL;
}
EXPORT_SYMBOL(via_fb_free);
