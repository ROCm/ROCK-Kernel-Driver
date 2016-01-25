/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dc_services.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#include "include/logger_interface.h"
#include "include/adapter_service_interface.h"
#include "include/fixed32_32.h"

#include "ext_clock_source_dce110.h"

/**
 * In this file ECS stands for External Clock Source.
 */

#define ECS110_FROM_BASE(clk_src_ptr)\
	container_of(\
		container_of((clk_src_ptr), struct ext_clock_source, base), \
			struct ext_clock_source_dce110, base)

#define ECS_WARNING(...) \
	dal_logger_write(ctx->logger, LOG_MAJOR_WARNING, \
			LOG_MINOR_COMPONENT_GPU, __VA_ARGS__)

#define ECS_ERROR(...) \
	dal_logger_write(ctx->logger, LOG_MAJOR_ERROR, \
			LOG_MINOR_COMPONENT_GPU, __VA_ARGS__)

/******************************************************************************
 * implementation functions
 *****************************************************************************/

static uint32_t controller_id_to_index(
		struct clock_source *clk_src,
		enum controller_id controller_id)
{
	struct dc_context *ctx = clk_src->ctx;
	uint32_t index = 0;

	switch (controller_id) {
	case CONTROLLER_ID_D0:
		index = 0;
		break;
	case CONTROLLER_ID_D1:
		index = 1;
		break;
	case CONTROLLER_ID_D2:
		index = 2;
		break;
	default:
		ECS_ERROR("%s: invalid input controller_id = %d!\n",
				__func__, controller_id);
		break;
	}

	return index;
}

/* Adjust pixel rate by DTO programming (used for DisplayPort) */
static bool adjust_dto_pixel_rate(
		struct clock_source *clk_src,
		struct pixel_clk_params *pix_clk_params,
		uint32_t requested_pix_clk_hz)
{
	struct ext_clock_source_dce110 *ecs110 =
			ECS110_FROM_BASE(clk_src);
	struct dc_context *ctx = clk_src->ctx;
	uint32_t index;
	uint32_t dto_phase_reg;
	uint32_t dto_modulo_reg;
	uint32_t dto_phase_rnd;
	uint32_t addr;
	uint32_t value;
	struct fixed32_32 dto_phase;

	if (NULL == pix_clk_params) {
		ECS_WARNING("%s: invalid input!\n", __func__);
		return false;
	}

	index = controller_id_to_index(clk_src, pix_clk_params->controller_id);

	addr = ecs110->registers[index].dp_dtox_phase;
	dto_phase_reg = dal_read_reg(ctx, addr);

	addr = ecs110->registers[index].dp_dtox_modulo;
	dto_modulo_reg = dal_read_reg(ctx, addr);

	if (!dto_modulo_reg) {
		ECS_WARNING("%s: current modulo is zero!\n", __func__);
		return false;
	}

	dto_phase = dal_fixed32_32_from_int(requested_pix_clk_hz);

	dto_phase = dal_fixed32_32_mul_int(dto_phase, dto_modulo_reg);

	dto_phase = dal_fixed32_32_div_int(dto_phase,
			pix_clk_params->dp_ref_clk * 1000);

	dto_phase_rnd = dal_fixed32_32_round(dto_phase);

	/* Program DTO Phase */
	if (dto_phase_reg != dto_phase_rnd) {
		/* If HW De-Spreading enabled on DP REF clock and if there will
		 * be case when Pixel rate > average DP Ref Clock, then need to
		 * disable de-spread for DP DTO (ATOMBIOS will program MODULO
		 * for average DP REF clock so no further SW adjustment
		 * needed) */
		if (pix_clk_params->de_spread_params.hw_dso_n_dp_ref_clk) {

			addr = ecs110->registers[index].crtcx_pixel_rate_cntl;
			value = dal_read_reg(ctx, addr);

			if (requested_pix_clk_hz / 1000 >
				pix_clk_params->
					de_spread_params.avg_dp_ref_clk_khz) {

				set_reg_field_value(value, 1,
						CRTC0_PIXEL_RATE_CNTL,
						DP_DTO0_DS_DISABLE);
			} else {
				set_reg_field_value(value, 0,
						CRTC0_PIXEL_RATE_CNTL,
						DP_DTO0_DS_DISABLE);
			}

			dal_write_reg(ctx, addr, value);
		}

		value = 0;
		addr = ecs110->registers[index].dp_dtox_phase;

		set_reg_field_value(value, dto_phase_rnd,
				DP_DTO0_PHASE,
				DP_DTO0_PHASE);

		dal_write_reg(ctx, addr, value);
	}

	return true;
}

