/* i830_dma.c -- DMA support for the I830 -*- linux-c -*-
 * Created: Mon Dec 13 01:50:01 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Rickard E. (Rik) Faith <faith@valinux.com>
 *	    Jeff Hartmann <jhartmann@valinux.com>
 *	    Keith Whitwell <keith@tungstengraphics.com>
 *	    Abraham vd Merwe <abraham@2d3d.co.za>
 *
 */

#define __NO_VERSION__
#include "i830.h"
#include "drmP.h"
#include "drm.h"
#include "i830_drm.h"
#include "i830_drv.h"
#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>

#define I830_BUF_FREE		2
#define I830_BUF_CLIENT		1
#define I830_BUF_HARDWARE      	0

#define I830_BUF_UNMAPPED 0
#define I830_BUF_MAPPED   1

#define RING_LOCALS	unsigned int outring, ringmask; volatile char *virt;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,2)
#define down_write down
#define up_write up
#endif

#ifndef LockPage
#define LockPage(page)		set_bit(PG_locked, &(page)->flags)
#endif
#ifndef UnlockPage
#define UnlockPage(page)	unlock_page(page)
#endif

#define I830_VERBOSE 0

#define BEGIN_LP_RING(n) do {				\
	if (I830_VERBOSE)				\
		printk("BEGIN_LP_RING(%d) in %s\n",	\
			  n, __FUNCTION__);		\
	if (dev_priv->ring.space < n*4) 		\
		i830_wait_ring(dev, n*4);		\
	dev_priv->ring.space -= n*4;			\
	outring = dev_priv->ring.tail;			\
	ringmask = dev_priv->ring.tail_mask;		\
	virt = dev_priv->ring.virtual_start;		\
} while (0)

#define ADVANCE_LP_RING() do {					\
	if (I830_VERBOSE) printk("ADVANCE_LP_RING %x\n", outring);	\
	dev_priv->ring.tail = outring;				\
	I830_WRITE(LP_RING + RING_TAIL, outring);		\
} while(0)

#define OUT_RING(n) do {						\
	if (I830_VERBOSE) printk("   OUT_RING %x\n", (int)(n));	\
	*(volatile unsigned int *)(virt + outring) = n;			\
	outring += 4;							\
	outring &= ringmask;						\
} while (0)

static inline void i830_print_status_page(drm_device_t *dev)
{
   	drm_device_dma_t *dma = dev->dma;
      	drm_i830_private_t *dev_priv = dev->dev_private;
	u32 *temp = (u32 *)dev_priv->hw_status_page;
   	int i;

   	DRM_DEBUG(  "hw_status: Interrupt Status : %x\n", temp[0]);
   	DRM_DEBUG(  "hw_status: LpRing Head ptr : %x\n", temp[1]);
   	DRM_DEBUG(  "hw_status: IRing Head ptr : %x\n", temp[2]);
      	DRM_DEBUG(  "hw_status: Reserved : %x\n", temp[3]);
   	DRM_DEBUG(  "hw_status: Driver Counter : %d\n", temp[5]);
   	for(i = 9; i < dma->buf_count + 9; i++) {
	   	DRM_DEBUG( "buffer status idx : %d used: %d\n", i - 9, temp[i]);
	}
}

static drm_buf_t *i830_freelist_get(drm_device_t *dev)
{
   	drm_device_dma_t *dma = dev->dma;
	int		 i;
   	int 		 used;
   
	/* Linear search might not be the best solution */

   	for (i = 0; i < dma->buf_count; i++) {
	   	drm_buf_t *buf = dma->buflist[ i ];
	   	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
		/* In use is already a pointer */
	   	used = cmpxchg(buf_priv->in_use, I830_BUF_FREE, 
			       I830_BUF_CLIENT);
	   	if(used == I830_BUF_FREE) {
			return buf;
		}
	}
   	return NULL;
}

/* This should only be called if the buffer is not sent to the hardware
 * yet, the hardware updates in use for us once its on the ring buffer.
 */

static int i830_freelist_put(drm_device_t *dev, drm_buf_t *buf)
{
   	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
   	int used;
   
   	/* In use is already a pointer */
   	used = cmpxchg(buf_priv->in_use, I830_BUF_CLIENT, I830_BUF_FREE);
   	if(used != I830_BUF_CLIENT) {
	   	DRM_ERROR("Freeing buffer thats not in use : %d\n", buf->idx);
	   	return -EINVAL;
	}
   
   	return 0;
}

static struct file_operations i830_buffer_fops = {
	.open	 = DRM(open),
	.flush	 = DRM(flush),
	.release = DRM(release),
	.ioctl	 = DRM(ioctl),
	.mmap	 = i830_mmap_buffers,
	.read	 = DRM(read),
	.fasync  = DRM(fasync),
      	.poll	 = DRM(poll),
};

int i830_mmap_buffers(struct file *filp, struct vm_area_struct *vma)
{
	drm_file_t	    *priv	  = filp->private_data;
	drm_device_t	    *dev;
	drm_i830_private_t  *dev_priv;
	drm_buf_t           *buf;
	drm_i830_buf_priv_t *buf_priv;

	lock_kernel();
	dev	 = priv->dev;
	dev_priv = dev->dev_private;
	buf      = dev_priv->mmap_buffer;
	buf_priv = buf->dev_private;
   
	vma->vm_flags |= (VM_IO | VM_DONTCOPY);
	vma->vm_file = filp;
   
   	buf_priv->currently_mapped = I830_BUF_MAPPED;
	unlock_kernel();

	if (remap_page_range(DRM_RPR_ARG(vma) vma->vm_start,
			     VM_OFFSET(vma),
			     vma->vm_end - vma->vm_start,
			     vma->vm_page_prot)) return -EAGAIN;
	return 0;
}

