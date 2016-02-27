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

/*
 * Pre-requisites: headers required by header of this unit
 */
#include "include/i2caux_interface.h"
#include "../engine.h"
#include "../aux_engine.h"

/*
 * Header of this unit
 */

#include "aux_engine_dce80.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

/*
 * This unit
 */

/*
 * @brief
 * Cast 'struct aux_engine *'
 * to 'struct aux_engine_dce80 *'
 */
#define FROM_AUX_ENGINE(ptr) \
	container_of((ptr), struct aux_engine_dce80, base)

/*
 * @brief
 * Cast 'struct engine *'
 * to 'struct aux_engine_dce80 *'
 */
#define FROM_ENGINE(ptr) \
	FROM_AUX_ENGINE(container_of((ptr), struct aux_engine, base))

static void release_engine(
	struct engine *engine)
{
	struct aux_engine_dce80 *aux_engine = FROM_ENGINE(engine);

	const uint32_t addr = aux_engine->addr.AUX_ARB_CONTROL;

	uint32_t value = dm_read_reg(engine->ctx, addr);

	set_reg_field_value(
		value,
		1,
		AUX_ARB_CONTROL,
		AUX_SW_DONE_USING_AUX_REG);

	dm_write_reg(engine->ctx, addr, value);
}

static void destruct(
	struct aux_engine_dce80 *engine);

static void destroy(
	struct aux_engine **aux_engine)
{
	struct aux_engine_dce80 *engine = FROM_AUX_ENGINE(*aux_engine);

	destruct(engine);

	dm_free(engine);

	*aux_engine = NULL;
}

#define SW_CAN_ACCESS_AUX 1

static bool acquire_engine(
	struct aux_engine *engine)
{
	struct aux_engine_dce80 *aux_engine = FROM_AUX_ENGINE(engine);
	uint32_t value;
	uint32_t field;

	/* enable AUX before request SW to access AUX */
	{
		const uint32_t addr = aux_engine->addr.AUX_CONTROL;

		value = dm_read_reg(engine->base.ctx, addr);

		field = get_reg_field_value(
				value,
				AUX_CONTROL,
				AUX_EN);

		if (field == 0) {
			set_reg_field_value(
				value,
				1,
				AUX_CONTROL,
				AUX_EN);

			dm_write_reg(engine->base.ctx, addr, value);
		}
	}

	/* request SW to access AUX */
	{
		const uint32_t addr = aux_engine->addr.AUX_ARB_CONTROL;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			1,
			AUX_ARB_CONTROL,
			AUX_SW_USE_AUX_REG_REQ);

		dm_write_reg(engine->base.ctx, addr, value);

		value = dm_read_reg(engine->base.ctx, addr);

		field = get_reg_field_value(
				value,
				AUX_ARB_CONTROL,
				AUX_REG_RW_CNTL_STATUS);

		return field == SW_CAN_ACCESS_AUX;
	}
}

static void configure(
	struct aux_engine *engine,
	union aux_config cfg)
{
	struct aux_engine_dce80 *aux_engine = FROM_AUX_ENGINE(engine);

	const uint32_t addr = aux_engine->addr.AUX_CONTROL;

	uint32_t value = dm_read_reg(engine->base.ctx, addr);

	set_reg_field_value(
		value,
		(0 != cfg.bits.ALLOW_AUX_WHEN_HPD_LOW),
		AUX_CONTROL,
		AUX_IGNORE_HPD_DISCON);

	dm_write_reg(engine->base.ctx, addr, value);
}

static bool start_gtc_sync(
	struct aux_engine *engine)
{
	/* TODO */
	return false;
}

static void stop_gtc_sync(
	struct aux_engine *engine)
{
	/* TODO */
}

#define COMPOSE_AUX_SW_DATA_16_20(command, address) \
	((command) | ((0xF0000 & (address)) >> 16))

#define COMPOSE_AUX_SW_DATA_8_15(address) \
	((0xFF00 & (address)) >> 8)

