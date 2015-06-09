#ifndef __DAL_TM_UTILS_H__
#define __DAL_TM_UTILS_H__

#include "include/grph_object_id.h"
#include "include/signal_types.h"
#include "include/grph_object_ctrl_defs.h"
#include "include/ddc_service_types.h"
#include "include/dcs_types.h"
#include "include/link_service_interface.h"
#include "include/clock_source_types.h"
#include "include/topology_mgr_interface.h"

#include "tm_internal_types.h"
#include "tm_resource_mgr.h"

struct tm_calc_subset {
	/* defines upper limit (exclusive) of possible values
	 * lower limit is 0
	 */
	uint32_t max_value;
	/* defines upper limit (inclusive) of subset size
	 * lower limit is 1
	 */
	uint32_t max_subset_size;
	/* size of current subset */
	uint32_t subset_size;
	/* stores display indices which are checked for co-func*/
	uint32_t buffer[MAX_COFUNCTIONAL_PATHS];
};

enum tm_utils_display_type {
	DISPLAY_MONITOR,
	DISPLAY_TELEVISION,
	DISPLAY_LCD_PANEL,
	DISPLAY_DFP,
	DISPLAY_COMPONENT_VIDEO
};

const char *tm_utils_encoder_id_to_str(enum encoder_id id);

const char *tm_utils_connector_id_to_str(enum connector_id id);

const char *tm_utils_audio_id_to_str(enum audio_id id);

const char *tm_utils_controller_id_to_str(enum controller_id id);

const char *tm_utils_clock_source_id_to_str(enum clock_source_id id);

const char *tm_utils_engine_id_to_str(enum engine_id id);

const char *tm_utils_go_type_to_str(struct graphics_object_id id);

const char *tm_utils_go_id_to_str(struct graphics_object_id id);

const char *tm_utils_go_enum_to_str(struct graphics_object_id id);

const char *tm_utils_transmitter_id_to_str(struct graphics_object_id id);

enum dal_device_type tm_utils_signal_type_to_device_type(
		enum signal_type signal);

enum dcs_interface_type dal_tm_utils_signal_type_to_interface_type(
		enum signal_type signal);

const char *tm_utils_signal_type_to_str(enum signal_type type);

const char *tm_utils_engine_priority_to_str(enum tm_engine_priority priority);

enum tm_display_type tm_utils_device_id_to_tm_display_type(struct device_id id);

const char *tm_utils_device_type_to_str(enum dal_device_type device);

const char *tm_utils_hpd_line_to_str(enum hpd_source_id line);

const char *tm_utils_ddc_line_to_str(enum channel_id line);

void tm_utils_set_bit(uint32_t *bitmap, uint8_t bit);

void tm_utils_clear_bit(uint32_t *bitmap, uint8_t bit);

bool tm_utils_test_bit(uint32_t *bitmap, uint8_t bit);

bool tm_utils_is_clock_sharing_mismatch(
		enum clock_sharing_level sharing_level,
		enum clock_sharing_group sharing_group);

enum link_service_type tm_utils_signal_to_link_service_type(
		enum signal_type signal);

bool tm_utils_is_destructive_method(enum tm_detection_method method);

bool tm_utils_is_edid_connector_type_valid_with_signal_type(
		enum display_dongle_type dongle_type,
		enum dcs_edid_connector_type edid_conn,
		enum signal_type signal);

enum signal_type tm_utils_get_downgraded_signal_type(
		enum signal_type signal,
		enum dcs_edid_connector_type connector_type);

enum signal_type tm_utils_downgrade_to_no_audio_signal(
		enum signal_type signal);

enum ddc_transaction_type tm_utils_get_ddc_transaction_type(
		enum signal_type sink_signal,
		enum signal_type asic_signal);

/******************************************************************************
 * TM Subset. A helper object to prepare a buffer of display indices to be
 * checked for co-functionality
 *****************************************************************************/
struct tm_calc_subset *dal_tm_calc_subset_create(void);

void dal_tm_calc_subset_destroy(struct tm_calc_subset *subset);

uint32_t dal_tm_calc_subset_get_value(
	struct tm_calc_subset *subset,
	uint32_t index);

bool dal_tm_calc_subset_start(
	struct tm_calc_subset *subset,
	uint32_t max_value,
	uint32_t max_subset_size);

bool dal_tm_calc_subset_step(struct tm_calc_subset *subset);

bool dal_tm_calc_subset_skip(struct tm_calc_subset *subset);

/******************************************************************************
 * Miscellaneous functions
 *****************************************************************************/
char *tm_utils_get_tm_resource_str(struct tm_resource *tm_resource);

bool tm_utils_is_supported_engine(union supported_stream_engines se,
		enum engine_id engine);

bool tm_utils_is_dp_connector(const struct connector *cntr);

/* Check if signal at "ASIC link" is Display Port. */
bool tm_utils_is_dp_asic_signal(const struct display_path *display_path);

#endif /* __DAL_TM_UTILS_H__ */
