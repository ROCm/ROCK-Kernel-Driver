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

#include "dm_services.h"

#include "include/logger_interface.h"
#include "../hw_ctx_audio.h"
#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"
#include "hw_ctx_audio_dce80.h"

#define FROM_BASE(ptr) \
	container_of((ptr), struct hw_ctx_audio_dce80, base)

#define DP_SEC_AUD_N__DP_SEC_AUD_N__DEFAULT 0x8000
#define DP_AUDIO_DTO_MODULE_WITHOUT_SS 360
#define DP_AUDIO_DTO_PHASE_WITHOUT_SS 24

#define DP_SEC_TIMESTAMP__DP_SEC_TIMESTAMP_MODE__AUDIO_FRONT_END 0
#define DP_SEC_TIMESTAMP__DP_SEC_TIMESTAMP_MODE__AUTO_CALC 1
#define DP_SEC_TIMESTAMP__DP_SEC_TIMESTAMP_MODE__REGISTER_PROGRAMMABLE 2

#define FIRST_AUDIO_STREAM_ID 1

static const uint32_t engine_offset[] = {
	0,
	mmDIG1_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
	mmDIG2_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
	mmDIG3_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
	mmDIG4_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
	mmDIG5_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL,
	mmDIG6_DIG_FE_CNTL - mmDIG0_DIG_FE_CNTL
};
/* --- static functions --- */

/* static void dal_audio_destruct_hw_ctx_audio_dce80(
	struct hw_ctx_audio_dce80 *ctx);*/

static void destroy(
	struct hw_ctx_audio **ptr)
{
	struct hw_ctx_audio_dce80 *hw_ctx_dce80;

	hw_ctx_dce80 = container_of(
		*ptr, struct hw_ctx_audio_dce80, base);

	/* release memory allocated for struct hw_ctx_audio_dce80 */
	dm_free(hw_ctx_dce80);

	*ptr = NULL;
}

/* ---  helpers --- */

static void write_indirect_azalia_reg(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t reg_index,
	uint32_t reg_data)
{
	uint32_t addr = 0;
	uint32_t value = 0;
	/* AZALIA_F0_CODEC_ENDPOINT_INDEX  endpoint index  */
	{
		addr =
			FROM_BASE(hw_ctx)->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_index;

		set_reg_field_value(value, reg_index,
			AZALIA_F0_CODEC_ENDPOINT_INDEX,
			AZALIA_ENDPOINT_REG_INDEX);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* AZALIA_F0_CODEC_ENDPOINT_DATA  endpoint data  */
	{
		addr =
			FROM_BASE(hw_ctx)->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_data;

		value = 0;
		set_reg_field_value(value, reg_data,
			AZALIA_F0_CODEC_ENDPOINT_DATA,
			AZALIA_ENDPOINT_REG_DATA);
		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	dal_logger_write(
		hw_ctx->ctx->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_AUDIO,
		"AUDIO:write_indirect_azalia_reg: index: %u  data: %u\n",
		reg_index, reg_data);
}

static uint32_t read_indirect_azalia_reg(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t reg_index)
{
	uint32_t ret_val = 0;
	uint32_t addr = 0;
	uint32_t value = 0;

	/* AZALIA_F0_CODEC_ENDPOINT_INDEX  endpoint index  */
	{
		addr =
			FROM_BASE(hw_ctx)->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_index;

		set_reg_field_value(value, reg_index,
			AZALIA_F0_CODEC_ENDPOINT_INDEX,
			AZALIA_ENDPOINT_REG_INDEX);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* AZALIA_F0_CODEC_ENDPOINT_DATA  endpoint data  */
	{
		addr =
			FROM_BASE(hw_ctx)->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_data;

		value = dm_read_reg(hw_ctx->ctx, addr);
		ret_val = value;
	}

	dal_logger_write(
		hw_ctx->ctx->logger,
		LOG_MAJOR_HW_TRACE,
		LOG_MINOR_HW_TRACE_AUDIO,
		"AUDIO:read_indirect_azalia_reg: index: %u  data: %u\n",
		reg_index, ret_val);

	return ret_val;
}

/* expose/not expose HBR capability to Audio driver */
static void set_high_bit_rate_capable(
	const struct hw_ctx_audio *hw_ctx,
	bool capable)
{
	uint32_t value = 0;

	/* set high bit rate audio capable*/
	value = read_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR);

	set_reg_field_value(value, capable,
		AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR,
		HBR_CAPABLE);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR,
		value);
}

/* set HBR channnel count */
/*static void set_hbr_channel_count(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t hbr_channel_count)
{
	if (hbr_channel_count > 7)
		return;

	{
		union AZALIA_F0_CODEC_CHANNEL_COUNT_CONTROL value;

		value.u32All = dal_read_reg(
			mmAZALIA_F0_CODEC_CHANNEL_COUNT_CONTROL);
		value.bits.HBR_CHANNEL_COUNT = hbr_channel_count;
		dal_write_reg(
			mmAZALIA_F0_CODEC_CHANNEL_COUNT_CONTROL, value.u32All);
	}
}*/

/* set compressed audio channel cound */
/*static void set_compressed_audio_channel_count(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t compressed_audio_ch_count)
{
	if (compressed_audio_ch_count > 7)
		return;

	{
		union AZALIA_F0_CODEC_CHANNEL_COUNT_CONTROL value;

		value.u32All = dal_read_reg(
			mmAZALIA_F0_CODEC_CHANNEL_COUNT_CONTROL);
		value.bits.COMPRESSED_CHANNEL_COUNT =
			compressed_audio_ch_count;
		dal_write_reg(
			mmAZALIA_F0_CODEC_CHANNEL_COUNT_CONTROL,
			value.u32All);
	}
}*/

/* set video latency in in ms/2+1 */
static void set_video_latency(
	const struct hw_ctx_audio *hw_ctx,
	int latency_in_ms)
{
	uint32_t value = 0;

	if ((latency_in_ms < 0) || (latency_in_ms > 255))
		return;

	value = read_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC);

	set_reg_field_value(value, latency_in_ms,
		AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
		VIDEO_LIPSYNC);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
		value);
}

/* set audio latency in in ms/2+1 */
static void set_audio_latency(
	const struct hw_ctx_audio *hw_ctx,
	int latency_in_ms)
{
	uint32_t value = 0;

	if (latency_in_ms < 0)
		latency_in_ms = 0;

	if (latency_in_ms > 255)
		latency_in_ms = 255;

	value = read_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC);

	set_reg_field_value(value, latency_in_ms,
		AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
		AUDIO_LIPSYNC);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC,
		value);
}

