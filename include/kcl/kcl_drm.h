#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <linux/version.h>
#include <linux/kconfig.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_gem.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_rect.h>
#include <linux/ctype.h>
#include <linux/console.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
#define DP_ADJUST_REQUEST_POST_CURSOR2      0x20c

#define DP_TEST_MISC0                       0x232

#define DP_TEST_PHY_PATTERN                 0x248
#define DP_TEST_80BIT_CUSTOM_PATTERN_7_0    0x250
#define DP_TEST_80BIT_CUSTOM_PATTERN_15_8   0x251
#define DP_TEST_80BIT_CUSTOM_PATTERN_23_16  0x252
#define DP_TEST_80BIT_CUSTOM_PATTERN_31_24  0x253
#define DP_TEST_80BIT_CUSTOM_PATTERN_39_32  0x254
#define DP_TEST_80BIT_CUSTOM_PATTERN_47_40  0x255
#define DP_TEST_80BIT_CUSTOM_PATTERN_55_48  0x256
#define DP_TEST_80BIT_CUSTOM_PATTERN_63_56  0x257
#define DP_TEST_80BIT_CUSTOM_PATTERN_71_64  0x258
#define DP_TEST_80BIT_CUSTOM_PATTERN_79_72  0x259

#define DP_BRANCH_REVISION_START            0x509

#define DP_DP13_DPCD_REV                    0x2200
#define DP_DP13_MAX_LINK_RATE               0x2201
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 13, 0)
#define DRM_MODE_ROTATE_0       (1<<0)
#define DRM_MODE_ROTATE_90      (1<<1)
#define DRM_MODE_ROTATE_180     (1<<2)
#define DRM_MODE_ROTATE_270     (1<<3)

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
extern void (*_kcl_drm_fb_helper_release_fbi)(struct drm_fb_helper *fb_helper);
extern void (*_kcl_drm_fb_helper_set_suspend_unlocked)(struct drm_fb_helper *fb_helper, int state);
extern void
(*_kcl_drm_atomic_helper_update_legacy_modeset_state)(struct drm_device *dev,
					      struct drm_atomic_state *old_state);

#if DRM_VERSION_CODE < DRM_VERSION(4, 5, 0) && !(defined(OS_NAME_UBUNTU) || defined(OS_NAME_SLE))
int drm_modeset_lock_all_ctx(struct drm_device *dev,
			     struct drm_modeset_acquire_ctx *ctx);
int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#ifndef OS_NAME_RHEL_6
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
#endif
struct drm_atomic_state *drm_atomic_helper_suspend(struct drm_device *dev);
int drm_atomic_helper_resume(struct drm_device *dev,
			     struct drm_atomic_state *state);
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 8, 0)
extern int drm_crtc_force_disable(struct drm_crtc *crtc);
extern int drm_crtc_force_disable_all(struct drm_device *dev);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
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
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0) && \
		!defined(OS_NAME_RHEL_7_X)
#define IS_REACHABLE(option) __or(IS_BUILTIN(option), \
				__and(IS_MODULE(option), __is_defined(MODULE)))
#endif

#if !defined(OS_NAME_RHEL_7_X)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
static inline void
drm_fb_helper_remove_conflicting_framebuffers(struct apertures_struct *a,
					      const char *name, bool primary)
{
#if IS_REACHABLE(CONFIG_FB)
	remove_conflicting_framebuffers(a, name, primary);
#else
	return;
#endif
}
#else
static inline int
drm_fb_helper_remove_conflicting_framebuffers(struct apertures_struct *a,
					      const char *name, bool primary)
{
#if IS_REACHABLE(CONFIG_FB)
	return remove_conflicting_framebuffers(a, name, primary);
#else
	return 0;
#endif
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0) */
#endif
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 5, 0)
extern int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw);
#endif

static inline void kcl_drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_cfb_fillrect(info, rect);
#else
	drm_fb_helper_cfb_fillrect(info, rect);
#endif
}

static inline void kcl_drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_cfb_copyarea(info, area);
#else
	drm_fb_helper_cfb_copyarea(info, area);
#endif
}

static inline void kcl_drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_cfb_imageblit(info, image);
#else
	drm_fb_helper_cfb_imageblit(info, image);
#endif
}