static int i830_map_buffer(drm_buf_t *buf, struct file *filp)
{
	drm_file_t	  *priv	  = filp->private_data;
	drm_device_t	  *dev	  = priv->dev;
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
      	drm_i830_private_t *dev_priv = dev->dev_private;
   	struct file_operations *old_fops;
	int retcode = 0;

	if(buf_priv->currently_mapped == I830_BUF_MAPPED) return -EINVAL;

	down_write( &current->mm->mmap_sem );
	old_fops = filp->f_op;
	filp->f_op = &i830_buffer_fops;
	dev_priv->mmap_buffer = buf;
	buf_priv->virtual = (void *)do_mmap(filp, 0, buf->total, 
					    PROT_READ|PROT_WRITE,
					    MAP_SHARED, 
					    buf->bus_address);
	dev_priv->mmap_buffer = NULL;
	filp->f_op = old_fops;
	if ((unsigned long)buf_priv->virtual > -1024UL) {
		/* Real error */
		DRM_ERROR("mmap error\n");
		retcode = (signed int)buf_priv->virtual;
		buf_priv->virtual = 0;
	}
	up_write( &current->mm->mmap_sem );

	return retcode;
}

static int i830_unmap_buffer(drm_buf_t *buf)
{
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
	int retcode = 0;

	if(buf_priv->currently_mapped != I830_BUF_MAPPED) 
		return -EINVAL;

	down_write(&current->mm->mmap_sem);
	retcode = do_munmap(current->mm,
			    (unsigned long)buf_priv->virtual,
			    (size_t) buf->total);
	up_write(&current->mm->mmap_sem);

   	buf_priv->currently_mapped = I830_BUF_UNMAPPED;
   	buf_priv->virtual = 0;

	return retcode;
}

static int i830_dma_get_buffer(drm_device_t *dev, drm_i830_dma_t *d, 
			       struct file *filp)
{
	drm_file_t	  *priv	  = filp->private_data;
	drm_buf_t	  *buf;
	drm_i830_buf_priv_t *buf_priv;
	int retcode = 0;

	buf = i830_freelist_get(dev);
	if (!buf) {
		retcode = -ENOMEM;
	   	DRM_ERROR("retcode=%d\n", retcode);
		return retcode;
	}
   
	retcode = i830_map_buffer(buf, filp);
	if(retcode) {
		i830_freelist_put(dev, buf);
	   	DRM_ERROR("mapbuf failed, retcode %d\n", retcode);
		return retcode;
	}
	buf->pid     = priv->pid;
	buf_priv = buf->dev_private;	
	d->granted = 1;
   	d->request_idx = buf->idx;
   	d->request_size = buf->total;
   	d->virtual = buf_priv->virtual;

	return retcode;
}

static int i830_dma_cleanup(drm_device_t *dev)
{
	drm_device_dma_t *dma = dev->dma;

	if(dev->dev_private) {
		int i;
	   	drm_i830_private_t *dev_priv = 
	     		(drm_i830_private_t *) dev->dev_private;
	   
	   	if(dev_priv->ring.virtual_start) {
		   	DRM(ioremapfree)((void *) dev_priv->ring.virtual_start,
					 dev_priv->ring.Size);
		}
	   	if(dev_priv->hw_status_page != 0UL) {
		   	pci_free_consistent(dev->pdev, PAGE_SIZE,
					    (void *)dev_priv->hw_status_page,
					    dev_priv->dma_status_page);
		   	/* Need to rewrite hardware status page */
		   	I830_WRITE(0x02080, 0x1ffff000);
		}
	   	DRM(free)(dev->dev_private, sizeof(drm_i830_private_t), 
			 DRM_MEM_DRIVER);
	   	dev->dev_private = NULL;

		for (i = 0; i < dma->buf_count; i++) {
			drm_buf_t *buf = dma->buflist[ i ];
			drm_i830_buf_priv_t *buf_priv = buf->dev_private;
			DRM(ioremapfree)(buf_priv->kernel_virtual, buf->total);
		}
	}
   	return 0;
}

static int i830_wait_ring(drm_device_t *dev, int n)
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
   	drm_i830_ring_buffer_t *ring = &(dev_priv->ring);
   	int iters = 0;
   	unsigned long end;
	unsigned int last_head = I830_READ(LP_RING + RING_HEAD) & HEAD_ADDR;

	end = jiffies + (HZ*3);
   	while (ring->space < n) {	
	   	ring->head = I830_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
	   	ring->space = ring->head - (ring->tail+8);
		if (ring->space < 0) ring->space += ring->Size;
	   
		if (ring->head != last_head) {
			end = jiffies + (HZ*3);
			last_head = ring->head;
		}
	  
	   	iters++;
		if(time_before(end, jiffies)) {
		   	DRM_ERROR("space: %d wanted %d\n", ring->space, n);
		   	DRM_ERROR("lockup\n");
		   	goto out_wait_ring;
		}
		udelay(1);
	}

out_wait_ring:   
   	return iters;
}

