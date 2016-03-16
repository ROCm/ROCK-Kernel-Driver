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
#include <stdarg.h>
#include "dm_services.h"
#include "include/dal_types.h"
#include "include/logger_interface.h"
#include "logger.h"

/* TODO: for now - empty, use DRM defines from dal services.
		Need to define appropriate levels of prints, and implement
		this component
void dal_log(const char *format, ...)
{
}
*/

/* ----------- Logging Major/Minor names ------------ */

#define NUM_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

static const struct log_minor_info component_minor_info_tbl[] = {
	{LOG_MINOR_COMPONENT_LINK_SERVICE, "LS"},
	{LOG_MINOR_COMPONENT_DAL_INTERFACE, "DalIf"},
	{LOG_MINOR_COMPONENT_HWSS, "HWSS"},
	{LOG_MINOR_COMPONENT_ADAPTER_SERVICE, "AS"},
	{LOG_MINOR_COMPONENT_DISPLAY_SERVICE, "DS"},
	{LOG_MINOR_COMPONENT_TOPOLOGY_MANAGER, "TM"},
	{LOG_MINOR_COMPONENT_ENCODER, "Encoder"},
	{LOG_MINOR_COMPONENT_I2C_AUX, "I2cAux"},
	{LOG_MINOR_COMPONENT_AUDIO, "Audio"},
	{LOG_MINOR_COMPONENT_DISPLAY_CAPABILITY_SERVICE, "Dcs"},
	{LOG_MINOR_COMPONENT_DMCU, "Dmcu"},
	{LOG_MINOR_COMPONENT_GPU, "GPU"},
	{LOG_MINOR_COMPONENT_CONTROLLER, "Cntrlr"},
	{LOG_MINOR_COMPONENT_ISR, "ISR"},
	{LOG_MINOR_COMPONENT_BIOS, "BIOS"},
	{LOG_MINOR_COMPONENT_DC, "DC"},
	{LOG_MINOR_COMPONENT_IRQ_SERVICE, "IRQ SERVICE"},

};

static const struct log_minor_info hw_trace_minor_info_tbl[] = {
	{LOG_MINOR_HW_TRACE_MST, "Mst" },
	{LOG_MINOR_HW_TRACE_TRAVIS, "Travis" },
	{LOG_MINOR_HW_TRACE_HOTPLUG, "Hotplug" },
	{LOG_MINOR_HW_TRACE_LINK_TRAINING, "LinkTraining" },
	{LOG_MINOR_HW_TRACE_SET_MODE, "SetMode" },
	{LOG_MINOR_HW_TRACE_RESUME_S3, "ResumeS3" },
	{LOG_MINOR_HW_TRACE_RESUME_S4, "ResumeS4" },
	{LOG_MINOR_HW_TRACE_BOOTUP, "BootUp" },
	{LOG_MINOR_HW_TRACE_AUDIO, "Audio"},
	{LOG_MINOR_HW_TRACE_HPD_IRQ, "HpdIrq" },
	{LOG_MINOR_HW_TRACE_INTERRUPT, "Interrupt" },
	{LOG_MINOR_HW_TRACE_MPO, "Planes" },
};

static const struct log_minor_info mst_minor_info_tbl[] = {
	{LOG_MINOR_MST_IRQ_HPD_RX, "IrqHpdRx"},
	{LOG_MINOR_MST_IRQ_TIMER, "IrqTimer"},
	{LOG_MINOR_MST_NATIVE_AUX, "NativeAux"},
	{LOG_MINOR_MST_SIDEBAND_MSG, "SB"},
	{LOG_MINOR_MST_MSG_TRANSACTION, "MT"},
	{LOG_MINOR_MST_SIDEBAND_MSG_PARSED, "SB Parsed"},
	{LOG_MINOR_MST_MSG_TRANSACTION_PARSED, "MT Parsed"},
	{LOG_MINOR_MST_AUX_MSG_DPCD_ACCESS, "AuxMsgDpcdAccess"},
	{LOG_MINOR_MST_PROGRAMMING, "Programming"},
	{LOG_MINOR_MST_TOPOLOGY_DISCOVERY, "TopologyDiscovery"},
	{LOG_MINOR_MST_CONVERTER_CAPS, "ConverterCaps"},
};

static const struct log_minor_info dcs_minor_info_tbl[] = {
	{LOG_MINOR_DCS_EDID_EMULATOR, "EdidEmul"},
	{LOG_MINOR_DCS_DONGLE_DETECTION, "DongleDetect"},
};

