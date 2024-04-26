#ifndef __AMDGPU_BACKPORT_KCL_DRM_DRV_H__
#define __AMDGPU_BACKPORT_KCL_DRM_DRV_H__
#include <linux/seq_file.h> 

#ifndef HAVE_DRM_SHOW_FDINFO
void drm_show_fdinfo(struct seq_file *m, struct file *f);
#endif
#endif