static void i830_kernel_lost_context(drm_device_t *dev)
{
      	drm_i830_private_t *dev_priv = dev->dev_private;
   	drm_i830_ring_buffer_t *ring = &(dev_priv->ring);
      
   	ring->head = I830_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
     	ring->tail = I830_READ(LP_RING + RING_TAIL) & TAIL_ADDR;
     	ring->space = ring->head - (ring->tail+8);
     	if (ring->space < 0) ring->space += ring->Size;
}

static int i830_freelist_init(drm_device_t *dev, drm_i830_private_t *dev_priv)
{
      	drm_device_dma_t *dma = dev->dma;
   	int my_idx = 36;
   	u32 *hw_status = (u32 *)(dev_priv->hw_status_page + my_idx);
   	int i;

   	if(dma->buf_count > 1019) {
	   	/* Not enough space in the status page for the freelist */
	   	return -EINVAL;
	}

   	for (i = 0; i < dma->buf_count; i++) {
	   	drm_buf_t *buf = dma->buflist[ i ];
	   	drm_i830_buf_priv_t *buf_priv = buf->dev_private;

	   	buf_priv->in_use = hw_status++;
	   	buf_priv->my_use_idx = my_idx;
	   	my_idx += 4;

	   	*buf_priv->in_use = I830_BUF_FREE;

		buf_priv->kernel_virtual = DRM(ioremap)(buf->bus_address, 
							buf->total);
	}
	return 0;
}

static int i830_dma_initialize(drm_device_t *dev, 
			       drm_i830_private_t *dev_priv,
			       drm_i830_init_t *init)
{
	struct list_head *list;

   	memset(dev_priv, 0, sizeof(drm_i830_private_t));

	list_for_each(list, &dev->maplist->head) {
		drm_map_list_t *r_list = (drm_map_list_t *)list;
		if( r_list->map &&
		    r_list->map->type == _DRM_SHM &&
		    r_list->map->flags & _DRM_CONTAINS_LOCK ) {
			dev_priv->sarea_map = r_list->map;
 			break;
 		}
 	}

	if(!dev_priv->sarea_map) {
		dev->dev_private = (void *)dev_priv;
		i830_dma_cleanup(dev);
		DRM_ERROR("can not find sarea!\n");
		return -EINVAL;
	}
	DRM_FIND_MAP( dev_priv->mmio_map, init->mmio_offset );
	if(!dev_priv->mmio_map) {
		dev->dev_private = (void *)dev_priv;
		i830_dma_cleanup(dev);
		DRM_ERROR("can not find mmio map!\n");
		return -EINVAL;
	}
	DRM_FIND_MAP( dev_priv->buffer_map, init->buffers_offset );
	if(!dev_priv->buffer_map) {
		dev->dev_private = (void *)dev_priv;
		i830_dma_cleanup(dev);
		DRM_ERROR("can not find dma buffer map!\n");
		return -EINVAL;
	}

	dev_priv->sarea_priv = (drm_i830_sarea_t *)
		((u8 *)dev_priv->sarea_map->handle +
		 init->sarea_priv_offset);

   	dev_priv->ring.Start = init->ring_start;
   	dev_priv->ring.End = init->ring_end;
   	dev_priv->ring.Size = init->ring_size;

   	dev_priv->ring.virtual_start = DRM(ioremap)(dev->agp->base + 
						    init->ring_start, 
						    init->ring_size);

   	if (dev_priv->ring.virtual_start == NULL) {
		dev->dev_private = (void *) dev_priv;
	   	i830_dma_cleanup(dev);
	   	DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
	   	return -ENOMEM;
	}

   	dev_priv->ring.tail_mask = dev_priv->ring.Size - 1;
   
	dev_priv->w = init->w;
	dev_priv->h = init->h;
	dev_priv->pitch = init->pitch;
	dev_priv->back_offset = init->back_offset;
	dev_priv->depth_offset = init->depth_offset;
	dev_priv->front_offset = init->front_offset;

	dev_priv->front_di1 = init->front_offset | init->pitch_bits;
	dev_priv->back_di1 = init->back_offset | init->pitch_bits;
	dev_priv->zi1 = init->depth_offset | init->pitch_bits;

	DRM_DEBUG("front_di1 %x\n",    dev_priv->front_di1);
	DRM_DEBUG("back_offset %x\n", dev_priv->back_offset);
	DRM_DEBUG("back_di1 %x\n",    dev_priv->back_di1);
	DRM_DEBUG("pitch_bits %x\n",    init->pitch_bits);

	dev_priv->cpp = init->cpp;
	/* We are using separate values as placeholders for mechanisms for
	 * private backbuffer/depthbuffer usage.
	 */

	dev_priv->back_pitch = init->back_pitch;
	dev_priv->depth_pitch = init->depth_pitch;

   	/* Program Hardware Status Page */
   	dev_priv->hw_status_page =
		(unsigned long) pci_alloc_consistent(dev->pdev, PAGE_SIZE,
						&dev_priv->dma_status_page);
   	if(dev_priv->hw_status_page == 0UL) {
		dev->dev_private = (void *)dev_priv;
		i830_dma_cleanup(dev);
		DRM_ERROR("Can not allocate hardware status page\n");
		return -ENOMEM;
	}
   	memset((void *) dev_priv->hw_status_page, 0, PAGE_SIZE);
	DRM_DEBUG("hw status page @ %lx\n", dev_priv->hw_status_page);
   
	I830_WRITE(0x02080, dev_priv->dma_status_page);
	DRM_DEBUG("Enabled hardware status page\n");
   
   	/* Now we need to init our freelist */
   	if(i830_freelist_init(dev, dev_priv) != 0) {
		dev->dev_private = (void *)dev_priv;
	   	i830_dma_cleanup(dev);
	   	DRM_ERROR("Not enough space in the status page for"
			  " the freelist\n");
	   	return -ENOMEM;
	}
	dev->dev_private = (void *)dev_priv;

   	return 0;
}