static inline struct fb_info *kcl_drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)
{
#ifdef BUILD_AS_DKMS
	return _kcl_drm_fb_helper_alloc_fbi(fb_helper);
#else
	return drm_fb_helper_alloc_fbi(fb_helper);
#endif
}

static inline void kcl_drm_fb_helper_release_fbi(struct drm_fb_helper *fb_helper)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_release_fbi(fb_helper);
#else
#if DRM_VERSION_CODE < DRM_VERSION(4, 12, 0)
	drm_fb_helper_release_fbi(fb_helper);
#endif
#endif
}

static inline void kcl_drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_unregister_fbi(fb_helper);
#else
	drm_fb_helper_unregister_fbi(fb_helper);
#endif
}

static inline void kcl_drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper, int state)
{
#ifdef BUILD_AS_DKMS
	_kcl_drm_fb_helper_set_suspend_unlocked(fb_helper, state);
#else
	drm_fb_helper_set_suspend_unlocked(fb_helper, state);
#endif
}

static inline void
kcl_drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state)
{
#ifdef BUILD_AS_DKMS
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

static inline int kcl_drm_encoder_init(struct drm_device *dev,
		      struct drm_encoder *encoder,
		      const struct drm_encoder_funcs *funcs,
		      int encoder_type, const char *name, ...)
{
#if DRM_VERSION_CODE >= DRM_VERSION(4, 5, 0)
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
#if DRM_VERSION_CODE >= DRM_VERSION(4, 5, 0)
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
#if DRM_VERSION_CODE >= DRM_VERSION(4, 14, 0) || \
	defined(OS_NAME_SUSE_15)
		return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
				 formats, format_count, format_modifiers, type, name);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0) || \
		defined(OS_NAME_RHEL_7_3) || \
		defined(OS_NAME_RHEL_7_4)
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
#if DRM_VERSION_CODE < DRM_VERSION(4, 7, 0)
		return drm_gem_object_lookup(dev, filp, handle);
#else
		return drm_gem_object_lookup(filp, handle);
#endif
}

#define kcl_drm_for_each_plane(plane, dev) \
	list_for_each_entry(plane, &(dev)->mode_config.plane_list, head)

#define kcl_drm_for_each_crtc(crtc, dev) \
	list_for_each_entry(crtc, &(dev)->mode_config.crtc_list, head)

#define kcl_drm_for_each_connector(connector, dev) \
	for (kcl_assert_drm_connector_list_read_locked(&(dev)->mode_config),	\
	     connector = list_first_entry(&(dev)->mode_config.connector_list,	\
					  struct drm_connector, head);		\
	     &connector->head != (&(dev)->mode_config.connector_list);		\
	     connector = list_next_entry(connector, head))

static inline void
kcl_assert_drm_connector_list_read_locked(struct drm_mode_config *mode_config)
{
	/*
	 * The connector hotadd/remove code currently grabs both locks when
	 * updating lists. Hence readers need only hold either of them to be
	 * safe and the check amounts to
	 *
	 * WARN_ON(not_holding(A) && not_holding(B)).
	 */
	WARN_ON(!mutex_is_locked(&mode_config->mutex) &&
			!drm_modeset_is_locked(&mode_config->connection_mutex));
}

static inline int
kcl_drm_calc_vbltimestamp_from_scanoutpos(struct drm_device *dev,
					  unsigned int pipe,
					  int *max_error,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
					  ktime_t *vblank_time,
#else
					  struct timeval *vblank_time,
#endif
#if DRM_VERSION_CODE < DRM_VERSION(4, 13, 0) && \
	!defined(OS_NAME_SUSE_15)
					  unsigned flags,
#else
					  bool in_vblank_irq,
#endif
					  const struct drm_crtc *refcrtc,
					  const struct drm_display_mode *mode)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0) && \
	!defined(OS_NAME_RHEL_6) && \
	!defined(OS_NAME_RHEL_7_3) && \
	!defined(OS_NAME_RHEL_7_X)
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error, vblank_time,
						     flags, refcrtc, mode);
#elif DRM_VERSION_CODE < DRM_VERSION(4, 13, 0) && \
	!defined(OS_NAME_SUSE_15)
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error, vblank_time,
						     flags, mode);
#else
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error, vblank_time, in_vblank_irq);
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#if !defined(OS_NAME_RHEL_7_X)
/**
 * struct drm_format_name_buf - name of a DRM format
 * @str: string buffer containing the format name
 */
