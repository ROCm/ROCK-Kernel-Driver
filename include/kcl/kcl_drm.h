#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <linux/version.h>
#include <linux/kconfig.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_gem.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_rect.h>
#include <drm/drm_modes.h>
#include <linux/ctype.h>
#include <linux/console.h>
#if defined(HAVE_DRM_PRINTER)
#include <drm/drm_print.h>
#endif
#if defined(HAVE_CHUNK_ID_SYNOBJ_IN_OUT)
#include <drm/drm_syncobj.h>
#endif
#if defined(HAVE_DRM_COLOR_LUT_SIZE)
#include <drm/drm_color_mgmt.h>
#endif

#ifndef DP_ADJUST_REQUEST_POST_CURSOR2
#define DP_ADJUST_REQUEST_POST_CURSOR2      0x20c
#endif

#ifndef DP_TEST_MISC0
#define DP_TEST_MISC0                       0x232
#endif

#ifndef DP_TEST_PHY_PATTERN
#define DP_TEST_PHY_PATTERN                 0x248
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_7_0
#define DP_TEST_80BIT_CUSTOM_PATTERN_7_0    0x250
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_15_8
#define DP_TEST_80BIT_CUSTOM_PATTERN_15_8   0x251
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_23_16
#define DP_TEST_80BIT_CUSTOM_PATTERN_23_16  0x252
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_31_24
#define DP_TEST_80BIT_CUSTOM_PATTERN_31_24  0x253
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_39_32
#define DP_TEST_80BIT_CUSTOM_PATTERN_39_32  0x254
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_47_40
#define DP_TEST_80BIT_CUSTOM_PATTERN_47_40  0x255
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_55_48
#define DP_TEST_80BIT_CUSTOM_PATTERN_55_48  0x256
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_63_56
#define DP_TEST_80BIT_CUSTOM_PATTERN_63_56  0x257
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_71_64
#define DP_TEST_80BIT_CUSTOM_PATTERN_71_64  0x258
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_79_72
#define DP_TEST_80BIT_CUSTOM_PATTERN_79_72  0x259
#endif

#ifndef DP_BRANCH_REVISION_START
#define DP_BRANCH_REVISION_START            0x509
#endif

#ifndef DP_DP13_DPCD_REV
#define DP_DP13_DPCD_REV                    0x2200
#endif
#ifndef DP_DP13_MAX_LINK_RATE
#define DP_DP13_MAX_LINK_RATE               0x2201
#endif

#ifndef DRM_MODE_ROTATE_0
#define DRM_MODE_ROTATE_0       (1<<0)
#endif
#ifndef DRM_MODE_ROTATE_90
#define DRM_MODE_ROTATE_90      (1<<1)
#endif
#ifndef DRM_MODE_ROTATE_180
#define DRM_MODE_ROTATE_180     (1<<2)
#endif
#ifndef DRM_MODE_ROTATE_270
#define DRM_MODE_ROTATE_270     (1<<3)
#endif

#ifndef DRM_MODE_ROTATE_MASK
#define DRM_MODE_ROTATE_MASK (\
		DRM_MODE_ROTATE_0  | \
		DRM_MODE_ROTATE_90  | \
		DRM_MODE_ROTATE_180 | \
		DRM_MODE_ROTATE_270)
#endif

extern void (*_kcl_drm_fb_helper_cfb_fillrect)(struct fb_info *info,
				const struct fb_fillrect *rect);
extern void (*_kcl_drm_fb_helper_cfb_copyarea)(struct fb_info *info,
				const struct fb_copyarea *area);
extern void (*_kcl_drm_fb_helper_cfb_imageblit)(struct fb_info *info,
				 const struct fb_image *image);
extern void (*_kcl_drm_fb_helper_unregister_fbi)(struct drm_fb_helper *fb_helper);
extern struct fb_info *(*_kcl_drm_fb_helper_alloc_fbi)(struct drm_fb_helper *fb_helper);
extern void (*_kcl_drm_fb_helper_set_suspend_unlocked)(struct drm_fb_helper *fb_helper, int state);
extern void
(*_kcl_drm_atomic_helper_update_legacy_modeset_state)(struct drm_device *dev,
					      struct drm_atomic_state *old_state);