int i830_dma_init(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
   	drm_file_t *priv = filp->private_data;
   	drm_device_t *dev = priv->dev;
   	drm_i830_private_t *dev_priv;
   	drm_i830_init_t init;
   	int retcode = 0;
	
  	if (copy_from_user(&init, (drm_i830_init_t *)arg, sizeof(init)))
		return -EFAULT;
	
   	switch(init.func) {
	 	case I830_INIT_DMA:
			dev_priv = DRM(alloc)(sizeof(drm_i830_private_t), 
					      DRM_MEM_DRIVER);
	   		if(dev_priv == NULL) return -ENOMEM;
	   		retcode = i830_dma_initialize(dev, dev_priv, &init);
	   	break;
	 	case I830_CLEANUP_DMA:
	   		retcode = i830_dma_cleanup(dev);
	   	break;
	 	default:
	   		retcode = -EINVAL;
	   	break;
	}
   
   	return retcode;
}

#define GFX_OP_STIPPLE           ((0x3<<29)|(0x1d<<24)|(0x83<<16))
#define ST1_ENABLE               (1<<16)
#define ST1_MASK                 (0xffff)

/* Most efficient way to verify state for the i830 is as it is
 * emitted.  Non-conformant state is silently dropped.
 */
static void i830EmitContextVerified( drm_device_t *dev,
				     unsigned int *code )
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
	int i, j = 0;
	unsigned int tmp;
	RING_LOCALS;

	BEGIN_LP_RING( I830_CTX_SETUP_SIZE + 2 );

	OUT_RING( GFX_OP_STIPPLE );
	OUT_RING( 0 );


	for ( i = 0 ; i < I830_CTXREG_BLENDCOLR0 ; i++ ) {
		tmp = code[i];
		if ((tmp & (7<<29)) == CMD_3D &&
		    (tmp & (0x1f<<24)) < (0x1d<<24)) {
			OUT_RING( tmp ); 
			j++;
		} else {
			DRM_ERROR("Skipping %d\n", i);
		}
	}

	OUT_RING( STATE3D_CONST_BLEND_COLOR_CMD ); 
	OUT_RING( code[I830_CTXREG_BLENDCOLR] ); 
	j += 2;

	for ( i = I830_CTXREG_VF ; i < I830_CTXREG_MCSB0 ; i++ ) {
		tmp = code[i];
		if ((tmp & (7<<29)) == CMD_3D &&
		    (tmp & (0x1f<<24)) < (0x1d<<24)) {
			OUT_RING( tmp ); 
			j++;
		} else {
			DRM_ERROR("Skipping %d\n", i);
		}
	}

	OUT_RING( STATE3D_MAP_COORD_SETBIND_CMD ); 
	OUT_RING( code[I830_CTXREG_MCSB1] ); 
	j += 2;

	if (j & 1) 
		OUT_RING( 0 ); 

	ADVANCE_LP_RING();
}

static void i830EmitTexVerified( drm_device_t *dev, 
				 volatile unsigned int *code ) 
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
	int i, j = 0;
	unsigned int tmp;
	RING_LOCALS;

	BEGIN_LP_RING( I830_TEX_SETUP_SIZE );

	OUT_RING( GFX_OP_MAP_INFO );
	OUT_RING( code[I830_TEXREG_MI1] );
	OUT_RING( code[I830_TEXREG_MI2] );
	OUT_RING( code[I830_TEXREG_MI3] );
	OUT_RING( code[I830_TEXREG_MI4] );
	OUT_RING( code[I830_TEXREG_MI5] );

	for ( i = 6 ; i < I830_TEX_SETUP_SIZE ; i++ ) {
		tmp = code[i];
		OUT_RING( tmp ); 
		j++;
	} 

	if (j & 1) 
		OUT_RING( 0 ); 

	ADVANCE_LP_RING();
}

static void i830EmitTexBlendVerified( drm_device_t *dev, 
				     volatile unsigned int *code,
				     volatile unsigned int num)
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
	int i, j = 0;
	unsigned int tmp;
	RING_LOCALS;

	if (!num)
		return;

	BEGIN_LP_RING( num );

	for ( i = 0 ; i < num ; i++ ) {
		tmp = code[i];
		OUT_RING( tmp );
		j++;
	}

	if (j & 1) 
		OUT_RING( 0 ); 

	ADVANCE_LP_RING();
}

static void i830EmitTexPalette( drm_device_t *dev,
			        unsigned int *palette,
			        int number,
			        int is_shared )
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
	int i;
	RING_LOCALS;

	BEGIN_LP_RING( 258 );

	if(is_shared == 1) {
		OUT_RING(CMD_OP_MAP_PALETTE_LOAD |
			 MAP_PALETTE_NUM(0) |
			 MAP_PALETTE_BOTH);
	} else {
		OUT_RING(CMD_OP_MAP_PALETTE_LOAD | MAP_PALETTE_NUM(number));
	}
	for(i = 0; i < 256; i++) {
		OUT_RING(palette[i]);
	}
	OUT_RING(0);
}

