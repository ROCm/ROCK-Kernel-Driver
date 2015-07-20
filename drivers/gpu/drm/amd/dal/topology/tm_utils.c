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

#include "dal_services.h"

#include "include/ddc_service_interface.h"
#include "include/connector_interface.h"

#include "tm_utils.h"
#include "tm_internal_types.h"

/* String representation of graphics objects */
const char *tm_utils_go_type_to_str(struct graphics_object_id id)
{
	switch (id.type) {
	case OBJECT_TYPE_GPU:
		return "GPU";
	case OBJECT_TYPE_ENCODER:
		return "Encoder";
	case OBJECT_TYPE_CONNECTOR:
		return "Connector";
	case OBJECT_TYPE_ROUTER:
		return "Router";
	case OBJECT_TYPE_AUDIO:
		return "Audio";
	case OBJECT_TYPE_CONTROLLER:
		return "Controller";
	case OBJECT_TYPE_CLOCK_SOURCE:
		return "ClockSource";
	case OBJECT_TYPE_ENGINE:
		return "Engine";
	default:
		return "Unknown";
	}
}

const char *tm_utils_go_enum_to_str(struct graphics_object_id id)
{
	switch (id.type) {
	case OBJECT_TYPE_UNKNOWN:
	case OBJECT_TYPE_GPU:
		return "\b";
	default:
		break;
	}

	switch (id.enum_id) {
	case ENUM_ID_1:
		return "1";
	case ENUM_ID_2:
		return "2";
	case ENUM_ID_3:
		return "3";
	case ENUM_ID_4:
		return "4";
	case ENUM_ID_5:
		return "5";
	case ENUM_ID_6:
		return "6";
	case ENUM_ID_7:
		return "7";
	default:
		return "?";
	}
}

const char *tm_utils_go_id_to_str(struct graphics_object_id id)
{
	switch (id.type) {
	case OBJECT_TYPE_ENCODER:
		return tm_utils_encoder_id_to_str(
				dal_graphics_object_id_get_encoder_id(id));
	case OBJECT_TYPE_CONNECTOR:
		return tm_utils_connector_id_to_str(
				dal_graphics_object_id_get_connector_id(id));
	case OBJECT_TYPE_AUDIO:
		return tm_utils_audio_id_to_str(
				dal_graphics_object_id_get_audio_id(id));
	case OBJECT_TYPE_CONTROLLER:
		return tm_utils_controller_id_to_str(
				dal_graphics_object_id_get_controller_id(id));
	case OBJECT_TYPE_CLOCK_SOURCE:
		return tm_utils_clock_source_id_to_str(
				dal_graphics_object_id_get_clock_source_id(id));
	case OBJECT_TYPE_ENGINE:
		return tm_utils_engine_id_to_str(
				dal_graphics_object_id_get_engine_id(id));
	default:
		return "\b";
	}
}