#define COMPOSE_AUX_SW_DATA_0_7(address) \
	(0xFF & (address))

static void submit_channel_request(
	struct aux_engine *engine,
	struct aux_request_transaction_data *request)
{
	struct aux_engine_dce80 *aux_engine = FROM_AUX_ENGINE(engine);
	uint32_t value;
	uint32_t length;

	bool is_write =
		((request->type == AUX_TRANSACTION_TYPE_DP) &&
		 (request->action == I2CAUX_TRANSACTION_ACTION_DP_WRITE)) ||
		((request->type == AUX_TRANSACTION_TYPE_I2C) &&
		((request->action == I2CAUX_TRANSACTION_ACTION_I2C_WRITE) ||
		 (request->action == I2CAUX_TRANSACTION_ACTION_I2C_WRITE_MOT)));

	/* clear_aux_error */
	{
		const uint32_t addr = mmAUXN_IMPCAL;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			1,
			AUXN_IMPCAL,
			AUXN_CALOUT_ERROR_AK);

		dm_write_reg(engine->base.ctx, addr, value);

		set_reg_field_value(
			value,
			0,
			AUXN_IMPCAL,
			AUXN_CALOUT_ERROR_AK);

		dm_write_reg(engine->base.ctx, addr, value);
	}
	{
		const uint32_t addr = mmAUXP_IMPCAL;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			1,
			AUXP_IMPCAL,
			AUXP_CALOUT_ERROR_AK);

		dm_write_reg(engine->base.ctx, addr, value);

		set_reg_field_value(
			value,
			0,
			AUXP_IMPCAL,
			AUXP_CALOUT_ERROR_AK);

		dm_write_reg(engine->base.ctx, addr, value);
	}

	/* force_default_calibrate */
	{
		const uint32_t addr = mmAUXN_IMPCAL;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			1,
			AUXN_IMPCAL,
			AUXN_IMPCAL_ENABLE);

		dm_write_reg(engine->base.ctx, addr, value);

		set_reg_field_value(
			value,
			0,
			AUXN_IMPCAL,
			AUXN_IMPCAL_OVERRIDE_ENABLE);

		dm_write_reg(engine->base.ctx, addr, value);
	}
	{
		const uint32_t addr = mmAUXP_IMPCAL;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			1,
			AUXP_IMPCAL,
			AUXP_IMPCAL_OVERRIDE_ENABLE);

		dm_write_reg(engine->base.ctx, addr, value);

		set_reg_field_value(
			value,
			0,
			AUXP_IMPCAL,
			AUXP_IMPCAL_OVERRIDE_ENABLE);

		dm_write_reg(engine->base.ctx, addr, value);
	}

	/* set the delay and the number of bytes to write */
	{
		const uint32_t addr = aux_engine->addr.AUX_SW_CONTROL;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			request->delay,
			AUX_SW_CONTROL,
			AUX_SW_START_DELAY);

		/* The length include
		 * the 4 bit header and the 20 bit address
		 * (that is 3 byte).
		 * If the requested length is non zero this means
		 * an addition byte specifying the length is required. */

		length = request->length ? 4 : 3;
		if (is_write)
			length += request->length;

		set_reg_field_value(
			value,
			length,
			AUX_SW_CONTROL,
			AUX_SW_WR_BYTES);

		dm_write_reg(engine->base.ctx, addr, value);
	}

	/* program action and address and payload data (if 'is_write') */
	{
		const uint32_t addr = aux_engine->addr.AUX_SW_DATA;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			0,
			AUX_SW_DATA,
			AUX_SW_INDEX);

		set_reg_field_value(
			value,
			0,
			AUX_SW_DATA,
			AUX_SW_DATA_RW);

		set_reg_field_value(
			value,
			1,
			AUX_SW_DATA,
			AUX_SW_AUTOINCREMENT_DISABLE);

		set_reg_field_value(
			value,
			COMPOSE_AUX_SW_DATA_16_20(
				request->action, request->address),
			AUX_SW_DATA,
			AUX_SW_DATA);

		dm_write_reg(engine->base.ctx, addr, value);

		set_reg_field_value(
			value,
			0,
			AUX_SW_DATA,
			AUX_SW_AUTOINCREMENT_DISABLE);

		set_reg_field_value(
			value,
			COMPOSE_AUX_SW_DATA_8_15(request->address),
			AUX_SW_DATA,
			AUX_SW_DATA);

		dm_write_reg(engine->base.ctx, addr, value);

		set_reg_field_value(
			value,
			COMPOSE_AUX_SW_DATA_0_7(request->address),
			AUX_SW_DATA,
			AUX_SW_DATA);

		dm_write_reg(engine->base.ctx, addr, value);

		if (request->length) {
			set_reg_field_value(
				value,
				request->length - 1,
				AUX_SW_DATA,
				AUX_SW_DATA);

			dm_write_reg(engine->base.ctx, addr, value);
		}

		if (is_write) {
			/* Load the HW buffer with the Data to be sent.
			 * This is relevant for write operation.
			 * For read, the data recived data will be
			 * processed in process_channel_reply(). */
			uint32_t i = 0;

			while (i < request->length) {

				set_reg_field_value(
					value,
					request->data[i],
					AUX_SW_DATA,
					AUX_SW_DATA);

				dm_write_reg(
					engine->base.ctx, addr, value);

				++i;
			}
		}
	}

	{
		const uint32_t addr = aux_engine->addr.AUX_INTERRUPT_CONTROL;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			1,
			AUX_INTERRUPT_CONTROL,
			AUX_SW_DONE_ACK);

		dm_write_reg(engine->base.ctx, addr, value);
	}

	{
		const uint32_t addr = aux_engine->addr.AUX_SW_CONTROL;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			1,
			AUX_SW_CONTROL,
			AUX_SW_GO);

		dm_write_reg(engine->base.ctx, addr, value);
	}
}

