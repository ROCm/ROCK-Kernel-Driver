/*
 * DRM/KMS device registration for TI OMAP platforms
 *
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#ifdef CONFIG_CMA
#  include <linux/dma-contiguous.h>
#endif

#include <plat/omap_device.h>
#include <plat/omap_hwmod.h>

#include <plat/drm.h>

#if defined(CONFIG_DRM_OMAP) || (CONFIG_DRM_OMAP_MODULE)

static struct omap_drm_platform_data omapdrm_platdata;

static struct platform_device omap_drm_device = {
		.dev = {
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.platform_data = &omapdrm_platdata,
		},
		.name = "omapdrm",
		.id = 0,
};

static int __init omap_init_drm(void)
{
	struct omap_hwmod *oh = NULL;
	struct platform_device *pdev;

	/* lookup and populate the DMM information, if present - OMAP4+ */
	oh = omap_hwmod_lookup("dmm");

	if (oh) {
		pdev = omap_device_build(oh->name, -1, oh, NULL, 0, NULL, 0,
					false);
		WARN(IS_ERR(pdev), "Could not build omap_device for %s\n",
			oh->name);
	}

	return platform_device_register(&omap_drm_device);

}

arch_initcall(omap_init_drm);

void omapdrm_reserve_vram(void)
{
#ifdef CONFIG_CMA
	/*
	 * Create private 32MiB contiguous memory area for omapdrm.0 device
	 * TODO revisit size.. if uc/wc buffers are allocated from CMA pages
	 * then the amount of memory we need goes up..
	 */
	dma_declare_contiguous(&omap_drm_device.dev, 32 * SZ_1M, 0, 0);
#else
#  warning "CMA is not enabled, there may be limitations about scanout buffer allocations on OMAP3 and earlier"
#endif
}

#endif