const char *tm_utils_encoder_id_to_str(enum encoder_id id)
{
	switch (id) {
	case ENCODER_ID_INTERNAL_LVDS:
		return "Int_LVDS";
	case ENCODER_ID_INTERNAL_TMDS1:
		return "Int_TMDS1";
	case ENCODER_ID_INTERNAL_TMDS2:
		return "Int_TMDS2";
	case ENCODER_ID_INTERNAL_DAC1:
		return "Int_DAC1";
	case ENCODER_ID_INTERNAL_DAC2:
		return "Int_DAC2";
	case ENCODER_ID_INTERNAL_SDVOA:
		return "Int_SDVOA";
	case ENCODER_ID_INTERNAL_SDVOB:
		return "Int_SDVOB";
	case ENCODER_ID_EXTERNAL_SI170B:
		return "Ext_Si170B";
	case ENCODER_ID_EXTERNAL_CH7303:
		return "Ext_CH7303";
	case ENCODER_ID_EXTERNAL_CH7301:
		return "Ext_CH7301";
	case ENCODER_ID_INTERNAL_DVO1:
		return "Int_DVO1";
	case ENCODER_ID_EXTERNAL_SDVOA:
		return "Ext_SDVOA";
	case ENCODER_ID_EXTERNAL_SDVOB:
		return "Ext_SDVOB";
	case ENCODER_ID_EXTERNAL_TITFP513:
		return "Ext_TITFP513";
	case ENCODER_ID_INTERNAL_LVTM1:
		return "Int_LVTM1";
	case ENCODER_ID_EXTERNAL_VT1623:
		return "Ext_VT1623";
	case ENCODER_ID_EXTERNAL_SI1930:
		return "Ext_Si1930";
	case ENCODER_ID_INTERNAL_HDMI:
		return "Int_HDMI";
	case ENCODER_ID_INTERNAL_KLDSCP_TMDS1:
		return "Int_Kldscp_TMDS1";
	case ENCODER_ID_INTERNAL_KLDSCP_DVO1:
		return "Int_Kldscp_DVO1";
	case ENCODER_ID_INTERNAL_KLDSCP_DAC1:
		return "Int_Kldscp_DAC1";
	case ENCODER_ID_INTERNAL_KLDSCP_DAC2:
		return "Int_Kldscp_DAC2";
	case ENCODER_ID_EXTERNAL_SI178:
		return "Ext_Si178";
	case ENCODER_ID_EXTERNAL_MVPU_FPGA:
		return "Ext_MVPU_FPGA";
	case ENCODER_ID_INTERNAL_DDI:
		return "Int_DDI";
	case ENCODER_ID_EXTERNAL_VT1625:
		return "Ext_VT1625";
	case ENCODER_ID_EXTERNAL_SI1932:
		return "Ext_Si1932";
	case ENCODER_ID_EXTERNAL_AN9801:
		return "Ext_AN9801";
	case ENCODER_ID_EXTERNAL_DP501:
		return "Ext_DP501";
	case ENCODER_ID_INTERNAL_UNIPHY:
		return "Int_Uniphy";
	case ENCODER_ID_INTERNAL_KLDSCP_LVTMA:
		return "Int_Kldscp_LVTMA";
	case ENCODER_ID_INTERNAL_UNIPHY1:
		return "Int_Uniphy1";
	case ENCODER_ID_INTERNAL_UNIPHY2:
		return "Int_Uniphy2";
	case ENCODER_ID_EXTERNAL_GENERIC_DVO:
		return "Ext_Generic_DVO";
	case ENCODER_ID_EXTERNAL_NUTMEG:
		return "Ext_Nutmeg";
	case ENCODER_ID_EXTERNAL_TRAVIS:
		return "Ext_Travis";
	case ENCODER_ID_INTERNAL_WIRELESS:
		return "Int_Wireless";
	case ENCODER_ID_INTERNAL_UNIPHY3:
		return "Int_Uniphy3";
	default:
		return "Unknown";
	}
}

const char *tm_utils_connector_id_to_str(enum connector_id id)
{
	switch (id) {
	case CONNECTOR_ID_SINGLE_LINK_DVII:
		return "SingleLinkDVII";
	case CONNECTOR_ID_DUAL_LINK_DVII:
		return "DualLinkDVII";
	case CONNECTOR_ID_SINGLE_LINK_DVID:
		return "SingleLinkDVID";
	case CONNECTOR_ID_DUAL_LINK_DVID:
		return "DualLinkDVID";
	case CONNECTOR_ID_VGA:
		return "VGA";
	case CONNECTOR_ID_COMPOSITE:
		return "Composite";
	case CONNECTOR_ID_SVIDEO:
		return "SVideo";
	case CONNECTOR_ID_YPBPR:
		return "YPbPr";
	case CONNECTOR_ID_DCONNECTOR:
		return "DConnector";
	case CONNECTOR_ID_9PIN_DIN:
		return "9pinDIN";
	case CONNECTOR_ID_SCART:
		return "SCART";
	case CONNECTOR_ID_HDMI_TYPE_A:
		return "HDMITypeA";
	case CONNECTOR_ID_LVDS:
		return "LVDS";
	case CONNECTOR_ID_7PIN_DIN:
		return "7pinDIN";
	case CONNECTOR_ID_PCIE:
		return "PCIE";
	case CONNECTOR_ID_DISPLAY_PORT:
		return "DisplayPort";
	case CONNECTOR_ID_EDP:
		return "EDP";
	case CONNECTOR_ID_WIRELESS:
		return "Wireless";
	default:
		return "Unknown";
	}
}

const char *tm_utils_audio_id_to_str(enum audio_id id)
{
	switch (id) {
	case AUDIO_ID_INTERNAL_AZALIA:
		return "Azalia";
	default:
		return "Unknown";
	}
}

const char *tm_utils_controller_id_to_str(enum controller_id id)
{
	switch (id) {
	case CONTROLLER_ID_D0:
		return "D0";
	case CONTROLLER_ID_D1:
		return "D1";
	case CONTROLLER_ID_D2:
		return "D2";
	case CONTROLLER_ID_D3:
		return "D3";
	case CONTROLLER_ID_D4:
		return "D4";
	case CONTROLLER_ID_D5:
		return "D5";
	case CONTROLLER_ID_UNDERLAY0:
		return "UNDERLAY0";
	default:
		return "Unknown";
	}
}

