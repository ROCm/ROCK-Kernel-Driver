/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef DC_INTERFACE_H_
#define DC_INTERFACE_H_

#include "dc_types.h"
#include "dal_types.h"
#include "grph_object_defs.h"
#include "logger_types.h"
#include "gpio_types.h"
#include "link_service_types.h"

#define MAX_TARGETS 6
#define MAX_SURFACES 6
#define MAX_SINKS_PER_LINK 4

/*******************************************************************************
 * Display Core Interfaces
 ******************************************************************************/

struct dc_caps {
	uint32_t max_targets;
	uint32_t max_links;
	uint32_t max_audios;
};

struct dc;
struct dc_surface;

struct dc_stream_funcs {
	bool (*dc_stream_adjust_vmin_vmax)(struct dc *dc,
			const struct dc_stream **stream, int num_streams, int vmin, int vmax);
};

struct dc {
	struct dc_caps caps;
	struct dc_stream_funcs stream_funcs;
};

struct dc_init_data {
	struct hw_asic_id asic_id;
	struct dal_override_parameters display_param;
	void *driver; /* ctx */
	struct cgs_device *cgs_device;

	uint8_t num_virtual_links;
	/*
	 * If 'vbios_override' not NULL, it will be called instead
	 * of the real VBIOS. Intended use is Diagnostics on FPGA.
	 */
	struct dc_bios *vbios_override;
	enum dce_environment dce_environment;
};

struct dc *dc_create(const struct dc_init_data *init_params);
void dc_destroy(struct dc **dc);

/*******************************************************************************
 * Surface Interfaces
 ******************************************************************************/

enum {
	RGB_256X3X16 = 256,
	FLOAT_GAMMA_RAMP_MAX = 1025
};

enum dc_gamma_ramp_type {
	GAMMA_RAMP_RBG256X3X16,
	GAMMA_RAMP_FLOAT,
};

struct float_rgb {
	struct fixed32_32 red;
	struct fixed32_32 green;
	struct fixed32_32 blue;
};

struct dc_gamma_ramp_float {
	struct float_rgb scale;
	struct float_rgb offset;
	struct float_rgb gamma_curve[FLOAT_GAMMA_RAMP_MAX];
};

struct dc_gamma_ramp_rgb256x3x16 {
	uint16_t red[RGB_256X3X16];
	uint16_t green[RGB_256X3X16];
	uint16_t blue[RGB_256X3X16];
};

struct dc_gamma {
	enum dc_gamma_ramp_type type;
	union {
		struct dc_gamma_ramp_rgb256x3x16 gamma_ramp_rgb256x3x16;
		struct dc_gamma_ramp_float gamma_ramp_float;
	};
	uint32_t size;
};

struct dc_surface {
	bool visible;
	bool flip_immediate;
	struct dc_plane_address address;

	struct scaling_taps scaling_quality;
	struct rect src_rect;
	struct rect dst_rect;
	struct rect clip_rect;

	union plane_size plane_size;
	struct dc_tiling_info tiling_info;
	enum dc_color_space color_space;

	enum surface_pixel_format format;
	enum dc_rotation_angle rotation;
	enum plane_stereo_format stereo_format;

	struct dc_gamma *gamma_correction;
};

/*
 * This structure is filled in by dc_surface_get_status and contains
 * the last requested address and the currently active address so the called
 * can determine if there are any outstanding flips
 */
struct dc_surface_status {
	struct dc_plane_address requested_address;
	struct dc_plane_address current_address;
};

/*
 * Create a new surface with default parameters;
 */
struct dc_surface *dc_create_surface(const struct dc *dc);
const struct dc_surface_status* dc_surface_get_status(
						struct dc_surface *dc_surface);

void dc_surface_retain(const struct dc_surface *dc_surface);
void dc_surface_release(const struct dc_surface *dc_surface);

