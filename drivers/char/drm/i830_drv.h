/* i830_drv.h -- Private header for the I830 driver -*- linux-c -*-
 * Created: Mon Dec 13 01:50:01 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
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
 * 	    Jeff Hartmann <jhartmann@valinux.com>
 *
 */

#ifndef _I830_DRV_H_
#define _I830_DRV_H_

typedef struct drm_i830_buf_priv {
   	u32 *in_use;
   	int my_use_idx;
	int currently_mapped;
	void *virtual;
	void *kernel_virtual;
	int map_count;
   	struct vm_area_struct *vma;
} drm_i830_buf_priv_t;

typedef struct _drm_i830_ring_buffer{
	int tail_mask;
	unsigned long Start;
	unsigned long End;
	unsigned long Size;
	u8 *virtual_start;
	int head;
	int tail;
	int space;
} drm_i830_ring_buffer_t;

typedef struct drm_i830_private {
	drm_map_t *sarea_map;
	drm_map_t *buffer_map;
	drm_map_t *mmio_map;

	drm_i830_sarea_t *sarea_priv;
   	drm_i830_ring_buffer_t ring;

      	unsigned long hw_status_page;
   	unsigned long counter;

   	atomic_t flush_done;
   	wait_queue_head_t flush_queue;	/* Processes waiting until flush    */
	drm_buf_t *mmap_buffer;
	
	u32 front_di1, back_di1, zi1;
	
	int back_offset;
	int depth_offset;
	int w, h;
	int pitch;
	int back_pitch;
	int depth_pitch;
	unsigned int cpp;
} drm_i830_private_t;

				/* i830_dma.c */