/* Need to do some additional checking when setting the dest buffer.
 */
static void i830EmitDestVerified( drm_device_t *dev, 
				  volatile unsigned int *code ) 
{	
   	drm_i830_private_t *dev_priv = dev->dev_private;
	unsigned int tmp;
	RING_LOCALS;

	BEGIN_LP_RING( I830_DEST_SETUP_SIZE + 6 );

	tmp = code[I830_DESTREG_CBUFADDR];
	if (tmp == dev_priv->front_di1) {
		/* Don't use fence when front buffer rendering */
		OUT_RING( CMD_OP_DESTBUFFER_INFO );
		OUT_RING( BUF_3D_ID_COLOR_BACK | 
			  BUF_3D_PITCH(dev_priv->back_pitch * dev_priv->cpp) );
		OUT_RING( tmp );

		OUT_RING( CMD_OP_DESTBUFFER_INFO );
		OUT_RING( BUF_3D_ID_DEPTH |
			  BUF_3D_PITCH(dev_priv->depth_pitch * dev_priv->cpp));
		OUT_RING( dev_priv->zi1 );
	} else if(tmp == dev_priv->back_di1) {
		OUT_RING( CMD_OP_DESTBUFFER_INFO );
		OUT_RING( BUF_3D_ID_COLOR_BACK | 
			  BUF_3D_PITCH(dev_priv->back_pitch * dev_priv->cpp) |
			  BUF_3D_USE_FENCE);
		OUT_RING( tmp );

		OUT_RING( CMD_OP_DESTBUFFER_INFO );
		OUT_RING( BUF_3D_ID_DEPTH | BUF_3D_USE_FENCE | 
			  BUF_3D_PITCH(dev_priv->depth_pitch * dev_priv->cpp));
		OUT_RING( dev_priv->zi1 );
	} else {
		DRM_ERROR("bad di1 %x (allow %x or %x)\n",
			  tmp, dev_priv->front_di1, dev_priv->back_di1);
	}

	/* invarient:
	 */


	OUT_RING( GFX_OP_DESTBUFFER_VARS );
	OUT_RING( code[I830_DESTREG_DV1] );

	OUT_RING( GFX_OP_DRAWRECT_INFO );
	OUT_RING( code[I830_DESTREG_DR1] );
	OUT_RING( code[I830_DESTREG_DR2] );
	OUT_RING( code[I830_DESTREG_DR3] );
	OUT_RING( code[I830_DESTREG_DR4] );

	/* Need to verify this */
	tmp = code[I830_DESTREG_SENABLE];
	if((tmp & ~0x3) == GFX_OP_SCISSOR_ENABLE) {
		OUT_RING( tmp );
	} else {
		DRM_ERROR("bad scissor enable\n");
		OUT_RING( 0 );
	}

	OUT_RING( code[I830_DESTREG_SENABLE] );

	OUT_RING( GFX_OP_SCISSOR_RECT );
	OUT_RING( code[I830_DESTREG_SR1] );
	OUT_RING( code[I830_DESTREG_SR2] );

	ADVANCE_LP_RING();
}

static void i830EmitState( drm_device_t *dev )
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
      	drm_i830_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;

	if (dirty & I830_UPLOAD_BUFFERS) {
		i830EmitDestVerified( dev, sarea_priv->BufferState );
		sarea_priv->dirty &= ~I830_UPLOAD_BUFFERS;
	}

	if (dirty & I830_UPLOAD_CTX) {
		i830EmitContextVerified( dev, sarea_priv->ContextState );
		sarea_priv->dirty &= ~I830_UPLOAD_CTX;
	}

	if (dirty & I830_UPLOAD_TEX0) {
		i830EmitTexVerified( dev, sarea_priv->TexState[0] );
		sarea_priv->dirty &= ~I830_UPLOAD_TEX0;
	}

	if (dirty & I830_UPLOAD_TEX1) {
		i830EmitTexVerified( dev, sarea_priv->TexState[1] );
		sarea_priv->dirty &= ~I830_UPLOAD_TEX1;
	}

	if (dirty & I830_UPLOAD_TEXBLEND0) {
		i830EmitTexBlendVerified( dev, sarea_priv->TexBlendState[0],
				sarea_priv->TexBlendStateWordsUsed[0]);
		sarea_priv->dirty &= ~I830_UPLOAD_TEXBLEND0;
	}

	if (dirty & I830_UPLOAD_TEXBLEND1) {
		i830EmitTexBlendVerified( dev, sarea_priv->TexBlendState[1],
				sarea_priv->TexBlendStateWordsUsed[1]);
		sarea_priv->dirty &= ~I830_UPLOAD_TEXBLEND1;
	}

	if (dirty & I830_UPLOAD_TEX_PALETTE_SHARED) {
	   i830EmitTexPalette(dev, sarea_priv->Palette[0], 0, 1);
	} else {
	   if (dirty & I830_UPLOAD_TEX_PALETTE_N(0)) {
	      i830EmitTexPalette(dev, sarea_priv->Palette[0], 0, 0);
	      sarea_priv->dirty &= ~I830_UPLOAD_TEX_PALETTE_N(0);
	   }
	   if (dirty & I830_UPLOAD_TEX_PALETTE_N(1)) {
	      i830EmitTexPalette(dev, sarea_priv->Palette[1], 1, 0);
	      sarea_priv->dirty &= ~I830_UPLOAD_TEX_PALETTE_N(1);
	   }
	}
}