static void process_channel_reply(
	struct aux_engine *engine,
	struct aux_reply_transaction_data *reply)
{
	struct aux_engine_dce80 *aux_engine = FROM_AUX_ENGINE(engine);

	/* Need to do a read to get the number of bytes to process
	 * Alternatively, this information can be passed -
	 * but that causes coupling which isn't good either. */

	uint32_t bytes_replied;
	uint32_t value;

	{
		const uint32_t addr = aux_engine->addr.AUX_SW_STATUS;

		value = dm_read_reg(engine->base.ctx, addr);

		bytes_replied = get_reg_field_value(
				value,
				AUX_SW_STATUS,
				AUX_SW_REPLY_BYTE_COUNT);
	}

	if (bytes_replied) {
		uint32_t reply_result;

		const uint32_t addr = aux_engine->addr.AUX_SW_DATA;

		value = dm_read_reg(engine->base.ctx, addr);

		set_reg_field_value(
			value,
			0,
			AUX_SW_DATA,
			AUX_SW_INDEX);

		dm_write_reg(engine->base.ctx, addr, value);

		set_reg_field_value(
			value,
			1,
			AUX_SW_DATA,
			AUX_SW_AUTOINCREMENT_DISABLE);

		dm_write_reg(engine->base.ctx, addr, value);

		set_reg_field_value(
			value,
			1,
			AUX_SW_DATA,
			AUX_SW_DATA_RW);

		dm_write_reg(engine->base.ctx, addr, value);

		value = dm_read_reg(engine->base.ctx, addr);

		reply_result = get_reg_field_value(
				value,
				AUX_SW_DATA,
				AUX_SW_DATA);

		reply_result = reply_result >> 4;

		switch (reply_result) {
		case 0: /* ACK */ {
			uint32_t i = 0;

			/* first byte was already used
			 * to get the command status */
			--bytes_replied;

			while (i < bytes_replied) {
				value = dm_read_reg(
					engine->base.ctx, addr);

				reply->data[i] = get_reg_field_value(
						value,
						AUX_SW_DATA,
						AUX_SW_DATA);

				++i;
			}

			reply->status = AUX_TRANSACTION_REPLY_AUX_ACK;
		}
		break;
		case 1: /* NACK */
			reply->status = AUX_TRANSACTION_REPLY_AUX_NACK;
		break;
		case 2: /* DEFER */
			reply->status = AUX_TRANSACTION_REPLY_AUX_DEFER;
		break;
		case 4: /* AUX ACK / I2C NACK */
			reply->status = AUX_TRANSACTION_REPLY_I2C_NACK;
		break;
		case 8: /* AUX ACK / I2C DEFER */
			reply->status = AUX_TRANSACTION_REPLY_I2C_DEFER;
		break;
		default:
			reply->status = AUX_TRANSACTION_REPLY_INVALID;
		}
	} else {
		/* Need to handle an error case...
		 * hopefully, upper layer function won't call this function
		 * if the number of bytes in the reply was 0
		 * because there was surely an error that was asserted
		 * that should have been handled
		 * for hot plug case, this could happens*/
		if (!(value & AUX_SW_STATUS__AUX_SW_HPD_DISCON_MASK))
			ASSERT_CRITICAL(false);
	}
}