#if !defined(HAVE_DRM_MODESET_LOCK_ALL_CTX)
int drm_modeset_lock_all_ctx(struct drm_device *dev,
			     struct drm_modeset_acquire_ctx *ctx);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_DISABLE_ALL)
int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_DUPLICATE_STATE)
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_SUSPEND)
struct drm_atomic_state *drm_atomic_helper_suspend(struct drm_device *dev);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_RESUME)
int drm_atomic_helper_resume(struct drm_device *dev,
			     struct drm_atomic_state *state);
#endif

#if !defined(HAVE_DRM_CRTC_FORCE_DISABLE_ALL)
extern int drm_crtc_force_disable_all(struct drm_device *dev);
#endif

#if !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS)

#if !defined(IS_REACHABLE)
#define __ARG_PLACEHOLDER_1 0,
#define __take_second_arg(__ignored, val, ...) val

/*
 * The use of "&&" / "||" is limited in certain expressions.
 * The followings enable to calculate "and" / "or" with macro expansion only.
 */
#define __and(x, y)			___and(x, y)
#define ___and(x, y)			____and(__ARG_PLACEHOLDER_##x, y)
#define ____and(arg1_or_junk, y)	__take_second_arg(arg1_or_junk y, 0)

#define __or(x, y)			___or(x, y)
#define ___or(x, y)			____or(__ARG_PLACEHOLDER_##x, y)
#define ____or(arg1_or_junk, y)		__take_second_arg(arg1_or_junk 1, y)

#define IS_REACHABLE(option) __or(IS_BUILTIN(option), \
				__and(IS_MODULE(option), __is_defined(MODULE)))
#endif
extern int remove_conflicting_pci_framebuffers(struct pci_dev *pdev, int res_id,
					       const char *name);
static inline int
drm_fb_helper_remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
						  int resource_id,
						  const char *name)
{
#if IS_REACHABLE(CONFIG_FB)
	return remove_conflicting_pci_framebuffers(pdev, resource_id, name);
#else
	return 0;
#endif
}
#endif

static inline void kcl_drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect)
{
#ifndef HAVE_DRM_FB_HELPER_CFB_XX
	_kcl_drm_fb_helper_cfb_fillrect(info, rect);
#else
	drm_fb_helper_cfb_fillrect(info, rect);
#endif
}

static inline void kcl_drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
#ifndef HAVE_DRM_FB_HELPER_CFB_XX
	_kcl_drm_fb_helper_cfb_copyarea(info, area);
#else
	drm_fb_helper_cfb_copyarea(info, area);
#endif
}

static inline void kcl_drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
#ifndef HAVE_DRM_FB_HELPER_CFB_XX
	_kcl_drm_fb_helper_cfb_imageblit(info, image);
#else
	drm_fb_helper_cfb_imageblit(info, image);
#endif
}

static inline struct fb_info *kcl_drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)
{
#ifndef HAVE_DRM_FB_HELPER_XX_FBI
	return _kcl_drm_fb_helper_alloc_fbi(fb_helper);
#else
	return drm_fb_helper_alloc_fbi(fb_helper);
#endif
}

static inline void kcl_drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
#ifndef HAVE_DRM_FB_HELPER_XX_FBI
	_kcl_drm_fb_helper_unregister_fbi(fb_helper);
#else
	drm_fb_helper_unregister_fbi(fb_helper);
#endif
}

static inline void kcl_drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper, int state)
{
#ifndef HAVE_DRM_FB_HELPER_SET_SUSPEND_UNLOCKED
	_kcl_drm_fb_helper_set_suspend_unlocked(fb_helper, state);
#else
	drm_fb_helper_set_suspend_unlocked(fb_helper, state);
#endif
}

#ifndef HAVE_DRM_FB_HELPER_FILL_INFO
void drm_fb_helper_fill_info(struct fb_info *info,
			     struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *sizes);
#endif

static inline void
kcl_drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state)
{
#ifndef HAVE_DRM_ATOMIC_HELPER_UPDATE_LEGACY_MODESET_STATE
	_kcl_drm_atomic_helper_update_legacy_modeset_state(dev, old_state);
#else
	drm_atomic_helper_update_legacy_modeset_state(dev, old_state);
#endif
}

#ifndef DRM_DEBUG_VBL
#define DRM_UT_VBL		0x20
#define DRM_DEBUG_VBL(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_VBL))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#endif

