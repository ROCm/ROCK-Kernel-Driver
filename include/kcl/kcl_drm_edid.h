#ifndef AMDKCL_DRM_EDID_H
#define AMDKCL_DRM_EDID_H

#include <drm/drm_edid.h>

#ifndef drm_edid_encode_panel_id
#define drm_edid_encode_panel_id(vend_chr_0, vend_chr_1, vend_chr_2, product_id) \
	((((u32)(vend_chr_0) - '@') & 0x1f) << 26 | \
	 (((u32)(vend_chr_1) - '@') & 0x1f) << 21 | \
	 (((u32)(vend_chr_2) - '@') & 0x1f) << 16 | \
	 ((product_id) & 0xffff))
#endif /* drm_edid_encode_panel_id */

#endif
