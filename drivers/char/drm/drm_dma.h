/**
 * \file drm_dma.h 
 * DMA IOCTL and function support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Fri Mar 19 14:30:16 1999 by faith@valinux.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
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

#include "drmP.h"


#ifndef __HAVE_DMA_WAITQUEUE
#define __HAVE_DMA_WAITQUEUE	0
#endif
#ifndef __HAVE_DMA_RECLAIM
#define __HAVE_DMA_RECLAIM	0
#endif

#if __HAVE_DMA

/**
 * Initialize the DMA data.
 * 
 * \param dev DRM device.
 * \return zero on success or a negative value on failure.
 *
 * Allocate and initialize a drm_device_dma structure.
 */
int DRM(dma_setup)( drm_device_t *dev )
{
	int i;

	dev->dma = DRM(alloc)( sizeof(*dev->dma), DRM_MEM_DRIVER );
	if ( !dev->dma )
		return -ENOMEM;

	memset( dev->dma, 0, sizeof(*dev->dma) );

	for ( i = 0 ; i <= DRM_MAX_ORDER ; i++ )
		memset(&dev->dma->bufs[i], 0, sizeof(dev->dma->bufs[0]));

	return 0;
}

/**
 * Cleanup the DMA resources.
 *
 * \param dev DRM device.
 *
 * Free all pages associated with DMA buffers, the buffers and pages lists, and
 * finally the the drm_device::dma structure itself.
 */
void DRM(dma_takedown)(drm_device_t *dev)
{
	drm_device_dma_t  *dma = dev->dma;
	int		  i, j;

	if (!dma) return;

				/* Clear dma buffers */
	for (i = 0; i <= DRM_MAX_ORDER; i++) {
		if (dma->bufs[i].seg_count) {
			DRM_DEBUG("order %d: buf_count = %d,"
				  " seg_count = %d\n",
				  i,
				  dma->bufs[i].buf_count,
				  dma->bufs[i].seg_count);
			for (j = 0; j < dma->bufs[i].seg_count; j++) {
				if (dma->bufs[i].seglist[j]) {
					DRM(free_pages)(dma->bufs[i].seglist[j],
							dma->bufs[i].page_order,
							DRM_MEM_DMA);
				}
			}
			DRM(free)(dma->bufs[i].seglist,
				  dma->bufs[i].seg_count
				  * sizeof(*dma->bufs[0].seglist),
				  DRM_MEM_SEGS);
		}
	   	if (dma->bufs[i].buf_count) {
		   	for (j = 0; j < dma->bufs[i].buf_count; j++) {
				if (dma->bufs[i].buflist[j].dev_private) {
					DRM(free)(dma->bufs[i].buflist[j].dev_private,
						  dma->bufs[i].buflist[j].dev_priv_size,
						  DRM_MEM_BUFS);
				}
			}
		   	DRM(free)(dma->bufs[i].buflist,
				  dma->bufs[i].buf_count *
				  sizeof(*dma->bufs[0].buflist),
				  DRM_MEM_BUFS);
#if __HAVE_DMA_FREELIST
		   	DRM(freelist_destroy)(&dma->bufs[i].freelist);
#endif
		}
	}

	if (dma->buflist) {
		DRM(free)(dma->buflist,
			  dma->buf_count * sizeof(*dma->buflist),
			  DRM_MEM_BUFS);
	}

	if (dma->pagelist) {
		DRM(free)(dma->pagelist,
			  dma->page_count * sizeof(*dma->pagelist),
			  DRM_MEM_PAGES);
	}
	DRM(free)(dev->dma, sizeof(*dev->dma), DRM_MEM_DRIVER);
	dev->dma = NULL;
}


/**
 * Free a buffer.
 *
 * \param dev DRM device.
 * \param buf buffer to free.
 * 
 * Resets the fields of \p buf.
 */
void DRM(free_buffer)(drm_device_t *dev, drm_buf_t *buf)
{
	if (!buf) return;

	buf->waiting  = 0;
	buf->pending  = 0;
	buf->filp     = NULL;
	buf->used     = 0;

	if ( __HAVE_DMA_WAITQUEUE && waitqueue_active(&buf->dma_wait)) {
		wake_up_interruptible(&buf->dma_wait);
	}
#if __HAVE_DMA_FREELIST
	else {
		drm_device_dma_t *dma = dev->dma;
				/* If processes are waiting, the last one
				   to wake will put the buffer on the free
				   list.  If no processes are waiting, we
				   put the buffer on the freelist here. */
		DRM(freelist_put)(dev, &dma->bufs[buf->order].freelist, buf);
	}
#endif
}

#if !__HAVE_DMA_RECLAIM
/**
 * Reclaim the buffers.
 *
 * \param filp file pointer.
 *
 * Frees each buffer associated with \p filp not already on the hardware.
 */
void DRM(reclaim_buffers)( struct file *filp )
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	int		 i;

	if (!dma) return;
	for (i = 0; i < dma->buf_count; i++) {
		if (dma->buflist[i]->filp == filp) {
			switch (dma->buflist[i]->list) {
			case DRM_LIST_NONE:
				DRM(free_buffer)(dev, dma->buflist[i]);
				break;
			case DRM_LIST_WAIT:
				dma->buflist[i]->list = DRM_LIST_RECLAIM;
				break;
			default:
				/* Buffer already on hardware. */
				break;
			}
		}
	}
}
#endif

#if !__HAVE_IRQ
/* This stub DRM_IOCTL_CONTROL handler is for the drivers that used to require
 * IRQs for DMA but no longer do.  It maintains compatibility with the X Servers
 * that try to use the control ioctl by simply returning success.
 */
int DRM(control)( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_control_t ctl;

	if ( copy_from_user( &ctl, (drm_control_t __user *)arg, sizeof(ctl) ) )
		return -EFAULT;

	switch ( ctl.func ) {
	case DRM_INST_HANDLER:
	case DRM_UNINST_HANDLER:
		return 0;
	default:
		return -EINVAL;
	}
}
#endif

#endif /* __HAVE_DMA */