/* enable HW/SW Sync */
/*static void enable_hw_sw_sync(
	const struct hw_ctx_audio *hw_ctx)
{
		union AZALIA_CYCLIC_BUFFER_SYNC value;

	value.u32All = dal_read_reg(mmAZALIA_CYCLIC_BUFFER_SYNC);
	value.bits.CYCLIC_BUFFER_SYNC_ENABLE = 1;
	dal_write_reg(mmAZALIA_CYCLIC_BUFFER_SYNC, value.u32All);
}*/

/* disable HW/SW Sync */
/*static void disable_hw_sw_sync(
	const struct hw_ctx_audio *hw_ctx)
{
	union AZALIA_CYCLIC_BUFFER_SYNC value;

	value.u32All = dal_read_reg(
		mmAZALIA_CYCLIC_BUFFER_SYNC);
	value.bits.CYCLIC_BUFFER_SYNC_ENABLE = 0;
	dal_write_reg(
		mmAZALIA_CYCLIC_BUFFER_SYNC, value.u32All);
}*/

/* update hardware with software's current position in cyclic buffer */
/*static void update_sw_write_ptr(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t offset)
{
		union AZALIA_APPLICATION_POSITION_IN_CYCLIC_BUFFER value;

	value.u32All = dal_read_reg(
		mmAZALIA_APPLICATION_POSITION_IN_CYCLIC_BUFFER);
	value.bits.APPLICATION_POSITION_IN_CYCLIC_BUFFER = offset;
	dal_write_reg(
		mmAZALIA_APPLICATION_POSITION_IN_CYCLIC_BUFFER,
		value.u32All);
}*/

/* update Audio/Video association */
/*static void update_av_association(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id,
	enum signal_type signal,
	uint32_t displayId)
{

}*/

/* ---  hook functions --- */

static bool get_azalia_clock_info_hdmi(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t crtc_pixel_clock_in_khz,
	uint32_t actual_pixel_clock_in_khz,
	struct azalia_clock_info *azalia_clock_info);

static bool get_azalia_clock_info_dp(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t requested_pixel_clock_in_khz,
	const struct audio_pll_info *pll_info,
	struct azalia_clock_info *azalia_clock_info);

static void setup_audio_wall_dto(
	const struct hw_ctx_audio *hw_ctx,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_pll_info *pll_info)
{
	struct azalia_clock_info clock_info = { 0 };

	uint32_t value = dm_read_reg(hw_ctx->ctx, mmDCCG_AUDIO_DTO_SOURCE);

	/* TODO: GraphicsObject\inc\GraphicsObjectDefs.hpp(131):
	 *inline bool isHdmiSignal(SignalType signal)
	 *if (Signals::isHdmiSignal(signal))
	 */
	if (dc_is_hdmi_signal(signal)) {
		/*DTO0 Programming goal:
		-generate 24MHz, 128*Fs from 24MHz
		-use DTO0 when an active HDMI port is connected
		(optionally a DP is connected) */

		/* calculate DTO settings */
		get_azalia_clock_info_hdmi(
			hw_ctx,
			crtc_info->requested_pixel_clock,
			crtc_info->calculated_pixel_clock,
			&clock_info);

		/* On TN/SI, Program DTO source select and DTO select before
		programming DTO modulo and DTO phase. These bits must be
		programmed first, otherwise there will be no HDMI audio at boot
		up. This is a HW sequence change (different from old ASICs).
		Caution when changing this programming sequence.

		HDMI enabled, using DTO0
		program master CRTC for DTO0 */
		{
			set_reg_field_value(value,
				pll_info->dto_source - DTO_SOURCE_ID0,
				DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO0_SOURCE_SEL);

			set_reg_field_value(value,
				0,
				DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO_SEL);

			dm_write_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO_SOURCE, value);
		}

		/* module */
		{
			value = dm_read_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO0_MODULE);
			set_reg_field_value(value,
				clock_info.audio_dto_module,
				DCCG_AUDIO_DTO0_MODULE,
				DCCG_AUDIO_DTO0_MODULE);
			dm_write_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO0_MODULE, value);
		}

		/* phase */
		{
			value = 0;

			value = dm_read_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO0_PHASE);
			set_reg_field_value(value,
				clock_info.audio_dto_phase,
				DCCG_AUDIO_DTO0_PHASE,
				DCCG_AUDIO_DTO0_PHASE);

			dm_write_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO0_PHASE, value);
		}

	} else {
		/*DTO1 Programming goal:
		-generate 24MHz, 512*Fs, 128*Fs from 24MHz
		-default is to used DTO1, and switch to DTO0 when an audio
		master HDMI port is connected
		-use as default for DP

		calculate DTO settings */
		get_azalia_clock_info_dp(
			hw_ctx,
			crtc_info->requested_pixel_clock,
			pll_info,
			&clock_info);

		/* Program DTO select before programming DTO modulo and DTO
		phase. default to use DTO1 */

		{
			set_reg_field_value(value, 1,
				DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO_SEL);
			/*dal_write_reg(mmDCCG_AUDIO_DTO_SOURCE, value)*/

			/* Select 512fs for DP TODO: web register definition
			does not match register header file */
			set_reg_field_value(value, 1,
				DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO2_USE_512FBR_DTO);

			dm_write_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO_SOURCE, value);
		}

		/* module */
		{
			value = 0;

			value = dm_read_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO1_MODULE);

			set_reg_field_value(value,
				clock_info.audio_dto_module,
				DCCG_AUDIO_DTO1_MODULE,
				DCCG_AUDIO_DTO1_MODULE);

			dm_write_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO1_MODULE, value);
		}

		/* phase */
		{
			value = 0;

			value = dm_read_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO1_PHASE);

			set_reg_field_value(value,
				clock_info.audio_dto_phase,
				DCCG_AUDIO_DTO1_PHASE,
				DCCG_AUDIO_DTO1_PHASE);

			dm_write_reg(hw_ctx->ctx,
					mmDCCG_AUDIO_DTO1_PHASE, value);
		}

		/* DAL2 code separate DCCG_AUDIO_DTO_SEL and
		DCCG_AUDIO_DTO2_USE_512FBR_DTO programming into two different
		location. merge together should not hurt */
		/*value.bits.DCCG_AUDIO_DTO2_USE_512FBR_DTO = 1;
		dal_write_reg(mmDCCG_AUDIO_DTO_SOURCE, value);*/
	}
}