const char *tm_utils_clock_source_id_to_str(enum clock_source_id id)
{
	switch (id) {
	case CLOCK_SOURCE_ID_PLL0:
		return "CLOCK_SOURCE_PLL0";
	case CLOCK_SOURCE_ID_PLL1:
		return "CLOCK_SOURCE_PLL1";
	case CLOCK_SOURCE_ID_PLL2:
		return "CLOCK_SOURCE_PLL2";
	case CLOCK_SOURCE_ID_DCPLL:
		return "CLOCK_SOURCE_DCPLL";
	case CLOCK_SOURCE_ID_EXTERNAL:
		return "CLOCK_SOURCE_External";
	case CLOCK_SOURCE_ID_DFS:
		return "CLOCK_SOURCE_DFS";
	case CLOCK_SOURCE_ID_VCE:
		return "CLOCK_SOURCE_ID_VCE";
	case CLOCK_SOURCE_ID_DP_DTO:
		return "CLOCK_SOURCE_ID_DP_DTO";
	default:
		return "Unknown";
	}
}

const char *tm_utils_engine_id_to_str(enum engine_id id)
{
	switch (id) {
	case ENGINE_ID_DACA:
		return "DACA";
	case ENGINE_ID_DACB:
		return "DACB";
	case ENGINE_ID_DVO:
		return "DVO";
	case ENGINE_ID_DIGA:
		return "DIGA";
	case ENGINE_ID_DIGB:
		return "DIGB";
	case ENGINE_ID_DIGC:
		return "DIGC";
	case ENGINE_ID_DIGD:
		return "DIGD";
	case ENGINE_ID_DIGE:
		return "DIGE";
	case ENGINE_ID_DIGF:
		return "DIGF";
	case ENGINE_ID_DIGG:
		return "DIGG";
	case ENGINE_ID_VCE:
		return "VCE";
	default:
		return "Unknown";
	}
}

const char *tm_utils_signal_type_to_str(enum signal_type type)
{
	switch (type) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
		return "DVISingleLink";
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
		return "DVISingleLink1";
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		return "DVIDualLink";
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return "HDMITypeA";
	case SIGNAL_TYPE_LVDS:
		return "LVDS";
	case SIGNAL_TYPE_RGB:
		return "RGB";
	case SIGNAL_TYPE_YPBPR:
		return "YPbPr";
	case SIGNAL_TYPE_SCART:
		return "SCART";
	case SIGNAL_TYPE_COMPOSITE:
		return "Composite";
	case SIGNAL_TYPE_SVIDEO:
		return "SVideo";
	case SIGNAL_TYPE_DISPLAY_PORT:
		return "DisplayPort";
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		return "DisplayPortMst";
	case SIGNAL_TYPE_EDP:
		return "EDP";
	case SIGNAL_TYPE_DVO:
		return "SIGNAL_TYPE_DVO";
	case SIGNAL_TYPE_DVO24:
		return "SIGNAL_TYPE_DVO24";
	case SIGNAL_TYPE_MVPU_A:
		return "SIGNAL_TYPE_MVPU_A";
	case SIGNAL_TYPE_MVPU_B:
		return "SIGNAL_TYPE_MVPU_B";
	case SIGNAL_TYPE_MVPU_AB:
		return "SIGNAL_TYPE_MVPU_AB";
	case SIGNAL_TYPE_WIRELESS:
		return "SIGNAL_TYPE_WIRELESS";
	default:
		return "Unknown";

	}
}

const char *tm_utils_engine_priority_to_str(enum tm_engine_priority priority)
{
	switch (priority) {
	case TM_ENGINE_PRIORITY_MST_DP_MST_ONLY:
		return "Priority_MST_DPMstOnly";
	case TM_ENGINE_PRIORITY_MST_DP_CONNECTED:
		return "Priority_MST_DPConnected";
	case TM_ENGINE_PRIORITY_MST_DVI:
		return "Priority_MST_Dvi";
	case TM_ENGINE_PRIORITY_MST_HDMI:
		return "Priority_MST_Hdmi";
	case TM_ENGINE_PRIORITY_MST_DVI_CONNECTED:
		return "Priority_MST_DviConnected";
	case TM_ENGINE_PRIORITY_MST_HDMI_CONNECTED:
		return "Priority_MST_HdmiConnected";
	case TM_ENGINE_PRIORITY_NON_MST_CAPABLE:
		return "Priority_Non_MST_Capable";
	default:
		return "Priority_Unknown";
	}
}