/**
 * Retrieve Pixel Rate (in Hz) from HW registers already programmed.
 */
static uint32_t retrieve_dp_pixel_rate_from_display_pll(
		struct clock_source *clk_src,
		struct pixel_clk_params *params)
{
	struct dc_context *ctx = clk_src->ctx;

	/* TODO: update when DAL2 implements  this function. */
	DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_GPU, "%s\n", __func__);
	return 0;
}

static uint32_t retrieve_dto_pix_rate_hz(
		struct clock_source *clk_src,
		struct pixel_clk_params *params)
{
	struct ext_clock_source_dce110 *ecs110 =
			ECS110_FROM_BASE(clk_src);
	struct dc_context *ctx = clk_src->ctx;
	uint32_t index;
	uint32_t dto_phase_reg;
	uint32_t dto_modulo_reg;
	uint32_t addr;
	uint32_t value;
	uint32_t pix_rate_hz;
	struct fixed32_32 p_clk;

	if (params == NULL)
		return 0;

	if (NULL == params) {
		ECS_WARNING("%s: invalid input!\n", __func__);
		return false;
	}

	index = controller_id_to_index(clk_src, params->controller_id);

	addr = ecs110->registers[index].crtcx_pixel_rate_cntl;
	value = dal_read_reg(ctx, addr);

	if (get_reg_field_value(value, CRTC0_PIXEL_RATE_CNTL, DP_DTO0_ENABLE)
			== 1) {

		addr = ecs110->registers[index].dp_dtox_phase;
		dto_phase_reg = dal_read_reg(ctx, addr);

		addr = ecs110->registers[index].dp_dtox_modulo;
		dto_modulo_reg = dal_read_reg(ctx, addr);

		if (!dto_modulo_reg) {
			ECS_WARNING("%s: current modulo is zero!\n", __func__);
			return 0;
		}

		/* Calculate pixel clock from DTO Phase & Modulo*/
		p_clk = dal_fixed32_32_from_int(params->dp_ref_clk * 1000);

		p_clk = dal_fixed32_32_mul_int(p_clk, dto_phase_reg);

		p_clk = dal_fixed32_32_div_int(p_clk, dto_modulo_reg);

		pix_rate_hz = dal_fixed32_32_round(p_clk);
	} else {
		pix_rate_hz = retrieve_dp_pixel_rate_from_display_pll(clk_src,
				params);
	}

	return pix_rate_hz;
}

/******************************************************************************
 * create/destroy functions
 *****************************************************************************/

static void destruct(struct ext_clock_source_dce110 *ecs110)
{
	struct ext_clock_source *ext_cs = &ecs110->base;
	struct clock_source *base = &ext_cs->base;

	if (NULL != base->dp_ss_params) {
		dc_service_free(base->ctx, base->dp_ss_params);
		base->dp_ss_params = NULL;
	}

	dc_service_free(base->ctx, ecs110->registers);
	ecs110->registers = NULL;
}


static void destroy(struct clock_source **clk_src)
{
	struct ext_clock_source_dce110 *ecs110;

	ecs110 = ECS110_FROM_BASE(*clk_src);

	destruct(ecs110);

	dc_service_free((*clk_src)->ctx, ecs110);

	*clk_src = NULL;
}

