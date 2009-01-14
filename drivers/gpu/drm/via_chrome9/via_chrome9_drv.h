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
#ifndef _VIA_CHROME9_DRV_H_
#define _VIA_CHROME9_DRV_H_

#include "drm_sman.h"
#include "via_chrome9_verifier.h"
#define DRIVER_AUTHOR	"Various"

#define DRIVER_NAME		"via_chrome9"
#define DRIVER_DESC		"VIA_CHROME9 Unichrome / Pro"
#define DRIVER_DATE		"20080415"

#define DRIVER_MAJOR		2
#define DRIVER_MINOR		11
#define DRIVER_PATCHLEVEL	1

#define via_chrome9_FIRE_BUF_SIZE  1024
#define via_chrome9_NUM_IRQS 4

#define MAX_MEMORY_HEAPS     4
#define NUMBER_OF_APERTURES     32

/*typedef struct drm_via_chrome9_shadow_map drm_via_chrome9_shadow_map_t;*/
struct drm_via_chrome9_shadow_map {
	struct drm_map  *shadow;
	unsigned int   shadow_size;
	unsigned int   *shadow_handle;
};

/*typedef struct drm_via_chrome9_pagetable_map
 *drm_via_chrome9_pagetable_map_t;
 */
struct drm_via_chrome9_pagetable_map {
	unsigned int pagetable_offset;
	unsigned int pagetable_size;
	unsigned int *pagetable_handle;
	unsigned int mmt_register;
};

/*typedef struct drm_via_chrome9_private drm_via_chrome9_private_t;*/
struct drm_via_chrome9_private {
	int chip_agp;
	int chip_index;
	int chip_sub_index;

	unsigned long front_offset;
	unsigned long back_offset;
	unsigned long depth_offset;
	unsigned long fb_base_address;
	unsigned long available_fb_size;
	int usec_timeout;
	int max_apertures;
	struct drm_sman sman;
	unsigned int alignment;
	/* bit[31]:0:indicate no alignment needed,1:indicate
	alignment needed and size is bit[0:30]*/

	struct drm_map *sarea;
	struct drm_via_chrome9_sarea *sarea_priv;

	struct drm_map  *mmio;
	struct drm_map  *hostBlt;
	struct drm_map  *fb;
	struct drm_map  *front;
	struct drm_map  *back;
	struct drm_map  *depth;
	struct drm_map  *agp_tex;
	unsigned int agp_size;
	unsigned int agp_offset;

	struct semaphore *drm_s3g_sem;

	struct drm_via_chrome9_shadow_map  shadow_map;
	struct drm_via_chrome9_pagetable_map pagetable_map;

	char *bci;

	int aperture_usage[NUMBER_OF_APERTURES];
	void *event_tag_info;

	/* DMA buffer manager */
	void *dma_manager;
	/* Indicate agp/pcie heap initialization flag */
	int agp_initialized;
	/* Indicate video heap initialization flag */
	int vram_initialized;

	unsigned long pcie_vmalloc_addr;

	/* pointer to device information */
	void *dev;
	/* if agp init fail, go ahead and force dri use PCI*/
	enum {
		DRM_AGP_RING_BUFFER,
		DRM_AGP_DOUBLE_BUFFER,
		DRM_AGP_DISABLED
	} drm_agp_type;
	/*end*/
#if VIA_CHROME9_VERIFY_ENABLE
	struct drm_via_chrome9_state hc_state;
#endif
	unsigned long *bci_buffer;
	unsigned long pcie_vmalloc_nocache;
	unsigned char gti_backup[13];
	int initialized;

};


enum via_chrome9_family {
	VIA_CHROME9_OTHER = 0,	/* Baseline */
	VIA_CHROME9_PRO_GROUP_A,/* Another video engine and DMA commands */
	VIA_CHROME9_DX9_0,
	VIA_CHROME9_PCIE_GROUP
};

/* VIA_CHROME9 MMIO register access */
#define VIA_CHROME9_BASE ((dev_priv->mmio))

#define VIA_CHROME9_READ(reg)		DRM_READ32(VIA_CHROME9_BASE, reg)
#define VIA_CHROME9_WRITE(reg, val)	DRM_WRITE32(VIA_CHROME9_BASE, reg, val)
#define VIA_CHROME9_READ8(reg)		DRM_READ8(VIA_CHROME9_BASE, reg)
#define VIA_CHROME9_WRITE8(reg, val)	DRM_WRITE8(VIA_CHROME9_BASE, reg, val)

#endif
