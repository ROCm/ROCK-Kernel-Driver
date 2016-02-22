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

#ifndef __DAL_HW_CTX_AUDIO_DCE80_H__
#define __DAL_HW_CTX_AUDIO_DCE80_H__

#include "audio/hw_ctx_audio.h"

struct hw_ctx_audio_dce80 {
	struct hw_ctx_audio base;

	/* azalia stream id 1 based indexing, corresponding to audio GO enumId*/
	uint32_t azalia_stream_id;

	/* azalia stream endpoint register offsets */
	struct azalia_reg_offsets az_mm_reg_offsets;

	/* audio encoder block MM register offset -- associate with DIG FRONT */
};

/* ---  helpers ---  all static functions*/
/*set_high_bit_rate_capable
set_hbr_channel_count
set_compressed_audio_channel_count
set_video_latency
set_audio_latency
enable_hw_sw_sync
disable_hw_sw_sync
update_sw_write_ptr
update_av_association
write_indirect_azalia_reg
read_indirect_azalia_reg
*/

/* in case dce83 may derived from dce80, expose dce80 constructor
*and destroy for derived */
bool dal_audio_construct_hw_ctx_audio_dce80(
	struct hw_ctx_audio_dce80 *hw_ctx,
	uint8_t azalia_stream_id,
	struct dc_context *ctx);

void dal_audio_destruct_hw_ctx_audio_dce80(
	struct hw_ctx_audio_dce80 *hw_ctx);

struct hw_ctx_audio *dal_audio_create_hw_ctx_audio_dce80(
	struct dc_context *ctx,
	uint32_t azalia_stream_id);

#endif  /* __DAL_HW_CTX_AUDIO_DCE80_H__ */

