/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_CLIENT_H
#define KCL_KCL_DRM_CLIENT_H

#include <drm/drm_client.h>

#ifndef HAVE_DRM_CLIENT_REGISTER
static inline void drm_client_register(struct drm_client_dev *client)
{
	drm_client_add(client);
}
#endif /* HAVE_DRM_CLIENT_REGISTER */

#endif