static const struct log_minor_info dcp_minor_info_tbl[] = {
	{ LOG_MINOR_DCP_GAMMA_GRPH, "GammaGrph"},
	{ LOG_MINOR_DCP_GAMMA_OVL, "GammaOvl"},
	{ LOG_MINOR_DCP_CSC_GRPH, "CscGrph"},
	{ LOG_MINOR_DCP_CSC_OVL, "CscOvl"},
	{ LOG_MINOR_DCP_SCALER, "Scaler"},
	{ LOG_MINOR_DCP_SCALER_TABLES, "ScalerTables"},
};

static const struct log_minor_info bios_minor_info_tbl[] = {
	{LOG_MINOR_BIOS_CMD_TABLE, "CmdTbl"},
};

static const struct log_minor_info reg_minor_info_tbl[] = {
	{LOG_MINOR_REGISTER_INDEX, "Index"},
};

static const struct log_minor_info info_packet_minor_info_tbl[] = {
	{LOG_MINOR_INFO_PACKETS_HDMI, "Hdmi"},
};

static const struct log_minor_info dsat_minor_info_tbl[] = {
	{LOG_MINOR_DSAT_LOGGER, "Logger"},
	{LOG_MINOR_DSAT_EDID_OVERRIDE, "EDID_Override"},
};

static const struct log_minor_info ec_minor_info_tbl[] = {
	{LOG_MINOR_EC_PPLIB_NOTIFY, "PPLib_Notify" }, /* PPLib notifies DAL */
	{LOG_MINOR_EC_PPLIB_QUERY, "PPLib_Query" } /* DAL requested info from
							PPLib */
};

static const struct log_minor_info bwm_minor_info_tbl[] = {
	{LOG_MINOR_BWM_MODE_VALIDATION, "ModeValidation"},
	{LOG_MINOR_BWM_REQUIRED_BANDWIDTH_CALCS, "Req_Bandw_Calcs"}
};

static const struct log_minor_info mode_enum_minor_info_tbl[] = {
	{LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES, "BestviewCandidates"},
	{LOG_MINOR_MODE_ENUM_VIEW_SOLUTION, "ViewSolution"},
	{LOG_MINOR_MODE_ENUM_TS_LIST_BUILD, "TsListBuild"},
	{LOG_MINOR_MODE_ENUM_TS_LIST, "TsList"},
	{LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST, "MasterViewList"},
	{LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST_UPDATE, "MasterViewListUpdate"},
};

static const struct log_minor_info i2caux_minor_info_tbl[] = {
	{LOG_MINOR_I2C_AUX_LOG, "Log"},
	{LOG_MINOR_I2C_AUX_AUX_TIMESTAMP, "Timestamp"},
	{LOG_MINOR_I2C_AUX_CFG, "Config"}
};

static const struct log_minor_info line_buffer_minor_info_tbl[] = {
	{LOG_MINOR_LINE_BUFFER_POWERGATING, "PowerGating"}
};

static const struct log_minor_info hwss_minor_info_tbl[] = {
	{LOG_MINOR_HWSS_TAPS_VALIDATION, "HWSS Taps"}
};

static const struct log_minor_info optimization_minor_info_tbl[] = {
	{LOG_MINOR_OPTMZ_GENERAL, "General Optimizations"},
	{LOG_MINOR_OPTMZ_DO_NOT_TURN_OFF_VCC_DURING_SET_MODE,
		"Skip Vcc Off During Set Mode"}
};

static const struct log_minor_info perf_measure_minor_info_tbl[] = {
	{LOG_MINOR_PERF_MEASURE_GENERAL, "General Performance Measurement"},
	{LOG_MINOR_PERF_MEASURE_HEAP_MEMORY, "Heap Memory Management"}
};

static const struct log_minor_info sync_minor_info_tbl[] = {
	{LOG_MINOR_SYNC_HW_CLOCK_ADJUST, "Pixel Rate Tune-up"},
	{LOG_MINOR_SYNC_TIMING, "Timing"}
};

static const struct log_minor_info backlight_minor_info_tbl[] = {
	{LOG_MINOR_BACKLIGHT_BRIGHTESS_CAPS, "Caps"},
	{LOG_MINOR_BACKLIGHT_DMCU_DELTALUT, "DMCU Delta LUT"},
	{LOG_MINOR_BACKLIGHT_DMCU_BUILD_DELTALUT, "Build DMCU Delta LUT"},
	{LOG_MINOR_BACKLIGHT_INTERFACE, "Interface"},
	{LOG_MINOR_BACKLIGHT_LID, "Lid Status"}
};

static const struct log_minor_info override_feature_minor_info_tbl[] = {
	{LOG_MINOR_FEATURE_OVERRIDE, "overriden feature"},
};

