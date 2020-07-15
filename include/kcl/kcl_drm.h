/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <linux/kconfig.h>
#include <linux/ctype.h>
#include <linux/console.h>

#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_vma_manager.h>
#include <kcl/header/kcl_drmP_h.h>
#include <drm/drm_gem.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_rect.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_modes.h>
#include <kcl/header/kcl_drm_print_h.h>
#if defined(HAVE_DRM_COLOR_LUT_SIZE)
#include <drm/drm_color_mgmt.h>
#endif
#include <uapi/drm/drm_mode.h>
#include <kcl/header/kcl_drm_drv_h.h>

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

/* helper for handling conditionals in various for_each macros */
#ifndef for_each_if
#define for_each_if(condition) if (!(condition)) {} else
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

#if !defined(HAVE_DRM_MODESET_LOCK_ALL_CTX)
int drm_modeset_lock_all_ctx(struct drm_device *dev,
			     struct drm_modeset_acquire_ctx *ctx);
#endif

#if !defined(HAVE_DRM_CRTC_FORCE_DISABLE_ALL)
extern int drm_crtc_force_disable(struct drm_crtc *crtc);
extern int drm_crtc_force_disable_all(struct drm_device *dev);
#endif

#ifndef HAVE_DRM_CRTC_FROM_INDEX
struct drm_crtc *_kcl_drm_crtc_from_index(struct drm_device *dev, int idx);
static inline struct drm_crtc *
drm_crtc_from_index(struct drm_device *dev, int idx)
{
	return _kcl_drm_crtc_from_index(dev, idx);
}
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
#endif /*IS_REACHABLE*/

extern int remove_conflicting_pci_framebuffers(struct pci_dev *pdev, int res_id,
					       const char *name);
static inline int
_kcl_drm_fb_helper_remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
						  const char *name)
{
#if IS_REACHABLE(CONFIG_FB)
	return remove_conflicting_pci_framebuffers(pdev, 0, name);
#else
	return 0;
#endif
}
#elif !defined(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PP)
static inline int
_kcl_drm_fb_helper_remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
						  const char *name)
{
	return drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, 0, name);
}
#endif

#if !defined(HAVE_DRM_GEM_OBJECT_PUT_UNLOCKED)
static inline void
drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	return drm_gem_object_unreference_unlocked(obj);
}

static inline void
drm_gem_object_get(struct drm_gem_object *obj)
{
	kref_get(&obj->refcount);
}
#endif

#if !defined(HAVE_DRM_IS_CURRENT_MASTER)
bool drm_is_current_master(struct drm_file *fpriv);
#endif

#if !defined(HAVE_DRM_GET_MAX_IOMEM)
u64 drm_get_max_iomem(void);
#endif

#if !defined(HAVE_DRM_DRM_PRINT_H)
struct drm_printer {
	void (*printfn)(struct drm_printer *p, struct va_format *vaf);
	void *arg;
	const char *prefix;
};

void drm_printf(struct drm_printer *p, const char *f, ...);
#endif

#if !defined(HAVE_DRM_SEND_EVENT_LOCKED)
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e);
#endif

/**
 * drm_color_lut_size - calculate the number of entries in the LUT
 * @blob: blob containing the LUT
 *
 * Returns:
 * The number of entries in the color LUT stored in @blob.
 */
#if defined(HAVE_DRM_COLOR_LUT) && !defined(HAVE_DRM_COLOR_LUT_SIZE)
static inline int drm_color_lut_size(const struct drm_property_blob *blob)
{
	return blob->length / sizeof(struct drm_color_lut);
}
#endif

#ifndef HAVE_DRM_FB_HELPER_FILL_INFO
void drm_fb_helper_fill_info(struct fb_info *info,
			     struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *sizes);
#endif

#ifndef HAVE_DRM_DEV_PUT
static inline void drm_dev_get(struct drm_device *dev)
{
	drm_dev_ref(dev);
}

static inline void drm_dev_put(struct drm_device *dev)
{
	return drm_dev_unref(dev);
}
#endif

/**
 * drm_debug_printer - construct a &drm_printer that outputs to pr_debug()
 * @prefix: debug output prefix
 *
 * RETURNS:
 * The &drm_printer object
 */
#if !defined(HAVE_DRM_DEBUG_PRINTER)
extern void __drm_printfn_debug(struct drm_printer *p, struct va_format *vaf);

static inline struct drm_printer drm_debug_printer(const char *prefix)
{
	struct drm_printer p = {
		.printfn = __drm_printfn_debug,
#if !defined(HAVE_DRM_DRM_PRINT_H)
		.prefix = prefix
#endif
	};
	return p;
}
#endif