/* setup HDMI audio */
static void setup_hdmi_audio(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id,
	const struct audio_crtc_info *crtc_info)
{
	struct audio_clock_info audio_clock_info = {0};
	uint32_t max_packets_per_line;
	uint32_t addr = 0;
	uint32_t value = 0;

	/* For now still do calculation, although this field is ignored when
	above HDMI_PACKET_GEN_VERSION set to 1 */
	max_packets_per_line =
		dal_audio_hw_ctx_calc_max_audio_packets_per_line(
			hw_ctx,
			crtc_info);

	/* HDMI_AUDIO_PACKET_CONTROL */
	{
		addr =
			mmHDMI_AUDIO_PACKET_CONTROL + engine_offset[engine_id];

		value = dm_read_reg(hw_ctx->ctx, addr);

		set_reg_field_value(value, max_packets_per_line,
			HDMI_AUDIO_PACKET_CONTROL,
			HDMI_AUDIO_PACKETS_PER_LINE);
		/* still apply RS600's default setting which is 1. */
		set_reg_field_value(value, 1,
			HDMI_AUDIO_PACKET_CONTROL,
			HDMI_AUDIO_DELAY_EN);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* AFMT_AUDIO_PACKET_CONTROL */
	{
		addr = mmAFMT_AUDIO_PACKET_CONTROL + engine_offset[engine_id];

		value = dm_read_reg(hw_ctx->ctx, addr);

		set_reg_field_value(value, 1,
			AFMT_AUDIO_PACKET_CONTROL,
			AFMT_60958_CS_UPDATE);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* AFMT_AUDIO_PACKET_CONTROL2 */
	{
		addr = mmAFMT_AUDIO_PACKET_CONTROL2 + engine_offset[engine_id];

		value = dm_read_reg(hw_ctx->ctx, addr);

		set_reg_field_value(value, 0,
				AFMT_AUDIO_PACKET_CONTROL2,
				AFMT_AUDIO_LAYOUT_OVRD);

		/*Register field changed.*/
		set_reg_field_value(value, 0,
			AFMT_AUDIO_PACKET_CONTROL2,
			AFMT_60958_OSF_OVRD);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* HDMI_ACR_PACKET_CONTROL */
	{
		addr = mmHDMI_ACR_PACKET_CONTROL + engine_offset[engine_id];

		value = dm_read_reg(hw_ctx->ctx, addr);
		set_reg_field_value(value, 1,
			HDMI_ACR_PACKET_CONTROL,
			HDMI_ACR_AUTO_SEND);

		/* Set HDMI_ACR_SOURCE to 0, to use hardwre
		 *  computed CTS values.*/
		set_reg_field_value(value, 0,
			HDMI_ACR_PACKET_CONTROL,
			HDMI_ACR_SOURCE);

		/* For now clear HDMI_ACR_AUDIO_PRIORITY =>ACR packet has
		higher priority over Audio Sample */
		set_reg_field_value(value, 0,
			HDMI_ACR_PACKET_CONTROL,
			HDMI_ACR_AUDIO_PRIORITY);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* Program audio clock sample/regeneration parameters */
	if (dal_audio_hw_ctx_get_audio_clock_info(
		hw_ctx,
		crtc_info->color_depth,
		crtc_info->requested_pixel_clock,
		crtc_info->calculated_pixel_clock,
		&audio_clock_info)) {

		/* HDMI_ACR_32_0__HDMI_ACR_CTS_32_MASK */
		{
			addr = mmHDMI_ACR_32_0 + engine_offset[engine_id];

			value = dm_read_reg(hw_ctx->ctx, addr);

			set_reg_field_value(value, audio_clock_info.cts_32khz,
				HDMI_ACR_32_0,
				HDMI_ACR_CTS_32);

			dm_write_reg(hw_ctx->ctx, addr, value);
		}

		/* HDMI_ACR_32_1__HDMI_ACR_N_32_MASK */
		{
			addr = mmHDMI_ACR_32_1 + engine_offset[engine_id];

			value = dm_read_reg(hw_ctx->ctx, addr);
			set_reg_field_value(value, audio_clock_info.n_32khz,
				HDMI_ACR_32_1,
				HDMI_ACR_N_32);

			dm_write_reg(hw_ctx->ctx, addr, value);
		}

		/* HDMI_ACR_44_0__HDMI_ACR_CTS_44_MASK */
		{
			addr = mmHDMI_ACR_44_0 + engine_offset[engine_id];

			value = dm_read_reg(hw_ctx->ctx, addr);
			set_reg_field_value(value, audio_clock_info.cts_44khz,
				HDMI_ACR_44_0,
				HDMI_ACR_CTS_44);

			dm_write_reg(hw_ctx->ctx, addr, value);
		}

		/* HDMI_ACR_44_1__HDMI_ACR_N_44_MASK */
		{
			addr = mmHDMI_ACR_44_1 + engine_offset[engine_id];

			value = dm_read_reg(hw_ctx->ctx, addr);
			set_reg_field_value(value, audio_clock_info.n_44khz,
				HDMI_ACR_44_1,
				HDMI_ACR_N_44);

			dm_write_reg(hw_ctx->ctx, addr, value);
		}

		/* HDMI_ACR_48_0__HDMI_ACR_CTS_48_MASK */
		{
			addr = mmHDMI_ACR_48_0 + engine_offset[engine_id];

			value = dm_read_reg(hw_ctx->ctx, addr);
			set_reg_field_value(value, audio_clock_info.cts_48khz,
				HDMI_ACR_48_0,
				HDMI_ACR_CTS_48);

			dm_write_reg(hw_ctx->ctx, addr, value);
		}

		/* HDMI_ACR_48_1__HDMI_ACR_N_48_MASK */
		{
			addr = mmHDMI_ACR_48_1 + engine_offset[engine_id];

			value = dm_read_reg(hw_ctx->ctx, addr);
			set_reg_field_value(value, audio_clock_info.n_48khz,
				HDMI_ACR_48_1,
				HDMI_ACR_N_48);

			dm_write_reg(hw_ctx->ctx, addr, value);
		}

		/* Video driver cannot know in advance which sample rate will
		be used by HD Audio driver
		HDMI_ACR_PACKET_CONTROL__HDMI_ACR_N_MULTIPLE field is
		programmed below in interruppt callback */
	} /* if */

	/* AFMT_60958_0__AFMT_60958_CS_CHANNEL_NUMBER_L_MASK &
	AFMT_60958_0__AFMT_60958_CS_CLOCK_ACCURACY_MASK */
	{
		addr = mmAFMT_60958_0 + engine_offset[engine_id];

		value = dm_read_reg(hw_ctx->ctx, addr);
		set_reg_field_value(value, 1,
			AFMT_60958_0,
			AFMT_60958_CS_CHANNEL_NUMBER_L);

		 /*HW default */
		set_reg_field_value(value, 0,
			AFMT_60958_0,
			AFMT_60958_CS_CLOCK_ACCURACY);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* AFMT_60958_1 AFMT_60958_CS_CHALNNEL_NUMBER_R */
	{
		addr = mmAFMT_60958_1 + engine_offset[engine_id];

		value = dm_read_reg(hw_ctx->ctx, addr);
		set_reg_field_value(value, 2,
			AFMT_60958_1,
			AFMT_60958_CS_CHANNEL_NUMBER_R);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/*AFMT_60958_2 now keep this settings until
	 *  Programming guide comes out*/
	{
		addr = mmAFMT_60958_2 + engine_offset[engine_id];

		value = dm_read_reg(hw_ctx->ctx, addr);
		set_reg_field_value(value, 3,
			AFMT_60958_2,
			AFMT_60958_CS_CHANNEL_NUMBER_2);

		set_reg_field_value(value, 4,
			AFMT_60958_2,
			AFMT_60958_CS_CHANNEL_NUMBER_3);

		set_reg_field_value(value, 5,
			AFMT_60958_2,
			AFMT_60958_CS_CHANNEL_NUMBER_4);

		set_reg_field_value(value, 6,
			AFMT_60958_2,
			AFMT_60958_CS_CHANNEL_NUMBER_5);

		set_reg_field_value(value, 7,
			AFMT_60958_2,
			AFMT_60958_CS_CHANNEL_NUMBER_6);

		set_reg_field_value(value, 8,
			AFMT_60958_2,
			AFMT_60958_CS_CHANNEL_NUMBER_7);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}
}

 /* setup DP audio */
static void setup_dp_audio(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id)
{
	/* --- DP Audio packet configurations --- */
	uint32_t addr = 0;
	uint32_t value = 0;

	/* ATP Configuration */
	{
		addr = mmDP_SEC_AUD_N + engine_offset[engine_id];

		set_reg_field_value(value,
			DP_SEC_AUD_N__DP_SEC_AUD_N__DEFAULT,
			DP_SEC_AUD_N,
			DP_SEC_AUD_N);
		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* Async/auto-calc timestamp mode */
	{
		addr = mmDP_SEC_TIMESTAMP +
			engine_offset[engine_id];

		value = 0;

		set_reg_field_value(value,
			DP_SEC_TIMESTAMP__DP_SEC_TIMESTAMP_MODE__AUTO_CALC,
			DP_SEC_TIMESTAMP,
			DP_SEC_TIMESTAMP_MODE);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* --- The following are the registers
	 *  copied from the SetupHDMI --- */

	/* AFMT_AUDIO_PACKET_CONTROL */
	{
		addr = mmAFMT_AUDIO_PACKET_CONTROL +
			engine_offset[engine_id];

		value = 0;

		value = dm_read_reg(hw_ctx->ctx, addr);
		set_reg_field_value(value,
			1,
			AFMT_AUDIO_PACKET_CONTROL,
			AFMT_60958_CS_UPDATE);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* AFMT_AUDIO_PACKET_CONTROL2 */
	{
		addr =
			mmAFMT_AUDIO_PACKET_CONTROL2 + engine_offset[engine_id];

		value = 0;

		value = dm_read_reg(hw_ctx->ctx, addr);
		set_reg_field_value(value,
			0,
			AFMT_AUDIO_PACKET_CONTROL2,
			AFMT_AUDIO_LAYOUT_OVRD);

		set_reg_field_value(value,
			0,
			AFMT_AUDIO_PACKET_CONTROL2,
			AFMT_60958_OSF_OVRD);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* AFMT_INFOFRAME_CONTROL0 */
	{
		addr =
			mmAFMT_INFOFRAME_CONTROL0 + engine_offset[engine_id];

		value = 0;

		value = dm_read_reg(hw_ctx->ctx, addr);

		set_reg_field_value(value,
			1,
			AFMT_INFOFRAME_CONTROL0,
			AFMT_AUDIO_INFO_UPDATE);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* AFMT_60958_0__AFMT_60958_CS_CLOCK_ACCURACY_MASK */
	{
		addr = mmAFMT_60958_0 + engine_offset[engine_id];

		value = 0;

		value = dm_read_reg(hw_ctx->ctx, addr);
		set_reg_field_value(value,
			0,
			AFMT_60958_0,
			AFMT_60958_CS_CLOCK_ACCURACY);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}
}

 /* setup VCE audio */
static void setup_vce_audio(
	const struct hw_ctx_audio *hw_ctx)
{
	/*TODO:
	const uint32_t addr = mmDOUT_DCE_VCE_CONTROL;
	uint32_t value = 0;

	value = dal_read_reg(hw_ctx->ctx,
			addr);

	set_reg_field_value(value,
		FROM_BASE(hw_ctx)->azalia_stream_id - 1,
		DOUT_DCE_VCE_CONTROL,
		DC_VCE_AUDIO_STREAM_SELECT);

	dal_write_reg(hw_ctx->ctx,
			addr, value);*/
}

/* enable Azalia audio */
static void enable_azalia_audio(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id)
{
	uint32_t value;

	value = read_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL);

	if (get_reg_field_value(value,
			AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			AUDIO_ENABLED) != 1)
		set_reg_field_value(value, 1,
			AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
			AUDIO_ENABLED);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
		value);
}

/* disable Azalia audio */
static void disable_azalia_audio(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id)
{
	uint32_t value;

	value = read_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL);

	set_reg_field_value(value, 0,
		AZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
		AUDIO_ENABLED);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_HOT_PLUG_CONTROL,
		value);
}

/* enable DP audio */
static void enable_dp_audio(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id)
{
	const uint32_t addr = mmDP_SEC_CNTL + engine_offset[engine_id];

	uint32_t value;

	/* Enable Audio packets */
	value = dm_read_reg(hw_ctx->ctx, addr);
	set_reg_field_value(value, 1,
		DP_SEC_CNTL,
		DP_SEC_ASP_ENABLE);

	dm_write_reg(hw_ctx->ctx, addr, value);

	/* Program the ATP and AIP next */
	set_reg_field_value(value, 1,
		DP_SEC_CNTL,
		DP_SEC_ATP_ENABLE);

	set_reg_field_value(value, 1,
		DP_SEC_CNTL,
		DP_SEC_AIP_ENABLE);

	dm_write_reg(hw_ctx->ctx, addr, value);

	/* Program STREAM_ENABLE after all the other enables. */
	set_reg_field_value(value, 1,
		DP_SEC_CNTL,
		DP_SEC_STREAM_ENABLE);

	dm_write_reg(hw_ctx->ctx, addr, value);
}

/* disable DP audio */
static void disable_dp_audio(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id)
{
	const uint32_t addr = mmDP_SEC_CNTL + engine_offset[engine_id];

	uint32_t value;

	/* Disable Audio packets */
	value = dm_read_reg(hw_ctx->ctx, addr);

	set_reg_field_value(value, 0,
		DP_SEC_CNTL,
		DP_SEC_ASP_ENABLE);

	set_reg_field_value(value, 0,
		DP_SEC_CNTL,
		DP_SEC_ATP_ENABLE);

	set_reg_field_value(value, 0,
		DP_SEC_CNTL,
		DP_SEC_AIP_ENABLE);

	set_reg_field_value(value, 0,
		DP_SEC_CNTL,
		DP_SEC_ACM_ENABLE);

	set_reg_field_value(value, 0,
		DP_SEC_CNTL,
		DP_SEC_STREAM_ENABLE);

	/* This register shared with encoder info frame. Therefore we need to
	keep master enabled if at least on of the fields is not 0 */
	if (value != 0)
		set_reg_field_value(value, 1,
			DP_SEC_CNTL,
			DP_SEC_STREAM_ENABLE);

	dm_write_reg(hw_ctx->ctx, addr, value);
}

static void configure_azalia(
	const struct hw_ctx_audio *hw_ctx,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_info *audio_info)
{
	uint32_t speakers = audio_info->flags.info.ALLSPEAKERS;
	uint32_t value;
	uint32_t field = 0;
	enum audio_format_code audio_format_code;
	uint32_t format_index;
	uint32_t index;
	bool is_ac3_supported = false;
	bool is_audio_format_supported = false;
	union audio_sample_rates sample_rate;
	uint32_t strlen = 0;

	/* Speaker Allocation */
	/*
	uint32_t value;
	uint32_t field = 0;*/
	value = read_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER);

	set_reg_field_value(value,
		speakers,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		SPEAKER_ALLOCATION);

	set_reg_field_value(value,
		0,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		HDMI_CONNECTION);

	set_reg_field_value(value,
		0,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		DP_CONNECTION);

	field = get_reg_field_value(value,
			AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			EXTRA_CONNECTION_INFO);

	field &= ~0x1;

	set_reg_field_value(value,
		field,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		EXTRA_CONNECTION_INFO);

	/* set audio for output signal */
	switch (signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		set_reg_field_value(value,
			1,
			AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			HDMI_CONNECTION);

		break;
	case SIGNAL_TYPE_WIRELESS: {
		/*LSB used for "is wireless" flag */
		field = 0;
		field = get_reg_field_value(value,
		AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		EXTRA_CONNECTION_INFO);
		field |= 0x1;
		set_reg_field_value(value,
			field,
			AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			EXTRA_CONNECTION_INFO);

		set_reg_field_value(value,
			1,
			AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			HDMI_CONNECTION);

		}
		break;
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		set_reg_field_value(value,
			1,
			AZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
			DP_CONNECTION);

		break;
	default:
		break;
	}

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER,
		value);

	/*  Audio Descriptors   */
	/* pass through all formats */
	for (format_index = 0; format_index < AUDIO_FORMAT_CODE_COUNT;
			format_index++) {
		audio_format_code =
			(AUDIO_FORMAT_CODE_FIRST + format_index);

		/* those are unsupported, skip programming */
		if (audio_format_code == AUDIO_FORMAT_CODE_1BITAUDIO ||
			audio_format_code == AUDIO_FORMAT_CODE_DST)
			continue;

		value = 0;

		/* check if supported */
		is_audio_format_supported =
			dal_audio_hw_ctx_is_audio_format_supported(
				hw_ctx,
				audio_info,
				audio_format_code, &index);

		if (is_audio_format_supported) {
			const struct audio_mode *audio_mode =
					&audio_info->modes[index];
			union audio_sample_rates sample_rates =
					audio_mode->sample_rates;
			uint8_t byte2 = audio_mode->max_bit_rate;

			/* adjust specific properties */
			switch (audio_format_code) {
			case AUDIO_FORMAT_CODE_LINEARPCM: {
				dal_hw_ctx_audio_check_audio_bandwidth(
					hw_ctx,
					crtc_info,
					audio_mode->channel_count,
					signal,
					&sample_rates);

				byte2 = audio_mode->sample_size;

				set_reg_field_value(value,
				sample_rates.all,
		AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
				SUPPORTED_FREQUENCIES_STEREO);

				}
				break;
			case AUDIO_FORMAT_CODE_AC3:
				is_ac3_supported = true;
				break;
			case AUDIO_FORMAT_CODE_DOLBYDIGITALPLUS:
			case AUDIO_FORMAT_CODE_DTS_HD:
			case AUDIO_FORMAT_CODE_MAT_MLP:
			case AUDIO_FORMAT_CODE_DST:
			case AUDIO_FORMAT_CODE_WMAPRO:
				byte2 = audio_mode->vendor_specific;
				break;
			default:
				break;
			}

			/* fill audio format data */
			set_reg_field_value(value,
			audio_mode->channel_count - 1,
			AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
			MAX_CHANNELS);

			set_reg_field_value(value,
			sample_rates.all,
			AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
			SUPPORTED_FREQUENCIES);

			set_reg_field_value(value,
			byte2,
			AZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0,
			DESCRIPTOR_BYTE_2);

		} /* if */

		write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0 +
		format_index,
		value);
	} /* for */

	if (is_ac3_supported)
		dm_write_reg(hw_ctx->ctx,
		mmAZALIA_F0_CODEC_FUNCTION_PARAMETER_STREAM_FORMATS,
		0x05);

	/* check for 192khz/8-Ch support for HBR requirements */
	sample_rate.all = 0;
	sample_rate.rate.RATE_192 = 1;
	dal_hw_ctx_audio_check_audio_bandwidth(
		hw_ctx,
		crtc_info,
		8,
		signal,
		&sample_rate);

	set_high_bit_rate_capable(hw_ctx, sample_rate.rate.RATE_192);

	/* Audio and Video Lipsync */
	set_video_latency(hw_ctx, audio_info->video_latency);
	set_audio_latency(hw_ctx, audio_info->audio_latency);

	value = 0;
	set_reg_field_value(value, audio_info->manufacture_id,
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO0,
		MANUFACTURER_ID);

	set_reg_field_value(value, audio_info->product_id,
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO0,
		PRODUCT_ID);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO0,
		value);

	value = 0;

	/*get display name string length */
	while (audio_info->display_name[strlen++] != '\0') {
		if (strlen >=
		MAX_HW_AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS)
			break;
		}
	set_reg_field_value(value, strlen,
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO1,
		SINK_DESCRIPTION_LEN);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO1,
		value);

	/*
	*write the port ID:
	*PORT_ID0 = display index
	*PORT_ID1 = 16bit BDF
	*(format MSB->LSB: 8bit Bus, 5bit Device, 3bit Function)
	*/

	value = 0;

	set_reg_field_value(value, audio_info->port_id[0],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO2,
		PORT_ID0);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO2,
		value);

	value = 0;
	set_reg_field_value(value, audio_info->port_id[1],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO3,
		PORT_ID1);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO3,
		value);

	/*write the 18 char monitor string */

	value = 0;
	set_reg_field_value(value, audio_info->display_name[0],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4,
		DESCRIPTION0);

	set_reg_field_value(value, audio_info->display_name[1],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4,
		DESCRIPTION1);

	set_reg_field_value(value, audio_info->display_name[2],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4,
		DESCRIPTION2);

	set_reg_field_value(value, audio_info->display_name[3],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4,
		DESCRIPTION3);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO4,
		value);

	value = 0;
	set_reg_field_value(value, audio_info->display_name[4],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5,
		DESCRIPTION4);

	set_reg_field_value(value, audio_info->display_name[5],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5,
		DESCRIPTION5);

	set_reg_field_value(value, audio_info->display_name[6],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5,
		DESCRIPTION6);

	set_reg_field_value(value, audio_info->display_name[7],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5,
		DESCRIPTION7);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO5,
		value);

	value = 0;
	set_reg_field_value(value, audio_info->display_name[8],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6,
		DESCRIPTION8);

	set_reg_field_value(value, audio_info->display_name[9],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6,
		DESCRIPTION9);

	set_reg_field_value(value, audio_info->display_name[10],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6,
		DESCRIPTION10);

	set_reg_field_value(value, audio_info->display_name[11],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6,
		DESCRIPTION11);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO6,
		value);

	value = 0;
	set_reg_field_value(value, audio_info->display_name[12],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7,
		DESCRIPTION12);

	set_reg_field_value(value, audio_info->display_name[13],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7,
		DESCRIPTION13);

	set_reg_field_value(value, audio_info->display_name[14],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7,
		DESCRIPTION14);

	set_reg_field_value(value, audio_info->display_name[15],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7,
		DESCRIPTION15);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO7,
		value);

	value = 0;
	set_reg_field_value(value, audio_info->display_name[16],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO8,
		DESCRIPTION16);

	set_reg_field_value(value, audio_info->display_name[17],
		AZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO8,
		DESCRIPTION17);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_SINK_INFO8,
		value);

}

