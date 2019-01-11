/*
 * Copyright © 2008 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef _KCL_DRM_DP_HELPER_H_
#define _KCL_DRM_DP_HELPER_H_

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include <drm/drm_dp_helper.h>

/*
 * commit v4.19-rc1-100-g5ce70c799ac2
 * drm_dp_cec: check that aux has a transfer function
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 20, 0)
#define AMDKCL_DRM_DP_CEC_XXX_CHECK_CB
#endif

#if defined(AMDKCL_DRM_DP_CEC_XXX_CHECK_CB)
static inline void _kcl_drm_dp_cec_irq(struct drm_dp_aux *aux)
{
#if defined(HAVE_DRM_DP_CEC_CORRELATION_FUNCTIONS)
#ifdef CONFIG_DRM_DP_CEC
	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;
#endif

	drm_dp_cec_irq(aux);
#endif
}

static inline void _kcl_drm_dp_cec_register_connector(struct drm_dp_aux *aux,
						 const char *name,
						 struct device *parent)
{
#if defined(HAVE_DRM_DP_CEC_CORRELATION_FUNCTIONS)
#ifdef CONFIG_DRM_DP_CEC
	if (WARN_ON(!aux->transfer))
		return;
#endif

	drm_dp_cec_register_connector(aux, name, parent);
#endif
}

static inline void _kcl_drm_dp_cec_set_edid(struct drm_dp_aux *aux,
				       const struct edid *edid)
{
#if defined(HAVE_DRM_DP_CEC_CORRELATION_FUNCTIONS)
#ifdef CONFIG_DRM_DP_CEC
	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;
#endif

	drm_dp_cec_set_edid(aux, edid);
#endif
}

static inline void _kcl_drm_dp_cec_unset_edid(struct drm_dp_aux *aux)
{
#if defined(HAVE_DRM_DP_CEC_CORRELATION_FUNCTIONS)
#ifdef CONFIG_DRM_DP_CEC
	/* No transfer function was set, so not a DP connector */
	if (!aux->transfer)
		return;
#endif

	drm_dp_cec_unset_edid(aux);
#endif
}
#endif

#if !defined(HAVE_DRM_DP_CEC_CORRELATION_FUNCTIONS)
static inline void drm_dp_cec_unregister_connector(struct drm_dp_aux *aux)
{
}
#endif
#endif /* _KCL_DRM_DP_HELPER_H_ */