#ifndef HAVE_DRM_FB_HELPER_CFB_XX
extern void _kcl_drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect);
extern void _kcl_drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area);
extern void _kcl_drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image);

static inline
void drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect)
{
	_kcl_drm_fb_helper_cfb_fillrect(info, rect);
}

static inline
void drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
	_kcl_drm_fb_helper_cfb_copyarea(info, area);
}

static inline
void drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
	_kcl_drm_fb_helper_cfb_imageblit(info, image);
}
#endif

#ifndef HAVE_DRM_FB_HELPER_XX_FBI
extern struct fb_info *_kcl_drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper);
extern void _kcl_drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper);

static inline
struct fb_info *drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)

{
	return _kcl_drm_fb_helper_alloc_fbi(fb_helper);
}

static inline
void drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
	_kcl_drm_fb_helper_unregister_fbi(fb_helper);
}
#endif

#ifndef HAVE_DRM_FB_HELPER_SET_SUSPEND_UNLOCKED
extern void _kcl_drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper, int state);
static inline
void drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper,
					bool suspend)

{
	_kcl_drm_fb_helper_set_suspend_unlocked(fb_helper, suspend);
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

#ifndef HAVE_DRM_MODE_IS_420_XXX
bool drm_mode_is_420_only(const struct drm_display_info *display,
		const struct drm_display_mode *mode);
bool drm_mode_is_420_also(const struct drm_display_info *display,
		const struct drm_display_mode *mode);
#endif

#ifndef _DRM_PRINTK
#define _DRM_PRINTK(once, level, fmt, ...)				\
	do {								\
		printk##once(KERN_##level "[" DRM_NAME "] " fmt,	\
			     ##__VA_ARGS__);				\
	} while (0)
#endif

#ifndef DRM_WARN
#define DRM_WARN(fmt, ...)						\
	_DRM_PRINTK(, WARNING, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_WARN_ONCE
#define DRM_WARN_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, WARNING, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_NOTE
#define DRM_NOTE(fmt, ...)						\
	_DRM_PRINTK(, NOTICE, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_NOTE_ONCE
#define DRM_NOTE_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, NOTICE, fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_ERROR
#define DRM_ERROR(fmt, ...)                                            \
       drm_err(fmt, ##__VA_ARGS__)
#endif

#if !defined(DRM_DEV_DEBUG)
#define DRM_DEV_DEBUG(dev, fmt, ...)					\
	DRM_DEBUG(fmt, ##__VA_ARGS__)
#endif

#if !defined(DRM_DEV_ERROR)
#define DRM_DEV_ERROR(dev, fmt, ...)					\
	DRM_ERROR(fmt, ##__VA_ARGS__)
#endif

#ifndef DRM_DEBUG_VBL
#define DRM_UT_VBL		0x20
#define DRM_DEBUG_VBL(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_VBL))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#endif

/*
 * Don't add fb_debug_* since the legacy drm_fb_helper_debug_* has segfault
 * history:
 * v2.6.35-21-gd219adc1228a fb: add hooks to handle KDB enter/exit
 * v2.6.35-22-g1a7aba7f4e45 drm: add KGDB/KDB support
 * v4.8-rc8-1391-g74064893901a drm/fb-helper: add DRM_FB_HELPER_DEFAULT_OPS for fb_ops
 * v4.9-rc4-808-g1e0089288b9b drm/fb-helper: add fb_debug_* to DRM_FB_HELPER_DEFAULT_OPS
 * v4.9-rc4-807-g1b99b72489c6 drm/fb-helper: fix segfaults in drm_fb_helper_debug_*
 * v4.10-rc8-1367-g0f3bbe074dd1 drm/fb-helper: implement ioctl FBIO_WAITFORVSYNC
 */
#ifndef DRM_FB_HELPER_DEFAULT_OPS
#define DRM_FB_HELPER_DEFAULT_OPS \
	.fb_check_var	= drm_fb_helper_check_var, \
	.fb_set_par	= drm_fb_helper_set_par, \
	.fb_setcmap	= drm_fb_helper_setcmap, \
	.fb_blank	= drm_fb_helper_blank, \
	.fb_pan_display	= drm_fb_helper_pan_display
#endif

#if !defined(HAVE_DRM_HELPER_FORCE_DISABLE_ALL)
static inline
int drm_helper_force_disable_all(struct drm_device *dev)
{
       return drm_crtc_force_disable_all(dev);
}
#endif

#endif /* AMDKCL_DRM_H */