void dc_gamma_release(const struct dc_gamma *dc_gamma);
struct dc_gamma *dc_create_gamma(const struct dc *dc);

/*
 * This structure holds a surface address.  There could be multiple addresses
 * in cases such as Stereo 3D, Planar YUV, etc.  Other per-flip attributes such
 * as frame durations and DCC format can also be set.
 */
struct dc_flip_addrs {
	struct dc_plane_address address;
	bool flip_immediate;
	/* TODO: DCC format info */
	/* TODO: add flip duration for FreeSync */
};

/*
 * Optimized flip address update function.
 *
 * After this call:
 *   Surface addresses and flip attributes are programmed.
 *   Surface flip occur at next configured time (h_sync or v_sync flip)
 */
void dc_flip_surface_addrs(struct dc *dc,
		const struct dc_surface *const surfaces[],
		struct dc_flip_addrs flip_addrs[],
		uint32_t count);

/*
 * Set up surface attributes and associate to a target
 * The surfaces parameter is an absolute set of all surface active for the target.
 * If no surfaces are provided, the target will be blanked; no memory read.
 * Any flip related attribute changes must be done through this interface.
 *
 * After this call:
 *   Surfaces attributes are programmed and configured to be composed into target.
 *   This does not trigger a flip.  No surface address is programmed.
 */
bool dc_commit_surfaces_to_target(
		struct dc *dc,
		struct dc_surface *dc_surfaces[],
		uint8_t surface_count,
		struct dc_target *dc_target);

/*******************************************************************************
 * Target Interfaces
 ******************************************************************************/
#define MAX_STREAM_NUM 1

struct dc_target {
	uint8_t stream_count;
	const struct dc_stream *streams[MAX_STREAM_NUM];
};

/*
 * Target status is returned from dc_target_get_status in order to get the
 * the IRQ source, current frame counter and currently attached surfaces.
 */
struct dc_target_status {
	enum dc_irq_source page_flip_src;
	enum dc_irq_source v_update_src;
	uint32_t cur_frame_count;
	const struct dc_surface *surfaces[MAX_SURFACE_NUM];
	uint8_t surface_count;
};

struct dc_target *dc_create_target_for_streams(
		struct dc_stream *dc_streams[],
		uint8_t stream_count);

/*
 * Get the current target status.
 */
const struct dc_target_status *dc_target_get_status(
					const struct dc_target* dc_target);

void dc_target_retain(struct dc_target *dc_target);
void dc_target_release(struct dc_target *dc_target);
void dc_target_log(
	const struct dc_target *dc_target,
	struct dal_logger *dal_logger,
	enum log_major log_major,
	enum log_minor log_minor);

uint8_t dc_get_current_target_count(const struct dc *dc);
struct dc_target *dc_get_target_at_index(const struct dc *dc, uint8_t i);

bool dc_target_is_connected_to_sink(
		const struct dc_target *dc_target,
		const struct dc_sink *dc_sink);

uint32_t dc_target_get_vblank_counter(const struct dc_target *dc_target);

/* TODO: Return parsed values rather than direct register read
 * This has a dependency on the caller (amdgpu_get_crtc_scanoutpos)
 * being refactored properly to be dce-specific
 */
uint32_t dc_target_get_scanoutpos(
		const struct dc_target *dc_target,
		uint32_t *vbl,
		uint32_t *position);

enum dc_irq_source dc_target_get_irq_src(
	const struct dc *dc,
	const struct dc_target *dc_target,
	const enum irq_type irq_type);

/*
 * Structure to store surface/target associations for validation
 */
struct dc_validation_set {
	const struct dc_target *target;
	const struct dc_surface *surfaces[MAX_SURFACES];
	uint8_t surface_count;
};

/*
 * This function takes a set of resources and checks that they are cofunctional.
 *
 * After this call:
 *   No hardware is programmed for call.  Only validation is done.
 */
bool dc_validate_resources(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count);