struct drm_format_name_buf {
	char str[32];
};
#endif

static char printable_char(int c)
{
	return isascii(c) && isprint(c) ? c : '?';
}
#endif

static inline const char *kcl_drm_get_format_name(uint32_t format, struct drm_format_name_buf *buf)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	snprintf(buf->str, sizeof(buf->str),
		 "%c%c%c%c %s-endian (0x%08x)",
		 printable_char(format & 0xff),
		 printable_char((format >> 8) & 0xff),
		 printable_char((format >> 16) & 0xff),
		 printable_char((format >> 24) & 0x7f),
		 format & DRM_FORMAT_BIG_ENDIAN ? "big" : "little",
		 format);

	return buf->str;
#else
	return drm_get_format_name(format, buf);
#endif
}

static inline void kcl_drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
#if DRM_VERSION_CODE < DRM_VERSION(4, 12, 0)
	return drm_gem_object_unreference_unlocked(obj);
#else
	return drm_gem_object_put_unlocked(obj);
#endif
}

#ifdef BUILD_AS_DKMS
extern struct dma_buf_ops *_kcl_drm_gem_prime_dmabuf_ops;
#define drm_gem_prime_dmabuf_ops (*_kcl_drm_gem_prime_dmabuf_ops)
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 8, 0)
bool drm_is_current_master(struct drm_file *fpriv);
#endif

static inline struct drm_crtc_state *
kcl_drm_atomic_get_old_crtc_state_before_commit(struct drm_atomic_state *state,
					    struct drm_crtc *crtc)
{
#if DRM_VERSION_CODE >= DRM_VERSION(4, 12, 0)
	return drm_atomic_get_old_crtc_state(state, crtc);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0) || defined(OS_NAME_RHEL_7_X)
	return state->crtcs[drm_crtc_index(crtc)].ptr->state;
#else
	return state->crtcs[drm_crtc_index(crtc)]->state;
#endif
}

static inline struct drm_crtc_state *
kcl_drm_atomic_get_old_crtc_state_after_commit(struct drm_atomic_state *state,
				  struct drm_crtc *crtc)
{
#if DRM_VERSION_CODE >= DRM_VERSION(4, 12, 0)
	return drm_atomic_get_old_crtc_state(state, crtc);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0) || defined(OS_NAME_RHEL_7_X)
	return state->crtcs[drm_crtc_index(crtc)].state;
#else
	return state->crtc_states[drm_crtc_index(crtc)];
#endif
}

static inline struct drm_crtc_state *
kcl_drm_atomic_get_new_crtc_state_before_commit(struct drm_atomic_state *state,
				  struct drm_crtc *crtc)
{
#if DRM_VERSION_CODE >= DRM_VERSION(4, 12, 0)
	return drm_atomic_get_new_crtc_state(state,crtc);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0) || defined(OS_NAME_RHEL_7_X)
	return state->crtcs[drm_crtc_index(crtc)].state;
#else
	return state->crtc_states[drm_crtc_index(crtc)];
#endif
}

static inline struct drm_crtc_state *
kcl_drm_atomic_get_new_crtc_state_after_commit(struct drm_atomic_state *state,
					    struct drm_crtc *crtc)
{
#if DRM_VERSION_CODE >= DRM_VERSION(4, 12, 0)
	return drm_atomic_get_new_crtc_state(state,crtc);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0) || defined(OS_NAME_RHEL_7_X)
	return state->crtcs[drm_crtc_index(crtc)].ptr->state;
#else
	return state->crtcs[drm_crtc_index(crtc)]->state;
#endif
}
static inline struct drm_plane_state *
kcl_drm_atomic_get_new_plane_state_before_commit(struct drm_atomic_state *state,
							struct drm_plane *plane)
{
#if DRM_VERSION_CODE >= DRM_VERSION(4, 12, 0)
	return drm_atomic_get_new_plane_state(state,plane);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0) || defined(OS_NAME_RHEL_7_X)
	return state->planes[drm_plane_index(plane)].state;
#else
	return state->plane_states[drm_plane_index(plane)];
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0) && \
	!defined(OS_NAME_RHEL_7_X)
extern void
__kcl_drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state);
#endif

static inline void
kcl_drm_atomic_helper_connector_reset(struct drm_connector *connector,
				    struct drm_connector_state *conn_state)
{
#if DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
	return __kcl_drm_atomic_helper_connector_reset(connector, conn_state);
#else
	return __drm_atomic_helper_connector_reset(connector, conn_state);
#endif
}