static const struct log_minor_info detection_minor_info_tbl[] = {
	{LOG_MINOR_DETECTION_EDID_PARSER, "EDID Parser"},
	{LOG_MINOR_DETECTION_DP_CAPS, "DP caps"},
};

static const struct log_minor_info tm_minor_info_tbl[] = {
	{LOG_MINOR_TM_INFO, "INFO"},
	{LOG_MINOR_TM_IFACE_TRACE, "IFACE_TRACE"},
	{LOG_MINOR_TM_RESOURCES, "RESOURCES"},
	{LOG_MINOR_TM_ENCODER_CTL, "ENCODER_CTL"},
	{LOG_MINOR_TM_ENG_ASN, "ENG_ASN"},
	{LOG_MINOR_TM_CONTROLLER_ASN, "CONTROLLER_ASN"},
	{LOG_MINOR_TM_PWR_GATING, "PWR_GATING"},
	{LOG_MINOR_TM_BUILD_DSP_PATH, "BUILD_PATH"},
	{LOG_MINOR_TM_DISPLAY_DETECT, "DISPLAY_DETECT"},
	{LOG_MINOR_TM_LINK_SRV,	"LINK_SRV"},
	{LOG_MINOR_TM_NOT_IMPLEMENTED, "NOT_IMPL"},
	{LOG_MINOR_TM_COFUNC_PATH, "COFUNC_PATH"}
};

static const struct log_minor_info ds_minor_info_tbl[] = {
	{LOG_MINOR_DS_MODE_SETTING, "Mode_Setting"},
};

static const struct log_minor_info connectivity_minor_info_tbl[] = {
	{LOG_MINOR_CONNECTIVITY_MODE_SET,  "Mode"},
	{LOG_MINOR_CONNECTIVITY_DETECTION, "Detect"},
	{LOG_MINOR_CONNECTIVITY_LINK_TRAINING, "LKTN"},
	{LOG_MINOR_CONNECTIVITY_LINK_LOSS, "LinkLoss"},
	{LOG_MINOR_CONNECTIVITY_UNDERFLOW, "Underflow"},
};

struct log_major_mask_info {
	struct log_major_info major_info;
	uint32_t default_mask;
	const struct log_minor_info *minor_tbl;
	uint32_t tbl_element_cnt;
};

/* A mask for each Major.
 * Use a mask or zero. */
#define LG_ERR_MSK 0xffffffff
#define LG_WRN_MSK 0xffffffff
#define LG_TM_MSK (1 << LOG_MINOR_TM_INFO)
#define LG_FO_MSK (1 << LOG_MINOR_FEATURE_OVERRIDE)
#define LG_EC_MSK ((1 << LOG_MINOR_EC_PPLIB_NOTIFY) | \
			(1 << LOG_MINOR_EC_PPLIB_QUERY))
#define LG_DSAT_MSK (1 << LOG_MINOR_DSAT_EDID_OVERRIDE)
#define LG_DT_MSK (1 << LOG_MINOR_DETECTION_EDID_PARSER)

/* IFT - InterFaceTrace */
#define LG_IFT_MSK (1 << LOG_MINOR_COMPONENT_DC)

#define LG_HW_TR_AUD_MSK (1 << LOG_MINOR_HW_TRACE_AUDIO)
#define LG_HW_TR_INTERRUPT_MSK (1 << LOG_MINOR_HW_TRACE_INTERRUPT) | \
		(1 << LOG_MINOR_HW_TRACE_HPD_IRQ)
#define LG_HW_TR_PLANES_MSK (1 << LOG_MINOR_HW_TRACE_MPO)
#define LG_ALL_MSK 0xffffffff
#define LG_DCP_MSK ~(1 << LOG_MINOR_DCP_SCALER)

#define LG_SYNC_MSK (1 << LOG_MINOR_SYNC_TIMING)

#define LG_BWM_MSK (1 << LOG_MINOR_BWM_MODE_VALIDATION)