static const struct clock_source_impl funcs = {
	.program_pix_clk = dal_ext_clock_source_program_pix_clk,
	.adjust_pll_pixel_rate = dal_clock_source_base_adjust_pll_pixel_rate,
	.adjust_dto_pixel_rate = adjust_dto_pixel_rate,
	.retrieve_pll_pix_rate_hz =
			dal_clock_source_base_retrieve_pll_pix_rate_hz,
	.get_pix_clk_dividers = dal_ext_clock_source_get_pix_clk_dividers,
	.destroy = destroy,
	.retrieve_dto_pix_rate_hz = retrieve_dto_pix_rate_hz,
	.power_down_pll = dal_ext_clock_source_power_down_pll
};

static bool construct(
		struct ext_clock_source_dce110 *ecs110,
		struct clock_source_init_data *clk_src_init_data)
{
	struct dc_context *ctx = clk_src_init_data->ctx;
	struct ext_clock_source *ext_cs = &ecs110->base;
	struct clock_source *base = &ext_cs->base;
	uint32_t controllers_num;
	struct registers *registers;

	/* None of the base construct() functions allocates memory.
	 * That means, in case of error, we don't have to free memory
	 * allocated by base. */
	if (!dal_ext_clock_source_construct(ext_cs, clk_src_init_data))
		return false;

	base->funcs = &funcs;

	base->is_gen_lock_capable = false;
	base->dp_ss_params = NULL;
	base->dp_ss_params_cnt = 0;

	ecs110->registers = NULL;

	if (base->clk_src_id != CLOCK_SOURCE_ID_EXTERNAL) {
		ECS_ERROR("ECS110:Invalid ClockSourceId = %d!\n",
				base->clk_src_id);
		return false;
	}

	controllers_num = dal_adapter_service_get_controllers_num(
			base->adapter_service);

	if (controllers_num <= 0 || controllers_num > 6) {
		ECS_ERROR("ECS110:Invalid number of controllers = %d!\n",
				controllers_num);
		return false;
	}

	ecs110->registers = (struct registers *)
		(dc_service_alloc(clk_src_init_data->ctx, sizeof(struct registers) * controllers_num));

	if (ecs110->registers == NULL) {
		ECS_ERROR("ECS110:Failed to allocate 'registers'!\n");
		return false;
	}

	registers = ecs110->registers;

	/* Assign register address. No break between cases */
	switch (controllers_num) {
	case 3:
		registers[2].dp_dtox_phase = mmDP_DTO2_PHASE;
		registers[2].dp_dtox_modulo = mmDP_DTO2_MODULO;
		registers[2].crtcx_pixel_rate_cntl = mmCRTC2_PIXEL_RATE_CNTL;
		/* fallthrough */
	case 2:
		registers[1].dp_dtox_phase = mmDP_DTO1_PHASE;
		registers[1].dp_dtox_modulo = mmDP_DTO1_MODULO;
		registers[1].crtcx_pixel_rate_cntl = mmCRTC1_PIXEL_RATE_CNTL;
		/* fallthrough */
	case 1:
		registers[0].dp_dtox_phase = mmDP_DTO0_PHASE;
		registers[0].dp_dtox_modulo = mmDP_DTO0_MODULO;
		registers[0].crtcx_pixel_rate_cntl = mmCRTC0_PIXEL_RATE_CNTL;
		break;

	default:
		/* We can not get here because we checked number of
		 * controllers already. */
		break;
	}

	dal_clock_source_get_ss_info_from_atombios(
			base,
			AS_SIGNAL_TYPE_DISPLAY_PORT,
			&base->dp_ss_params,
			&base->dp_ss_params_cnt);

	return true;
}


struct clock_source *dal_ext_clock_source_dce110_create(
		struct clock_source_init_data *clk_src_init_data)
{
	struct ext_clock_source_dce110 *ecs110;

	ecs110 = dc_service_alloc(clk_src_init_data->ctx, sizeof(struct ext_clock_source_dce110));

	if (ecs110 == NULL)
		return NULL;

	if (!construct(ecs110, clk_src_init_data)) {
		dc_service_free(clk_src_init_data->ctx, ecs110);
		return NULL;
	}

	return &ecs110->base.base;
}