/* setup Azalia HW block */
static void setup_azalia(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id,
	enum signal_type signal,
	const struct audio_crtc_info *crtc_info,
	const struct audio_pll_info *pll_info,
	const struct audio_info *audio_info)
{
	uint32_t speakers = 0;
	uint32_t channels = 0;

	if (audio_info == NULL)
		/* This should not happen.it does so we don't get BSOD*/
		return;

	speakers = audio_info->flags.info.ALLSPEAKERS;
	channels = dal_audio_hw_ctx_speakers_to_channels(
		hw_ctx,
		audio_info->flags.speaker_flags).all;

	/* setup the audio stream source select (audio -> dig mapping) */
	{
		const uint32_t addr =
			mmAFMT_AUDIO_SRC_CONTROL + engine_offset[engine_id];

		uint32_t value = 0;
		/*convert one-based index to zero-based */
		set_reg_field_value(value,
			FROM_BASE(hw_ctx)->azalia_stream_id - 1,
			AFMT_AUDIO_SRC_CONTROL,
			AFMT_AUDIO_SRC_SELECT);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/* Channel allocation */
	{
		const uint32_t addr =
			mmAFMT_AUDIO_PACKET_CONTROL2 + engine_offset[engine_id];
		uint32_t value = dm_read_reg(hw_ctx->ctx, addr);

		set_reg_field_value(value,
			channels,
			AFMT_AUDIO_PACKET_CONTROL2,
			AFMT_AUDIO_CHANNEL_ENABLE);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	configure_azalia(hw_ctx, signal, crtc_info, audio_info);
}

/* unmute audio */
static void unmute_azalia_audio(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id)
{
	const uint32_t addr = mmAFMT_AUDIO_PACKET_CONTROL +
		engine_offset[engine_id];

	uint32_t value = 0;

	value = dm_read_reg(hw_ctx->ctx, addr);

	set_reg_field_value(value, 1,
		AFMT_AUDIO_PACKET_CONTROL, AFMT_AUDIO_SAMPLE_SEND);

	dm_write_reg(hw_ctx->ctx, addr, value);
}

/* mute audio */
static void mute_azalia_audio(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id)
{
	const uint32_t addr = mmAFMT_AUDIO_PACKET_CONTROL +
		engine_offset[engine_id];

	uint32_t value = 0;

	value = dm_read_reg(hw_ctx->ctx, addr);

	set_reg_field_value(value, 0,
		AFMT_AUDIO_PACKET_CONTROL, AFMT_AUDIO_SAMPLE_SEND);

	dm_write_reg(hw_ctx->ctx, addr, value);
}

/* enable channel splitting mapping */
static void setup_channel_splitting_mapping(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id,
	enum signal_type signal,
	const struct audio_channel_associate_info *audio_mapping,
	bool enable)
{
	uint32_t value = 0;

	if ((audio_mapping == NULL || audio_mapping->u32all == 0) && enable)
		return;

	value = audio_mapping->u32all;

	if (enable == false)
		/*0xFFFFFFFF;*/
		value = MULTI_CHANNEL_SPLIT_NO_ASSO_INFO;

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_ASSOCIATION_INFO,
		value);
}

/* get current channel spliting */
static bool get_channel_splitting_mapping(
	const struct hw_ctx_audio *hw_ctx,
	enum engine_id engine_id,
	struct audio_channel_associate_info *audio_mapping)
{
	uint32_t value = 0;

	if (audio_mapping == NULL)
		return false;

	value = read_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_ASSOCIATION_INFO);

	/*0xFFFFFFFF*/
	if (get_reg_field_value(value,
			AZALIA_F0_CODEC_PIN_ASSOCIATION_INFO,
			ASSOCIATION_INFO) !=
			MULTI_CHANNEL_SPLIT_NO_ASSO_INFO) {
		uint32_t multi_channel01_enable = 0;
		uint32_t multi_channel23_enable = 0;
		uint32_t multi_channel45_enable = 0;
		uint32_t multi_channel67_enable = 0;
		/* get the one we set.*/
		audio_mapping->u32all = value;

		/* check each enable status*/
		value = read_indirect_azalia_reg(
			hw_ctx,
			ixAZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_ENABLE);

		multi_channel01_enable = get_reg_field_value(value,
		AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_ENABLE,
		MULTICHANNEL01_ENABLE);

		multi_channel23_enable = get_reg_field_value(value,
		AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_ENABLE,
		MULTICHANNEL23_ENABLE);

		multi_channel45_enable = get_reg_field_value(value,
		AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_ENABLE,
		MULTICHANNEL45_ENABLE);

		multi_channel67_enable = get_reg_field_value(value,
		AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_ENABLE,
		MULTICHANNEL67_ENABLE);

		if (multi_channel01_enable == 0 &&
			multi_channel23_enable == 0 &&
			multi_channel45_enable == 0 &&
			multi_channel67_enable == 0)
			dal_logger_write(hw_ctx->ctx->logger,
				LOG_MAJOR_HW_TRACE,
				LOG_MINOR_COMPONENT_AUDIO,
				"Audio driver did not enable multi-channel\n");

		return true;
	}

	return false;
}

