/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_DRM_SYSFS_H
#define AMDKCL_DRM_SYSFS_H

struct drm_connector;
#ifndef HAVE_DRM_SYSFS_CONNECTOR_HOTPLUG_EVENT
void drm_sysfs_connector_hotplug_event(struct drm_connector *connector);
#endif

#endif