static inline bool kcl_drm_arch_can_wc_memory(void)
{
#if defined(CONFIG_PPC) && !defined(CONFIG_NOT_COHERENT_CACHE)
	return false;
#elif defined(CONFIG_MIPS) && defined(CONFIG_CPU_LOONGSON3)
	return false;
#else
	return true;
#endif
}

#if defined(HAVE_CHUNK_ID_SYNOBJ_IN_OUT)
static inline int kcl_drm_syncobj_find_fence(struct drm_file *file_private,
						u32 handle, u64 point, u64 flags,
						struct dma_fence **fence)
{
#if defined(HAVE_DRM_SYNCOBJ_FENCE_GET)
	return drm_syncobj_fence_get(file_private, handle, fence);
#elif defined(HAVE_3ARGS_DRM_SYNCOBJ_FIND_FENCE)
	return drm_syncobj_find_fence(file_private, handle, fence);
#elif defined(HAVE_4ARGS_DRM_SYNCOBJ_FIND_FENCE)
	return drm_syncobj_find_fence(file_private, handle, point, fence);
#else
	return drm_syncobj_find_fence(file_private, handle, point, flags, fence);
#endif
}
#endif

#if defined(HAVE_DRM_COLOR_LUT) && !defined(HAVE_DRM_COLOR_LUT_SIZE)
/**
 * drm_color_lut_size - calculate the number of entries in the LUT
 * @blob: blob containing the LUT
 *
 * Returns:
 * The number of entries in the color LUT stored in @blob.
 */
static inline int drm_color_lut_size(const struct drm_property_blob *blob)
{
	return blob->length / sizeof(struct drm_color_lut);
}
#endif

static inline int kcl_drm_encoder_init(struct drm_device *dev,
		      struct drm_encoder *encoder,
		      const struct drm_encoder_funcs *funcs,
		      int encoder_type, const char *name, ...)
{
#if defined(HAVE_DRM_ENCODER_INIT_VALID_WITH_NAME)
	return drm_encoder_init(dev, encoder, funcs,
			 encoder_type, name);
#else
	return drm_encoder_init(dev, encoder, funcs,
			 encoder_type);
#endif
}

static inline int kcl_drm_crtc_init_with_planes(struct drm_device *dev, struct drm_crtc *crtc,
			      struct drm_plane *primary,
			      struct drm_plane *cursor,
			      const struct drm_crtc_funcs *funcs,
			      const char *name, ...)
{
#if defined(HAVE_DRM_CRTC_INIT_WITH_PLANES_VALID_WITH_NAME)
		return drm_crtc_init_with_planes(dev, crtc, primary,
				 cursor, funcs, name);
#else
		return drm_crtc_init_with_planes(dev, crtc, primary,
				 cursor, funcs);
#endif
}

static inline int kcl_drm_universal_plane_init(struct drm_device *dev, struct drm_plane *plane,
			     unsigned long possible_crtcs,
			     const struct drm_plane_funcs *funcs,
			     const uint32_t *formats, unsigned int format_count,
			     const uint64_t *format_modifiers,
			     enum drm_plane_type type,
			     const char *name, ...)
{
#if defined(HAVE_9ARGS_DRM_UNIVERSAL_PLANE_INIT)
		return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
				 formats, format_count, format_modifiers, type, name);
#elif defined(HAVE_8ARGS_DRM_UNIVERSAL_PLANE_INIT)
		return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
				 formats, format_count, type, name);
#else
		return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
				 formats, format_count, type);
#endif
}

static inline struct drm_gem_object *
kcl_drm_gem_object_lookup(struct drm_device *dev, struct drm_file *filp,
				u32 handle)
{
#if defined(HAVE_2ARGS_DRM_GEM_OBJECT_LOOKUP)
		return drm_gem_object_lookup(filp, handle);
#else
		return drm_gem_object_lookup(dev, filp, handle);
#endif
}

#if !defined(HAVE_DRM_GET_FORMAT_NAME)
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

static inline const char *kcl_drm_get_format_name(uint32_t format, struct drm_format_name_buf *buf)
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
#else
static inline const char *kcl_drm_get_format_name(uint32_t format, struct drm_format_name_buf *buf)
{
	return drm_get_format_name(format, buf);
}
#endif