const char *tm_utils_transmitter_id_to_str(struct graphics_object_id encoder)
{
	if (encoder.type != OBJECT_TYPE_ENCODER)
		return "\b";

	switch (dal_graphics_object_id_get_encoder_id(encoder)) {
	case ENCODER_ID_INTERNAL_UNIPHY: {
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return "PhyA";
		case ENUM_ID_2:
			return "PhyB";
		default:
			break;
		}
	}
		break;

	case ENCODER_ID_INTERNAL_UNIPHY1: {
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return "PhyC";
		case ENUM_ID_2:
			return "PhyD";
		default:
			break;
		}
	}
		break;

	case ENCODER_ID_INTERNAL_UNIPHY2: {
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return "PhyE";
		case ENUM_ID_2:
			return "PhyF";
		default:
			break;
		}
	}
		break;

	case ENCODER_ID_INTERNAL_UNIPHY3: {
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return "PhyG";
		default:
			break;
		}
	}
		break;

	case ENCODER_ID_EXTERNAL_NUTMEG: {
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return "NutmegCRT";
		default:
			break;
		}
	}
		break;
	case ENCODER_ID_EXTERNAL_TRAVIS: {
		switch (encoder.enum_id) {
		case ENUM_ID_1:
			return "TravisCRT";
		case ENUM_ID_2:
			return "TravisLCD";
		default:
			break;
		}
	}
		break;

	case ENCODER_ID_INTERNAL_DAC1:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC1: {
		return "DACA";
	}
		break;

	case ENCODER_ID_INTERNAL_DAC2:
	case ENCODER_ID_INTERNAL_KLDSCP_DAC2: {
		return "DACB";
	}
		break;

	case ENCODER_ID_INTERNAL_DVO1:
	case ENCODER_ID_INTERNAL_KLDSCP_DVO1: {
		return "DVO";
	}
		break;

	case ENCODER_ID_INTERNAL_WIRELESS: {
		return "Wireless";
	}
		break;

	default:
		break;
	}

	return "Unknown";
}

const char *tm_utils_hpd_line_to_str(enum hpd_source_id line)
{
	switch (line) {
	case HPD_SOURCEID1:
		return "HPD-1";
	case HPD_SOURCEID2:
		return "HPD-2";
	case HPD_SOURCEID3:
		return "HPD-3";
	case HPD_SOURCEID4:
		return "HPD-4";
	case HPD_SOURCEID5:
		return "HPD-5";
	case HPD_SOURCEID6:
		return "HPD-6";
	default:
		break;
	}

	return "No HPD";
}

const char *tm_utils_ddc_line_to_str(enum channel_id line)
{
	switch (line) {
	case CHANNEL_ID_DDC1:
		return "DDC-1";
	case CHANNEL_ID_DDC2:
		return "DDC-2";
	case CHANNEL_ID_DDC3:
		return "DDC-3";
	case CHANNEL_ID_DDC4:
		return "DDC-4";
	case CHANNEL_ID_DDC5:
		return "DDC-5";
	case CHANNEL_ID_DDC6:
		return "DDC-6";
	case CHANNEL_ID_DDC_VGA:
		return "DDC-VGA";
	case CHANNEL_ID_I2C_PAD:
		return "DDC-I2CPAD";
	default:
		break;
	}

	return "No DDC";
}

const char *tm_utils_device_type_to_str(enum dal_device_type device)
{
	switch (device) {
	case DEVICE_TYPE_LCD:		return "LCD";
	case DEVICE_TYPE_CRT:		return "CRT";
	case DEVICE_TYPE_DFP:		return "DFP";
	case DEVICE_TYPE_CV:		return "CV";
	case DEVICE_TYPE_TV:		return "TV";
	case DEVICE_TYPE_CF:		return "CF";
	case DEVICE_TYPE_WIRELESS:	return "Wireless";
	default:
		break;
	}

	return "Unknown";
}


