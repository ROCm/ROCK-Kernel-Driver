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

/**
 * \file tm_internal_types.h
 *
 * \brief Internal types for use inside of Topology Manager.
 */

#ifndef __DAL_TM_INTERNAL_TYPES_H__
#define __DAL_TM_INTERNAL_TYPES_H__

#include "include/display_path_types.h"
#include "include/grph_object_ctrl_defs.h"
#include "include/timing_service_interface.h"
#include "include/logger_interface.h"
#include "include/ddc_service_interface.h"


/*****************
 Debug facilities
******************/

#define TM_BREAK_TO_DEBUGGER()	/*ASSERT_CRITICAL(0)*/

/* debugging macro definitions */
#define TM_IFACE_TRACE() \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_IFACE_TRACE, \
			"%s():line:%d\n", __func__, __LINE__)

#define TM_RESOURCES(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_RESOURCES, __VA_ARGS__)

#define TM_ENCODER_CTL(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_ENCODER_CTL, __VA_ARGS__)

#define TM_ENG_ASN(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_ENG_ASN, __VA_ARGS__)

#define TM_CONTROLLER_ASN(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_CONTROLLER_ASN, __VA_ARGS__)

#define TM_PWR_GATING(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_PWR_GATING, __VA_ARGS__)

#define TM_BUILD_DSP_PATH(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_BUILD_DSP_PATH, __VA_ARGS__)

#define TM_INFO(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_INFO, __VA_ARGS__)

#define TM_DISPLAY_DETECT(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_DISPLAY_DETECT, __VA_ARGS__)

#define TM_LINK_SRV(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_LINK_SRV, __VA_ARGS__)

#define TM_COFUNC_PATH(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_COFUNC_PATH, __VA_ARGS__)

#define TM_HPD_IRQ(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_HW_TRACE, LOG_MINOR_HW_TRACE_INTERRUPT, __VA_ARGS__)

#define TM_MPO(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_HW_TRACE, LOG_MINOR_HW_TRACE_MPO, __VA_ARGS__)

#define TM_NOT_IMPLEMENTED()  \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_TM, LOG_MINOR_TM_NOT_IMPLEMENTED, \
				"%s()\n", __func__)

