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
#ifndef _VIA_CHROME9_DRM_H_
#define _VIA_CHROME9_DRM_H_

/* WARNING: These defines must be the same as what the Xserver uses.
 * if you change them, you must change the defines in the Xserver.
 */

#ifndef _VIA_CHROME9_DEFINES_
#define _VIA_CHROME9_DEFINES_

#ifndef __KERNEL__
#include "via_drmclient.h"
#endif

#define VIA_CHROME9_NR_SAREA_CLIPRECTS 		8
#define VIA_CHROME9_NR_XVMC_PORTS               10
#define VIA_CHROME9_NR_XVMC_LOCKS               5
#define VIA_CHROME9_MAX_CACHELINE_SIZE          64
#define XVMCLOCKPTR(saPriv,lockNo)					\
	((__volatile__ struct drm_hw_lock *)				\
	(((((unsigned long) (saPriv)->XvMCLockArea) + 			\
				      (VIA_CHROME9_MAX_CACHELINE_SIZE - 1)) & \
				     ~(VIA_CHROME9_MAX_CACHELINE_SIZE - 1)) + \
				    VIA_CHROME9_MAX_CACHELINE_SIZE*(lockNo)))

/* Each region is a minimum of 64k, and there are at most 64 of them.
 */
#define VIA_CHROME9_NR_TEX_REGIONS 64
#define VIA_CHROME9_LOG_MIN_TEX_REGION_SIZE 16
#endif

#define VIA_CHROME9_UPLOAD_TEX0IMAGE  0x1	/* handled clientside */
#define VIA_CHROME9_UPLOAD_TEX1IMAGE  0x2	/* handled clientside */
#define VIA_CHROME9_UPLOAD_CTX        0x4
#define VIA_CHROME9_UPLOAD_BUFFERS    0x8
#define VIA_CHROME9_UPLOAD_TEX0       0x10
#define VIA_CHROME9_UPLOAD_TEX1       0x20
#define VIA_CHROME9_UPLOAD_CLIPRECTS  0x40
#define VIA_CHROME9_UPLOAD_ALL        0xff

/* VIA_CHROME9 specific ioctls */
#define DRM_VIA_CHROME9_ALLOCMEM                    0x00
#define DRM_VIA_CHROME9_FREEMEM                     0x01
#define DRM_VIA_CHROME9_FREE                        0x02
#define DRM_VIA_CHROME9_ALLOCATE_EVENT_TAG          0x03
#define DRM_VIA_CHROME9_FREE_EVENT_TAG              0x04
#define DRM_VIA_CHROME9_ALLOCATE_APERTURE           0x05
#define DRM_VIA_CHROME9_FREE_APERTURE               0x06
#define DRM_VIA_CHROME9_ALLOCATE_VIDEO_MEM          0x07
#define DRM_VIA_CHROME9_FREE_VIDEO_MEM              0x08
#define DRM_VIA_CHROME9_WAIT_CHIP_IDLE              0x09
#define DRM_VIA_CHROME9_PROCESS_EXIT                0x0A
#define DRM_VIA_CHROME9_RESTORE_PRIMARY             0x0B
#define DRM_VIA_CHROME9_FLUSH_CACHE                 0x0C
#define DRM_VIA_CHROME9_INIT                        0x0D
#define DRM_VIA_CHROME9_FLUSH                       0x0E
#define DRM_VIA_CHROME9_CHECKVIDMEMSIZE             0x0F
#define DRM_VIA_CHROME9_PCIEMEMCTRL                 0x10
#define DRM_VIA_CHROME9_AUTH_MAGIC	            0x11
#define DRM_VIA_CHROME9_GET_PCI_ID	            0x12
#define DRM_VIA_CHROME9_INIT_JUDGE		    0x16
#define DRM_VIA_CHROME9_DMA		    0x17

#define DRM_IOCTL_VIA_CHROME9_INIT                  	 \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_INIT, \
	struct drm_via_chrome9_init)
#define DRM_IOCTL_VIA_CHROME9_FLUSH                 \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_FLUSH, \
	struct drm_via_chrome9_flush)
#define DRM_IOCTL_VIA_CHROME9_FREE                  \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_FREE, int)
#define DRM_IOCTL_VIA_CHROME9_ALLOCATE_EVENT_TAG    \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_ALLOCATE_EVENT_TAG, \
	struct drm_event_via_chrome9_tag)
