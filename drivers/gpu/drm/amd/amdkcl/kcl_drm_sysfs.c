// SPDX-License-Identifier: GPL-2.0-only

/*
 * drm_sysfs.c - Modifications to drm_sysfs_class.c to support
 *               extra sysfs attribute from DRM. Normal drm_sysfs_class
 *               does not allow adding attributes.
 *
 * Copyright (c) 2004 Jon Smirl <jonsmirl@gmail.com>
 * Copyright (c) 2003-2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2003-2004 IBM Corp.
 */
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>
#include <kcl/kcl_drm_print.h>

#ifndef HAVE_DRM_SYSFS_CONNECTOR_HOTPLUG_EVENT
/**
 * drm_sysfs_connector_hotplug_event - generate a DRM uevent for any connector
 * change
 * @connector: connector which has changed
 *
 * Send a uevent for the DRM connector specified by @connector. This will send
 * a uevent with the properties HOTPLUG=1 and CONNECTOR.
 */
void drm_sysfs_connector_hotplug_event(struct drm_connector *connector)
{
        struct drm_device *dev = connector->dev;
        char hotplug_str[] = "HOTPLUG=1", conn_id[21];
        char *envp[] = { hotplug_str, conn_id, NULL };

        snprintf(conn_id, sizeof(conn_id),
                 "CONNECTOR=%u", connector->base.id);

        drm_dbg_kms(connector->dev,
                    "[CONNECTOR:%d:%s] generating connector hotplug event\n",
                    connector->base.id, connector->name);

        kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(drm_sysfs_connector_hotplug_event);

#endif