static const struct log_major_mask_info log_major_mask_info_tbl[] = {
	/* LogMajor                  major name       default     MinorTble                    tblElementCnt */
	{{LOG_MAJOR_ERROR,           "Error"       }, LG_ALL_MSK, component_minor_info_tbl,    NUM_ELEMENTS(component_minor_info_tbl)},
	{{LOG_MAJOR_WARNING,         "Warning"     }, LG_ALL_MSK, component_minor_info_tbl,    NUM_ELEMENTS(component_minor_info_tbl)},
	{{LOG_MAJOR_INTERFACE_TRACE, "IfTrace"     }, LG_ALL_MSK, component_minor_info_tbl,    NUM_ELEMENTS(component_minor_info_tbl)},
	{{LOG_MAJOR_HW_TRACE,        "HwTrace"     }, (LG_ALL_MSK &
			~((1 << LOG_MINOR_HW_TRACE_LINK_TRAINING) |
			(1 << LOG_MINOR_HW_TRACE_AUDIO))),
								hw_trace_minor_info_tbl,     NUM_ELEMENTS(hw_trace_minor_info_tbl)},
	{{LOG_MAJOR_MST,             "MST"         }, LG_ALL_MSK, mst_minor_info_tbl,          NUM_ELEMENTS(mst_minor_info_tbl)},
	{{LOG_MAJOR_DCS,             "DCS"         }, LG_ALL_MSK, dcs_minor_info_tbl,          NUM_ELEMENTS(dcs_minor_info_tbl)},
	{{LOG_MAJOR_DCP,             "DCP"         }, LG_DCP_MSK, dcp_minor_info_tbl,          NUM_ELEMENTS(dcp_minor_info_tbl)},
	{{LOG_MAJOR_BIOS,            "Bios"        }, LG_ALL_MSK, bios_minor_info_tbl,         NUM_ELEMENTS(bios_minor_info_tbl)},
	{{LOG_MAJOR_REGISTER,        "Register"    }, LG_ALL_MSK, reg_minor_info_tbl,          NUM_ELEMENTS(reg_minor_info_tbl)},
	{{LOG_MAJOR_INFO_PACKETS,    "InfoPacket"  }, LG_ALL_MSK, info_packet_minor_info_tbl,  NUM_ELEMENTS(info_packet_minor_info_tbl)},
	{{LOG_MAJOR_DSAT,            "DSAT"        }, LG_ALL_MSK, dsat_minor_info_tbl,         NUM_ELEMENTS(dsat_minor_info_tbl)},
	{{LOG_MAJOR_EC,              "EC"          }, LG_ALL_MSK, ec_minor_info_tbl,           NUM_ELEMENTS(ec_minor_info_tbl)},
	{{LOG_MAJOR_BWM,             "BWM"         }, LG_BWM_MSK, bwm_minor_info_tbl,          NUM_ELEMENTS(bwm_minor_info_tbl)},
	{{LOG_MAJOR_MODE_ENUM,       "ModeEnum"    }, LG_ALL_MSK, mode_enum_minor_info_tbl,    NUM_ELEMENTS(mode_enum_minor_info_tbl)},
	{{LOG_MAJOR_I2C_AUX,         "I2cAux"      }, LG_ALL_MSK, i2caux_minor_info_tbl,       NUM_ELEMENTS(i2caux_minor_info_tbl)},
	{{LOG_MAJOR_LINE_BUFFER,     "LineBuffer"  }, LG_ALL_MSK, line_buffer_minor_info_tbl,  NUM_ELEMENTS(line_buffer_minor_info_tbl)},
	{{LOG_MAJOR_HWSS,            "HWSS"        }, LG_ALL_MSK, hwss_minor_info_tbl,         NUM_ELEMENTS(hwss_minor_info_tbl)},
	{{LOG_MAJOR_OPTIMIZATION,    "Optimization"}, LG_ALL_MSK, optimization_minor_info_tbl, NUM_ELEMENTS(optimization_minor_info_tbl)},
	{{LOG_MAJOR_PERF_MEASURE,    "PerfMeasure" }, LG_ALL_MSK, perf_measure_minor_info_tbl, NUM_ELEMENTS(perf_measure_minor_info_tbl)},
	{{LOG_MAJOR_SYNC,            "Sync"        }, LG_SYNC_MSK,sync_minor_info_tbl,         NUM_ELEMENTS(sync_minor_info_tbl)},
	{{LOG_MAJOR_BACKLIGHT,       "Backlight"   }, LG_ALL_MSK, backlight_minor_info_tbl,    NUM_ELEMENTS(backlight_minor_info_tbl)},
	{{LOG_MAJOR_INTERRUPTS,      "Interrupts"  }, LG_ALL_MSK, component_minor_info_tbl,    NUM_ELEMENTS(component_minor_info_tbl)},
	{{LOG_MAJOR_TM,              "TM"          }, 0,          tm_minor_info_tbl,           NUM_ELEMENTS(tm_minor_info_tbl)},
	{{LOG_MAJOR_DISPLAY_SERVICE, "DS"          }, LG_ALL_MSK, ds_minor_info_tbl,           NUM_ELEMENTS(ds_minor_info_tbl)},
	{{LOG_MAJOR_FEATURE_OVERRIDE, "FeatureOverride" }, LG_ALL_MSK, override_feature_minor_info_tbl, NUM_ELEMENTS(override_feature_minor_info_tbl)},
	{{LOG_MAJOR_DETECTION,       "Detection"   }, LG_ALL_MSK,  detection_minor_info_tbl,    NUM_ELEMENTS(detection_minor_info_tbl)},
	{{LOG_MAJOR_CONNECTIVITY,    "Conn"		   }, LG_ALL_MSK,  connectivity_minor_info_tbl, NUM_ELEMENTS(connectivity_minor_info_tbl)},
};