static inline void kcl_drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
#if !defined(HAVE_DRM_GEM_OBJECT_PUT_UNLOCKED)
	return drm_gem_object_unreference_unlocked(obj);
#else
	return drm_gem_object_put_unlocked(obj);
#endif
}

#ifdef BUILD_AS_DKMS
extern struct dma_buf_ops *_kcl_drm_gem_prime_dmabuf_ops;
#define drm_gem_prime_dmabuf_ops (*_kcl_drm_gem_prime_dmabuf_ops)
#endif

#if !defined(HAVE_DRM_IS_CURRENT_MASTER)
bool drm_is_current_master(struct drm_file *fpriv);
#endif

static inline struct drm_crtc_state *
kcl_drm_atomic_get_old_crtc_state_before_commit(struct drm_atomic_state *state,
					    struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ATOMIC_GET_CRTC_STATE)
	return drm_atomic_get_old_crtc_state(state, crtc);
#elif defined(HAVE_DRM_CRTCS_STATE_MEMBER)
	return state->crtcs[drm_crtc_index(crtc)].ptr->state;
#else
	return state->crtcs[drm_crtc_index(crtc)].state;
#endif
}

static inline struct drm_crtc_state *
kcl_drm_atomic_get_old_crtc_state_after_commit(struct drm_atomic_state *state,
				  struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ATOMIC_GET_CRTC_STATE)
	return drm_atomic_get_old_crtc_state(state, crtc);
#else
	return drm_atomic_get_existing_crtc_state(state, crtc);
#endif
}

static inline struct drm_crtc_state *
kcl_drm_atomic_get_new_crtc_state_before_commit(struct drm_atomic_state *state,
				  struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ATOMIC_GET_CRTC_STATE)
	return drm_atomic_get_new_crtc_state(state,crtc);
#else
	return drm_atomic_get_existing_crtc_state(state, crtc);
#endif
}

static inline struct drm_crtc_state *
kcl_drm_atomic_get_new_crtc_state_after_commit(struct drm_atomic_state *state,
					    struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ATOMIC_GET_CRTC_STATE)
	return drm_atomic_get_new_crtc_state(state,crtc);
#elif defined(HAVE_DRM_CRTCS_STATE_MEMBER)
	return state->crtcs[drm_crtc_index(crtc)].ptr->state;
#else
	return state->crtcs[drm_crtc_index(crtc)].state;
#endif
}

static inline struct drm_plane_state *
kcl_drm_atomic_get_new_plane_state_before_commit(struct drm_atomic_state *state,
							struct drm_plane *plane)
{
#if defined(HAVE_DRM_ATOMIC_GET_NEW_PLANE_STATE)
	return drm_atomic_get_new_plane_state(state, plane);
#else
	return drm_atomic_get_existing_plane_state(state, plane);
#endif
}

#if !defined(HAVE_DRM_ATOMIC_HELPER_CONNECTOR_RESET)
extern void
__kcl_drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state);
#endif

static inline void
kcl_drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state)
{
#if !defined(HAVE_DRM_ATOMIC_HELPER_CONNECTOR_RESET)
	return __kcl_drm_atomic_helper_connector_reset(connector, conn_state);
#else
	return __drm_atomic_helper_connector_reset(connector, conn_state);
#endif
}

#if !defined(HAVE_DRM_GET_MAX_IOMEM)
u64 drm_get_max_iomem(void);
#endif

#if !defined(HAVE_DRM_PRINTER)
struct drm_printer {
	void (*printfn)(struct drm_printer *p, struct va_format *vaf);
	void *arg;
	const char *prefix;
};

void drm_printf(struct drm_printer *p, const char *f, ...);
#endif

#if !defined(HAVE_DRM_DEBUG_PRINTER)
extern void __drm_printfn_debug(struct drm_printer *p, struct va_format *vaf);
/**
 * drm_debug_printer - construct a &drm_printer that outputs to pr_debug()
 * @prefix: debug output prefix
 *
 * RETURNS:
 * The &drm_printer object
 */
static inline struct drm_printer drm_debug_printer(const char *prefix)
{
	struct drm_printer p = {
		.printfn = __drm_printfn_debug,
#if !defined(HAVE_DRM_PRINTER)
		.prefix = prefix
#endif
	};
	return p;
}
#endif

