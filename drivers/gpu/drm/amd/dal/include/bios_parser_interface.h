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

#ifndef __DAL_BIOS_PARSER_INTERFACE_H__
#define __DAL_BIOS_PARSER_INTERFACE_H__

#include "bios_parser_types.h"
#include "adapter_service_types.h"
#include "gpio_types.h"

struct adapter_service;
struct bios_parser;

struct bp_gpio_cntl_info {
	uint32_t id;
	enum gpio_pin_output_state state;
};

enum bp_result {
	BP_RESULT_OK = 0, /* There was no error */
	BP_RESULT_BADINPUT, /*Bad input parameter */
	BP_RESULT_BADBIOSTABLE, /* Bad BIOS table */
	BP_RESULT_UNSUPPORTED, /* BIOS Table is not supported */
	BP_RESULT_NORECORD, /* Record can't be found */
	BP_RESULT_FAILURE
};

struct bp_init_data {
	struct dc_context *ctx;
	uint8_t *bios;
};

struct bios_parser *dal_bios_parser_create(
	struct bp_init_data *init,
	struct adapter_service *as);
void dal_bios_parser_destroy(
	struct bios_parser **bp);
void dal_bios_parser_power_down(
	struct bios_parser *bp);
void dal_bios_parser_power_up(
	struct bios_parser *bp);

uint8_t dal_bios_parser_get_encoders_number(
	struct bios_parser *bp);
uint8_t dal_bios_parser_get_connectors_number(
	struct bios_parser *bp);
uint32_t dal_bios_parser_get_oem_ddc_lines_number(
	struct bios_parser *bp);
struct graphics_object_id dal_bios_parser_get_encoder_id(
	struct bios_parser *bp,
	uint32_t i);
struct graphics_object_id dal_bios_parser_get_connector_id(
	struct bios_parser *bp,
	uint8_t connector_index);
uint32_t dal_bios_parser_get_src_number(
	struct bios_parser *bp,
	struct graphics_object_id id);
uint32_t dal_bios_parser_get_dst_number(
	struct bios_parser *bp,
	struct graphics_object_id id);
uint32_t dal_bios_parser_get_gpio_record(
	struct bios_parser *bp,
	struct graphics_object_id id,
	struct bp_gpio_cntl_info *gpio_record,
	uint32_t record_size);
enum bp_result dal_bios_parser_get_src_obj(
	struct bios_parser *bp,
	struct graphics_object_id object_id, uint32_t index,
	struct graphics_object_id *src_object_id);
enum bp_result dal_bios_parser_get_dst_obj(
	struct bios_parser *bp,
	struct graphics_object_id object_id, uint32_t index,
	struct graphics_object_id *dest_object_id);
enum bp_result dal_bios_parser_get_i2c_info(
	struct bios_parser *bp,
	struct graphics_object_id id,
	struct graphics_object_i2c_info *info);
enum bp_result dal_bios_parser_get_oem_ddc_info(
	struct bios_parser *bp,
	uint32_t index,
	struct graphics_object_i2c_info *info);
enum bp_result dal_bios_parser_get_voltage_ddc_info(
	struct bios_parser *bp,
	uint32_t index,
	struct graphics_object_i2c_info *info);
enum bp_result dal_bios_parser_get_thermal_ddc_info(
	struct bios_parser *bp,
	uint32_t i2c_channel_id,
	struct graphics_object_i2c_info *info);
enum bp_result dal_bios_parser_get_hpd_info(
	struct bios_parser *bp,
	struct graphics_object_id id,
	struct graphics_object_hpd_info *info);
enum bp_result dal_bios_parser_get_device_tag(
	struct bios_parser *bp,
	struct graphics_object_id connector_object_id,
	uint32_t device_tag_index,
	struct connector_device_tag_info *info);
enum bp_result dal_bios_parser_get_firmware_info(
	struct bios_parser *bp,
	struct firmware_info *info);
enum bp_result dal_bios_parser_get_spread_spectrum_info(
	struct bios_parser *bp,
	enum as_signal_type signal,
	uint32_t index,
	struct spread_spectrum_info *ss_info);
uint32_t dal_bios_parser_get_ss_entry_number(
	struct bios_parser *bp,
	enum as_signal_type signal);
enum bp_result dal_bios_parser_get_embedded_panel_info(
	struct bios_parser *bp,
	struct embedded_panel_info *info);
enum bp_result dal_bios_parser_enum_embedded_panel_patch_mode(
	struct bios_parser *bp,
	uint32_t index,
	struct embedded_panel_patch_mode *mode);
enum bp_result dal_bios_parser_get_gpio_pin_info(
	struct bios_parser *bp,
	uint32_t gpio_id,
	struct gpio_pin_info *info);
enum bp_result dal_bios_parser_get_embedded_panel_info(
	struct bios_parser *bp,
	struct embedded_panel_info *info);
enum bp_result dal_bios_parser_get_gpio_pin_info(
	struct bios_parser *bp,
	uint32_t gpio_id,
	struct gpio_pin_info *info);
enum bp_result dal_bios_parser_get_faked_edid_len(
	struct bios_parser *bp,
	uint32_t *len);
enum bp_result dal_bios_parser_get_faked_edid_buf(
	struct bios_parser *bp,
	uint8_t *buff,
	uint32_t len);
enum bp_result dal_bios_parser_get_encoder_cap_info(
	struct bios_parser *bp,
	struct graphics_object_id object_id,
	struct bp_encoder_cap_info *info);