static void i830_dma_dispatch_clear( drm_device_t *dev, int flags, 
				    unsigned int clear_color,
				    unsigned int clear_zval,
				    unsigned int clear_depthmask)
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
      	drm_i830_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	int pitch = dev_priv->pitch;
	int cpp = dev_priv->cpp;
	int i;
	unsigned int BR13, CMD, D_CMD;
	RING_LOCALS;

  	i830_kernel_lost_context(dev);

	switch(cpp) {
	case 2: 
		BR13 = (0xF0 << 16) | (pitch * cpp) | (1<<24);
		D_CMD = CMD = XY_COLOR_BLT_CMD;
		break;
	case 4:
		BR13 = (0xF0 << 16) | (pitch * cpp) | (1<<24) | (1<<25);
		CMD = (XY_COLOR_BLT_CMD | XY_COLOR_BLT_WRITE_ALPHA | 
		       XY_COLOR_BLT_WRITE_RGB);
		D_CMD = XY_COLOR_BLT_CMD;
		if(clear_depthmask & 0x00ffffff)
			D_CMD |= XY_COLOR_BLT_WRITE_RGB;
		if(clear_depthmask & 0xff000000)
			D_CMD |= XY_COLOR_BLT_WRITE_ALPHA;
		break;
	default:
		BR13 = (0xF0 << 16) | (pitch * cpp) | (1<<24);
		D_CMD = CMD = XY_COLOR_BLT_CMD;
		break;
	}

      	if (nbox > I830_NR_SAREA_CLIPRECTS)
     		nbox = I830_NR_SAREA_CLIPRECTS;

	for (i = 0 ; i < nbox ; i++, pbox++) {
		if (pbox->x1 > pbox->x2 ||
		    pbox->y1 > pbox->y2 ||
		    pbox->x2 > dev_priv->w ||
		    pbox->y2 > dev_priv->h)
			continue;

	   	if ( flags & I830_FRONT ) {	    
		   	DRM_DEBUG("clear front\n");
			BEGIN_LP_RING( 6 );	    
			OUT_RING( CMD );
			OUT_RING( BR13 );
			OUT_RING( (pbox->y1 << 16) | pbox->x1 );
			OUT_RING( (pbox->y2 << 16) | pbox->x2 );
			OUT_RING( dev_priv->front_offset );
			OUT_RING( clear_color );
			ADVANCE_LP_RING();
		}

		if ( flags & I830_BACK ) {
			DRM_DEBUG("clear back\n");
			BEGIN_LP_RING( 6 );	    
			OUT_RING( CMD );
			OUT_RING( BR13 );
			OUT_RING( (pbox->y1 << 16) | pbox->x1 );
			OUT_RING( (pbox->y2 << 16) | pbox->x2 );
			OUT_RING( dev_priv->back_offset );
			OUT_RING( clear_color );
			ADVANCE_LP_RING();
		}

		if ( flags & I830_DEPTH ) {
			DRM_DEBUG("clear depth\n");
			BEGIN_LP_RING( 6 );
			OUT_RING( D_CMD );
			OUT_RING( BR13 );
			OUT_RING( (pbox->y1 << 16) | pbox->x1 );
			OUT_RING( (pbox->y2 << 16) | pbox->x2 );
			OUT_RING( dev_priv->depth_offset );
			OUT_RING( clear_zval );
			ADVANCE_LP_RING();
		}
	}
}

static void i830_dma_dispatch_swap( drm_device_t *dev )
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
      	drm_i830_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	int pitch = dev_priv->pitch;
	int cpp = dev_priv->cpp;
	int ofs = dev_priv->back_offset;
	int i;
	unsigned int CMD, BR13;
	RING_LOCALS;

	DRM_DEBUG("swapbuffers\n");

	switch(cpp) {
	case 2: 
		BR13 = (pitch * cpp) | (0xCC << 16) | (1<<24);
		CMD = XY_SRC_COPY_BLT_CMD;
		break;
	case 4:
		BR13 = (pitch * cpp) | (0xCC << 16) | (1<<24) | (1<<25);
		CMD = (XY_SRC_COPY_BLT_CMD | XY_SRC_COPY_BLT_WRITE_ALPHA |
		       XY_SRC_COPY_BLT_WRITE_RGB);
		break;
	default:
		BR13 = (pitch * cpp) | (0xCC << 16) | (1<<24);
		CMD = XY_SRC_COPY_BLT_CMD;
		break;
	}

  	i830_kernel_lost_context(dev);

      	if (nbox > I830_NR_SAREA_CLIPRECTS)
     		nbox = I830_NR_SAREA_CLIPRECTS;

	for (i = 0 ; i < nbox; i++, pbox++) 
	{
		if (pbox->x1 > pbox->x2 ||
		    pbox->y1 > pbox->y2 ||
		    pbox->x2 > dev_priv->w ||
		    pbox->y2 > dev_priv->h)
			continue;
 
		DRM_DEBUG("dispatch swap %d,%d-%d,%d!\n",
			  pbox->x1, pbox->y1,
			  pbox->x2, pbox->y2);

		BEGIN_LP_RING( 8 );
		OUT_RING( CMD );
		OUT_RING( BR13 );

		OUT_RING( (pbox->y1 << 16) |
			  pbox->x1 );
		OUT_RING( (pbox->y2 << 16) |
			  pbox->x2 );

		OUT_RING( dev_priv->front_offset );
		OUT_RING( (pbox->y1 << 16) |
			  pbox->x1 );

		OUT_RING( BR13 & 0xffff );
		OUT_RING( ofs );

		ADVANCE_LP_RING();
	}
}