static enum aux_channel_operation_result get_channel_status(
	struct aux_engine *engine,
	uint8_t *returned_bytes)
{
	struct aux_engine_dce80 *aux_engine = FROM_AUX_ENGINE(engine);

	const uint32_t addr = aux_engine->addr.AUX_SW_STATUS;

	uint32_t value;
	uint32_t aux_sw_done;

	if (returned_bytes == NULL) {
		/*caller pass NULL pointer*/
		ASSERT_CRITICAL(false);
		return AUX_CHANNEL_OPERATION_FAILED_REASON_UNKNOWN;
	}
	*returned_bytes = 0;

	/* poll to make sure that SW_DONE is asserted */
	{
		uint32_t time_elapsed = 0;

		do {
			value = dm_read_reg(engine->base.ctx, addr);

			aux_sw_done = get_reg_field_value(
					value,
					AUX_SW_STATUS,
					AUX_SW_DONE);

			if (aux_sw_done)
				break;

			udelay(10);

			time_elapsed += 10;
		} while (time_elapsed < aux_engine->timeout_period);

	}

	/* Note that the following bits are set in 'status.bits'
	 * during CTS 4.2.1.2:
	 * AUX_SW_RX_MIN_COUNT_VIOL, AUX_SW_RX_INVALID_STOP,
	 * AUX_SW_RX_RECV_NO_DET, AUX_SW_RX_RECV_INVALID_H.
	 *
	 * AUX_SW_RX_MIN_COUNT_VIOL is an internal,
	 * HW debugging bit and should be ignored. */
	if (aux_sw_done) {
		if (get_reg_field_value(
			value,
			AUX_SW_STATUS,
			AUX_SW_RX_TIMEOUT_STATE) ||
			get_reg_field_value(
				value,
				AUX_SW_STATUS,
				AUX_SW_RX_TIMEOUT))
			return AUX_CHANNEL_OPERATION_FAILED_TIMEOUT;
		else if (get_reg_field_value(
			value,
			AUX_SW_STATUS,
			AUX_SW_RX_INVALID_STOP))
			return AUX_CHANNEL_OPERATION_FAILED_INVALID_REPLY;

		*returned_bytes = get_reg_field_value(
				value,
				AUX_SW_STATUS,
				AUX_SW_REPLY_BYTE_COUNT);
		if (*returned_bytes == 0)
			return
			AUX_CHANNEL_OPERATION_FAILED_INVALID_REPLY;
		else {
			*returned_bytes -= 1;
			return AUX_CHANNEL_OPERATION_SUCCEEDED;
		}
	} else {
		/*time_elapsed >= aux_engine->timeout_period */
		if (!(value & AUX_SW_STATUS__AUX_SW_HPD_DISCON_MASK))
			ASSERT_CRITICAL(false);
		return AUX_CHANNEL_OPERATION_FAILED_TIMEOUT;
	}
}