enum bp_result dal_bios_parser_get_din_connector_info(
	struct bios_parser *bp,
	struct graphics_object_id id,
	struct din_connector_info *info);

bool dal_bios_parser_is_lid_open(
	struct bios_parser *bp);
bool dal_bios_parser_is_lid_status_changed(
	struct bios_parser *bp);
bool dal_bios_parser_is_display_config_changed(
	struct bios_parser *bp);
bool dal_bios_parser_is_accelerated_mode(
	struct bios_parser *bp);
void dal_bios_parser_set_scratch_lcd_scale(
	struct bios_parser *bp,
	enum lcd_scale scale);
enum lcd_scale  dal_bios_parser_get_scratch_lcd_scale(
	struct bios_parser *bp);
void dal_bios_parser_get_bios_event_info(
	struct bios_parser *bp,
	struct bios_event_info *info);
void dal_bios_parser_update_requested_backlight_level(
	struct bios_parser *bp,
	uint32_t backlight_8bit);
uint32_t dal_bios_parser_get_requested_backlight_level(
	struct bios_parser *bp);
void dal_bios_parser_take_backlight_control(
	struct bios_parser *bp,
	bool cntl);
bool dal_bios_parser_is_active_display(
	struct bios_parser *bp,
	enum signal_type signal,
	const struct connector_device_tag_info *device_tag);
enum controller_id dal_bios_parser_get_embedded_display_controller_id(
	struct bios_parser *bp);
uint32_t dal_bios_parser_get_embedded_display_refresh_rate(
	struct bios_parser *bp);
void dal_bios_parser_set_scratch_connected(
	struct bios_parser *bp,
	struct graphics_object_id connector_id,
	bool connected,
	const struct connector_device_tag_info *device_tag);
void dal_bios_parser_prepare_scratch_active_and_requested(
	struct bios_parser *bp,
	enum controller_id controller_id,
	enum signal_type signal,
	const struct connector_device_tag_info *device_tag);
void dal_bios_parser_set_scratch_active_and_requested(
	struct bios_parser *bp);
void dal_bios_parser_set_scratch_critical_state(
	struct bios_parser *bp,
	bool state);
void dal_bios_parser_set_scratch_acc_mode_change(
	struct bios_parser *bp);

bool dal_bios_parser_is_device_id_supported(
	struct bios_parser *bp,
	struct device_id id);

/* COMMANDS */

enum bp_result dal_bios_parser_encoder_control(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl);
enum bp_result dal_bios_parser_transmitter_control(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl);
enum bp_result dal_bios_parser_crt_control(
	struct bios_parser *bp,
	enum engine_id engine_id,
	bool enable,
	uint32_t pixel_clock);
enum bp_result dal_bios_parser_dvo_encoder_control(
	struct bios_parser *bp,
	struct bp_dvo_encoder_control *cntl);
enum bp_result dal_bios_parser_enable_crtc(
	struct bios_parser *bp,
	enum controller_id id,
	bool enable);
enum bp_result dal_bios_parser_adjust_pixel_clock(
	struct bios_parser *bp,
	struct bp_adjust_pixel_clock_parameters *bp_params);
enum bp_result dal_bios_parser_set_pixel_clock(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);
enum bp_result dal_bios_parser_set_dce_clock(
	struct bios_parser *bp,
	struct bp_set_dce_clock_parameters *bp_params);
enum bp_result dal_bios_parser_enable_spread_spectrum_on_ppll(
	struct bios_parser *bp,
	struct bp_spread_spectrum_parameters *bp_params,
	bool enable);
enum bp_result dal_bios_parser_program_crtc_timing(
	struct bios_parser *bp,
	struct bp_hw_crtc_timing_parameters *bp_params);
enum bp_result dal_bios_parser_blank_crtc(
	struct bios_parser *bp,
	struct bp_blank_crtc_parameters *bp_params,
	bool blank);
enum bp_result dal_bios_parser_set_overscan(
	struct bios_parser *bp,
	struct bp_hw_crtc_overscan_parameters *bp_params);
enum bp_result dal_bios_parser_crtc_source_select(
	struct bios_parser *bp,
	struct bp_crtc_source_select *bp_params);
enum bp_result dal_bios_parser_program_display_engine_pll(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);
enum bp_result dal_bios_parser_get_divider_for_target_display_clock(
	struct bios_parser *bp,
	struct bp_display_clock_parameters *bp_params);
enum signal_type dal_bios_parser_dac_load_detect(
	struct bios_parser *bp,
	struct graphics_object_id encoder,
	struct graphics_object_id connector,
	enum signal_type display_signal);
enum bp_result dal_bios_parser_enable_memory_requests(
	struct bios_parser *bp,
	enum controller_id controller_id,
	bool enable);
enum bp_result dal_bios_parser_external_encoder_control(
	struct bios_parser *bp,
	struct bp_external_encoder_control *cntl);
enum bp_result dal_bios_parser_enable_disp_power_gating(
	struct bios_parser *bp,
	enum controller_id controller_id,
	enum bp_pipe_control_action action);

void dal_bios_parser_post_init(struct bios_parser *bp);

/* Parse integrated BIOS info */
struct integrated_info *dal_bios_parser_create_integrated_info(
	struct bios_parser *bp);

/* Destroy provided integrated info */
void dal_bios_parser_destroy_integrated_info(struct dc_context *ctx, struct integrated_info **info);
#endif