/* ----------- Object init and destruction ----------- */
static bool construct(struct dc_context *ctx, struct dal_logger *logger)
{
	uint32_t i;
	/* malloc buffer and init offsets */

	logger->log_buffer_size = DAL_LOGGER_BUFFER_MAX_SIZE;
	logger->log_buffer = (char *)dm_alloc(logger->log_buffer_size *
		sizeof(char));

	if (!logger->log_buffer)
		return false;

	/* todo: Fill buffer with \0 if not done by dal_alloc */

	/* Initialize both offsets to start of buffer (empty) */
	logger->buffer_read_offset = 0;
	logger->buffer_write_offset = 0;

	logger->write_wrap_count = 0;
	logger->read_wrap_count = 0;
	logger->open_count = 0;

	logger->flags.bits.ENABLE_CONSOLE = 1;
	logger->flags.bits.ENABLE_BUFFER = 0;

	logger->ctx = ctx;

	/* malloc and init minor mask array */
	logger->log_enable_mask_minors =
			(uint32_t *)dm_alloc(NUM_ELEMENTS(log_major_mask_info_tbl)
				* sizeof(uint32_t));
	if (!logger->log_enable_mask_minors)
		return false;

	/* Set default values for mask */
	for (i = 0; i < NUM_ELEMENTS(log_major_mask_info_tbl); i++) {

		uint32_t dflt_mask = log_major_mask_info_tbl[i].default_mask;

		logger->log_enable_mask_minors[i] = dflt_mask;
	}

	return true;
}

static void destruct(struct dal_logger *logger)
{
	if (logger->log_buffer) {
		dm_free(logger->log_buffer);
		logger->log_buffer = NULL;
	}

	if (logger->log_enable_mask_minors) {
		dm_free(logger->log_enable_mask_minors);
		logger->log_enable_mask_minors = NULL;
	}
}

struct dal_logger *dal_logger_create(struct dc_context *ctx)
{
	/* malloc struct */
	struct dal_logger *logger = dm_alloc(sizeof(struct dal_logger));

	if (!logger)
		return NULL;
	if (!construct(ctx, logger)) {
		dm_free(logger);
		return NULL;
	}

	return logger;
}

uint32_t dal_logger_destroy(struct dal_logger **logger)
{
	if (logger == NULL || *logger == NULL)
		return 1;
	destruct(*logger);
	dm_free(*logger);
	*logger = NULL;

	return 0;
}

/* ------------------------------------------------------------------------ */

static void lock(struct dal_logger *logger)
{
	/* Todo: lock mutex? */
}

static void unlock(struct dal_logger *logger)
{
	/* Todo: unlock mutex */
}

bool dal_logger_should_log(
	struct dal_logger *logger,
	enum log_major major,
	enum log_minor minor)
{
	if (major < LOG_MAJOR_COUNT) {

		uint32_t minor_mask = logger->log_enable_mask_minors[major];

		if ((minor_mask & (1 << minor)) != 0)
			return true;
	}

	return false;
}

static void log_to_debug_console(struct log_entry *entry)
{
	struct dal_logger *logger = entry->logger;

	if (logger->flags.bits.ENABLE_CONSOLE == 0)
		return;

	if (entry->buf_offset) {
		switch (entry->major) {
		case LOG_MAJOR_ERROR:
			dm_error("%s", entry->buf);
			break;
		default:
			dm_output_to_console("%s", entry->buf);
			break;
		}
	}
}

/* Print everything unread existing in log_buffer to debug console*/
static void flush_to_debug_console(struct dal_logger *logger)
{
	int i = logger->buffer_read_offset;
	char *string_start = &logger->log_buffer[i];

	dm_output_to_console(
		"---------------- FLUSHING LOG BUFFER ----------------\n");
	while (i < logger->buffer_write_offset)	{

		if (logger->log_buffer[i] == '\0') {
			dm_output_to_console("%s", string_start);
			string_start = (char *)logger->log_buffer + i + 1;
		}
		i++;
	}
	dm_output_to_console(
		"-------------- END FLUSHING LOG BUFFER --------------\n\n");
}