#define DRM_IOCTL_VIA_CHROME9_FREE_EVENT_TAG        \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_FREE_EVENT_TAG, \
	struct drm_event_via_chrome9_tag)
#define DRM_IOCTL_VIA_CHROME9_ALLOCATE_APERTURE     \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_ALLOCATE_APERTURE, \
	struct drm_via_chrome9_aperture)
#define DRM_IOCTL_VIA_CHROME9_FREE_APERTURE         \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_FREE_APERTURE, \
	struct drm_via_chrome9_aperture)
#define DRM_IOCTL_VIA_CHROME9_ALLOCATE_VIDEO_MEM    \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_ALLOCATE_VIDEO_MEM, \
	struct drm_via_chrome9_memory_alloc)
#define DRM_IOCTL_VIA_CHROME9_FREE_VIDEO_MEM        \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_FREE_VIDEO_MEM, \
	struct drm_via_chrome9_memory_alloc)
#define DRM_IOCTL_VIA_CHROME9_WAIT_CHIP_IDLE        \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_WAIT_CHIP_IDLE, int)
#define DRM_IOCTL_VIA_CHROME9_PROCESS_EXIT          \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_PROCESS_EXIT, int)
#define DRM_IOCTL_VIA_CHROME9_RESTORE_PRIMARY       \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_RESTORE_PRIMARY, int)
#define DRM_IOCTL_VIA_CHROME9_FLUSH_CACHE           \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_FLUSH_CACHE, int)
#define DRM_IOCTL_VIA_CHROME9_ALLOCMEM       	    \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_ALLOCMEM, int)
#define DRM_IOCTL_VIA_CHROME9_FREEMEM               \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_FREEMEM, int)
#define DRM_IOCTL_VIA_CHROME9_CHECK_VIDMEM_SIZE     \
	DRM_IOW(DRM_COMMAND_BASE +  DRM_VIA_CHROME9_CHECKVIDMEMSIZE, \
	struct drm_via_chrome9_memory_alloc)
#define DRM_IOCTL_VIA_CHROME9_PCIEMEMCTRL           \
	DRM_IOW(DRM_COMMAND_BASE +  DRM_VIA_CHROME9_PCIEMEMCTRL,\
	drm_via_chrome9_pciemem_ctrl_t)
#define DRM_IOCTL_VIA_CHROME9_AUTH_MAGIC            \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_AUTH_MAGIC, drm_auth_t)
#define DRM_IOCTL_VIA_CHROME9_GET_PCI_ID            \
	DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_CHROME9_GET_PCI_ID, \
	struct get_pci_id_struct)
#define DRM_IOCTL_VIA_CHROME9_INIT_JUDGE	    \
	DRM_IOR(DRM_COMMAND_BASE + DRM_VIA_CHROME9_INIT_JUDGE, int)
#define DRM_IOCTL_VIA_CHROME9_DMA	    \
	DRM_IO(DRM_COMMAND_BASE + DRM_VIA_CHROME9_DMA, int)

enum S3GCHIPIDS {
	CHIP_UNKNOWN = -1,
	CHIP_CMODEL,    /*Model for any chip. */
	CHIP_CLB,       /*Columbia */
	CHIP_DST,       /*Destination */
	CHIP_CSR,       /*Castlerock */
	CHIP_INV,       /*Innovation (H3) */
	CHIP_H5,        /*Innovation (H5) */
	CHIP_H5S1,      /*Innovation (H5S1) */
	CHIP_H6S2,      /*Innovation (H6S2) */
	CHIP_CMS,       /*Columbia MS */
	CHIP_METRO,     /*Metropolis */
	CHIP_MANHATTAN, /*manhattan */
	CHIP_MATRIX,    /*matrix */
	CHIP_EVO,	    /*change for GCC 4.1 -add- 07.02.12*/
	CHIP_H6S1,      /*Innovation (H6S1)*/
	CHIP_DST2,      /*Destination-2 */
	CHIP_LAST       /*Maximum number of chips supported. */
};

enum VIA_CHROME9CHIPBUS {
	CHIP_PCI,
	CHIP_AGP,
	CHIP_PCIE
};

