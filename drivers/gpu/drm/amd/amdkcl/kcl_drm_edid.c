// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Intel Corporation.
 *
 * Authors:
 * Ramalingam C <ramalingam.c@intel.com>
 */
#include <kcl/kcl_drm_edid.h>
#include <linux/slab.h>

#ifndef HAVE_DRM_EDID_MALLOC
static const struct drm_edid *__kcl_drm_edid_alloc(const void *edid, size_t size)
{
	struct drm_edid *drm_edid;

	if (!edid || !size || size < EDID_LENGTH)
		return NULL;

	drm_edid = kzalloc(sizeof(*drm_edid), GFP_KERNEL);
	if (drm_edid) {
		drm_edid->edid = edid;
		drm_edid->size = size;
	}

	return drm_edid;
}

const struct drm_edid *_kcl_drm_edid_alloc(const void *edid, size_t size)
{
	const struct drm_edid *drm_edid;

	if (!edid || !size || size < EDID_LENGTH)
		return NULL;

	edid = kmemdup(edid, size, GFP_KERNEL);
	if (!edid)
		return NULL;

	drm_edid = __kcl_drm_edid_alloc(edid, size);
	if (!drm_edid)
		kfree(edid);

	return drm_edid;
}
EXPORT_SYMBOL(_kcl_drm_edid_alloc);

void _kcl_drm_edid_free(const struct drm_edid *drm_edid)
{
	if (!drm_edid)
		return;

	kfree(drm_edid->edid);
	kfree(drm_edid);
}
EXPORT_SYMBOL(_kcl_drm_edid_free);
#endif

#ifndef HAVE_DRM_EDID_RAW
static int edid_extension_block_count(const struct edid *edid)
{
	return edid->extensions;
}

static int edid_block_count(const struct edid *edid)
{
	return edid_extension_block_count(edid) + 1;
}

static int edid_size_by_blocks(int num_blocks)
{
	return num_blocks * EDID_LENGTH;
}

static int edid_size(const struct edid *edid)
{
	return edid_size_by_blocks(edid_block_count(edid));
}

const struct edid *_kcl_drm_edid_raw(const struct drm_edid *drm_edid)
{
	if (!drm_edid || !drm_edid->size)
		return NULL;

	/*
	 * Do not return pointers where relying on EDID extension count would
	 * lead to buffer overflow.
	 */
	if (WARN_ON(edid_size(drm_edid->edid) > drm_edid->size))
		return NULL;

	return drm_edid->edid;
}
EXPORT_SYMBOL(_kcl_drm_edid_raw);
#endif