#define TM_ASSERT(condition) \
	do { \
		if (!(condition)) { \
			dal_logger_write(dal_context->logger, \
				LOG_MAJOR_TM, LOG_MINOR_TM_INFO, \
				"TM_ASSERT: '%s'\n", #condition); \
			TM_BREAK_TO_DEBUGGER(); \
		} \
	} while (0)

#define TM_ERROR(...) \
	do { \
		dal_logger_write(dal_context->logger, LOG_MAJOR_ERROR, \
			LOG_MINOR_COMPONENT_TOPOLOGY_MANAGER, \
			__VA_ARGS__); \
		TM_BREAK_TO_DEBUGGER(); \
	} while (0)

#define TM_WARNING(...) \
	dal_logger_write(dal_context->logger, LOG_MAJOR_WARNING, \
		LOG_MINOR_COMPONENT_TOPOLOGY_MANAGER, \
		__VA_ARGS__)

/*******
 Enums
********/

enum tm_display_type {
	TM_DISPLAY_TYPE_UNK = 0x00000000,
	TM_DISPLAY_TYPE_CRT = 0x00000001,
	TM_DISPLAY_TYPE_CRT_DAC2 = 0x00000002,
	TM_DISPLAY_TYPE_LCD = 0x00000004,
	TM_DISPLAY_TYPE_TV = 0x00000008,
	TM_DISPLAY_TYPE_CV = 0x00000010,
	TM_DISPLAY_TYPE_DFP = 0x00000020,
	TM_DISPLAY_TYPE_WIRELESS = 0x00000040
};

enum tm_stereo_priority {
	/* Lowest priority */
	TM_STEREO_PRIORITY_UNDEFINED = 0,
	/* DVO which can be used as display path resource for their display */
	TM_STEREO_PRIORITY_DISPLAYPATH_RESOURCE_DVO,
	/* DAC which can be used as display path resource for their display */
	TM_STEREO_PRIORITY_DISPLAYPATH_RESOURCE_DAC,
	/* DAC */
	TM_STEREO_PRIORITY_DAC,
	/* DVO */
	TM_STEREO_PRIORITY_DVO,
	/* Highest priority, stereo resource located on display path itself */
	TM_STEREO_PRIORITY_ON_PATH
};

/* We always prefer path that outputs VGA */
enum tm_path_stereo_priority {
	 /* Invalid priority */
	TM_PATH_STEREO_PRIORITY_UNDEFINED = 0,
	 /* Lowest priority */
	TM_PATH_STEREO_PRIORITY_DEFAULT,
	TM_PATH_STEREO_PRIORITY_DISPLAYPORT,
	TM_PATH_STEREO_PRIORITY_HDMI,
	TM_PATH_STEREO_PRIORITY_DVI,
	/* Active dongle that converts to VGA signal */
	TM_PATH_STEREO_PRIORITY_VGA_CONVERTER,
	/* ASIC signal is not VGA, but there is external encoder
	 * which converts to VGA */
	TM_PATH_STEREO_PRIORITY_VGA_EXT_ENCODER,
	/* ASIC and Sink signal are VGA */
	TM_PATH_STEREO_PRIORITY_VGA_NATIVE
};

/******************************************************************************
 MST Display Path Stream Engine resources can be used by Display Paths
 The priority of the Stream Engine resource assignment is defined as follows:
 For DP MST Display Path Stream Engine resource with highest priority should
 be assigned to make available the resource with lower priority for
 non MST DP display path Resource with the priority
 TM_ENGINE_PRIORITY_NON_MST_CAPABLE will not be used by MST DP
 TM_ENGINE_PRIORITY_MST_DP_MST_ONLY has highest priority to assign engine
 to MST path
******************************************************************************/
enum tm_engine_priority {
	/* DP_MST_ONLY not driving any connector, and could only be
	 * used as MST stream Engine. */
	TM_ENGINE_PRIORITY_MST_DP_MST_ONLY = 0,
	TM_ENGINE_PRIORITY_MST_DP_CONNECTED,
	TM_ENGINE_PRIORITY_MST_DVI,
	TM_ENGINE_PRIORITY_MST_HDMI,
	TM_ENGINE_PRIORITY_MST_DVI_CONNECTED,
	TM_ENGINE_PRIORITY_MST_HDMI_CONNECTED,
	TM_ENGINE_PRIORITY_NON_MST_CAPABLE,
	TM_ENGINE_PRIORITY_UNKNOWN
};

enum tm_encoder_ctx_priority {
	TM_ENCODER_CTX_PRIORITY_INVALID = 0,
	TM_ENCODER_CTX_PRIORITY_DEFAULT,
	TM_ENCODER_CTX_PRIORITY_CONNECTED,
	TM_ENCODER_CTX_PRIORITY_ACQUIRED,
	TM_ENCODER_CTX_PRIORITY_ACQUIRED_CONNECTED,
	/* should be equal to highest */
	TM_ENCODER_CTX_PRIORITY_HIGHEST =
			TM_ENCODER_CTX_PRIORITY_ACQUIRED_CONNECTED
};

enum tm_interrupt_type {
	TM_INTERRUPT_TYPE_TIMER = 0,
	TM_INTERRUPT_TYPE_HOTPLUG,
	TM_INTERRUPT_TYPE_COUNT
};

#define TM_DECODE_INTERRUPT_TYPE(type) \
	(type == TM_INTERRUPT_TYPE_TIMER) ? "TIMER" : \
	(type == TM_INTERRUPT_TYPE_HOTPLUG) ? "HOTPLUG" : \
			"Invalid"

enum tm_power_gate_state {
	TM_POWER_GATE_STATE_NONE = 0,
	TM_POWER_GATE_STATE_OFF,
	TM_POWER_GATE_STATE_ON
};

enum tm_acquire_method {
	/* Activates all resources, checks for co-functionality and
	 * updates HW and Display path context if needed */
	TM_ACQUIRE_METHOD_HW = 0,
	/* Checks for co-functionality only. Will NOT change HW state. */
	TM_ACQUIRE_METHOD_SW
};

/************
 Structures
*************/

/* Used when building display paths */
struct tm_display_path_init_data {
	struct connector *connector;
	uint32_t num_of_encoders;
	/* Encoders in reverse order, starting with connector */
	struct encoder *encoders[MAX_NUM_OF_LINKS_PER_PATH];

	struct ddc_service *ddc_service;
	/* DeviceType_Unknown if this is real display path */
	struct device_id faked_path_device_id;
	enum signal_type sink_signal;
};


#endif /* __DAL_TM_INTERNAL_TYPES_H__ */