struct drm_via_chrome9_init {
	enum {
		VIA_CHROME9_INIT    = 0x01,
		VIA_CHROME9_CLEANUP = 0x02
	} func;
	int chip_agp;
	int chip_index;
	int chip_sub_index;
	int usec_timeout;
	unsigned int   sarea_priv_offset;
	unsigned int   fb_cpp;
	unsigned int   front_offset;
	unsigned int   back_offset;
	unsigned int   depth_offset;
	unsigned int   mmio_handle;
	unsigned int   dma_handle;
	unsigned int   fb_handle;
	unsigned int   front_handle;
	unsigned int   back_handle;
	unsigned int   depth_handle;

	unsigned int   fb_tex_offset;
	unsigned int   fb_tex_size;

	unsigned int   agp_tex_size;
	unsigned int   agp_tex_handle;
	unsigned int   shadow_size;
	unsigned int   shadow_handle;
	unsigned int   garttable_size;
	unsigned int   garttable_offset;
	unsigned long  available_fb_size;
	unsigned long  fb_base_address;
	unsigned int   DMA_size;
	unsigned long  DMA_phys_address;
	enum {
		AGP_RING_BUFFER,
		AGP_DOUBLE_BUFFER,
		AGP_DISABLED
	} agp_type;
	unsigned int hostBlt_handle;
};

enum dma_cmd_type {
	flush_bci = 0,
	flush_bci_and_wait,
	dma_kickoff,
	flush_dma_buffer,
	flush_dma_and_wait
};

struct drm_via_chrome9_flush {
	enum dma_cmd_type    dma_cmd_type;
	/* command buffer index */
	int    cmd_idx;
	/* command buffer offset */
	int    cmd_offset;
	/* command dword size,command always from beginning */
	int    cmd_size;
	/* if use dma kick off,it is dma kick off command */
	unsigned long  dma_kickoff[2];
	/* user mode DMA buffer pointer */
	unsigned int *usermode_dma_buf;
};

struct event_value {
	int event_low;
	int event_high;
};

struct drm_via_chrome9_event_tag {
	unsigned int  event_size;         /* event tag size */
	int	 event_offset;                /* event tag id */
	struct event_value last_sent_event_value;
	struct event_value current_event_value;
	int         query_mask0;
	int         query_mask1;
	int         query_Id1;
};

/* Indices into buf.Setup where various bits of state are mirrored per
 * context and per buffer.  These can be fired at the card as a unit,
 * or in a piecewise fashion as required.
 */

#define VIA_CHROME9_TEX_SETUP_SIZE 8

/* Flags for clear ioctl
 */
#define VIA_CHROME9_FRONT   0x1
#define VIA_CHROME9_BACK    0x2
#define VIA_CHROME9_DEPTH   0x4
#define VIA_CHROME9_STENCIL 0x8
#define VIA_CHROME9_MEM_VIDEO   0	/* matches drm constant */
#define VIA_CHROME9_MEM_AGP     1	/* matches drm constant */
#define VIA_CHROME9_MEM_SYSTEM  2
#define VIA_CHROME9_MEM_MIXED   3
#define VIA_CHROME9_MEM_UNKNOWN 4

struct drm_via_chrome9_agp {
	uint32_t offset;
	uint32_t size;
};

struct drm_via_chrome9_fb {
	uint32_t offset;
	uint32_t size;
};

struct drm_via_chrome9_mem {
	uint32_t context;
	uint32_t type;
	uint32_t size;
	unsigned long index;
	unsigned long offset;
};

struct drm_via_chrome9_aperture {
	/*IN: The frame buffer offset of the surface. */
	int surface_offset;
	/*IN: Surface pitch in byte, */
	int pitch;
	/*IN: Surface width in pixel */
	int width;
	/*IN: Surface height in pixel */
	int height;
	/*IN: Surface color format, Columbia has more color formats */
	int color_format;
	/*IN: Rotation degrees, only for Columbia */
	int rotation_degree;
	/*IN Is the PCIE Video, for MATRIX support NONLOCAL Aperture */
	int isPCIEVIDEO;
	/*IN: Is the surface tilled, only for Columbia */
	int is_tiled;
	/*IN:  Only allocate apertur, not hardware setup. */
	int allocate_only;
	/* OUT: linear address for aperture */
	unsigned int *aperture_linear_address;
	/*OUT:  The pitch of the aperture,for CPU write not for GE */
	int aperture_pitch;
	/*OUT: The index of the aperture */
	int aperture_handle;
	int apertureID;
	/* always =0xAAAAAAAA */
	/* Aligned surface's width(in pixel) */
	int width_aligned;
	/* Aligned surface's height(in pixel) */
	int height_aligned;
};

