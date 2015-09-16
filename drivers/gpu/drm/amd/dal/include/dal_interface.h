/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_INTERFACE_H__
#define __DAL_INTERFACE_H__

#include "dal_types.h"
#include "dal_asic_id.h"
#include "set_mode_types.h"
#include "plane_types.h"
#include "dcs_types.h"
#include "timing_list_query_interface.h"

struct dal;
struct dal_to_path_mode_set;
struct surface_attributes;
struct surface_address;
struct view_port_area;
struct dal_timing_list_query;
struct topology_mgr;
struct power_to_dal_info;
struct address_info;
struct wifi_display_caps;
struct path_mode;
struct path_mode_set;
struct amdgpu_device;
struct amdgpu_mode_mc_save;

struct dal *dal_create(struct dal_init_data *init);
void dal_destroy(struct dal **dal);

/**
 *     Enable the DAL instance after resume from (S3/S4)
 *
 *     Enables instance of the DAL specific to an adapter after resume.
 *     The function sets up the display mapping based on appropriate boot-up
 *     behaviour.
 */
void dal_resume(struct dal *dal);

/* HW Capability queries */
uint32_t dal_get_controllers_number(struct dal *dal);
uint32_t dal_get_connected_targets_vector(struct dal *dal);
uint32_t dal_get_cofunctional_targets_number(struct dal *dal);

/* Return a bitvector of *all* Display Paths. */
uint32_t dal_get_supported_displays_vector(struct dal *dal);
/* Return a bitvector of a *single* Display Path. */
uint32_t dal_get_display_vector_by_index(struct dal *dal,
		uint32_t display_index);

uint32_t dal_get_a_display_index_by_type(
	struct dal *dal,
	uint32_t display_type);

enum signal_type  dal_get_display_signal(
	struct dal *dal,
	uint32_t display_index);

const uint8_t *dal_get_display_edid(
	struct dal *dal,
	uint32_t display_index,
	uint32_t *buff_size);

void dal_get_screen_info(
	struct dal *dal,
	uint32_t display_index,
	struct edid_screen_info *screen_info);

/* TODO: implement later
bool dal_get_display_output_descriptor (struct dal *dal,
		uint32_t display_index,
		struct dal_display_output_descriptor *display_descriptor);
*/

/**************** Mode Validate / Set ****************/
bool dal_set_path_mode(struct dal *dal,
		const struct path_mode_set *pms);

bool dal_reset_path_mode(struct dal *dal,
		const uint32_t displays_num,
		const uint32_t *display_indexes);

struct mode_query;
void dal_pin_active_path_modes(
	struct dal *dal,
	void *param,
	uint32_t display_index,
	void (*func)(void *, const struct path_mode *));

/******************** PowerPlay ********************/

bool dal_pre_adapter_clock_change(struct dal *dal,
		struct power_to_dal_info *clks_info);

bool dal_post_adapter_clock_change(struct dal *dal);

/***************** Power Control *****************/
void dal_set_display_dpms(
	struct dal *dal,
	uint32_t display_index,
	uint32_t state);

void dal_set_power_state(
	struct dal *dal,
	uint32_t power_state,
	uint32_t video_power_state);

void dal_set_blanking(struct dal *dal, uint32_t display_index, bool blank);
void dal_shut_down_display_block(struct dal *dal);

/***************** Brightness Interface *****************/
bool dal_set_backlight_level(
	struct dal *dal,
	uint32_t display_index,
	uint32_t brightness);
bool dal_get_backlight_level_old(
	struct dal *dal,
	uint32_t display_index,
	void *adjustment);
bool dal_backlight_control_on(struct dal *dal);
bool dal_backlight_control_off(struct dal *dal);

/***************** Brightness and color Control *****************/
void  dal_set_palette(
	struct dal *dal,
	uint32_t display_index,
	struct dal_dev_c_lut palette,
	uint32_t start,
	uint32_t length);

bool  dal_set_gamma(
	struct dal *dal,
	uint32_t display_index,
	struct raw_gamma_ramp *gamma);

/* Active Stereo Interface */
bool dal_control_stereo(struct dal *dal, uint32_t display_index, bool enable);

/* Disable All DC Pipes - blank CRTC and disable memory requests */
void dal_disable_all_dc_pipes(struct dal *dal);

/*
uint32_t dal_get_interrupt_source(const struct dal_display_index_set *set,
	uint64_t *irq_source);
*/

/* Timing List Query */
struct dal_timing_list_query *dal_create_timing_list_query(struct dal *dal,
	uint32_t display_index);

/*hard code color square at CRTC0 for HDMI light up*/
void dal_set_crtc_test_pattern(struct dal *dal);

/* returns the display_index associated (that generated) to irq source */
enum dal_irq_source;
uint32_t dal_get_display_index_from_int_src(
		struct dal *dal,
		enum dal_irq_source src);

/* return crtc scanout v/h counter, return if inside v_blank*/
uint32_t dal_get_crtc_scanoutpos(
		struct dal *dal,
		uint32_t display_index,
		int32_t *vpos,
		int32_t *hpos);

/* Return IRQ Source for Controller currently assigned to the Path. */
enum dal_irq_source dal_get_vblank_irq_src_from_display_index(struct dal *dal,
		uint32_t display_index);
enum dal_irq_source dal_get_pflip_irq_src_from_display_index(struct dal *dal,
		uint32_t display_index,
		uint32_t plane_no);

void dal_set_vblank_irq(struct dal *dal, uint32_t display_index, bool enable);


struct topology;
enum query_option;

struct mode_query *dal_get_mode_query(
	struct dal *dal,
	struct topology *tp,
	enum query_option query_option);

struct mode_manager *dal_get_mode_manager(struct dal *dal);

uint32_t dal_wifi_display_acquire(
	struct dal *dal,
	uint8_t *edid_data,
	uint32_t edid_size,
	struct dal_remote_display_receiver_capability *caps,
	uint32_t *display_index);

uint32_t dal_wifi_display_release(
	struct dal *dal,
	uint32_t *display_index);

/* Get the current vertical blank counts for given CRTC. */
uint32_t dal_get_vblank_counter(struct dal *dal, int disp_idx);

/* Surface programming interface */

/* TODO: move these below to separate header */
bool dal_set_cursor_position(
		struct dal *dal,
		const uint32_t display_index,
		const struct cursor_position *position
		);

bool dal_set_cursor_attributes(
		struct dal *dal,
		const uint32_t display_index,
		const struct cursor_attributes *attributes
		);

bool dal_setup_plane_configurations(
	struct dal *dal,
	uint32_t num_planes,
	struct plane_config *configs);

bool dal_update_plane_addresses(
	struct dal *dal,
	uint32_t num_planes,
	struct plane_addr_flip_info *info);

bool dal_validate_plane_configurations(struct dal *dal,
		int num_planes,
		const struct plane_config *pl_config,
		bool *supported);

void dal_interrupt_set(struct dal *dal, enum dal_irq_source src, bool enable);

void dal_interrupt_ack(struct dal *dal, enum dal_irq_source src);

enum dal_irq_source dal_interrupt_to_irq_source(
		struct dal *dal,
		uint32_t src_id,
		uint32_t ext_id);

void dal_stop_mc_access(struct amdgpu_device *adev,
				     struct amdgpu_mode_mc_save *save);

void dal_resume_mc_access(struct amdgpu_device *adev,
				       struct amdgpu_mode_mc_save *save);

#endif