static void log_to_internal_buffer(struct log_entry *entry)
{

	uint32_t size = entry->buf_offset;
	struct dal_logger *logger = entry->logger;

	if (logger->flags.bits.ENABLE_BUFFER == 0)
		return;

	if (logger->log_buffer == NULL)
		return;

	if (size > 0 && size < logger->log_buffer_size) {

		int total_free_space = 0;
		int space_before_wrap = 0;

		if (logger->buffer_write_offset > logger->buffer_read_offset) {
			total_free_space = logger->log_buffer_size -
					logger->buffer_write_offset +
					logger->buffer_read_offset;
			space_before_wrap = logger->log_buffer_size -
					logger->buffer_write_offset;
		} else if (logger->buffer_write_offset <
				logger->buffer_read_offset) {
			total_free_space = logger->log_buffer_size -
					logger->buffer_read_offset +
					logger->buffer_write_offset;
			space_before_wrap = total_free_space;
		} else if (logger->write_wrap_count !=
				logger->read_wrap_count) {
			/* Buffer is completely full already */
			total_free_space = 0;
			space_before_wrap = 0;
		} else {
			/* Buffer is empty, start writing at beginning */
			total_free_space = logger->log_buffer_size;
			space_before_wrap = logger->log_buffer_size;
			logger->buffer_write_offset = 0;
			logger->buffer_read_offset = 0;
		}

		if (space_before_wrap > size) {
			/* No wrap around, copy 'size' bytes
			 * from 'entry->buf' to 'log_buffer'
			 */
			memmove(logger->log_buffer +
					logger->buffer_write_offset,
					entry->buf, size);
			logger->buffer_write_offset += size;

		} else if (total_free_space > size) {
			/* We have enough room without flushing,
			 * but need to wrap around */

			int space_after_wrap = total_free_space -
					space_before_wrap;

			memmove(logger->log_buffer +
					logger->buffer_write_offset,
					entry->buf, space_before_wrap);
			memmove(logger->log_buffer, entry->buf +
					space_before_wrap, space_after_wrap);

			logger->buffer_write_offset = space_after_wrap;
			logger->write_wrap_count++;

		} else {
			/* Not enough room remaining, we should flush
			 * existing logs */

			/* Flush existing unread logs to console */
			flush_to_debug_console(logger);

			/* Start writing to beginning of buffer */
			memmove(logger->log_buffer, entry->buf, size);
			logger->buffer_write_offset = size;
			logger->buffer_read_offset = 0;
		}

	}

	unlock(logger);
}

static void log_timestamp(struct log_entry *entry)
{
/*	dal_logger_append(entry, "00:00:00 ");*/
}

static void log_major_minor(struct log_entry *entry)
{
	uint32_t i;
	enum log_major major = entry->major;
	enum log_minor minor = entry->minor;

	for (i = 0; i < NUM_ELEMENTS(log_major_mask_info_tbl); i++) {

		const struct log_major_mask_info *maj_mask_info =
				&log_major_mask_info_tbl[i];

		if (maj_mask_info->major_info.major == major) {

			dal_logger_append(entry, "[%s_",
					maj_mask_info->major_info.major_name);

			if (maj_mask_info->minor_tbl != NULL) {
				uint32_t j;

				for (j = 0; j < maj_mask_info->tbl_element_cnt; j++) {

					const struct log_minor_info *min_info = &maj_mask_info->minor_tbl[j];

					if (min_info->minor == minor)
						dal_logger_append(entry, "%s]\t", min_info->minor_name);
				}
			}

			break;
		}
	}
}

static void log_heading(struct log_entry *entry,
			enum log_major major,
			enum log_minor minor)
{
	log_timestamp(entry);
	log_major_minor(entry);
}

static void append_entry(
		struct log_entry *entry,
		char *buffer,
		uint32_t buf_size)
{
	if (!entry->buf ||
		entry->buf_offset + buf_size > entry->max_buf_bytes
	) {
		BREAK_TO_DEBUGGER();
		return;
	}

	/* Todo: check if off by 1 byte due to \0 anywhere */
	memmove(entry->buf + entry->buf_offset, buffer, buf_size);
	entry->buf_offset += buf_size;
}

/* ------------------------------------------------------------------------ */

/* Warning: Be careful that 'msg' is null terminated and the total size is
 * less than DAL_LOGGER_BUFFER_MAX_LOG_LINE_SIZE (256) including '\0'
 */