static void i830_dma_dispatch_vertex(drm_device_t *dev, 
				     drm_buf_t *buf,
				     int discard,
				     int used)
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
   	drm_i830_sarea_t *sarea_priv = dev_priv->sarea_priv;
   	drm_clip_rect_t *box = sarea_priv->boxes;
   	int nbox = sarea_priv->nbox;
	unsigned long address = (unsigned long)buf->bus_address;
	unsigned long start = address - dev->agp->base;     
	int i = 0, u;
   	RING_LOCALS;

   	i830_kernel_lost_context(dev);

   	if (nbox > I830_NR_SAREA_CLIPRECTS) 
		nbox = I830_NR_SAREA_CLIPRECTS;

	if (discard) {
		u = cmpxchg(buf_priv->in_use, I830_BUF_CLIENT, 
			    I830_BUF_HARDWARE);
		if(u != I830_BUF_CLIENT) {
			DRM_DEBUG("xxxx 2\n");
		}
	}

	if (used > 4*1023) 
		used = 0;

	if (sarea_priv->dirty)
	   i830EmitState( dev );

  	DRM_DEBUG("dispatch vertex addr 0x%lx, used 0x%x nbox %d\n", 
		  address, used, nbox);

   	dev_priv->counter++;
   	DRM_DEBUG(  "dispatch counter : %ld\n", dev_priv->counter);
   	DRM_DEBUG(  "i830_dma_dispatch\n");
   	DRM_DEBUG(  "start : %lx\n", start);
	DRM_DEBUG(  "used : %d\n", used);
   	DRM_DEBUG(  "start + used - 4 : %ld\n", start + used - 4);

	if (buf_priv->currently_mapped == I830_BUF_MAPPED) {
		u32 *vp = buf_priv->virtual;

		vp[0] = (GFX_OP_PRIMITIVE |
			 sarea_priv->vertex_prim |
			 ((used/4)-2));

		vp[used/4] = MI_BATCH_BUFFER_END;
		used += 4;
		
		if (used & 4) {
			vp[used/4] = 0;
			used += 4;
		}

		i830_unmap_buffer(buf);
	}
		   
	if (used) {
		do {
			if (i < nbox) {
				BEGIN_LP_RING(6);
				OUT_RING( GFX_OP_DRAWRECT_INFO );
				OUT_RING( sarea_priv->BufferState[I830_DESTREG_DR1] );
				OUT_RING( box[i].x1 | (box[i].y1<<16) );
				OUT_RING( box[i].x2 | (box[i].y2<<16) );
				OUT_RING( sarea_priv->BufferState[I830_DESTREG_DR4] );
				OUT_RING( 0 );
				ADVANCE_LP_RING();
			}

			BEGIN_LP_RING(2);
			OUT_RING( MI_BATCH_BUFFER_START | (2<<6) );
			OUT_RING( start | MI_BATCH_NON_SECURE );
			ADVANCE_LP_RING();
			
		} while (++i < nbox);
	}

	if (discard) {
		dev_priv->counter++;

		(void) cmpxchg(buf_priv->in_use, I830_BUF_CLIENT,
			       I830_BUF_HARDWARE);

		BEGIN_LP_RING(8);
		OUT_RING( CMD_STORE_DWORD_IDX );
		OUT_RING( 20 );
		OUT_RING( dev_priv->counter );
		OUT_RING( CMD_STORE_DWORD_IDX );
		OUT_RING( buf_priv->my_use_idx );
		OUT_RING( I830_BUF_FREE );
		OUT_RING( CMD_REPORT_HEAD );
		OUT_RING( 0 );
		ADVANCE_LP_RING();
	}
}


void i830_dma_quiescent(drm_device_t *dev)
{
      	drm_i830_private_t *dev_priv = dev->dev_private;
   	RING_LOCALS;

  	i830_kernel_lost_context(dev);

   	BEGIN_LP_RING(4);
   	OUT_RING( INST_PARSER_CLIENT | INST_OP_FLUSH | INST_FLUSH_MAP_CACHE );
   	OUT_RING( CMD_REPORT_HEAD );
      	OUT_RING( 0 );
      	OUT_RING( 0 );
   	ADVANCE_LP_RING();

	i830_wait_ring( dev, dev_priv->ring.Size - 8 );
}

static int i830_flush_queue(drm_device_t *dev)
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
   	int i, ret = 0;
   	RING_LOCALS;
	
   	i830_kernel_lost_context(dev);

   	BEGIN_LP_RING(2);
      	OUT_RING( CMD_REPORT_HEAD );
      	OUT_RING( 0 );
      	ADVANCE_LP_RING();

	i830_wait_ring( dev, dev_priv->ring.Size - 8 );

   	for (i = 0; i < dma->buf_count; i++) {
	   	drm_buf_t *buf = dma->buflist[ i ];
	   	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
	   
		int used = cmpxchg(buf_priv->in_use, I830_BUF_HARDWARE, 
				   I830_BUF_FREE);

		if (used == I830_BUF_HARDWARE)
			DRM_DEBUG("reclaimed from HARDWARE\n");
		if (used == I830_BUF_CLIENT)
			DRM_DEBUG("still on client\n");
	}

   	return ret;
}