bool tm_utils_is_edid_connector_type_valid_with_signal_type(
		enum display_dongle_type dongle_type,
		enum dcs_edid_connector_type edid_conn,
		enum signal_type signal)
{
	bool is_signal_digital;
	bool is_edid_digital;

	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_EDP: {
		is_signal_digital = true;
	}
		break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST: {
		/* DP connector can have converter (active dongle) attached,
		 that may convert digital signal to analog. In this case
		 EDID connector type will be analog. Here we need to check
		 the dongle type and switch to analog signal */
		if (dongle_type == DISPLAY_DONGLE_DP_VGA_CONVERTER)
			is_signal_digital = false;
		else
			is_signal_digital = true;

	}
		break;
	case SIGNAL_TYPE_RGB:
	case SIGNAL_TYPE_YPBPR:
	case SIGNAL_TYPE_SCART:
	case SIGNAL_TYPE_COMPOSITE:
	case SIGNAL_TYPE_SVIDEO: {
		is_signal_digital = false;
	}
		break;
	default:
		return false;
	}

	switch (edid_conn) {
	case EDID_CONNECTOR_DIGITAL:
	case EDID_CONNECTOR_DVI:
	case EDID_CONNECTOR_HDMIA:
	case EDID_CONNECTOR_MDDI:
	case EDID_CONNECTOR_DISPLAYPORT: {
		is_edid_digital = true;
	}
		break;
	case EDID_CONNECTOR_ANALOG: {
		is_edid_digital = false;
	}
		break;
	default:
		return false;
	}

	return (is_edid_digital == is_signal_digital);
}


enum tm_utils_display_type tm_utils_signal_to_display_type(
	enum signal_type signal)
{
	enum tm_utils_display_type res = DISPLAY_DFP;

	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_WIRELESS:
		res = DISPLAY_DFP;
		break;
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_EDP:
		res = DISPLAY_LCD_PANEL;
		break;
	case SIGNAL_TYPE_YPBPR:
		res = DISPLAY_COMPONENT_VIDEO;
		break;
	case SIGNAL_TYPE_RGB:
		res = DISPLAY_MONITOR;
		break;
	case SIGNAL_TYPE_SCART:
	case SIGNAL_TYPE_COMPOSITE:
	case SIGNAL_TYPE_SVIDEO:
		res = DISPLAY_TELEVISION;
		break;
	case SIGNAL_TYPE_DVO:
	default:
		/* @todo probably shouldn't need to handle DVO case */
		break;

	}
	return res;
}


enum dcs_interface_type dal_tm_utils_signal_type_to_interface_type(
		enum signal_type signal)
{
	/* VGA will be default interface */
	enum dcs_interface_type interface_type = INTERFACE_TYPE_VGA;

	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		interface_type = INTERFACE_TYPE_DVI;
		break;

	case SIGNAL_TYPE_LVDS:
		interface_type = INTERFACE_TYPE_LVDS;
		break;

	case SIGNAL_TYPE_EDP:
		interface_type = INTERFACE_TYPE_EDP;
		break;

	case SIGNAL_TYPE_RGB:
		interface_type = INTERFACE_TYPE_VGA;
		break;

	case SIGNAL_TYPE_YPBPR:
		interface_type = INTERFACE_TYPE_CV;
		break;

	case SIGNAL_TYPE_SCART:
	case SIGNAL_TYPE_COMPOSITE:
	case SIGNAL_TYPE_SVIDEO:
		interface_type = INTERFACE_TYPE_TV;
		break;

	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		interface_type = INTERFACE_TYPE_DP;
		break;

	case SIGNAL_TYPE_MVPU_A:
	case SIGNAL_TYPE_MVPU_B:
	case SIGNAL_TYPE_MVPU_AB:
		interface_type = INTERFACE_TYPE_CF;
		break;

	case SIGNAL_TYPE_WIRELESS:
		interface_type = INTERFACE_TYPE_WIRELESS;
		break;

	default:
		break;
	};

	return interface_type;
}


enum dal_device_type tm_utils_signal_type_to_device_type(
		enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_LVDS:
	case SIGNAL_TYPE_EDP:
		return DEVICE_TYPE_LCD;

	case SIGNAL_TYPE_RGB:
		return DEVICE_TYPE_CRT;

	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return DEVICE_TYPE_DFP;

	case SIGNAL_TYPE_YPBPR:
		return DEVICE_TYPE_CV;

	case SIGNAL_TYPE_SCART:
	case SIGNAL_TYPE_COMPOSITE:
	case SIGNAL_TYPE_SVIDEO:
		return DEVICE_TYPE_TV;

	case SIGNAL_TYPE_WIRELESS:
		return DEVICE_TYPE_WIRELESS;

	default:
		return DEVICE_TYPE_UNKNOWN;
	};

	return DEVICE_TYPE_UNKNOWN;
}

enum link_service_type tm_utils_signal_to_link_service_type(
		enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_EDP:
		return LINK_SERVICE_TYPE_DP_SST;

	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		return LINK_SERVICE_TYPE_DP_MST;

	default:
		return LINK_SERVICE_TYPE_LEGACY;
	};

	return LINK_SERVICE_TYPE_LEGACY;
}


