// SPDX-License-Identifier: GPL-2.0-only
/*
 * cec-adap.c - HDMI Consumer Electronics Control framework - CEC adapter
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */
#include <kcl/kcl_cec.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>

#if !defined(HAVE_CEC_CONNECTOR_INFO)
void _kcl_cec_fill_conn_info_from_drm(struct cec_connector_info *conn_info,
				 const struct drm_connector *connector)
{
	memset(conn_info, 0, sizeof(*conn_info));
	conn_info->type = CEC_CONNECTOR_TYPE_DRM;
	conn_info->drm.card_no = connector->dev->primary->index;
	conn_info->drm.connector_id = connector->base.id;
}
EXPORT_SYMBOL_GPL(_kcl_cec_fill_conn_info_from_drm);

#endif
