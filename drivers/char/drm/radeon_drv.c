/**
 * \file radeon_drv.c
 * ATI Radeon driver
 *
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
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


#include <linux/config.h>
#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"

#include "drm_pciids.h"

static int postinit( struct drm_device *dev, unsigned long flags )
{
	DRM_INFO( "Initialized %s %d.%d.%d %s on minor %d: %s\n",
		DRIVER_NAME,
		DRIVER_MAJOR,
		DRIVER_MINOR,
		DRIVER_PATCHLEVEL,
		DRIVER_DATE,
		dev->minor,
		pci_pretty_name(dev->pdev)
		);
	return 0;
}

static int version( drm_version_t *version )
{
	int len;

	version->version_major = DRIVER_MAJOR;
	version->version_minor = DRIVER_MINOR;
	version->version_patchlevel = DRIVER_PATCHLEVEL;
	DRM_COPY( version->name, DRIVER_NAME );
	DRM_COPY( version->date, DRIVER_DATE );
	DRM_COPY( version->desc, DRIVER_DESC );
	return 0;
}

static struct pci_device_id pciidlist[] = {
	radeon_PCI_IDS
};

/* Interface history:
 *
 * 1.1 - ??
 * 1.2 - Add vertex2 ioctl (keith)
 *     - Add stencil capability to clear ioctl (gareth, keith)
 *     - Increase MAX_TEXTURE_LEVELS (brian)
 * 1.3 - Add cmdbuf ioctl (keith)
 *     - Add support for new radeon packets (keith)
 *     - Add getparam ioctl (keith)
 *     - Add flip-buffers ioctl, deprecate fullscreen foo (keith).
 * 1.4 - Add scratch registers to get_param ioctl.
 * 1.5 - Add r200 packets to cmdbuf ioctl
 *     - Add r200 function to init ioctl
 *     - Add 'scalar2' instruction to cmdbuf
 * 1.6 - Add static GART memory manager
 *       Add irq handler (won't be turned on unless X server knows to)
 *       Add irq ioctls and irq_active getparam.
 *       Add wait command for cmdbuf ioctl
 *       Add GART offset query for getparam
 * 1.7 - Add support for cube map registers: R200_PP_CUBIC_FACES_[0..5]
 *       and R200_PP_CUBIC_OFFSET_F1_[0..5].
 *       Added packets R200_EMIT_PP_CUBIC_FACES_[0..5] and
 *       R200_EMIT_PP_CUBIC_OFFSETS_[0..5].  (brian)
 * 1.8 - Remove need to call cleanup ioctls on last client exit (keith)
 *       Add 'GET' queries for starting additional clients on different VT's.
 * 1.9 - Add DRM_IOCTL_RADEON_CP_RESUME ioctl.
 *       Add texture rectangle support for r100.
 * 1.10- Add SETPARAM ioctl; first parameter to set is FB_LOCATION, which
 *       clients use to tell the DRM where they think the framebuffer is 
 *       located in the card's address space
 * 1.11- Add packet R200_EMIT_RB3D_BLENDCOLOR to support GL_EXT_blend_color
 *       and GL_EXT_blend_[func|equation]_separate on r200
 * 1.12- Add R300 CP microcode support - this just loads the CP on r300
 *       (No 3D support yet - just microcode loading)
 */
static drm_ioctl_desc_t ioctls[] = {
 [DRM_IOCTL_NR(DRM_RADEON_CP_INIT)]    = { radeon_cp_init,      1, 1 },
 [DRM_IOCTL_NR(DRM_RADEON_CP_START)]   = { radeon_cp_start,     1, 1 },
 [DRM_IOCTL_NR(DRM_RADEON_CP_STOP)]    = { radeon_cp_stop,      1, 1 },
 [DRM_IOCTL_NR(DRM_RADEON_CP_RESET)]   = { radeon_cp_reset,     1, 1 },
 [DRM_IOCTL_NR(DRM_RADEON_CP_IDLE)]    = { radeon_cp_idle,      1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_CP_RESUME)]  = { radeon_cp_resume,    1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_RESET)]      = { radeon_engine_reset, 1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_FULLSCREEN)] = { radeon_fullscreen,   1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_SWAP)]       = { radeon_cp_swap,      1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_CLEAR)]      = { radeon_cp_clear,     1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_VERTEX)]     = { radeon_cp_vertex,    1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_INDICES)]    = { radeon_cp_indices,   1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_TEXTURE)]    = { radeon_cp_texture,   1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_STIPPLE)]    = { radeon_cp_stipple,   1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_INDIRECT)]   = { radeon_cp_indirect,  1, 1 },
 [DRM_IOCTL_NR(DRM_RADEON_VERTEX2)]    = { radeon_cp_vertex2,   1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_CMDBUF)]     = { radeon_cp_cmdbuf,    1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_GETPARAM)]   = { radeon_cp_getparam,  1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_FLIP)]       = { radeon_cp_flip,      1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_ALLOC)]      = { radeon_mem_alloc,    1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_FREE)]       = { radeon_mem_free,     1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_INIT_HEAP)]  = { radeon_mem_init_heap,1, 1 },
 [DRM_IOCTL_NR(DRM_RADEON_IRQ_EMIT)]   = { radeon_irq_emit,     1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_IRQ_WAIT)]   = { radeon_irq_wait,     1, 0 },
 [DRM_IOCTL_NR(DRM_RADEON_SETPARAM)]   = { radeon_cp_setparam,  1, 0 },
};

static struct drm_driver driver = {
	.driver_features = DRIVER_USE_AGP | DRIVER_USE_MTRR | DRIVER_PCI_DMA | DRIVER_SG | DRIVER_HAVE_IRQ | DRIVER_HAVE_DMA | DRIVER_IRQ_SHARED | DRIVER_IRQ_VBL,
	.dev_priv_size = sizeof(drm_radeon_buf_priv_t),
	.prerelease = radeon_driver_prerelease,
	.pretakedown = radeon_driver_pretakedown,
	.open_helper = radeon_driver_open_helper,
	.vblank_wait = radeon_driver_vblank_wait,
	.irq_preinstall = radeon_driver_irq_preinstall,
	.irq_postinstall = radeon_driver_irq_postinstall,
	.irq_uninstall = radeon_driver_irq_uninstall,
	.irq_handler = radeon_driver_irq_handler,
	.free_filp_priv = radeon_driver_free_filp_priv,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.postinit = postinit,
	.version = version,
	.ioctls = ioctls,
	.num_ioctls = DRM_ARRAY_SIZE(ioctls),
	.dma_ioctl = radeon_cp_buffers,
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
		.ioctl = drm_ioctl,
		.mmap = drm_mmap,
		.poll = drm_poll,
		.fasync = drm_fasync,
	},
	.pci_driver = {
		.name          = DRIVER_NAME,
		.id_table      = pciidlist,
	}
};

static int __init radeon_init(void)
{
	return drm_init(&driver);
}

static void __exit radeon_exit(void)
{
	drm_exit(&driver);
}

module_init(radeon_init);
module_exit(radeon_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL and additional rights");