void dal_logger_write(
	struct dal_logger *logger,
	enum log_major major,
	enum log_minor minor,
	const char *msg,
	...)
{

	if (logger && dal_logger_should_log(logger, major, minor)) {

		uint32_t size;
		va_list args;
		char buffer[DAL_LOGGER_BUFFER_MAX_LOG_LINE_SIZE];
		struct log_entry entry;

		va_start(args, msg);
		dal_logger_open(logger, &entry, major, minor);

		size = dm_log_to_buffer(
			buffer, DAL_LOGGER_BUFFER_MAX_LOG_LINE_SIZE, msg, args);

		if (size > 0 && size <
				DAL_LOGGER_BUFFER_MAX_LOG_LINE_SIZE - 1) {

			if (buffer[size] == '\0')
				size++; /* Add one for null terminator */

			/* Concatenate onto end of entry buffer */
			append_entry(&entry, buffer, size);
		} else {
			append_entry(&entry,
				"LOG_ERROR, line too long or null\n", 35);
		}

		dal_logger_close(&entry);
		va_end(args);

	}
}

/* Same as dal_logger_write, except without open() and close(), which must
 * be done separately.
 */
void dal_logger_append(
	struct log_entry *entry,
	const char *msg,
	...)
{
	struct dal_logger *logger;

	if (!entry) {
		BREAK_TO_DEBUGGER();
		return;
	}

	logger = entry->logger;

	if (logger && logger->open_count > 0 &&
		dal_logger_should_log(logger, entry->major, entry->minor)) {

		uint32_t size;
		va_list args;
		char buffer[DAL_LOGGER_BUFFER_MAX_LOG_LINE_SIZE];

		va_start(args, msg);

		size = dm_log_to_buffer(
			buffer, DAL_LOGGER_BUFFER_MAX_LOG_LINE_SIZE, msg, args);

		if (size < DAL_LOGGER_BUFFER_MAX_LOG_LINE_SIZE - 1) {
			append_entry(entry, buffer, size);
		} else {
			append_entry(entry, "LOG_ERROR, line too long\n", 27);
		}

		va_end(args);
	}
}

uint32_t dal_logger_read(
	struct dal_logger *logger, /* <[in] */
	uint32_t output_buffer_size, /* <[in] */
	char *output_buffer, /* >[out] */
	uint32_t *bytes_read, /* >[out] */
	bool single_line)
{
	uint32_t bytes_remaining = 0;
	uint32_t bytes_read_count = 0;
	bool keep_reading = true;

	if (!logger || output_buffer == NULL || output_buffer_size == 0) {
		BREAK_TO_DEBUGGER();
		*bytes_read = 0;
		return 0;
	}

	lock(logger);

	/* Read until null terminator (if single_line==true,
	 *  max buffer size, or until we've read everything new
	 */

	do {
		char cur;

		/* Stop when we've read everything */
		if (logger->buffer_read_offset ==
			logger->buffer_write_offset) {

			break;
		}

		cur = logger->log_buffer[logger->buffer_read_offset];
		logger->buffer_read_offset++;

		/* Wrap read pointer if at end */
		if (logger->buffer_read_offset == logger->log_buffer_size) {

			logger->buffer_read_offset = 0;
			logger->read_wrap_count++;
		}

		/* Don't send null terminators to buffer */
		if (cur != '\0') {
			output_buffer[bytes_read_count] = cur;
			bytes_read_count++;
		} else if (single_line) {
			keep_reading = false;
		}

	} while (bytes_read_count <= output_buffer_size && keep_reading);

	/* We assume that reading can never be ahead of writing */
	if (logger->write_wrap_count > logger->read_wrap_count) {
		bytes_remaining = logger->log_buffer_size -
			logger->buffer_read_offset +
			logger->buffer_write_offset;
	} else {
		bytes_remaining = logger->buffer_write_offset -
				logger->buffer_read_offset;
	}

	/* reset write/read wrap count to 0 if we've read everything */
	if (bytes_remaining == 0) {

		logger->write_wrap_count = 0;
		logger->read_wrap_count = 0;
	}

	*bytes_read = bytes_read_count;
	unlock(logger);

	return bytes_remaining;
}

void dal_logger_open(
		struct dal_logger *logger,
		struct log_entry *entry, /* out */
		enum log_major major,
		enum log_minor minor)
{
	if (!entry) {
		BREAK_TO_DEBUGGER();
		return;
	}

	entry->major = LOG_MAJOR_COUNT;
	entry->minor = 0;
	entry->logger = logger;

	entry->buf = dm_alloc(DAL_LOGGER_BUFFER_MAX_SIZE * sizeof(char));

	entry->buf_offset = 0;
	entry->max_buf_bytes = DAL_LOGGER_BUFFER_MAX_SIZE * sizeof(char);

	logger->open_count++;
	entry->major = major;
	entry->minor = minor;

