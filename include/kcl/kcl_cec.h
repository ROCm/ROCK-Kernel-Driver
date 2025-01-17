/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * cec - HDMI Consumer Electronics Control public header
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _KCL_CEC_UAPI_H
#define _KCL_CEC_UAPI_H

#include <uapi/linux/cec.h>
#include <media/cec.h>

#if !defined(HAVE_CEC_CONNECTOR_INFO)
/**
 * struct cec_drm_connector_info - tells which drm connector is
 * associated with the CEC adapter.
 * @card_no: drm card number
 * @connector_id: drm connector ID
 */
struct cec_drm_connector_info {
	__u32 card_no;
	__u32 connector_id;
};

#define CEC_CONNECTOR_TYPE_NO_CONNECTOR	0
#define CEC_CONNECTOR_TYPE_DRM		1

/**
 * struct cec_connector_info - tells if and which connector is
 * associated with the CEC adapter.
 * @type: connector type (if any)
 * @drm: drm connector info
 * @raw: array to pad the union
 */
struct cec_connector_info {
	__u32 type;
	union {
		struct cec_drm_connector_info drm;
		__u32 raw[16];
	};
};

#if IS_REACHABLE(CONFIG_CEC_CORE)
void _kcl_cec_fill_conn_info_from_drm(struct cec_connector_info *conn_info,
				 const struct drm_connector *connector);
#define cec_fill_conn_info_from_drm _kcl_cec_fill_conn_info_from_drm

#else
static inline void
cec_fill_conn_info_from_drm(struct cec_connector_info *conn_info,
			    const struct drm_connector *connector)
{
	memset(conn_info, 0, sizeof(*conn_info));
}
#endif

#endif

#endif
