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

#include <linux/interrupt.h>	/* For task queue support */

#ifndef __HAVE_DMA_WAITQUEUE
#define __HAVE_DMA_WAITQUEUE	0
#endif
#ifndef __HAVE_DMA_RECLAIM
#define __HAVE_DMA_RECLAIM	0
#endif
#ifndef __HAVE_SHARED_IRQ
#define __HAVE_SHARED_IRQ	0
#endif

#if __HAVE_SHARED_IRQ
#define DRM_IRQ_TYPE		SA_SHIRQ
#else
#define DRM_IRQ_TYPE		0
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
	buf->filp     = 0;
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




#if __HAVE_DMA_IRQ

/**
 * Install IRQ handler.
 *
 * \param dev DRM device.
 * \param irq IRQ number.
 *
 * Initializes the IRQ related data, and setups drm_device::vbl_queue. Installs the handler, calling the driver
 * \c DRM(driver_irq_preinstall)() and \c DRM(driver_irq_postinstall)() functions
 * before and after the installation.
 */
int DRM(irq_install)( drm_device_t *dev, int irq )
{
	int ret;

	if ( !irq )
		return -EINVAL;

	down( &dev->struct_sem );

	/* Driver must have been initialized */
	if ( !dev->dev_private ) {
		up( &dev->struct_sem );
		return -EINVAL;
	}

	if ( dev->irq ) {
		up( &dev->struct_sem );
		return -EBUSY;
	}
	dev->irq = irq;
	up( &dev->struct_sem );

	DRM_DEBUG( "%s: irq=%d\n", __FUNCTION__, irq );

	dev->context_flag = 0;
	dev->interrupt_flag = 0;
	dev->dma_flag = 0;

	dev->dma->next_buffer = NULL;
	dev->dma->next_queue = NULL;
	dev->dma->this_buffer = NULL;

#if __HAVE_DMA_IRQ_BH
	INIT_WORK(&dev->work, DRM(dma_immediate_bh), dev);
#endif

#if __HAVE_VBL_IRQ
	init_waitqueue_head(&dev->vbl_queue);

	spin_lock_init( &dev->vbl_lock );

	INIT_LIST_HEAD( &dev->vbl_sigs.head );

	dev->vbl_pending = 0;
#endif

				/* Before installing handler */
	DRM(driver_irq_preinstall)(dev);

				/* Install handler */
	ret = request_irq( dev->irq, DRM(dma_service),
			   DRM_IRQ_TYPE, dev->devname, dev );
	if ( ret < 0 ) {
		down( &dev->struct_sem );
		dev->irq = 0;
		up( &dev->struct_sem );
		return ret;
	}

				/* After installing handler */
	DRM(driver_irq_postinstall)(dev);

	return 0;
}

/**
 * Uninstall the IRQ handler.
 *
 * \param dev DRM device.
 *
 * Calls the driver's \c DRM(driver_irq_uninstall)() function, and stops the irq.
 */
int DRM(irq_uninstall)( drm_device_t *dev )
{
	int irq;

	down( &dev->struct_sem );
	irq = dev->irq;
	dev->irq = 0;
	up( &dev->struct_sem );

	if ( !irq )
		return -EINVAL;

	DRM_DEBUG( "%s: irq=%d\n", __FUNCTION__, irq );

	DRM(driver_irq_uninstall)( dev );

	free_irq( irq, dev );

	return 0;
}

/**
 * IRQ control ioctl.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_control structure.
 * \return zero on success or a negative number on failure.
 *
 * Calls irq_install() or irq_uninstall() according to \p arg.
 */
int DRM(control)( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_control_t ctl;

	if ( copy_from_user( &ctl, (drm_control_t *)arg, sizeof(ctl) ) )
		return -EFAULT;

	switch ( ctl.func ) {
	case DRM_INST_HANDLER:
		return DRM(irq_install)( dev, ctl.irq );
	case DRM_UNINST_HANDLER:
		return DRM(irq_uninstall)( dev );
	default:
		return -EINVAL;
	}
}

#if __HAVE_VBL_IRQ

/**
 * Wait for VBLANK.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param data user argument, pointing to a drm_wait_vblank structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the IRQ is installed. 
 *
 * If a signal is requested checks if this task has already scheduled the same signal
 * for the same vblank sequence number - nothing to be done in
 * that case. If the number of tasks waiting for the interrupt exceeds 100 the
 * function fails. Otherwise adds a new entry to drm_device::vbl_sigs for this
 * task.
 *
 * If a signal is not requested, then calls vblank_wait().
 */