extern int  i830_dma_schedule(drm_device_t *dev, int locked);
extern int  i830_getbuf(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int  i830_dma_init(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int  i830_flush_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern void i830_reclaim_buffers(drm_device_t *dev, pid_t pid);
extern int  i830_getage(struct inode *inode, struct file *filp, unsigned int cmd,
			unsigned long arg);
extern int i830_mmap_buffers(struct file *filp, struct vm_area_struct *vma);
extern int i830_copybuf(struct inode *inode, struct file *filp, 
			unsigned int cmd, unsigned long arg);
extern int i830_docopy(struct inode *inode, struct file *filp, 
		       unsigned int cmd, unsigned long arg);

extern void i830_dma_quiescent(drm_device_t *dev);

extern int i830_dma_vertex(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);

extern int i830_swap_bufs(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);

extern int i830_clear_bufs(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);

#define I830_VERBOSE 0

#define I830_BASE(reg)		((unsigned long) \
				dev_priv->mmio_map->handle)
#define I830_ADDR(reg)		(I830_BASE(reg) + reg)
#define I830_DEREF(reg)		*(__volatile__ int *)I830_ADDR(reg)
#define I830_READ(reg)		I830_DEREF(reg)
#define I830_WRITE(reg,val) 	do { I830_DEREF(reg) = val; } while (0)
#define I830_DEREF16(reg)	*(__volatile__ u16 *)I830_ADDR(reg)
#define I830_READ16(reg) 	I830_DEREF16(reg)
#define I830_WRITE16(reg,val)	do { I830_DEREF16(reg) = val; } while (0)

#define GFX_OP_USER_INTERRUPT 		((0<<29)|(2<<23))
#define GFX_OP_BREAKPOINT_INTERRUPT	((0<<29)|(1<<23))
#define CMD_REPORT_HEAD			(7<<23)
#define CMD_STORE_DWORD_IDX		((0x21<<23) | 0x1)
#define CMD_OP_BATCH_BUFFER  ((0x0<<29)|(0x30<<23)|0x1)

#define INST_PARSER_CLIENT   0x00000000
#define INST_OP_FLUSH        0x02000000
#define INST_FLUSH_MAP_CACHE 0x00000001


#define BB1_START_ADDR_MASK   (~0x7)
#define BB1_PROTECTED         (1<<0)
#define BB1_UNPROTECTED       (0<<0)
#define BB2_END_ADDR_MASK     (~0x7)

#define I830REG_HWSTAM		0x02098
#define I830REG_INT_IDENTITY_R	0x020a4
#define I830REG_INT_MASK_R 	0x020a8
#define I830REG_INT_ENABLE_R	0x020a0

#define LP_RING     		0x2030
#define HP_RING     		0x2040
#define RING_TAIL      		0x00
#define TAIL_ADDR		0x000FFFF8
#define RING_HEAD      		0x04
#define HEAD_WRAP_COUNT     	0xFFE00000
#define HEAD_WRAP_ONE       	0x00200000
#define HEAD_ADDR           	0x001FFFFC
#define RING_START     		0x08
#define START_ADDR          	0x00FFFFF8
#define RING_LEN       		0x0C
#define RING_NR_PAGES       	0x000FF000 
#define RING_REPORT_MASK    	0x00000006
#define RING_REPORT_64K     	0x00000002
#define RING_REPORT_128K    	0x00000004
#define RING_NO_REPORT      	0x00000000
#define RING_VALID_MASK     	0x00000001
#define RING_VALID          	0x00000001
#define RING_INVALID        	0x00000000

#define GFX_OP_SCISSOR         ((0x3<<29)|(0x1c<<24)|(0x10<<19))
#define SC_UPDATE_SCISSOR       (0x1<<1)
#define SC_ENABLE_MASK          (0x1<<0)
#define SC_ENABLE               (0x1<<0)

#define GFX_OP_SCISSOR_INFO    ((0x3<<29)|(0x1d<<24)|(0x81<<16)|(0x1))
#define SCI_YMIN_MASK      (0xffff<<16)
#define SCI_XMIN_MASK      (0xffff<<0)
#define SCI_YMAX_MASK      (0xffff<<16)
#define SCI_XMAX_MASK      (0xffff<<0)

#define GFX_OP_SCISSOR_ENABLE	 ((0x3<<29)|(0x1c<<24)|(0x10<<19))
#define GFX_OP_SCISSOR_RECT	 ((0x3<<29)|(0x1d<<24)|(0x81<<16)|1)
#define GFX_OP_COLOR_FACTOR      ((0x3<<29)|(0x1d<<24)|(0x1<<16)|0x0)
#define GFX_OP_STIPPLE           ((0x3<<29)|(0x1d<<24)|(0x83<<16))
#define GFX_OP_MAP_INFO          ((0x3<<29)|(0x1d<<24)|0x4)
#define GFX_OP_DESTBUFFER_VARS   ((0x3<<29)|(0x1d<<24)|(0x85<<16)|0x0)
#define GFX_OP_DRAWRECT_INFO     ((0x3<<29)|(0x1d<<24)|(0x80<<16)|(0x3))
#define GFX_OP_PRIMITIVE         ((0x3<<29)|(0x1f<<24))

#define CMD_OP_DESTBUFFER_INFO	 ((0x3<<29)|(0x1d<<24)|(0x8e<<16)|1)


#define BR00_BITBLT_CLIENT   0x40000000
#define BR00_OP_COLOR_BLT    0x10000000
#define BR00_OP_SRC_COPY_BLT 0x10C00000
#define BR13_SOLID_PATTERN   0x80000000

#define BUF_3D_ID_COLOR_BACK    (0x3<<24)
#define BUF_3D_ID_DEPTH         (0x7<<24)
#define BUF_3D_USE_FENCE        (1<<23)
#define BUF_3D_PITCH(x)         (((x)/4)<<2)

#define CMD_OP_MAP_PALETTE_LOAD	((3<<29)|(0x1d<<24)|(0x82<<16)|255)
#define MAP_PALETTE_NUM(x)	((x<<8) & (1<<8))
#define MAP_PALETTE_BOTH	(1<<11)

#define XY_COLOR_BLT_CMD		((2<<29)|(0x50<<22)|0x4)
#define XY_COLOR_BLT_WRITE_ALPHA	(1<<21)
#define XY_COLOR_BLT_WRITE_RGB		(1<<20)

#define XY_SRC_COPY_BLT_CMD             ((2<<29)|(0x53<<22)|6)
#define XY_SRC_COPY_BLT_WRITE_ALPHA     (1<<21)
#define XY_SRC_COPY_BLT_WRITE_RGB       (1<<20)

#define MI_BATCH_BUFFER 	((0x30<<23)|1)
#define MI_BATCH_NON_SECURE	(1)


#endif