/*
    Some fileds of this data structure has no meaning now since
    we have managed heap based on mechanism provided by DRM
    Remain what it was to keep consistent with 3D driver interface.
*/
struct drm_via_chrome9_memory_alloc {
	enum {
		memory_heap_video = 0,
		memory_heap_agp,
		memory_heap_pcie_video,
		memory_heap_pcie,
		max_memory_heaps
	} heap_type;
	struct {
		void *lpL1Node;
		unsigned int       alcL1Tag;
		unsigned int       usageCount;
		unsigned int       dwVersion;
		unsigned int       dwResHandle;
		unsigned int       dwProcessID;
	} heap_info;
	unsigned int flags;
	unsigned int size;
	unsigned int physaddress;
	unsigned int offset;
	unsigned int align;
	void *linearaddress;
};

struct drm_via_chrome9_dma_init {
	enum {
		VIA_CHROME9_INIT_DMA = 0x01,
		VIA_CHROME9_CLEANUP_DMA = 0x02,
		VIA_CHROME9_DMA_INITIALIZED = 0x03
	} func;

	unsigned long offset;
	unsigned long size;
	unsigned long reg_pause_addr;
};

struct drm_via_chrome9_cmdbuffer {
	char __user *buf;
	unsigned long size;
};

/* Warning: If you change the SAREA structure you must change the Xserver
 * structure as well */

struct drm_via_chrome9_tex_region {
	unsigned char next, prev;	/* indices to form a circular LRU  */
	unsigned char inUse;	/* owned by a client, or free? */
	int age;		/* tracked by clients to update local LRU's */
};

struct drm_via_chrome9_sarea {
	int page_flip;
	int current_page;
	unsigned int req_drawable;/* the X drawable id */
	unsigned int req_draw_buffer;/* VIA_CHROME9_FRONT or VIA_CHROME9_BACK */
	/* Last context that uploaded state */
	int ctx_owner;
};

struct drm_via_chrome9_cmdbuf_size {
	enum {
		VIA_CHROME9_CMDBUF_SPACE = 0x01,
		VIA_CHROME9_CMDBUF_LAG = 0x02
	} func;
	int wait;
	uint32_t size;
};

struct drm_via_chrome9_DMA_manager {
	unsigned int     *addr_linear;
	unsigned int     DMASize;
	unsigned int     bDMAAgp;
	unsigned int     LastIssuedEventTag;
	unsigned int     *pBeg;
	unsigned int     *pInUseByHW;
	unsigned int     **ppInUseByHW;
	unsigned int     *pInUseBySW;
	unsigned int     *pFree;
	unsigned int     *pEnd;

	unsigned long    pPhysical;
	unsigned int     MaxKickoffSize;
};

struct get_pci_id_struct {
	unsigned int     x;
	unsigned int     y;
	unsigned int     z;
	unsigned int     f;
};


extern void *via_chrome9_dev_v4l;
extern void *via_chrome9_filepriv_v4l;
extern int via_chrome9_ioctl_wait_chip_idle(struct drm_device *dev,
	void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_init(struct drm_device *dev,
	void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_allocate_event_tag(struct drm_device
	*dev, void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_free_event_tag(struct drm_device *dev,
	void *data, struct drm_file *file_priv);
extern int via_chrome9_driver_load(struct drm_device *dev,
	unsigned long chipset);
extern int via_chrome9_driver_unload(struct drm_device *dev);
extern int via_chrome9_ioctl_process_exit(struct drm_device *dev,
	void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_restore_primary(struct drm_device *dev,
	void *data, struct drm_file *file_priv);
extern int  via_chrome9_drm_resume(struct pci_dev *dev);
extern int  via_chrome9_drm_suspend(struct pci_dev *dev,
	pm_message_t state);
extern void __via_chrome9ke_udelay(unsigned long usecs);
extern void via_chrome9_lastclose(struct drm_device *dev);
extern void via_chrome9_preclose(struct drm_device *dev,
	struct drm_file *file_priv);
extern int via_chrome9_is_agp(struct drm_device *dev);


#endif				/* _VIA_CHROME9_DRM_H_ */
