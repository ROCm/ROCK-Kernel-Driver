/* SPDX-License-Identifier: MIT */
#ifndef KCL_BACKPORT_KCL_DRM_FB_H
#define KCL_BACKPORT_KCL_DRM_FB_H

#include <kcl/kcl_drm_fb.h>
#include <drm/drm_fb_helper.h>

#if !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PP)
#define drm_fb_helper_remove_conflicting_pci_framebuffers _kcl_drm_fb_helper_remove_conflicting_pci_framebuffers
#endif

#ifndef HAVE_DRM_HELPER_MODE_FILL_FB_STRUCT_DEV
#define drm_helper_mode_fill_fb_struct _kcl_drm_helper_mode_fill_fb_struct
#endif

#endif
