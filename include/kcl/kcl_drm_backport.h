#ifndef AMDKCL_DRM_BACKPORT_H
#define AMDKCL_DRM_BACKPORT_H

#include <drm/drm_atomic_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_edid.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_cache.h>
#include <drm/drm_gem.h>
#include <kcl/kcl_drm_file_h.h>
#if defined(HAVE_CHUNK_ID_SYNOBJ_IN_OUT)
#include <drm/drm_syncobj.h>
#endif
#include <kcl/kcl_drm.h>

#if DRM_VERSION_CODE >= DRM_VERSION(4, 17, 0)
#define AMDKCL_AMDGPU_DMABUF_OPS
#endif

#if !defined(HAVE_DRM_ENCODER_FIND_VALID_WITH_FILE)
#define drm_encoder_find(dev, file, id) drm_encoder_find(dev, id)
#endif

#if defined(HAVE_DRM_EDID_TO_ELD)
static inline
int _kcl_drm_add_edid_modes(struct drm_connector *connector, struct edid *edid)
{
	int ret;

	ret = drm_add_edid_modes(connector, edid);

	if (drm_edid_is_valid(edid))
		drm_edid_to_eld(connector, edid);

	return ret;
}
#define drm_add_edid_modes _kcl_drm_add_edid_modes
#endif

#if !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PP)
#define drm_fb_helper_remove_conflicting_pci_framebuffers _kcl_drm_fb_helper_remove_conflicting_pci_framebuffers
#endif

#if !defined(HAVE_DRM_ENCODER_INIT_VALID_WITH_NAME)
static inline int _kcl_drm_encoder_init(struct drm_device *dev,
		      struct drm_encoder *encoder,
		      const struct drm_encoder_funcs *funcs,
		      int encoder_type, const char *name, ...)
{
	return drm_encoder_init(dev, encoder, funcs, encoder_type);
}
#define drm_encoder_init _kcl_drm_encoder_init
#endif

#if !defined(HAVE_DRM_CRTC_INIT_WITH_PLANES_VALID_WITH_NAME)
static inline
int _kcl_drm_crtc_init_with_planes(struct drm_device *dev, struct drm_crtc *crtc,
			      struct drm_plane *primary,
			      struct drm_plane *cursor,
			      const struct drm_crtc_funcs *funcs,
			      const char *name, ...)
{
	return drm_crtc_init_with_planes(dev, crtc, primary, cursor, funcs);
}
#define drm_crtc_init_with_planes _kcl_drm_crtc_init_with_planes
#endif

#ifndef HAVE_DRM_UNIVERSAL_PLANE_INIT_9ARGS
static inline int _kcl_drm_universal_plane_init(struct drm_device *dev, struct drm_plane *plane,
			     unsigned long possible_crtcs,
			     const struct drm_plane_funcs *funcs,
			     const uint32_t *formats, unsigned int format_count,
			     const uint64_t *format_modifiers,
			     enum drm_plane_type type,
			     const char *name, ...)
{
#if defined(HAVE_DRM_UNIVERSAL_PLANE_INIT_8ARGS)
	return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
			 formats, format_count, type, name);
#else
	return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
			 formats, format_count, type);
#endif
}
#define drm_universal_plane_init _kcl_drm_universal_plane_init
#endif

#if !defined(HAVE_DRM_GET_FORMAT_NAME_I_P)
/**
 * struct drm_format_name_buf - name of a DRM format
 * @str: string buffer containing the format name
 */
struct drm_format_name_buf {
	char str[32];
};

static char printable_char(int c)
{
	return isascii(c) && isprint(c) ? c : '?';
}

static inline const char *_kcl_drm_get_format_name(uint32_t format, struct drm_format_name_buf *buf)
{
	snprintf(buf->str, sizeof(buf->str),
		 "%c%c%c%c %s-endian (0x%08x)",
		 printable_char(format & 0xff),
		 printable_char((format >> 8) & 0xff),
		 printable_char((format >> 16) & 0xff),
		 printable_char((format >> 24) & 0x7f),
		 format & DRM_FORMAT_BIG_ENDIAN ? "big" : "little",
		 format);

	return buf->str;
}
#define drm_get_format_name _kcl_drm_get_format_name
#endif

#if defined(HAVE_CHUNK_ID_SYNOBJ_IN_OUT)
static inline
int _kcl_drm_syncobj_find_fence(struct drm_file *file_private,
						u32 handle, u64 point, u64 flags,
						struct dma_fence **fence)
{
#if defined(HAVE_DRM_SYNCOBJ_FIND_FENCE)
#if defined(HAVE_DRM_SYNCOBJ_FIND_FENCE_5ARGS)
	return drm_syncobj_find_fence(file_private, handle, point, flags, fence);
#elif defined(HAVE_DRM_SYNCOBJ_FIND_FENCE_4ARGS)
	return drm_syncobj_find_fence(file_private, handle, point, fence);
#else
	return drm_syncobj_find_fence(file_private, handle, fence);
#endif
#elif defined(HAVE_DRM_SYNCOBJ_FENCE_GET)
	return drm_syncobj_fence_get(file_private, handle, fence);
#endif
}
#define drm_syncobj_find_fence _kcl_drm_syncobj_find_fence
#endif

#ifdef BUILD_AS_DKMS
#define drm_arch_can_wc_memory kcl_drm_arch_can_wc_memory
#endif

/*
 * commit d3252ace0bc652a1a244455556b6a549f969bf99
 * PCI: Restore resized BAR state on resume
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
#define AMDKCL_ENABLE_RESIZE_FB_BAR
#endif

/*
 * commit v4.10-rc3-539-g086f2e5cde74
 * drm: debugfs: Remove all files automatically on cleanup
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 11, 0)
#define AMDKCL_AMDGPU_DEBUGFS_CLEANUP
#endif

#ifndef HAVE_DRM_GEM_OBJECT_LOOKUP_2ARGS
static inline struct drm_gem_object *
_kcl_drm_gem_object_lookup(struct drm_file *filp, u32 handle)
{
	return drm_gem_object_lookup(filp->minor->dev, filp, handle);
}
#define drm_gem_object_lookup _kcl_drm_gem_object_lookup
#endif

#endif /* AMDKCL_DRM_BACKPORT_H */
