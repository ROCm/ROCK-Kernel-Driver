/* radeon_mem.c -- Simple agp/fb memory manager for radeon -*- linux-c -*-
 *
 * Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.
 * 
 * The Weather Channel (TM) funded Tungsten Graphics to develop the
 * initial release of the Radeon 8500 driver under the XFree86 license.
 * This notice must be preserved.
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keith@tungstengraphics.com>
 */

#include "radeon.h"
#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"

/* Interrupts - Used for device synchronization and flushing in the
 * following circumstances:
 *
 * - Exclusive FB access with hw idle:
 *    - Wait for GUI Idle (?) interrupt, then do normal flush.
 *
 * - Frame throttling, NV_fence:
 *    - Drop marker irq's into command stream ahead of time.
 *    - Wait on irq's with lock *not held*
 *    - Check each for termination condition
 *
 * - Internally in cp_getbuffer, etc:
 *    - as above, but wait with lock held???
 *
 * NOTE: These functions are misleadingly named -- the irq's aren't
 * tied to dma at all, this is just a hangover from dri prehistory.
 */

void DRM(dma_service)( DRM_IRQ_ARGS )
{
	drm_device_t *dev = (drm_device_t *) arg;
	drm_radeon_private_t *dev_priv = 
	   (drm_radeon_private_t *)dev->dev_private;
   	u32 temp;

	/* Need to wait for fifo to drain?
	 */
	temp = RADEON_READ(RADEON_GEN_INT_STATUS);  
	temp = temp & RADEON_SW_INT_TEST_ACK;  
	if (temp == 0) return;  
	RADEON_WRITE(RADEON_GEN_INT_STATUS, temp);  

	atomic_inc(&dev_priv->irq_received);
#ifdef __linux__
	schedule_task(&dev->tq);
#endif /* __linux__ */
#ifdef __FreeBSD__
	taskqueue_enqueue(taskqueue_swi, &dev->task);
#endif /* __FreeBSD__ */
}

void DRM(dma_immediate_bh)( DRM_TASKQUEUE_ARGS )
{
	drm_device_t *dev = (drm_device_t *) arg;
	drm_radeon_private_t *dev_priv = 
	   (drm_radeon_private_t *)dev->dev_private;

#ifdef __linux__
	wake_up_interruptible(&dev_priv->irq_queue); 
#endif /* __linux__ */
#ifdef __FreeBSD__
	wakeup( &dev_priv->irq_queue );
#endif
}


int radeon_emit_irq(drm_device_t *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	atomic_inc(&dev_priv->irq_emitted);

	BEGIN_RING(2); 
	OUT_RING( CP_PACKET0( RADEON_GEN_INT_STATUS, 0 ) );
	OUT_RING( RADEON_SW_INT_FIRE );
	ADVANCE_RING(); 
 	COMMIT_RING();

	return atomic_read(&dev_priv->irq_emitted);
}


int radeon_wait_irq(drm_device_t *dev, int irq_nr)
{
  	drm_radeon_private_t *dev_priv = 
	   (drm_radeon_private_t *)dev->dev_private;
#ifdef __linux__
	DECLARE_WAITQUEUE(entry, current);
	unsigned long end = jiffies + HZ*3;
#endif /* __linux__ */
	int ret = 0;

 	if (atomic_read(&dev_priv->irq_received) >= irq_nr)  
 		return 0; 

	dev_priv->stats.boxes |= RADEON_BOX_WAIT_IDLE;

#ifdef __linux__
	add_wait_queue(&dev_priv->irq_queue, &entry);

	for (;;) {
		current->state = TASK_INTERRUPTIBLE;
	   	if (atomic_read(&dev_priv->irq_received) >= irq_nr) 
		   break;
		if((signed)(end - jiffies) <= 0) {
		   	ret = -EBUSY;	/* Lockup?  Missed irq? */
			break;
		}
	      	schedule_timeout(HZ*3);
	      	if (signal_pending(current)) {
		   	ret = -EINTR;
			break;
		}
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&dev_priv->irq_queue, &entry);
	return ret;
#endif /* __linux__ */
	
#ifdef __FreeBSD__
	ret = tsleep( &dev_priv->irq_queue, PZERO | PCATCH, \
		"rdnirq", 3*hz );
	if ( (ret == EWOULDBLOCK) || (ret == EINTR) )
		return DRM_ERR(EBUSY);
	return ret;
#endif /* __FreeBSD__ */
}


int radeon_emit_and_wait_irq(drm_device_t *dev)
{
	return radeon_wait_irq( dev, radeon_emit_irq(dev) );
}


/* Needs the lock as it touches the ring.
 */
int radeon_irq_emit( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_irq_emit_t emit;
	int result;

	LOCK_TEST_WITH_RETURN( dev );

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL( emit, (drm_radeon_irq_emit_t *)data,
				  sizeof(emit) );

	result = radeon_emit_irq( dev );

	if ( DRM_COPY_TO_USER( emit.irq_seq, &result, sizeof(int) ) ) {
		DRM_ERROR( "copy_to_user\n" );
		return DRM_ERR(EFAULT);
	}

	return 0;
}


/* Doesn't need the hardware lock.
 */
int radeon_irq_wait( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_irq_wait_t irqwait;

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL( irqwait, (drm_radeon_irq_wait_t *)data,
				  sizeof(irqwait) );

	return radeon_wait_irq( dev, irqwait.irq_seq );
}