/* Must be called with the lock held */
void i830_reclaim_buffers(drm_device_t *dev, pid_t pid)
{
	drm_device_dma_t *dma = dev->dma;
	int		 i;

	if (!dma) return;
      	if (!dev->dev_private) return;
	if (!dma->buflist) return;

        i830_flush_queue(dev);

	for (i = 0; i < dma->buf_count; i++) {
	   	drm_buf_t *buf = dma->buflist[ i ];
	   	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
	   
		if (buf->pid == pid && buf_priv) {
			int used = cmpxchg(buf_priv->in_use, I830_BUF_CLIENT, 
					   I830_BUF_FREE);

			if (used == I830_BUF_CLIENT)
				DRM_DEBUG("reclaimed from client\n");
		   	if(buf_priv->currently_mapped == I830_BUF_MAPPED)
		     		buf_priv->currently_mapped = I830_BUF_UNMAPPED;
		}
	}
}

int i830_flush_ioctl(struct inode *inode, struct file *filp, 
		     unsigned int cmd, unsigned long arg)
{
   	drm_file_t	  *priv	  = filp->private_data;
   	drm_device_t	  *dev	  = priv->dev;

   	if(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("i830_flush_ioctl called without lock held\n");
		return -EINVAL;
	}

   	i830_flush_queue(dev);
   	return 0;
}

int i830_dma_vertex(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
   	drm_i830_private_t *dev_priv = (drm_i830_private_t *)dev->dev_private;
      	u32 *hw_status = (u32 *)dev_priv->hw_status_page;
   	drm_i830_sarea_t *sarea_priv = (drm_i830_sarea_t *) 
     					dev_priv->sarea_priv; 
	drm_i830_vertex_t vertex;

	if (copy_from_user(&vertex, (drm_i830_vertex_t *)arg, sizeof(vertex)))
		return -EFAULT;

   	if(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("i830_dma_vertex called without lock held\n");
		return -EINVAL;
	}

	DRM_DEBUG("i830 dma vertex, idx %d used %d discard %d\n",
		  vertex.idx, vertex.used, vertex.discard);

	if(vertex.idx < 0 || vertex.idx > dma->buf_count) return -EINVAL;

	i830_dma_dispatch_vertex( dev, 
				  dma->buflist[ vertex.idx ], 
				  vertex.discard, vertex.used );

	sarea_priv->last_enqueue = dev_priv->counter-1;
   	sarea_priv->last_dispatch = (int) hw_status[5];
   
	return 0;
}

int i830_clear_bufs(struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_i830_clear_t clear;

   	if (copy_from_user(&clear, (drm_i830_clear_t *)arg, sizeof(clear)))
		return -EFAULT;
   
   	if(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("i830_clear_bufs called without lock held\n");
		return -EINVAL;
	}

	/* GH: Someone's doing nasty things... */
	if (!dev->dev_private) {
		return -EINVAL;
	}

	i830_dma_dispatch_clear( dev, clear.flags, 
				 clear.clear_color, 
				 clear.clear_depth,
			         clear.clear_depthmask);
   	return 0;
}

int i830_swap_bufs(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
   
	DRM_DEBUG("i830_swap_bufs\n");

   	if(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("i830_swap_buf called without lock held\n");
		return -EINVAL;
	}

	i830_dma_dispatch_swap( dev );
   	return 0;
}

int i830_getage(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
   	drm_file_t	  *priv	    = filp->private_data;
	drm_device_t	  *dev	    = priv->dev;
   	drm_i830_private_t *dev_priv = (drm_i830_private_t *)dev->dev_private;
      	u32 *hw_status = (u32 *)dev_priv->hw_status_page;
   	drm_i830_sarea_t *sarea_priv = (drm_i830_sarea_t *) 
     					dev_priv->sarea_priv; 

      	sarea_priv->last_dispatch = (int) hw_status[5];
	return 0;
}

int i830_getbuf(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	drm_file_t	  *priv	    = filp->private_data;
	drm_device_t	  *dev	    = priv->dev;
	int		  retcode   = 0;
	drm_i830_dma_t	  d;
   	drm_i830_private_t *dev_priv = (drm_i830_private_t *)dev->dev_private;
   	u32 *hw_status = (u32 *)dev_priv->hw_status_page;
   	drm_i830_sarea_t *sarea_priv = (drm_i830_sarea_t *) 
     					dev_priv->sarea_priv; 

	DRM_DEBUG("getbuf\n");
   	if (copy_from_user(&d, (drm_i830_dma_t *)arg, sizeof(d)))
		return -EFAULT;
   
	if(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("i830_dma called without lock held\n");
		return -EINVAL;
	}
	
	d.granted = 0;

	retcode = i830_dma_get_buffer(dev, &d, filp);

	DRM_DEBUG("i830_dma: %d returning %d, granted = %d\n",
		  current->pid, retcode, d.granted);

	if (copy_to_user((drm_dma_t *)arg, &d, sizeof(d)))
		return -EFAULT;
   	sarea_priv->last_dispatch = (int) hw_status[5];

	return retcode;
}

int i830_copybuf(struct inode *inode,
		 struct file *filp, 
		 unsigned int cmd,
		 unsigned long arg)
{
	/* Never copy - 2.4.x doesn't need it */
	return 0;
}

int i830_docopy(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	return 0;
}