	log_heading(entry, major, minor);
}

void dal_logger_close(struct log_entry *entry)
{
	struct dal_logger *logger = entry->logger;

	if (logger && logger->open_count > 0) {
		logger->open_count--;
	} else {
		BREAK_TO_DEBUGGER();
		goto cleanup;
	}

	/* --Flush log_entry buffer-- */
	/* print to kernel console */
	log_to_debug_console(entry);
	/* log internally for dsat */
	log_to_internal_buffer(entry);

	/* TODO: Write end heading */

cleanup:
	if (entry->buf) {
		dm_free(entry->buf);
		entry->buf = NULL;
		entry->buf_offset = 0;
		entry->max_buf_bytes = 0;
	}
}

uint32_t dal_logger_get_mask(
	struct dal_logger *logger,
	enum log_major lvl_major, enum log_minor lvl_minor)
{
	uint32_t log_mask = 0;

	if (logger && lvl_major < LOG_MAJOR_COUNT)
		log_mask = logger->log_enable_mask_minors[lvl_major];

	log_mask &= 1 << lvl_minor;
	return log_mask;
}

uint32_t dal_logger_set_mask(
	struct dal_logger *logger,
	enum log_major lvl_major, enum log_minor lvl_minor)
{

	if (logger && lvl_major < LOG_MAJOR_COUNT) {
		if (lvl_minor == LOG_MINOR_MASK_ALL) {
			logger->log_enable_mask_minors[lvl_major] = 0xFFFFFFFF;
		} else {
			logger->log_enable_mask_minors[lvl_major] |=
				(1 << lvl_minor);
		}
		return 0;
	}
	return 1;
}

uint32_t dal_logger_get_masks(
	struct dal_logger *logger,
	enum log_major lvl_major)
{
	uint32_t log_mask = 0;

	if (logger && lvl_major < LOG_MAJOR_COUNT)
		log_mask = logger->log_enable_mask_minors[lvl_major];

	return log_mask;
}

void dal_logger_set_masks(
	struct dal_logger *logger,
	enum log_major lvl_major, uint32_t log_mask)
{
	if (logger && lvl_major < LOG_MAJOR_COUNT)
		logger->log_enable_mask_minors[lvl_major] = log_mask;
}

uint32_t dal_logger_unset_mask(
	struct dal_logger *logger,
	enum log_major lvl_major, enum log_minor lvl_minor)
{

	if (lvl_major < LOG_MAJOR_COUNT) {
		if (lvl_minor == LOG_MINOR_MASK_ALL) {
			logger->log_enable_mask_minors[lvl_major] = 0;
		} else {
			logger->log_enable_mask_minors[lvl_major] &=
				~(1 << lvl_minor);
		}
		return 0;
	}
	return 1;
}

uint32_t dal_logger_get_flags(
		struct dal_logger *logger)
{

	return logger->flags.value;
}

void dal_logger_set_flags(
		struct dal_logger *logger,
		union logger_flags flags)
{

	logger->flags = flags;
}

uint32_t dal_logger_get_buffer_size(struct dal_logger *logger)
{
	return DAL_LOGGER_BUFFER_MAX_SIZE;
}

uint32_t dal_logger_set_buffer_size(
		struct dal_logger *logger,
		uint32_t new_size)
{
	/* ToDo: implement dynamic size */

	/* return new size */
	return DAL_LOGGER_BUFFER_MAX_SIZE;
}

const struct log_major_info *dal_logger_enum_log_major_info(
		struct dal_logger *logger,
		unsigned int enum_index)
{
	const struct log_major_info *major_info;

	if (enum_index >= NUM_ELEMENTS(log_major_mask_info_tbl))
		return NULL;

	major_info = &log_major_mask_info_tbl[enum_index].major_info;
	return major_info;
}

const struct log_minor_info *dal_logger_enum_log_minor_info(
		struct dal_logger *logger,
		enum log_major major,
		unsigned int enum_index)
{
	const struct log_minor_info *minor_info = NULL;
	uint32_t i;

	for (i = 0; i < NUM_ELEMENTS(log_major_mask_info_tbl); i++) {

		const struct log_major_mask_info *maj_mask_info =
				&log_major_mask_info_tbl[i];

		if (maj_mask_info->major_info.major == major) {

			if (maj_mask_info->minor_tbl != NULL) {
				uint32_t j;

				for (j = 0; j < maj_mask_info->tbl_element_cnt; j++) {

					minor_info = &maj_mask_info->minor_tbl[j];

					if (minor_info->minor == enum_index)
						return minor_info;
				}
			}

			break;
		}
	}
	return NULL;

}