/* helper for handling conditionals in various for_each macros */
#ifndef for_each_if
#define for_each_if(condition) if (!(condition)) {} else
#endif

#if !defined(HAVE_DRM_SEND_EVENT_LOCKED)
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e);
#endif

#ifndef _DRM_PRINTK
#define _DRM_PRINTK(once, level, fmt, ...)				\
	do {								\
		printk##once(KERN_##level "[" DRM_NAME "] " fmt,	\
			     ##__VA_ARGS__);				\
	} while (0)
#endif

#ifndef DRM_NOTE
#define DRM_NOTE(fmt, ...)						\
	_DRM_PRINTK(, NOTICE, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_WARN
#define DRM_WARN(fmt, ...)						\
	_DRM_PRINTK(, WARNING, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_NOTE_ONCE
#define DRM_NOTE_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, NOTICE, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_WARN_ONCE
#define DRM_WARN_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, WARNING, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_ERROR
#define DRM_ERROR(fmt, ...)                                            \
       drm_err(fmt, ##__VA_ARGS__)
#endif

#ifndef DP_LANE0_1_STATUS_ESI
#define DP_LANE0_1_STATUS_ESI                  0x200c /* status same as 0x202 */
#endif
#ifndef DP_LANE2_3_STATUS_ESI
#define DP_LANE2_3_STATUS_ESI                  0x200d /* status same as 0x203 */
#endif
#ifndef DP_LANE_ALIGN_STATUS_UPDATED_ESI
#define DP_LANE_ALIGN_STATUS_UPDATED_ESI       0x200e /* status same as 0x204 */
#endif
#ifndef DP_SINK_STATUS_ESI
#define DP_SINK_STATUS_ESI                     0x200f /* status same as 0x205 */
#endif

#if !defined(HAVE_DRM_ENCODER_FIND_VALID_WITH_FILE)
#define drm_encoder_find(dev, file, id) drm_encoder_find(dev, id)
#endif

#if !defined(HAVE_DRM_HELPER_FORCE_DISABLE_ALL)
static inline
int drm_helper_force_disable_all(struct drm_device *dev)
{
       return drm_crtc_force_disable_all(dev);
}
#endif

#ifndef HAVE_DRM_DEV_PUT
static inline void drm_dev_put(struct drm_device *dev)
{
	return drm_dev_unref(dev);
}
#endif

#if !defined(HAVE_DRM_CRTC_ACCURATE_VBLANK_COUNT)
static inline u64 drm_crtc_accurate_vblank_count(struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ACCURATE_VBLANK_COUNT)
	return drm_accurate_vblank_count(crtc);
#else
	pr_warn_once("drm_crtc_accurate_vblank_count is not supported");
	return 0;
#endif
}
#endif

#ifndef drm_for_each_plane
#define drm_for_each_plane(plane, dev) \
	list_for_each_entry(plane, &(dev)->mode_config.plane_list, head)
#endif

#ifndef drm_for_each_crtc
#define drm_for_each_crtc(crtc, dev) \
	list_for_each_entry(crtc, &(dev)->mode_config.crtc_list, head)
#endif

#ifndef drm_for_each_connector
#define drm_for_each_connector(connector, dev) \
	list_for_each_entry(connector, &(dev)->mode_config.connector_list, head)
#endif

#ifndef drm_for_each_encoder
#define drm_for_each_encoder(encoder, dev) \
	list_for_each_entry(encoder, &(dev)->mode_config.encoder_list, head)
#endif

#ifndef drm_for_each_fb
#define drm_for_each_fb(fb, dev) \
	list_for_each_entry(fb, &(dev)->mode_config.fb_list, head)
#endif

#ifndef HAVE_DRM_MODE_IS_420_XXX
static inline bool drm_mode_is_420_only(const struct drm_display_info *display,
			  const struct drm_display_mode *mode)
{
	/* DRM < 4.4 ,un-support this pixel format */
	printk_once(KERN_WARNING "This kernel version not support API: drm_mode_is_420_only!\n");
	return false;
}
static inline bool drm_mode_is_420_also(const struct drm_display_info *display,
			  const struct drm_display_mode *mode)
{
	printk_once(KERN_WARNING "This kernel version not support API: drm_mode_is_420_also!\n");
	return false;
}
#endif

#endif /* AMDKCL_DRM_H */