/* set the payload value for the unsolicited response */
static void set_unsolicited_response_payload(
	const struct hw_ctx_audio *hw_ctx,
	enum audio_payload payload)
{
	/* set the payload value for the unsolicited response
	 Jack presence is not required to be enabled */
	uint32_t value = 0;

	value = read_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_FORCE);

	set_reg_field_value(value, payload,
		AZALIA_F0_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_FORCE,
		UNSOLICITED_RESPONSE_PAYLOAD);

	set_reg_field_value(value, 1,
		AZALIA_F0_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_FORCE,
		UNSOLICITED_RESPONSE_FORCE);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_FORCE,
		value);
}

/* initialize HW state */
static void hw_initialize(
	const struct hw_ctx_audio *hw_ctx)
{
	uint32_t stream_id = FROM_BASE(hw_ctx)->azalia_stream_id;
	uint32_t addr;

	/* we only need to program the following registers once, so we only do
	it for the first audio stream.*/
	if (stream_id != FIRST_AUDIO_STREAM_ID)
		return;

	/* Suport R5 - 32khz
	 * Suport R6 - 44.1khz
	 * Suport R7 - 48khz
	 */
	addr = mmAZALIA_F0_CODEC_FUNCTION_PARAMETER_SUPPORTED_SIZE_RATES;
	{
		uint32_t value;

		value = dm_read_reg(hw_ctx->ctx, addr);

		set_reg_field_value(value, 0x70,
		AZALIA_F0_CODEC_FUNCTION_PARAMETER_SUPPORTED_SIZE_RATES,
		AUDIO_RATE_CAPABILITIES);

		dm_write_reg(hw_ctx->ctx, addr, value);
	}

	/*Keep alive bit to verify HW block in BU. */
	addr = mmAZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES;
	{
		uint32_t value;

		value = dm_read_reg(hw_ctx->ctx, addr);

		set_reg_field_value(value, 1,
		AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES,
		CLKSTOP);

		set_reg_field_value(value, 1,
		AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES,
		EPSS);
		dm_write_reg(hw_ctx->ctx, addr, value);
	}
}

