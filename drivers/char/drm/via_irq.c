/* via_irq.c
 *
 * Copyright 2004 BEAM Ltd.
 * Copyright 2002 Tungsten Graphics, Inc.
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
 * BEAM LTD, TUNGSTEN GRAPHICS  AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: 
 *    Terry Barnaby <terry1@beam.ltd.uk>
 *    Keith Whitwell <keith@tungstengraphics.com>
 *
 *
 * This code provides standard DRM access to the Via CLE266's Vertical blank
 * interrupt.
 */

#include "via.h"
#include "drmP.h"
#include "drm.h"
#include "via_drm.h"
#include "via_drv.h"

#define VIA_REG_INTERRUPT       0x200

/* VIA_REG_INTERRUPT */
#define VIA_IRQ_GLOBAL          (1 << 31)
#define VIA_IRQ_VBI_ENABLE      (1 << 19)
#define VIA_IRQ_SEC_VBI_ENABLE  (1 << 17)
#define VIA_IRQ_SEC_VBI_PENDING (1 << 15)
#define VIA_IRQ_VBI_PENDING     (1 << 3)



irqreturn_t via_driver_irq_handler(DRM_IRQ_ARGS)
{
	drm_device_t *dev = (drm_device_t *) arg;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status = VIA_READ(VIA_REG_INTERRUPT);

	if (status & VIA_IRQ_VBI_PENDING) {
		atomic_inc(&dev->vbl_received);
		DRM_WAKEUP(&dev->vbl_queue);
		DRM(vbl_send_signals) (dev);

		VIA_WRITE(VIA_REG_INTERRUPT, status);
		return IRQ_HANDLED;
	}

#if 0
	if (status & VIA_IRQ_SEC_VBI_PENDING) {
		atomic_inc(&dev->sec_vbl_received);
		DRM_WAKEUP(&dev->sec_vbl_queue);
		DRM(vbl_send_signals)(dev); /* KW: Need a parameter here? */
		handled = IRQ_HANDLED;
	}
#endif

	VIA_WRITE(VIA_REG_INTERRUPT, status);
	return IRQ_NONE;
}

static __inline__ void viadrv_acknowledge_irqs(drm_via_private_t * dev_priv)
{
	u32 status;

	if (dev_priv) {
		/* Acknowlege interrupts ?? */
		status = VIA_READ(VIA_REG_INTERRUPT);
		VIA_WRITE(VIA_REG_INTERRUPT, status | VIA_IRQ_VBI_PENDING);
	}
}

int via_driver_vblank_wait(drm_device_t * dev, unsigned int *sequence)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	unsigned int cur_vblank;
	int ret = 0;

	DRM_DEBUG("viadrv_vblank_wait\n");
	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return -EINVAL;
	}

	viadrv_acknowledge_irqs(dev_priv);

	/* Assume that the user has missed the current sequence number
	 * by about a day rather than she wants to wait for years
	 * using vertical blanks... 
	 */
	DRM_WAIT_ON(ret, dev->vbl_queue, 3 * DRM_HZ,
		    (((cur_vblank = atomic_read(&dev->vbl_received)) -
		      *sequence) <= (1 << 23)));

	*sequence = cur_vblank;
	return ret;
}

/*
 * drm_dma.h hooks
 */
void via_driver_irq_preinstall(drm_device_t * dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;

	DRM_DEBUG("driver_irq_preinstall: dev_priv: %p\n", dev_priv);
	if (dev_priv) {
	        dev_priv->last_vblank_valid = 0;
		DRM_DEBUG("mmio: %p\n", dev_priv->mmio);
		status = VIA_READ(VIA_REG_INTERRUPT);
		DRM_DEBUG("intreg: %x\n", status & VIA_IRQ_VBI_ENABLE);

		// Clear VSync interrupt regs
		VIA_WRITE(VIA_REG_INTERRUPT, status & ~VIA_IRQ_VBI_ENABLE);

		/* Clear bits if they're already high */
		viadrv_acknowledge_irqs(dev_priv);
	}
}

void via_driver_irq_postinstall(drm_device_t * dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;

	DRM_DEBUG("via_driver_irq_postinstall\n");
	if (dev_priv) {
		status = VIA_READ(VIA_REG_INTERRUPT);
		VIA_WRITE(VIA_REG_INTERRUPT, status | VIA_IRQ_GLOBAL
			  | VIA_IRQ_VBI_ENABLE);
		/* Some magic, oh for some data sheets ! */

		VIA_WRITE8(0x83d4, 0x11);
		VIA_WRITE8(0x83d5, VIA_READ8(0x83d5) | 0x30);

	}
}

void via_driver_irq_uninstall(drm_device_t * dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;

	DRM_DEBUG("driver_irq_uninstall)\n");
	if (dev_priv) {

		/* Some more magic, oh for some data sheets ! */

		VIA_WRITE8(0x83d4, 0x11);
		VIA_WRITE8(0x83d5, VIA_READ8(0x83d5) & ~0x30);

		status = VIA_READ(VIA_REG_INTERRUPT);
		VIA_WRITE(VIA_REG_INTERRUPT, status & ~VIA_IRQ_VBI_ENABLE);
	}
}

