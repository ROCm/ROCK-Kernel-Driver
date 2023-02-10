/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DisplayPort CEC-Tunneling-over-AUX support
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef __KCL_KCL_DRM_DP_CEC_H__
#define __KCL_KCL_DRM_DP_CEC_H__

#include <drm/display/drm_dp_helper.h>	

/*
 * commit v4.19-rc1-100-g5ce70c799ac2
 * drm_dp_cec: check that aux has a transfer function
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 20, 0)
#define AMDKCL_DRM_DP_CEC_XXX_CHECK_CB
#endif

/* Copied from gpu/drm/drm_dp_cec.c and modified for KCL */
#if defined(AMDKCL_DRM_DP_CEC_XXX_CHECK_CB)
static inline void _kcl_drm_dp_cec_irq(struct drm_dp_aux *aux)
{
#ifdef CONFIG_DRM_DP_CEC
	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;
#endif

	drm_dp_cec_irq(aux);
}

static inline void _kcl_drm_dp_cec_set_edid(struct drm_dp_aux *aux,
				       const struct edid *edid)
{
#ifdef CONFIG_DRM_DP_CEC
	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;
#endif

	drm_dp_cec_set_edid(aux, edid);
}

static inline void _kcl_drm_dp_cec_unset_edid(struct drm_dp_aux *aux)
{
#ifdef CONFIG_DRM_DP_CEC
	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;
#endif

	drm_dp_cec_unset_edid(aux);
}
#endif

#if !defined(HAVE_DRM_DP_CEC_REGISTER_CONNECTOR_PP)
static inline void _kcl_drm_dp_cec_register_connector(struct drm_dp_aux *aux,
				   struct drm_connector *connector)
{
#ifdef CONFIG_DRM_DP_CEC
	if (WARN_ON(!aux->transfer))
		return;
#endif

	drm_dp_cec_register_connector(aux, connector->name, connector->dev->dev);
}
#endif


#endif