/* Assign GTC group and enable GTC value embedding */
static void enable_gtc_embedding_with_group(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t group_num,
	uint32_t audio_latency)
{
	/*need to replace the static number with variable */
	if (group_num <= 6) {
		uint32_t value = read_indirect_azalia_reg(
			hw_ctx,
			ixAZALIA_F0_CODEC_CONVERTER_CONTROL_GTC_EMBEDDING);

		set_reg_field_value(
			value,
			group_num,
			AZALIA_F0_CODEC_CONVERTER_CONTROL_GTC_EMBEDDING,
			PRESENTATION_TIME_EMBEDDING_GROUP);

		set_reg_field_value(
			value,
			1,
			AZALIA_F0_CODEC_CONVERTER_CONTROL_GTC_EMBEDDING,
			PRESENTATION_TIME_EMBEDDING_ENABLE);

		write_indirect_azalia_reg(
			hw_ctx,
			ixAZALIA_F0_CODEC_CONVERTER_CONTROL_GTC_EMBEDDING,
			value);

		/*update audio latency to LIPSYNC*/
		set_audio_latency(hw_ctx, audio_latency);
	} else {
		dal_logger_write(
			hw_ctx->ctx->logger,
			LOG_MAJOR_HW_TRACE,
			LOG_MINOR_COMPONENT_AUDIO,
			"GTC group number %d is too big",
			group_num);
	}
}

 /* Disable GTC value embedding */
