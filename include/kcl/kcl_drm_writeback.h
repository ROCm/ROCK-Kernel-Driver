/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Brian Starkey <brian.starkey@arm.com>
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 */
#ifndef AMDKCL_DRM_WRITEBACK_H
#define AMDKCL_DRM_WRITEBACK_H

#include <drm/drm_writeback.h>

#ifndef HAVE_DRM_WRITEBACK_CONNECTOR_INIT_7_ARGS
static inline int _kcl_drm_writeback_connector_init(struct drm_device *dev,
				 struct drm_writeback_connector *wb_connector,
				 const struct drm_connector_funcs *con_funcs,
				 const struct drm_encoder_helper_funcs *enc_helper_funcs,
				 const u32 *formats, int n_formats,
				 u32 possible_crtcs)
{
    wb_connector->encoder.possible_crtcs = possible_crtcs;

	return drm_writeback_connector_init(dev, wb_connector, con_funcs, enc_helper_funcs, formats, n_formats);
}
#define drm_writeback_connector_init _kcl_drm_writeback_connector_init
#endif

#endif