enum tm_display_type tm_utils_device_id_to_tm_display_type(struct device_id id)
{
	enum tm_display_type type = TM_DISPLAY_TYPE_UNK;

	if (id.device_type == DEVICE_TYPE_CRT && id.enum_id == 1)
		type = TM_DISPLAY_TYPE_CRT;
	else if (id.device_type == DEVICE_TYPE_CRT && id.enum_id == 2)
		type = TM_DISPLAY_TYPE_CRT_DAC2;
	else if (id.device_type == DEVICE_TYPE_LCD && id.enum_id == 1)
		type = TM_DISPLAY_TYPE_LCD;
	else if (id.device_type == DEVICE_TYPE_TV && id.enum_id == 1)
		type = TM_DISPLAY_TYPE_TV;
	else if (id.device_type == DEVICE_TYPE_CV && id.enum_id == 1)
		type = TM_DISPLAY_TYPE_CV;
	else if (id.device_type == DEVICE_TYPE_DFP)
		type = TM_DISPLAY_TYPE_DFP;
	else if (id.device_type == DEVICE_TYPE_WIRELESS)
		type = TM_DISPLAY_TYPE_WIRELESS;

	return type;
}


/**
 * Function returns the downgraded signal (or the same) based on
 * the rule that DVI < HDMI and SingleLink < DualLink.
 * We assign to all signal types and connector types their
 * corresponding weights in terms of DVI/HDMI and SL/DL.
 */
enum signal_type tm_utils_get_downgraded_signal_type(
		enum signal_type signal,
		enum dcs_edid_connector_type connector_type)
{
	/*
	Connector types
	EDID_CONNECTOR_DIGITAL        DL DVI
	EDID_CONNECTOR_DVI            DL DVI
	EDID_CONNECTOR_HDMIA          SL HDMI
	EDID_CONNECTOR_MDDI           SL DVI
	EDID_CONNECTOR_DISPLAYPORT    SL DVI

	Signal types
	SIGNAL_TYPE_HDMI_TYPE_A           SL HDMI
	SIGNAL_TYPE_DVI_SINGLE_LINK       SL DVI
	SIGNAL_TYPE_DVI_DUAL_LINK         DL DVI
	*/

	bool dl1 = false;
	bool hdmi1 = false;
	bool dl2 = false;
	bool hdmi2 = false;
	bool dl3;
	bool hdmi3;

	switch (connector_type) {
	case EDID_CONNECTOR_DIGITAL:
	case EDID_CONNECTOR_DVI:
		dl1 = true;
		hdmi1 = false;
		break;
	case EDID_CONNECTOR_HDMIA:
		dl1 = false;
		hdmi1 = true;
		break;
	case EDID_CONNECTOR_MDDI:
	case EDID_CONNECTOR_DISPLAYPORT:
		dl1 = false;
		hdmi1 = false;
		break;
	default:
		return signal; /* No need to downgrade the signal */

	};

	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		dl2 = false;
		hdmi2 = true;
		break;
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		dl2 = true;
		hdmi2 = false;
		break;
	default:
		return signal; /* No need to downgrade the signal */

	};

	dl3 = dl1 && dl2;
	hdmi3 = hdmi1 && hdmi2;

	if (dl3 && !hdmi3)
		signal = SIGNAL_TYPE_DVI_DUAL_LINK;
	else if (!dl3 && hdmi3)
		signal = SIGNAL_TYPE_HDMI_TYPE_A;
	else if (!dl3 && !hdmi3)
		signal = SIGNAL_TYPE_DVI_SINGLE_LINK;

	return signal;
}


/* HdmiA --> SingleLink
 * DP remains DP (DP audio capability does not change signal) */
enum signal_type tm_utils_downgrade_to_no_audio_signal(enum signal_type signal)
{
	enum signal_type downgraded_signal = SIGNAL_TYPE_NONE;

	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		downgraded_signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		break;

	default:
		downgraded_signal = signal;
		break;
	}

	return downgraded_signal;
}


enum ddc_transaction_type tm_utils_get_ddc_transaction_type(
		enum signal_type sink_signal,
		enum signal_type asic_signal)
{
	enum ddc_transaction_type transaction_type = DDC_TRANSACTION_TYPE_NONE;