static void disable_gtc_embedding(
	const struct hw_ctx_audio *hw_ctx)
{
	uint32_t value = 0;

	value = read_indirect_azalia_reg(
	hw_ctx,
	ixAZALIA_F0_CODEC_CONVERTER_CONTROL_GTC_EMBEDDING);

	set_reg_field_value(value, 0,
	AZALIA_F0_CODEC_CONVERTER_CONTROL_GTC_EMBEDDING,
	PRESENTATION_TIME_EMBEDDING_ENABLE);

	set_reg_field_value(value, 0,
	AZALIA_F0_CODEC_CONVERTER_CONTROL_GTC_EMBEDDING,
	PRESENTATION_TIME_EMBEDDING_GROUP);

	write_indirect_azalia_reg(
		hw_ctx,
		ixAZALIA_F0_CODEC_CONVERTER_CONTROL_GTC_EMBEDDING,
		value);
}

 /* Disable Azalia Clock Gating Feature */
static void disable_az_clock_gating(
	const struct hw_ctx_audio *hw_ctx)
{
	uint32_t value;

	value = dm_read_reg(hw_ctx->ctx,
			mmAZALIA_CONTROLLER_CLOCK_GATING);
	set_reg_field_value(value, 0, AZALIA_CONTROLLER_CLOCK_GATING, ENABLE_CLOCK_GATING);
	dm_write_reg(hw_ctx->ctx,
			mmAZALIA_CONTROLLER_CLOCK_GATING, value);
}

