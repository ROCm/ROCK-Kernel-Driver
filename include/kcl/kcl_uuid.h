/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_UUID_H
#define KCL_KCL_UUID_H

#include <linux/uuid.h>

#ifndef HAVE_IMPORT_GUID
static inline void import_guid(guid_t *dst, const __u8 *src)
{
	memcpy(dst, src, sizeof(guid_t));
}

static inline void export_guid(__u8 *dst, const guid_t *src)
{
	memcpy(dst, src, sizeof(guid_t));
}
#endif

#endif