	if (sink_signal == asic_signal) {
		switch (sink_signal) {
		case SIGNAL_TYPE_DVI_SINGLE_LINK:
		case SIGNAL_TYPE_DVI_SINGLE_LINK1:
		case SIGNAL_TYPE_DVI_DUAL_LINK:
		case SIGNAL_TYPE_HDMI_TYPE_A:
		case SIGNAL_TYPE_LVDS:
		case SIGNAL_TYPE_RGB:
			transaction_type = DDC_TRANSACTION_TYPE_I2C;
			break;

		case SIGNAL_TYPE_DISPLAY_PORT:
		case SIGNAL_TYPE_EDP:
			transaction_type = DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
			break;

		case SIGNAL_TYPE_DISPLAY_PORT_MST:
			/* MST does not use I2COverAux, but there is the
			 * SPECIAL use case for "immediate dwnstrm device
			 * access" (EPR#370830). */
			transaction_type = DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
			break;

		default:
			break;
		}
	} else {
		switch (asic_signal) {
		case SIGNAL_TYPE_DISPLAY_PORT:

			if (sink_signal == SIGNAL_TYPE_RGB
				|| sink_signal == SIGNAL_TYPE_LVDS) {
				transaction_type =
				DDC_TRANSACTION_TYPE_I2C_OVER_AUX_WITH_DEFER;
			}
			break;

		case SIGNAL_TYPE_DVO:
		case SIGNAL_TYPE_DVO24:
			if (sink_signal == SIGNAL_TYPE_DVI_SINGLE_LINK
			|| sink_signal == SIGNAL_TYPE_DVI_DUAL_LINK
			|| sink_signal == SIGNAL_TYPE_DVI_SINGLE_LINK1
			|| sink_signal == SIGNAL_TYPE_HDMI_TYPE_A
			|| sink_signal == SIGNAL_TYPE_RGB) {
				transaction_type =
					DDC_TRANSACTION_TYPE_I2C_OVER_AUX;
			}
			break;

		default:
			break;
		}
	}

	return transaction_type;
}


/* We require that clock sharing group matches clock sharing level */
bool tm_utils_is_clock_sharing_mismatch(
		enum clock_sharing_level sharing_level,
		enum clock_sharing_group sharing_group)
{
	/* Common case we allow sharing group to be used at any sharing level */
	bool mismatch = false;

	switch (sharing_group) {
	case CLOCK_SHARING_GROUP_DISPLAY_PORT:
	case CLOCK_SHARING_GROUP_ALTERNATIVE_DP_REF:
		mismatch = (sharing_level
			< CLOCK_SHARING_LEVEL_DISPLAY_PORT_SHAREABLE);
		break;

	case CLOCK_SHARING_GROUP_DP_MST:
		mismatch = (sharing_level
			< CLOCK_SHARING_LEVEL_DP_MST_SHAREABLE);
		break;

	default:
		break;
	}

	return mismatch;
}


bool tm_utils_is_destructive_method(enum tm_detection_method method)
{
	switch (method) {
	case DETECTION_METHOD_DESTRUCTIVE:
	case DETECTION_METHOD_DESTRUCTIVE_AND_EMBEDDED:
	case DETECTION_METHOD_HOTPLUG:
		return true;
	default:
		break;
	}

	return false;
}

void tm_utils_set_bit(uint32_t *bitmap, uint8_t bit)
{
	*bitmap |= (1 << bit);
}

void tm_utils_clear_bit(uint32_t *bitmap, uint8_t bit)
{
	*bitmap &= ~(1 << bit);
}

bool tm_utils_test_bit(uint32_t *bitmap, uint8_t bit)
{
	return ((*bitmap & (1 << bit)) != 0);
}

void tm_utils_init_formatted_buffer(char *buf, uint32_t buf_size,
		const char *format, ...)
{
	va_list args;

	va_start(args, format);

	dal_log_to_buffer(buf, buf_size, format, args);

	va_end(args);
}

/******************************************************************************
 * TM Calc Subset implementation.
 *****************************************************************************/
struct tm_calc_subset *dal_tm_calc_subset_create(void)
{
	struct tm_calc_subset *tm_calc_subset;

	tm_calc_subset = dal_alloc(
		sizeof(struct tm_calc_subset));

	if (tm_calc_subset == NULL)
		return NULL;

	tm_calc_subset->max_subset_size = 0;
	tm_calc_subset->max_value = 0;
	tm_calc_subset->subset_size = 0;
	dal_memset(tm_calc_subset->buffer, 0,
			sizeof(tm_calc_subset->buffer));

	return tm_calc_subset;
}

void dal_tm_calc_subset_destroy(struct tm_calc_subset *subset)
{
	dal_free(subset);
	subset = NULL;
}

