/* i810.h -- Intel i810/i815 DRM template customization -*- linux-c -*-
 * Created: Thu Feb 15 00:01:12 2001 by gareth@valinux.com
 *
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
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 */

#ifndef __I810_H__
#define __I810_H__

/* This remains constant for all DRM template files.
 */
#define DRM(x) i810_##x

/* General customization:
 */
#define __HAVE_AGP		1
#define __MUST_HAVE_AGP		1
#define __HAVE_MTRR		1
#define __HAVE_CTX_BITMAP	1

/* Driver customization:
 */
#define __HAVE_RELEASE		1
#define DRIVER_RELEASE() do {						\
	i810_reclaim_buffers( dev, priv->pid );				\
} while (0)

/* DMA customization:
 */
#define __HAVE_DMA		1
#define __HAVE_DMA_QUEUE	1
#define __HAVE_DMA_WAITLIST	1
#define __HAVE_DMA_RECLAIM	1

#define __HAVE_DMA_QUIESCENT	1
#define DRIVER_DMA_QUIESCENT() do {					\
	i810_dma_quiescent( dev );					\
} while (0)

#define __HAVE_DMA_IRQ		1
#define __HAVE_DMA_IRQ_BH	1
#define __HAVE_SHARED_IRQ       1
#define DRIVER_PREINSTALL() do {					\
	drm_i810_private_t *dev_priv =					\
		(drm_i810_private_t *)dev->dev_private;			\
	u16 tmp;							\
   	tmp = I810_READ16( I810REG_HWSTAM );				\
   	tmp = tmp & 0x6000;						\
   	I810_WRITE16( I810REG_HWSTAM, tmp );				\
									\
      	tmp = I810_READ16( I810REG_INT_MASK_R );			\
   	tmp = tmp & 0x6000;		/* Unmask interrupts */		\
   	I810_WRITE16( I810REG_INT_MASK_R, tmp );			\
   	tmp = I810_READ16( I810REG_INT_ENABLE_R );			\
   	tmp = tmp & 0x6000;		/* Disable all interrupts */	\
      	I810_WRITE16( I810REG_INT_ENABLE_R, tmp );			\
} while (0)

#define DRIVER_POSTINSTALL() do {					\
	drm_i810_private_t *dev_priv =					\
		(drm_i810_private_t *)dev->dev_private;			\
	u16 tmp;							\
   	tmp = I810_READ16( I810REG_INT_ENABLE_R );			\
   	tmp = tmp & 0x6000;						\
   	tmp = tmp | 0x0003;	/* Enable bp & user interrupts */	\
   	I810_WRITE16( I810REG_INT_ENABLE_R, tmp );			\
} while (0)

#define DRIVER_UNINSTALL() do {						\
	drm_i810_private_t *dev_priv =					\
		(drm_i810_private_t *)dev->dev_private;			\
	u16 tmp;							\
	if ( dev_priv ) {						\
		tmp = I810_READ16( I810REG_INT_IDENTITY_R );		\
		tmp = tmp & ~(0x6000);	/* Clear all interrupts */	\
		if ( tmp != 0 )						\
			I810_WRITE16( I810REG_INT_IDENTITY_R, tmp );	\
									\
		tmp = I810_READ16( I810REG_INT_ENABLE_R );		\
		tmp = tmp & 0x6000;	/* Disable all interrupts */	\
		I810_WRITE16( I810REG_INT_ENABLE_R, tmp );		\
	}								\
} while (0)

/* Buffer customization:
 */

#define DRIVER_BUF_PRIV_T	drm_i810_buf_priv_t

#define DRIVER_AGP_BUFFERS_MAP( dev )					\
	((drm_i810_private_t *)((dev)->dev_private))->buffer_map

#endif
