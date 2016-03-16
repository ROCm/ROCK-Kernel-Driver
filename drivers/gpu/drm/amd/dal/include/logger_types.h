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

#ifndef __DAL_LOGGER_TYPES_H__
#define __DAL_LOGGER_TYPES_H__

/*
 * TODO: This logger functionality needs to be implemented and reworked.
 */

struct dal_logger;

enum log_major {
/*00*/
	LOG_MAJOR_ERROR = 0,	/*< DAL subcomponent error MSG*/
/*01*/  LOG_MAJOR_WARNING,	/*< DAL subcomponent warning MSG*/
/*02*/  LOG_MAJOR_INTERFACE_TRACE,/*< DAL subcomponent interface tracing*/
/*03*/  LOG_MAJOR_HW_TRACE,	/*< Log ASIC register read/write,
				* ATOMBIOS exec table call and delays*/

/*04*/  LOG_MAJOR_MST,		/*< related to multi-stream*/
/*05*/  LOG_MAJOR_DCS,		/*< related to Dcs*/
/*06*/  LOG_MAJOR_DCP,		/*< related to Dcp grph and ovl,gamam and csc*/
/*07*/  LOG_MAJOR_BIOS,		/*< related to BiosParser*/
/*08*/  LOG_MAJOR_REGISTER,	/*< register access*/
/*09*/  LOG_MAJOR_INFO_PACKETS,	/*< info packets*/
/*10*/  LOG_MAJOR_DSAT,		/*< related
				*	to Display System Analysis Tool*/
/*11*/  LOG_MAJOR_EC,		/*< External Components - MCIL Events/queries,
				*	PPLib notifications/queries*/
/*12*/  LOG_MAJOR_BWM,		/*< related to Bandwidth Manager*/
/*13*/  LOG_MAJOR_MODE_ENUM,	/*< related to mode enumeration*/
/*14*/  LOG_MAJOR_I2C_AUX,	/*< i2c and aux channel log*/
/*15*/  LOG_MAJOR_LINE_BUFFER,	/*< Line Buffer object logging activity*/
/*16*/  LOG_MAJOR_HWSS,		/*< HWSS object logging activity*/
/*17*/  LOG_MAJOR_OPTIMIZATION,	/*< Optimization code path*/
/*18*/  LOG_MAJOR_PERF_MEASURE,	/*< Performance measurement dumps*/
/*19*/  LOG_MAJOR_SYNC,		/*< related to HW and SW Synchronization*/
/*20*/  LOG_MAJOR_BACKLIGHT,	/*< related to backlight */
/*21*/  LOG_MAJOR_INTERRUPTS,	/*< logging for all interrupts */

/*22*/  LOG_MAJOR_TM,		/*< related to Topology Manager*/
/*23*/  LOG_MAJOR_DISPLAY_SERVICE, /*< related to Display Service*/
/*24*/	LOG_MAJOR_FEATURE_OVERRIDE,	/*< related to features*/
/*25*/	LOG_MAJOR_DETECTION,	/*< related to detection*/
/*26*/	LOG_MAJOR_CONNECTIVITY,	/*< related to connectivity*/
	LOG_MAJOR_COUNT,	/*< count of the Major categories*/
};

/**
* @brief defines minor switch for logging.  each of these define sub category
*        of log message per LogMajor
*/

enum log_minor {