bool dal_tm_calc_subset_start(
	struct tm_calc_subset *subset,
	uint32_t max_value,
	uint32_t max_subset_size)
{
	if (max_subset_size < 1 ||
		max_subset_size > MAX_COFUNCTIONAL_PATHS)
		return false;

	subset->max_value = max_value;
	subset->max_subset_size = max_subset_size;
	subset->subset_size = 1;
	subset->buffer[0] = 0;

	return true;
}

uint32_t dal_tm_calc_subset_get_value(
	struct tm_calc_subset *subset,
	uint32_t index)
{
	if (index < subset->subset_size)
		return subset->buffer[index];

	return (uint32_t)(-1);
}

bool dal_tm_calc_subset_step(struct tm_calc_subset *subset)
{
	uint32_t next_value;

	if (subset->subset_size == 0 ||
		subset->subset_size > subset->max_subset_size)
		return false;

	/* Try to increase subset size. new entry will
	* be assigned subsequent value
	*/
	next_value = subset->buffer[subset->subset_size - 1] + 1;
	if (next_value < subset->max_value &&
		subset->subset_size < subset->max_subset_size) {

		subset->buffer[subset->subset_size++] = next_value;
		return true;
	}

	/*If we cannot increase subset size, try to
	* increase value of last entry in subset.
	* If we cannot increase value of last entry,
	* we reduce the size of the subset and try again
	*/
	return dal_tm_calc_subset_skip(subset);

}

bool dal_tm_calc_subset_skip(struct tm_calc_subset *subset)
{
	uint32_t next_value;

	/* Try to increase value of last entry in subset
	* If we cannot increase value of last entry,
	* we reduce the size of the subset and try again
	*/
	while (subset->subset_size > 0) {

		next_value = subset->buffer[subset->subset_size-1] + 1;
		if (next_value < subset->max_value) {

			subset->buffer[subset->subset_size-1] = next_value;
			return true;
		}
		subset->subset_size--;
	}

	/* We failed to advance and reach empty subset*/
	return false;

}

char *tm_utils_get_tm_resource_str(struct tm_resource *tm_resource)
{
	static char tmp_buf[128];
	struct graphics_object_id id =
		tm_resource->funcs->get_grph_id(tm_resource);

	tm_utils_init_formatted_buffer(
		tmp_buf,
		sizeof(tmp_buf),
		"0x%08X:[%u-%u-%u]: (%s %s-%s %s)",
		*(uint32_t *)(&id),
		id.type,
		id.id,
		id.enum_id,
		tm_utils_go_type_to_str(id),
		tm_utils_go_id_to_str(id),
		tm_utils_go_enum_to_str(id),
		tm_utils_transmitter_id_to_str(id));

	return tmp_buf;
}

bool tm_utils_is_supported_engine(union supported_stream_engines se,
		enum engine_id engine)
{
	bool rc = false;

	switch (engine) {
	case ENGINE_ID_DIGA:
		rc = se.engine.ENGINE_ID_DIGA;
		break;
	case ENGINE_ID_DIGB:
		rc = se.engine.ENGINE_ID_DIGB;
		break;
	case ENGINE_ID_DIGC:
		rc = se.engine.ENGINE_ID_DIGC;
		break;
	case ENGINE_ID_DIGD:
		rc = se.engine.ENGINE_ID_DIGD;
		break;
	case ENGINE_ID_DIGE:
		rc = se.engine.ENGINE_ID_DIGE;
		break;
	case ENGINE_ID_DIGF:
		rc = se.engine.ENGINE_ID_DIGF;
		break;
	case ENGINE_ID_DIGG:
		rc = se.engine.ENGINE_ID_DIGG;
		break;
	case ENGINE_ID_DVO:
		rc = se.engine.ENGINE_ID_DVO;
		break;
	case ENGINE_ID_DACA:
		rc = se.engine.ENGINE_ID_DACA;
		break;
	case ENGINE_ID_DACB:
		rc = se.engine.ENGINE_ID_DACB;
		break;
	case ENGINE_ID_VCE:
		rc = se.engine.ENGINE_ID_VCE;
		break;
	default:
		break;
	}

	return rc;
}

bool tm_utils_is_dp_connector(const struct connector *cntr)
{
	if (dal_graphics_object_id_get_connector_id(
		dal_connector_get_graphics_object_id(cntr)) ==
				CONNECTOR_ID_DISPLAY_PORT)
		return true;
	else
		return false;
}

bool tm_utils_is_dp_asic_signal(const struct display_path *display_path)
{
	return dal_is_dp_signal(dal_display_path_get_query_signal(
			display_path, ASIC_LINK_INDEX));
}
