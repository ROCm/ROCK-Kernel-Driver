#ifndef __AMDGPU_BACKPORT_KCL_DRM_DRV_H__
#define __AMDGPU_BACKPORT_KCL_DRM_DRV_H__
#include <linux/seq_file.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>
#include <kcl/kcl_drm_gem.h>

#ifndef HAVE_DRM_SHOW_FDINFO
void drm_show_fdinfo(struct seq_file *m, struct file *f);
#endif

#ifndef HAVE_DRM_PRINT_MEMORY_STATS
struct drm_memory_stats {
	u64 shared;
	u64 private;
	u64 resident;
	u64 purgeable;
	u64 active;
};

void _kcl_drm_print_memory_stats(struct drm_printer *p,
			    const struct drm_memory_stats *stats,
			    enum drm_gem_object_status supported_status,
			    const char *region);
#define drm_print_memory_stats _kcl_drm_print_memory_stats;
#endif

#endif
