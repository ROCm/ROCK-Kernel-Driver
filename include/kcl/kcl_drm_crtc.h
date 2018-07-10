/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_CRTC_H
#define KCL_KCL_DRM_CRTC_H

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <kcl/header/kcl_drm_device_h.h>

#ifndef DRM_MODE_ROTATE_0
#define DRM_MODE_ROTATE_0       (1<<0)
#endif
#ifndef DRM_MODE_ROTATE_90
#define DRM_MODE_ROTATE_90      (1<<1)
#endif
#ifndef DRM_MODE_ROTATE_180
#define DRM_MODE_ROTATE_180     (1<<2)
#endif
#ifndef DRM_MODE_ROTATE_270
#define DRM_MODE_ROTATE_270     (1<<3)
#endif

#ifndef DRM_MODE_ROTATE_MASK
#define DRM_MODE_ROTATE_MASK (\
		DRM_MODE_ROTATE_0  | \
		DRM_MODE_ROTATE_90  | \
		DRM_MODE_ROTATE_180 | \
		DRM_MODE_ROTATE_270)
#endif

/* helper for handling conditionals in various for_each macros */
#ifndef for_each_if
#define for_each_if(condition) if (!(condition)) {} else
#endif

#ifndef drm_for_each_plane
#define drm_for_each_plane(plane, dev) \
	list_for_each_entry(plane, &(dev)->mode_config.plane_list, head)
#endif

#ifndef drm_for_each_crtc
#define drm_for_each_crtc(crtc, dev) \
	list_for_each_entry(crtc, &(dev)->mode_config.crtc_list, head)
#endif

#ifndef drm_for_each_connector
#define drm_for_each_connector(connector, dev) \
	list_for_each_entry(connector, &(dev)->mode_config.connector_list, head)
#endif

#ifndef drm_for_each_encoder
#define drm_for_each_encoder(encoder, dev) \
	list_for_each_entry(encoder, &(dev)->mode_config.encoder_list, head)
#endif

#ifndef drm_for_each_fb
#define drm_for_each_fb(fb, dev) \
	list_for_each_entry(fb, &(dev)->mode_config.fb_list, head)
#endif

#if !defined(HAVE_DRM_CRTC_FORCE_DISABLE_ALL)
extern int drm_crtc_force_disable(struct drm_crtc *crtc);
extern int drm_crtc_force_disable_all(struct drm_device *dev);
#endif

#endif