/* search pixel clock value for Azalia HDMI Audio */
static bool get_azalia_clock_info_hdmi(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t crtc_pixel_clock_in_khz,
	uint32_t actual_pixel_clock_in_khz,
	struct azalia_clock_info *azalia_clock_info)
{
	if (azalia_clock_info == NULL)
		return false;

	/* audio_dto_phase= 24 * 10,000;
	 *   24MHz in [100Hz] units */
	azalia_clock_info->audio_dto_phase =
			24 * 10000;

	/* audio_dto_module = PCLKFrequency * 10,000;
	 *  [khz] -> [100Hz] */
	azalia_clock_info->audio_dto_module =
			actual_pixel_clock_in_khz * 10;

	return true;
}

/* search pixel clock value for Azalia DP Audio */
static bool get_azalia_clock_info_dp(
	const struct hw_ctx_audio *hw_ctx,
	uint32_t requested_pixel_clock_in_khz,
	const struct audio_pll_info *pll_info,
	struct azalia_clock_info *azalia_clock_info)
{
	if (pll_info == NULL || azalia_clock_info == NULL)
		return false;

	/* Reported dpDtoSourceClockInkhz value for
	 * DCE8 already adjusted for SS, do not need any
	 * adjustment here anymore
	 */

	/*audio_dto_phase = 24 * 10,000;
	 * 24MHz in [100Hz] units */
	azalia_clock_info->audio_dto_phase = 24 * 10000;

	/*audio_dto_module = dpDtoSourceClockInkhz * 10,000;
	 *  [khz] ->[100Hz] */
	azalia_clock_info->audio_dto_module =
		pll_info->dp_dto_source_clock_in_khz * 10;

	return true;
}

static const struct hw_ctx_audio_funcs funcs = {
	.destroy = destroy,
	.setup_audio_wall_dto =
		setup_audio_wall_dto,
	.setup_hdmi_audio =
		setup_hdmi_audio,
	.setup_dp_audio = setup_dp_audio,
	.setup_vce_audio = setup_vce_audio,
	.enable_azalia_audio =
		enable_azalia_audio,
	.disable_azalia_audio =
		disable_azalia_audio,
	.enable_dp_audio =
		enable_dp_audio,
	.disable_dp_audio =
		disable_dp_audio,
	.setup_azalia =
		setup_azalia,
	.disable_az_clock_gating =
		disable_az_clock_gating,
	.unmute_azalia_audio =
		unmute_azalia_audio,
	.mute_azalia_audio =
		mute_azalia_audio,
	.setup_channel_splitting_mapping =
		setup_channel_splitting_mapping,
	.get_channel_splitting_mapping =
		get_channel_splitting_mapping,
	.set_unsolicited_response_payload =
		set_unsolicited_response_payload,
	.hw_initialize =
		hw_initialize,
	.enable_gtc_embedding_with_group =
		enable_gtc_embedding_with_group,
	.disable_gtc_embedding =
		disable_gtc_embedding,
	.get_azalia_clock_info_hdmi =
		get_azalia_clock_info_hdmi,
	.get_azalia_clock_info_dp =
		get_azalia_clock_info_dp,
};

bool dal_audio_construct_hw_ctx_audio_dce80(
	struct hw_ctx_audio_dce80 *hw_ctx,
	uint8_t azalia_stream_id,
	struct dc_context *ctx)
{
	struct hw_ctx_audio *base = &hw_ctx->base;

	base->funcs = &funcs;

	/* save audio endpoint or dig front for current dce80 audio object */
	hw_ctx->azalia_stream_id = azalia_stream_id;
	hw_ctx->base.ctx = ctx;

	/* azalia audio endpoints register offsets. azalia is associated with
	DIG front. save AUDIO register offset */
	switch (azalia_stream_id) {
	case 1: {
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_index =
			mmAZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_INDEX;
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_data =
			mmAZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_DATA;
		}
		break;
	case 2: {
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_index =
			mmAZF0ENDPOINT1_AZALIA_F0_CODEC_ENDPOINT_INDEX;
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_data =
			mmAZF0ENDPOINT1_AZALIA_F0_CODEC_ENDPOINT_DATA;
		}
		break;
	case 3: {
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_index =
			mmAZF0ENDPOINT2_AZALIA_F0_CODEC_ENDPOINT_INDEX;
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_data =
			mmAZF0ENDPOINT2_AZALIA_F0_CODEC_ENDPOINT_DATA;
		}
		break;
	case 4: {
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_index =
			mmAZF0ENDPOINT3_AZALIA_F0_CODEC_ENDPOINT_INDEX;
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_data =
			mmAZF0ENDPOINT3_AZALIA_F0_CODEC_ENDPOINT_DATA;
		}
		break;
	case 5: {
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_index =
			mmAZF0ENDPOINT4_AZALIA_F0_CODEC_ENDPOINT_INDEX;
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_data =
			mmAZF0ENDPOINT4_AZALIA_F0_CODEC_ENDPOINT_DATA;
		}
		break;
	case 6: {
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_index =
			mmAZF0ENDPOINT5_AZALIA_F0_CODEC_ENDPOINT_INDEX;
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_data =
			mmAZF0ENDPOINT5_AZALIA_F0_CODEC_ENDPOINT_DATA;
		}
		break;
	case 7: {
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_index =
			mmAZF0ENDPOINT6_AZALIA_F0_CODEC_ENDPOINT_INDEX;
			hw_ctx->az_mm_reg_offsets.
			azf0endpointx_azalia_f0_codec_endpoint_data =
			mmAZF0ENDPOINT6_AZALIA_F0_CODEC_ENDPOINT_DATA;
		}
		break;
	default:
		/*DALASSERT_MSG(false,("Invalid Azalia stream ID!"));*/
		BREAK_TO_DEBUGGER();
		break;
	}

	return true;
}

struct hw_ctx_audio *dal_audio_create_hw_ctx_audio_dce80(
	struct dc_context *ctx,
	uint32_t azalia_stream_id)
{
	/* allocate memory for struc hw_ctx_audio_dce80 */
	struct hw_ctx_audio_dce80 *hw_ctx_dce80 =
			dm_alloc(sizeof(struct hw_ctx_audio_dce80));

	if (!hw_ctx_dce80) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	/*return pointer to hw_ctx_audio back to caller  -- audio object */
	if (dal_audio_construct_hw_ctx_audio_dce80(
			hw_ctx_dce80, azalia_stream_id, ctx))
		return &hw_ctx_dce80->base;

	BREAK_TO_DEBUGGER();

	dm_free(hw_ctx_dce80);

	return NULL;
}