int DRM(wait_vblank)( DRM_IOCTL_ARGS )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_wait_vblank_t vblwait;
	struct timeval now;
	int ret = 0;
	unsigned int flags;

	if (!dev->irq)
		return -EINVAL;

	DRM_COPY_FROM_USER_IOCTL( vblwait, (drm_wait_vblank_t *)data,
				  sizeof(vblwait) );

	switch ( vblwait.request.type & ~_DRM_VBLANK_FLAGS_MASK ) {
	case _DRM_VBLANK_RELATIVE:
		vblwait.request.sequence += atomic_read( &dev->vbl_received );
		vblwait.request.type &= ~_DRM_VBLANK_RELATIVE;
	case _DRM_VBLANK_ABSOLUTE:
		break;
	default:
		return -EINVAL;
	}

	flags = vblwait.request.type & _DRM_VBLANK_FLAGS_MASK;
	
	if ( flags & _DRM_VBLANK_SIGNAL ) {
		unsigned long irqflags;
		drm_vbl_sig_t *vbl_sig;
		
		vblwait.reply.sequence = atomic_read( &dev->vbl_received );

		spin_lock_irqsave( &dev->vbl_lock, irqflags );

		/* Check if this task has already scheduled the same signal
		 * for the same vblank sequence number; nothing to be done in
		 * that case
		 */
		list_for_each_entry( vbl_sig, &dev->vbl_sigs.head, head ) {
			if (vbl_sig->sequence == vblwait.request.sequence
			    && vbl_sig->info.si_signo == vblwait.request.signal
			    && vbl_sig->task == current)
			{
				spin_unlock_irqrestore( &dev->vbl_lock, irqflags );
				goto done;
			}
		}

		if ( dev->vbl_pending >= 100 ) {
			spin_unlock_irqrestore( &dev->vbl_lock, irqflags );
			return -EBUSY;
		}

		dev->vbl_pending++;

		spin_unlock_irqrestore( &dev->vbl_lock, irqflags );

		if ( !( vbl_sig = DRM_MALLOC( sizeof( drm_vbl_sig_t ) ) ) ) {
			return -ENOMEM;
		}

		memset( (void *)vbl_sig, 0, sizeof(*vbl_sig) );

		vbl_sig->sequence = vblwait.request.sequence;
		vbl_sig->info.si_signo = vblwait.request.signal;
		vbl_sig->task = current;

		spin_lock_irqsave( &dev->vbl_lock, irqflags );

		list_add_tail( (struct list_head *) vbl_sig, &dev->vbl_sigs.head );

		spin_unlock_irqrestore( &dev->vbl_lock, irqflags );
	} else {
		ret = DRM(vblank_wait)( dev, &vblwait.request.sequence );

		do_gettimeofday( &now );
		vblwait.reply.tval_sec = now.tv_sec;
		vblwait.reply.tval_usec = now.tv_usec;
	}

done:
	DRM_COPY_TO_USER_IOCTL( (drm_wait_vblank_t *)data, vblwait,
				sizeof(vblwait) );

	return ret;
}

/**
 * Send the VBLANK signals.
 *
 * \param dev DRM device.
 *
 * Sends a signal for each task in drm_device::vbl_sigs and empties the list.
 *
 * If a signal is not requested, then calls vblank_wait().
 */
void DRM(vbl_send_signals)( drm_device_t *dev )
{
	struct list_head *list, *tmp;
	drm_vbl_sig_t *vbl_sig;
	unsigned int vbl_seq = atomic_read( &dev->vbl_received );
	unsigned long flags;

	spin_lock_irqsave( &dev->vbl_lock, flags );

	list_for_each_safe( list, tmp, &dev->vbl_sigs.head ) {
		vbl_sig = list_entry( list, drm_vbl_sig_t, head );
		if ( ( vbl_seq - vbl_sig->sequence ) <= (1<<23) ) {
			vbl_sig->info.si_code = vbl_seq;
			send_sig_info( vbl_sig->info.si_signo, &vbl_sig->info, vbl_sig->task );

			list_del( list );

			DRM_FREE( vbl_sig, sizeof(*vbl_sig) );

			dev->vbl_pending--;
		}
	}

	spin_unlock_irqrestore( &dev->vbl_lock, flags );
}

#endif	/* __HAVE_VBL_IRQ */

#else

int DRM(control)( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_control_t ctl;

	if ( copy_from_user( &ctl, (drm_control_t *)arg, sizeof(ctl) ) )
		return -EFAULT;

	switch ( ctl.func ) {
	case DRM_INST_HANDLER:
	case DRM_UNINST_HANDLER:
		return 0;
	default:
		return -EINVAL;
	}
}

#endif /* __HAVE_DMA_IRQ */

#endif /* __HAVE_DMA */