/*
 * Set up streams and links associated to targets to drive sinks
 * The targets parameter is an absolute set of all active targets.
 *
 * After this call:
 *   Phy, Encoder, Timing Generator are programmed and enabled.
 *   New targets are enabled with blank stream; no memory read.
 */
bool dc_commit_targets(
		struct dc *dc,
		struct dc_target *targets[],
		uint8_t target_count);

/*******************************************************************************
 * Stream Interfaces
 ******************************************************************************/
struct dc_stream {
	const struct dc_sink *sink;
	struct dc_crtc_timing timing;

	struct rect src; /* viewport in target space*/
	struct rect dst; /* stream addressable area */

	struct audio_info audio_info;

	bool ignore_msa_timing_param;

	/* TODO: dithering */
	/* TODO: transfer function (CSC/regamma/gamut remap) */
	/* TODO: custom INFO packets */
	/* TODO: ABM info (DMCU) */
	/* TODO: PSR info */
	/* TODO: CEA VIC */
};

/**
 * Create a new default stream for the requested sink
 */
struct dc_stream *dc_create_stream_for_sink(const struct dc_sink *dc_sink);

void dc_stream_retain(struct dc_stream *dc_stream);
void dc_stream_release(struct dc_stream *dc_stream);

void dc_update_stream(const struct dc_stream *dc_stream,
		struct rect *src, struct rect *dst);

struct dc_stream_status {
	/*
	 * link this stream passes through
	 */
	const struct dc_link *link;
};

const struct dc_stream_status *dc_stream_get_status(
	const struct dc_stream *dc_stream);

/*******************************************************************************
 * Link Interfaces
 ******************************************************************************/

/*
 * A link contains one or more sinks and their connected status.
 * The currently active signal type (HDMI, DP-SST, DP-MST) is also reported.
 */
struct dc_link {
	const struct dc_sink *remote_sinks[MAX_SINKS_PER_LINK];
	unsigned int sink_count;
	const struct dc_sink *local_sink;
	unsigned int link_index;
	enum dc_connection_type type;
	enum signal_type connector_signal;
	enum dc_irq_source irq_source_hpd;
	enum dc_irq_source irq_source_hpd_rx;/* aka DP Short Pulse  */
	/* caps is the same as reported_link_cap. link_traing use
	 * reported_link_cap. Will clean up.  TODO
	 */
	struct dc_link_settings reported_link_cap;
	struct dc_link_settings verified_link_cap;
	struct dc_link_settings max_link_setting;
	struct dc_link_settings cur_link_settings;
	struct dc_lane_settings cur_lane_setting;

	uint8_t ddc_hw_inst;
	uint8_t link_enc_hw_inst;
};

/*
 * Return an enumerated dc_link.  dc_link order is constant and determined at
 * boot time.  They cannot be created or destroyed.
 * Use dc_get_caps() to get number of links.
 */
const struct dc_link *dc_get_link_at_index(struct dc *dc, uint32_t link_index);

/* Return id of physical connector represented by a dc_link at link_index.*/
const struct graphics_object_id dc_get_link_id_at_index(
		struct dc *dc, uint32_t link_index);

/* Set backlight level of an embedded panel (eDP, LVDS). */
bool dc_link_set_backlight_level(const struct dc_link *dc_link, uint32_t level);

/* Request DC to detect if there is a Panel connected.
 * boot - If this call is during initial boot.
 * Return false for any type of detection failure or MST detection
 * true otherwise. True meaning further action is required (status update
 * and OS notification).
 */
bool dc_link_detect(const struct dc_link *dc_link, bool boot);

/* Notify DC about DP RX Interrupt (aka Short Pulse Interrupt).
 * Return:
 * true - Downstream port status changed. DM should call DC to do the
 * detection.
 * false - no change in Downstream port status. No further action required
 * from DM. */
bool dc_link_handle_hpd_rx_irq(const struct dc_link *dc_link);