static const int32_t aux_channel_offset[] = {
	mmDP_AUX0_AUX_CONTROL - mmDP_AUX0_AUX_CONTROL,
	mmDP_AUX1_AUX_CONTROL - mmDP_AUX0_AUX_CONTROL,
	mmDP_AUX2_AUX_CONTROL - mmDP_AUX0_AUX_CONTROL,
	mmDP_AUX3_AUX_CONTROL - mmDP_AUX0_AUX_CONTROL,
	mmDP_AUX4_AUX_CONTROL - mmDP_AUX0_AUX_CONTROL,
	mmDP_AUX5_AUX_CONTROL - mmDP_AUX0_AUX_CONTROL
};

static const struct aux_engine_funcs aux_engine_funcs = {
	.destroy = destroy,
	.acquire_engine = acquire_engine,
	.configure = configure,
	.start_gtc_sync = start_gtc_sync,
	.stop_gtc_sync = stop_gtc_sync,
	.submit_channel_request = submit_channel_request,
	.process_channel_reply = process_channel_reply,
	.get_channel_status = get_channel_status,
};

static const struct engine_funcs engine_funcs = {
	.release_engine = release_engine,
	.submit_request = dal_aux_engine_submit_request,
	.keep_power_up_count = dal_i2caux_keep_power_up_count,
	.get_engine_type = dal_aux_engine_get_engine_type,
	.acquire = dal_aux_engine_acquire,
};

static bool construct(
	struct aux_engine_dce80 *engine,
	const struct aux_engine_dce80_create_arg *arg)
{
	int32_t offset;

	if (arg->engine_id >= sizeof(aux_channel_offset) / sizeof(int32_t)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!dal_aux_engine_construct(&engine->base, arg->ctx)) {
		BREAK_TO_DEBUGGER();
		return false;
	}
	engine->base.base.funcs = &engine_funcs;
	engine->base.funcs = &aux_engine_funcs;
	offset = aux_channel_offset[arg->engine_id];
	engine->addr.AUX_CONTROL = mmAUX_CONTROL + offset;
	engine->addr.AUX_ARB_CONTROL = mmAUX_ARB_CONTROL + offset;
	engine->addr.AUX_SW_DATA = mmAUX_SW_DATA + offset;
	engine->addr.AUX_SW_CONTROL = mmAUX_SW_CONTROL + offset;
	engine->addr.AUX_INTERRUPT_CONTROL = mmAUX_INTERRUPT_CONTROL + offset;
	engine->addr.AUX_SW_STATUS = mmAUX_SW_STATUS + offset;
	engine->addr.AUX_GTC_SYNC_CONTROL = mmAUX_GTC_SYNC_CONTROL + offset;
	engine->addr.AUX_GTC_SYNC_STATUS = mmAUX_GTC_SYNC_STATUS + offset;
	engine->addr.AUX_GTC_SYNC_CONTROLLER_STATUS =
		mmAUX_GTC_SYNC_CONTROLLER_STATUS + offset;

	engine->timeout_period = arg->timeout_period;

	return true;
}

static void destruct(
	struct aux_engine_dce80 *engine)
{
	dal_aux_engine_destruct(&engine->base);
}

struct aux_engine *dal_aux_engine_dce80_create(
	const struct aux_engine_dce80_create_arg *arg)
{
	struct aux_engine_dce80 *engine;

	if (!arg) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	engine = dm_alloc(sizeof(struct aux_engine_dce80));

	if (!engine) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (construct(engine, arg))
		return &engine->base;

	BREAK_TO_DEBUGGER();

	dm_free(engine);

	return NULL;
}
