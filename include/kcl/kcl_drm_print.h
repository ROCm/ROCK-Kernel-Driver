#ifndef AMDKCL_DRM_PRINT_H
#define AMDKCL_DRM_PRINT_H

#include <kcl/kcl_drm_print_h.h>
#include <kcl/kcl_drm_drv_h.h>

#ifndef HAVE_DRM_DEBUG_ENABLED
static  inline bool drm_debug_enabled(unsigned int category)
{
	return unlikely(drm_debug & category);
}
#endif /* HAVE_DRM_DEBUG_ENABLED */
#endif