	/* Special case for 'all' checkbox */
	LOG_MINOR_MASK_ALL = (uint32_t)-1, /* 0xFFFFFFFF */
/**
* @brief defines minor category for
*         LOG_MAJOR_ERROR,
*         LOG_MAJOR_WARNING,
*         LOG_MAJOR_INTERFACE_TRACE
*
* @note  each DAL subcomponent should have a corresponding enum
*/
	LOG_MINOR_COMPONENT_LINK_SERVICE = 0,
	LOG_MINOR_COMPONENT_DAL_INTERFACE,
	LOG_MINOR_COMPONENT_HWSS,
	LOG_MINOR_COMPONENT_ADAPTER_SERVICE,
	LOG_MINOR_COMPONENT_DISPLAY_SERVICE,
	LOG_MINOR_COMPONENT_TOPOLOGY_MANAGER,
	LOG_MINOR_COMPONENT_ENCODER,
	LOG_MINOR_COMPONENT_I2C_AUX,
	LOG_MINOR_COMPONENT_AUDIO,
	LOG_MINOR_COMPONENT_DISPLAY_CAPABILITY_SERVICE,
	LOG_MINOR_COMPONENT_DMCU,
	LOG_MINOR_COMPONENT_GPU,
	LOG_MINOR_COMPONENT_CONTROLLER,
	LOG_MINOR_COMPONENT_ISR,
	LOG_MINOR_COMPONENT_BIOS,
	LOG_MINOR_COMPONENT_DC,
	LOG_MINOR_COMPONENT_IRQ_SERVICE,

/**
* @brief define minor category for LogMajor_HardwareTrace
*
* @note  defines functionality based HW programming trace
*/
	LOG_MINOR_HW_TRACE_MST = 0,
	LOG_MINOR_HW_TRACE_TRAVIS,
	LOG_MINOR_HW_TRACE_HOTPLUG,
	LOG_MINOR_HW_TRACE_LINK_TRAINING,
	LOG_MINOR_HW_TRACE_SET_MODE,
	LOG_MINOR_HW_TRACE_RESUME_S3,
	LOG_MINOR_HW_TRACE_RESUME_S4,
	LOG_MINOR_HW_TRACE_BOOTUP,
	LOG_MINOR_HW_TRACE_AUDIO,
	LOG_MINOR_HW_TRACE_HPD_IRQ,
	LOG_MINOR_HW_TRACE_INTERRUPT,
	LOG_MINOR_HW_TRACE_MPO,

/**
* @brief defines minor category for LogMajor_Mst
*
* @note  define sub functionality related to MST
*/
	LOG_MINOR_MST_IRQ_HPD_RX = 0,
	LOG_MINOR_MST_IRQ_TIMER,
	LOG_MINOR_MST_NATIVE_AUX,
	LOG_MINOR_MST_SIDEBAND_MSG,
	LOG_MINOR_MST_MSG_TRANSACTION,
	LOG_MINOR_MST_SIDEBAND_MSG_PARSED,
	LOG_MINOR_MST_MSG_TRANSACTION_PARSED,
	LOG_MINOR_MST_AUX_MSG_DPCD_ACCESS,
	LOG_MINOR_MST_PROGRAMMING,
	LOG_MINOR_MST_TOPOLOGY_DISCOVERY,
	LOG_MINOR_MST_CONVERTER_CAPS,

/**
* @brief defines minor category for LogMajor_DCS
*
* @note  should define sub functionality related to Dcs
*/
	LOG_MINOR_DCS_EDID_EMULATOR = 0,
	LOG_MINOR_DCS_DONGLE_DETECTION,

/**
* @brief defines minor category for DCP
*
* @note  should define sub functionality related to Dcp
*/
	LOG_MINOR_DCP_GAMMA_GRPH = 0,
	LOG_MINOR_DCP_GAMMA_OVL,
	LOG_MINOR_DCP_CSC_GRPH,
	LOG_MINOR_DCP_CSC_OVL,
	LOG_MINOR_DCP_SCALER,
	LOG_MINOR_DCP_SCALER_TABLES,
/**
* @brief defines minor category for LogMajor_Bios
*
* @note define sub functionality related to BiosParser
*/
	LOG_MINOR_BIOS_CMD_TABLE = 0,
/**
* @brief defines minor category for LogMajor_Bios
*
* @note define sub functionality related to BiosParser
*/
	LOG_MINOR_REGISTER_INDEX = 0,
/**
* @brief defines minor category for info packets
*
*/
	LOG_MINOR_INFO_PACKETS_HDMI = 0,

/**
* @brief defines minor category for LOG_MAJOR_DSAT
*
* @note define sub functionality related to Display System Analysis Tool
*/
	LOG_MINOR_DSAT_LOGGER = 0,
	LOG_MINOR_DSAT_GET_EDID,
	LOG_MINOR_DSAT_EDID_OVERRIDE,
	LOG_MINOR_DSAT_SET_ADJUSTMENTS,
	LOG_MINOR_DSAT_GET_ADJUSTMENTS,

/**
* @brief defines minor category for LOG_MAJOR_EC
*
* @note define sub functionality related to External components notifications
*/
	LOG_MINOR_EC_PPLIB_NOTIFY = 0,
	LOG_MINOR_EC_PPLIB_QUERY,

/**
* @brief defines minor category for LOG_MAJOR_BWM
*
* @note define sub functionality related to Bandwidth Manager
*/
	LOG_MINOR_BWM_MODE_VALIDATION = 0,
	LOG_MINOR_BWM_REQUIRED_BANDWIDTH_CALCS,

/**
* @brief define minor category for LogMajor_ModeEnum
*
* @note  defines functionality mode enumeration trace
*/
	LOG_MINOR_MODE_ENUM_BEST_VIEW_CANDIDATES = 0,
	LOG_MINOR_MODE_ENUM_VIEW_SOLUTION,
	LOG_MINOR_MODE_ENUM_TS_LIST_BUILD,
	LOG_MINOR_MODE_ENUM_TS_LIST,
	LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST,
	LOG_MINOR_MODE_ENUM_MASTER_VIEW_LIST_UPDATE,

/**
* @brief defines minor category for LogMajor_I2C_AUX
*
* @note define sub functionality related to I2c and Aux Channel Log
*/
	LOG_MINOR_I2C_AUX_LOG = 0,
	LOG_MINOR_I2C_AUX_AUX_TIMESTAMP,
	LOG_MINOR_I2C_AUX_CFG,

/**
* @brief defines minor category for LogMajor_LineBuffer
*
* @note define sub functionality related to LineBuffer
*/
	LOG_MINOR_LINE_BUFFER_POWERGATING = 0,

/**
* @brief defines minor category for LogMajor_HWSS
*
* @note define sub functionality related to HWSS
*/
	LOG_MINOR_HWSS_TAPS_VALIDATION = 0,

/**
* @brief defines minor category for LogMajor_Optimization
*
* @note define sub functionality related to Optimization
*/
	LOG_MINOR_OPTMZ_GENERAL = 0,
	LOG_MINOR_OPTMZ_DO_NOT_TURN_OFF_VCC_DURING_SET_MODE,

/**
* @brief defines minor category for LogMajor_PerfMeasure
*
* @note define sub functionality related to Performance measurement dumps
*/
	LOG_MINOR_PERF_MEASURE_GENERAL = 0,
	LOG_MINOR_PERF_MEASURE_HEAP_MEMORY,

/**
* @brief defines minor category for LogMajor_Sync
*
* @note define sub functionality related to HW and SW Synchronization
*/
	LOG_MINOR_SYNC_HW_CLOCK_ADJUST = 0,
	LOG_MINOR_SYNC_TIMING,

/**
* @brief defines minor category for LogMajor_Backlight
*
* @note define sub functionality related to backlight (including VariBright)
*/
	LOG_MINOR_BACKLIGHT_BRIGHTESS_CAPS = 0,
	LOG_MINOR_BACKLIGHT_DMCU_DELTALUT,
	LOG_MINOR_BACKLIGHT_DMCU_BUILD_DELTALUT,
	LOG_MINOR_BACKLIGHT_INTERFACE,
	LOG_MINOR_BACKLIGHT_LID,

/**
* @brief defines minor category for LOG_MAJOR_TM
*
* @note define sub functionality related to Topology Manager
*/
	LOG_MINOR_TM_INFO = 0,
	LOG_MINOR_TM_IFACE_TRACE,
	LOG_MINOR_TM_RESOURCES,
	LOG_MINOR_TM_ENCODER_CTL,
	LOG_MINOR_TM_ENG_ASN,
	LOG_MINOR_TM_CONTROLLER_ASN,
	LOG_MINOR_TM_PWR_GATING,
	LOG_MINOR_TM_BUILD_DSP_PATH,
	LOG_MINOR_TM_DISPLAY_DETECT,
	LOG_MINOR_TM_LINK_SRV,
	LOG_MINOR_TM_NOT_IMPLEMENTED,
	LOG_MINOR_TM_COFUNC_PATH,

/**
* @brief defines minor category for LOG_MAJOR_DISPLAY_SERVICE
*
* @note define sub functionality related to Display Service
*/
	LOG_MINOR_DS_MODE_SETTING = 0,

/**
* @brief defines minor category for LOG_MAJOR_FEATURE_OVERRIDE
*
* @note define sub functionality related to features in adapter service
*/
	LOG_MINOR_FEATURE_OVERRIDE = 0,

/**
* @brief defines minor category for LOG_MAJOR_DETECTION
*
* @note define sub functionality related to detection
*/
	LOG_MINOR_DETECTION_EDID_PARSER = 0,
	LOG_MINOR_DETECTION_DP_CAPS,

/**
* @brief defines minor category for LOG_MAJOR_CONNECTIVITY
*
* @note define sub functionality related to connectivity
*/
	LOG_MINOR_CONNECTIVITY_MODE_SET = 0,
	LOG_MINOR_CONNECTIVITY_DETECTION,
	LOG_MINOR_CONNECTIVITY_LINK_TRAINING,
	LOG_MINOR_CONNECTIVITY_LINK_LOSS,
	LOG_MINOR_CONNECTIVITY_UNDERFLOW,

};

union logger_flags {
	struct {
		uint32_t ENABLE_CONSOLE:1; /* Print to console */
		uint32_t ENABLE_BUFFER:1; /* Print to buffer */
		uint32_t RESERVED:30;
	} bits;
	uint32_t value;
};

struct log_entry {

	struct dal_logger *logger;
	enum log_major major;
	enum log_minor minor;

	char *buf;
	uint32_t buf_offset;
	uint32_t max_buf_bytes;
};

/**
* Structure for enumerating LogMajors and LogMinors
*/

#define MAX_MAJOR_NAME_LEN 32
#define MAX_MINOR_NAME_LEN 32

struct log_major_info {
	enum log_major major;
	char major_name[MAX_MAJOR_NAME_LEN];
};

struct log_minor_info {
	enum log_minor minor;
	char minor_name[MAX_MINOR_NAME_LEN];
};

#endif /* __DAL_LOGGER_TYPES_H__ */
