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

#include <kcl/kcl_drm_connector_h.h>
#include <kcl/kcl_drm_device_h.h>
#include <drm/drm_dp_helper.h>

#if !defined(DP_DPRX_FEATURE_ENUMERATION_LIST)
#define DP_DPRX_FEATURE_ENUMERATION_LIST    0x2210  /* DP 1.3 */
#endif

#if !defined(DP_TRAINING_PATTERN_SET_PHY_REPEATER1)
#define DP_TRAINING_PATTERN_SET_PHY_REPEATER1              0xf0010 /* 1.3 */
#endif

#if !defined(DP_LANE0_1_STATUS_PHY_REPEATER1)
#define DP_LANE0_1_STATUS_PHY_REPEATER1                            0xf0030 /* 1.3 */
#endif

#if !defined(DP_ADJUST_REQUEST_LANE0_1_PHY_REPEATER1)
#define DP_ADJUST_REQUEST_LANE0_1_PHY_REPEATER1                    0xf0033 /* 1.3 */
#endif

#if !defined(DP_TRAINING_LANE0_SET_PHY_REPEATER1)
#define DP_TRAINING_LANE0_SET_PHY_REPEATER1                0xf0011 /* 1.3 */
#endif

#if !defined(DP_PHY_REPEATER_MODE_TRANSPARENT)
#define DP_PHY_REPEATER_MODE_TRANSPARENT                   0x55    /* 1.3 */
#endif

#if !defined(DP_PHY_REPEATER_MODE)
#define DP_PHY_REPEATER_MODE                               0xf0003 /* 1.3 */
#endif

#if !defined(DP_PHY_REPEATER_MODE_NON_TRANSPARENT)
#define DP_PHY_REPEATER_MODE_NON_TRANSPARENT               0xaa    /* 1.3 */
#endif

#if !defined(DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1)
#define DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1          0xf0020 /* 1.4a */
#endif

#if !defined(DP_TRAINING_PATTERN_SET_PHY_REPEATER1)
#define DP_TRAINING_PATTERN_SET_PHY_REPEATER1              0xf0010 /* 1.3 */
#endif

#if !defined(DP_PHY_REPEATER_EXTENDED_WAIT_TIMEOUT)
#define DP_PHY_REPEATER_EXTENDED_WAIT_TIMEOUT              0xf0005 /* 1.4a */
#endif

#if !defined(DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV)
#define DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV 0xf0000 /* 1.3 */
#endif

#if !defined(DP_MAX_LINK_RATE_PHY_REPEATER)
#define DP_MAX_LINK_RATE_PHY_REPEATER                      0xf0001 /* 1.4a */
#endif

#if !defined(DP_PHY_REPEATER_CNT)
#define DP_PHY_REPEATER_CNT                                0xf0002 /* 1.3 */
#endif

#if !defined(DP_MAX_LANE_COUNT_PHY_REPEATER)
#define DP_MAX_LANE_COUNT_PHY_REPEATER                     0xf0004 /* 1.4a */
#endif

#if !defined(DP_TEST_AUDIO_MODE)
#define DP_TEST_AUDIO_MODE                 0x271
#endif

#if !defined(DP_TEST_AUDIO_PATTERN_TYPE)
#define DP_TEST_AUDIO_PATTERN_TYPE         0x272
#endif

#if !defined(DP_TEST_AUDIO_PERIOD_CH1)
#define DP_TEST_AUDIO_PERIOD_CH1           0x273
#endif

#if !defined(DP_DSC_SUPPORT)
#define DP_DSC_SUPPORT                      0x060   /* DP 1.4 */
#endif

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

#if !defined(HAVE_DRM_DP_CEC_REGISTER_CONNECTOR_PP)
static inline void _kcl_drm_dp_cec_register_connector(struct drm_dp_aux *aux,
				   struct drm_connector *connector)
{
#if defined(HAVE_DRM_DP_CEC_CORRELATION_FUNCTIONS)
#ifdef CONFIG_DRM_DP_CEC
	if (WARN_ON(!aux->transfer))
		return;
#endif

	drm_dp_cec_register_connector(aux, connector->name, connector->dev->dev);
#endif
}
#endif
#endif /* _KCL_DRM_DP_HELPER_H_ */