#if DRM_VERSION_CODE < DRM_VERSION(4, 17, 0) && defined(BUILD_AS_DKMS)
u64 drm_get_max_iomem(void);
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
#define DRM_MODE_FMT	"%d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x"
#define DRM_MODE_ARG(m)	\
	(m)->base.id, (m)->name, (m)->vrefresh, (m)->clock, \
	(m)->hdisplay, (m)->hsync_start, (m)->hsync_end, (m)->htotal, \
	(m)->vdisplay, (m)->vsync_start, (m)->vsync_end, (m)->vtotal, \
	(m)->type, (m)->flags

#define DRM_RECT_FMT	"%dx%d%+d%+d"
#define DRM_RECT_ARG(r) drm_rect_width(r), drm_rect_height(r), (r)->x1, (r)->y1

#define DRM_RECT_FP_FMT	"%d.%06ux%d.%06u%+d.%06u%+d.%06u"
#define DRM_RECT_FP_ARG(r) \
	drm_rect_width(r) >> 16, ((drm_rect_width(r) & 0xffff) * 15625) >> 10, \
	drm_rect_height(r) >> 16, ((drm_rect_height(r) & 0xffff) * 15625) >> 10, \
	(r)->x1 >> 16, (((r)->x1 & 0xffff) * 15625) >> 10, \
	(r)->y1 >> 16, (((r)->y1 & 0xffff) * 15625) >> 10

static inline struct drm_rect
drm_plane_state_src(const struct drm_plane_state *state)
{
	struct drm_rect src = {
		.x1 = state->src_x,
		.y1 = state->src_y,
		.x2 = state->src_x + state->src_w,
		.y2 = state->src_y + state->src_h,
	};
	return src;
}

static inline struct drm_rect
drm_plane_state_dest(const struct drm_plane_state *state)
{
	struct drm_rect dest = {
		.x1 = state->crtc_x,
		.y1 = state->crtc_y,
		.x2 = state->crtc_x + state->crtc_w,
		.y2 = state->crtc_y + state->crtc_h,
	};
	return dest;
}

struct drm_printer {
	void (*printfn)(struct drm_printer *p, struct va_format *vaf);
	void *arg;
};

void __drm_printfn_info(struct drm_printer *p, struct va_format *vaf);
void drm_printf(struct drm_printer *p, const char *f, ...);

static inline struct drm_printer drm_info_printer(struct device *dev)
{
	struct drm_printer p = {
		.printfn = __drm_printfn_info,
		.arg = dev,
	};
	return p;
}

void drm_state_dump(struct drm_device *dev, struct drm_printer *p);
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 6, 0)
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e);
void drm_send_event(struct drm_device *dev, struct drm_pending_event *e);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0) && \
		defined(BUILD_AS_DKMS)

#define _DRM_PRINTK(once, level, fmt, ...)				\
	do {								\
		printk##once(KERN_##level "[" DRM_NAME "] " fmt,	\
			     ##__VA_ARGS__);				\
	} while (0)

#define DRM_NOTE(fmt, ...)						\
	_DRM_PRINTK(, NOTICE, fmt, ##__VA_ARGS__)
#define DRM_WARN(fmt, ...)						\
	_DRM_PRINTK(, WARNING, fmt, ##__VA_ARGS__)

#define DRM_NOTE_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, NOTICE, fmt, ##__VA_ARGS__)
#define DRM_WARN_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, WARNING, fmt, ##__VA_ARGS__)

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
#define DP_LANE0_1_STATUS_ESI                  0x200c /* status same as 0x202 */
#define DP_LANE2_3_STATUS_ESI                  0x200d /* status same as 0x203 */
#define DP_LANE_ALIGN_STATUS_UPDATED_ESI       0x200e /* status same as 0x204 */
#define DP_SINK_STATUS_ESI                     0x200f /* status same as 0x205 */
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 15, 0) && \
	!defined(OS_NAME_SUSE_15)
#define drm_encoder_find(dev, file, id) drm_encoder_find(dev, id)
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 9, 0)
#define DRM_DEV_DEBUG	dev_dbg
#define DRM_DEV_ERROR	dev_err
#endif

#endif /* AMDKCL_DRM_H */