bool dc_link_add_remote_sink(const struct dc_link *link, struct dc_sink *sink);

void dc_link_remove_remote_sink(
	const struct dc_link *link,
	const struct dc_sink *sink);

/* Used by diagnostics for virtual link at the moment */
void dc_link_set_sink(const struct dc_link *link, struct dc_sink *sink);

/*******************************************************************************
 * Sink Interfaces - A sink corresponds to a display output device
 ******************************************************************************/

/*
 * The sink structure contains EDID and other display device properties
 */
struct dc_sink {
	enum signal_type sink_signal;
	struct dc_edid dc_edid; /* raw edid */
	struct dc_edid_caps edid_caps; /* parse display caps */
};

void dc_sink_retain(const struct dc_sink *sink);
void dc_sink_release(const struct dc_sink *sink);

const struct audio **dc_get_audios(struct dc *dc);

struct dc_sink_init_data {
	enum signal_type sink_signal;
	const struct dc_link *link;
	uint32_t dongle_max_pix_clk;
	bool converter_disable_audio;
};

struct dc_sink *dc_sink_create(const struct dc_sink_init_data *init_params);

/*******************************************************************************
 * Cursor interfaces - To manages the cursor within a target
 ******************************************************************************/
/* TODO: Deprecated once we switch to dc_set_cursor_position */
bool dc_target_set_cursor_attributes(
	struct dc_target *dc_target,
	const struct dc_cursor_attributes *attributes);

bool dc_target_set_cursor_position(
	struct dc_target *dc_target,
	const struct dc_cursor_position *position);

/* Newer interfaces  */
struct dc_cursor {
	struct dc_plane_address address;
	struct dc_cursor_attributes attributes;
};

/*
 * Create a new cursor with default values for a given target.
 */
struct dc_cursor *dc_create_cursor_for_target(
		const struct dc *dc,
		struct dc_target *dc_target);

/**
 * Commit cursor attribute changes such as pixel format and dimensions and
 * surface address.
 *
 * After this call:
 *   Cursor address and format is programmed to the new values.
 *   Cursor position is unmodified.
 */
bool dc_commit_cursor(
		const struct dc *dc,
		struct dc_cursor *cursor);

/*
 * Optimized cursor position update
 *
 * After this call:
 *   Cursor position will be programmed as well as enable/disable bit.
 */
bool dc_set_cursor_position(
		const struct dc *dc,
		struct dc_cursor *cursor,
		struct dc_cursor_position *pos);

/*******************************************************************************
 * Interrupt interfaces
 ******************************************************************************/
enum dc_irq_source dc_interrupt_to_irq_source(
		struct dc *dc,
		uint32_t src_id,
		uint32_t ext_id);
void dc_interrupt_set(const struct dc *dc, enum dc_irq_source src, bool enable);
void dc_interrupt_ack(struct dc *dc, enum dc_irq_source src);
const enum dc_irq_source dc_get_hpd_irq_source_at_index(
		struct dc *dc, uint32_t link_index);
const struct dc_target *dc_get_target_on_irq_source(
		const struct dc *dc,
		enum dc_irq_source src);

/*******************************************************************************
 * Power Interfaces
 ******************************************************************************/

void dc_set_power_state(
		struct dc *dc,
		enum dc_acpi_cm_power_state power_state,
		enum dc_video_power_state video_power_state);
void dc_resume(const struct dc *dc);

/*******************************************************************************
 * DDC Interfaces
 ******************************************************************************/

const struct ddc_service *dc_get_ddc_at_index(
		struct dc *dc, uint32_t link_index);

/*
 * DPCD access interfaces
 */

bool dc_read_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		uint8_t *data,
		uint32_t size);

bool dc_write_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		const uint8_t *data,
	uint32_t size);

bool dc_submit_i2c(
		struct dc *dc,
		uint32_t link_index,
		struct i2c_command *cmd);

#endif /* DC_INTERFACE_H_ */
