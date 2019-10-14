#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <drm/drm_dp_helper.h>
#include <drm/drm_modes.h>
#if defined(HAVE_DRM_PRINTER)
#include <drm/drm_print.h>
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

#if !defined(HAVE_DRM_ATOMIC_HELPER_DISABLE_ALL)
int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_DUPLICATE_STATE)
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_SUSPEND_RESUME)
struct drm_atomic_state *drm_atomic_helper_suspend(struct drm_device *dev);
int drm_atomic_helper_resume(struct drm_device *dev,
			     struct drm_atomic_state *state);
#endif

#if !defined(HAVE_DRM_CRTC_FORCE_DISABLE_ALL)
extern int drm_crtc_force_disable(struct drm_crtc *crtc);
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
#endif

#if !defined(HAVE_DRM_IS_CURRENT_MASTER)
bool drm_is_current_master(struct drm_file *fpriv);
#endif

#if !defined(HAVE_DRM_ATOMIC_HELPER_CONNECTOR_RESET)
extern void
__kcl_drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state);

static inline void
__drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state)
{
	return __kcl_drm_atomic_helper_connector_reset(connector, conn_state);
}
#endif

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

static inline struct drm_crtc_state *
kcl_drm_atomic_get_old_crtc_state_before_commit(struct drm_atomic_state *state,
					    struct drm_crtc *crtc)
{
#if defined(HAVE_DRM_ATOMIC_GET_CRTC_STATE)
	return drm_atomic_get_old_crtc_state(state, crtc);
#elif defined(HAVE_DRM_CRTCS_STATE_MEMBER)
	return state->crtcs[drm_crtc_index(crtc)].ptr->state;
#else
	return state->crtcs[drm_crtc_index(crtc)]->state;
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
	return state->crtcs[drm_crtc_index(crtc)]->state;
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

#ifndef HAVE_DRM_FB_HELPER_FILL_INFO
void drm_fb_helper_fill_info(struct fb_info *info,
			     struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *sizes);
#endif

#ifndef HAVE_DRM_DEV_PUT
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
#if !defined(HAVE_DRM_PRINTER)
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

#ifndef HAVE_DRM_ATOMIC_HELPER_UPDATE_LEGACY_MODESET_STATE
extern void _kcl_drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state);

static inline void
drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state)
{
	_kcl_drm_atomic_helper_update_legacy_modeset_state(dev, old_state);
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
bool _kcl_drm_mode_is_420_only(const struct drm_display_info *display,
		const struct drm_display_mode *mode);
bool _kcl_drm_mode_is_420_also(const struct drm_display_info *display,
		const struct drm_display_mode *mode);

static inline bool drm_mode_is_420_only(const struct drm_display_info *display,
		const struct drm_display_mode *mode)
{
	return _kcl_drm_mode_is_420_only(display, mode);
}
static inline bool drm_mode_is_420_also(const struct drm_display_info *display,
		const struct drm_display_mode *mode)
{
	return _kcl_drm_mode_is_420_also(display, mode);
}
#endif

#ifndef DRM_ERROR
#define DRM_ERROR(fmt, ...)                                            \
       drm_err(fmt, ##__VA_ARGS__)
#endif

#endif /* AMDKCL_DRM_H */
