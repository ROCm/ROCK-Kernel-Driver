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
/*
 * Pre-requisites: headers required by header of this unit
 */

#include "include/i2caux_interface.h"
#include "../engine.h"
#include "../i2c_engine.h"
#include "../i2c_hw_engine.h"
#include "../i2c_generic_hw_engine.h"
/*
 * Header of this unit
 */

#include "i2c_hw_engine_dce112.h"

#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"

/*
 * This unit
 */

enum dc_i2c_status {
	DC_I2C_STATUS__DC_I2C_STATUS_IDLE,
	DC_I2C_STATUS__DC_I2C_STATUS_USED_BY_SW,
	DC_I2C_STATUS__DC_I2C_STATUS_USED_BY_HW
};

enum dc_i2c_arbitration {
	DC_I2C_ARBITRATION__DC_I2C_SW_PRIORITY_NORMAL,
	DC_I2C_ARBITRATION__DC_I2C_SW_PRIORITY_HIGH
};

enum {
	/* No timeout in HW
	 * (timeout implemented in SW by querying status) */
	I2C_SETUP_TIME_LIMIT = 255,
	I2C_HW_BUFFER_SIZE = 538
};

/*
 * @brief
 * Cast pointer to 'struct i2c_hw_engine *'
 * to pointer 'struct i2c_hw_engine_dce110 *'
 */
#define FROM_I2C_HW_ENGINE(ptr) \
	container_of((ptr), struct i2c_hw_engine_dce110, base)
/*
 * @brief
 * Cast pointer to 'struct i2c_engine *'
 * to pointer to 'struct i2c_hw_engine_dce110 *'
 */
#define FROM_I2C_ENGINE(ptr) \
	FROM_I2C_HW_ENGINE(container_of((ptr), struct i2c_hw_engine, base))

/*
 * @brief
 * Cast pointer to 'struct engine *'
 * to 'pointer to struct i2c_hw_engine_dce110 *'
 */
#define FROM_ENGINE(ptr) \
	FROM_I2C_ENGINE(container_of((ptr), struct i2c_engine, base))

static bool setup_engine(
	struct i2c_engine *i2c_engine)
{
	uint32_t value = 0;
	struct i2c_hw_engine_dce110 *engine = FROM_I2C_ENGINE(i2c_engine);

	/* Program pin select */
	{
		const uint32_t addr = mmDC_I2C_CONTROL;

		value = dm_read_reg(i2c_engine->base.ctx, addr);

		set_reg_field_value(
			value,
			0,
			DC_I2C_CONTROL,
			DC_I2C_GO);

		set_reg_field_value(
			value,
			0,
			DC_I2C_CONTROL,
			DC_I2C_SOFT_RESET);

		set_reg_field_value(
			value,
			0,
			DC_I2C_CONTROL,
			DC_I2C_SEND_RESET);

		set_reg_field_value(
			value,
			1,
			DC_I2C_CONTROL,
			DC_I2C_SW_STATUS_RESET);

		set_reg_field_value(
			value,
			0,
			DC_I2C_CONTROL,
			DC_I2C_TRANSACTION_COUNT);

		set_reg_field_value(
			value,
			engine->engine_id,
			DC_I2C_CONTROL,
			DC_I2C_DDC_SELECT);

		dm_write_reg(i2c_engine->base.ctx, addr, value);
	}

	/* Program time limit */
	{
		const uint32_t addr = engine->addr.DC_I2C_DDCX_SETUP;

		value = dm_read_reg(i2c_engine->base.ctx, addr);

		set_reg_field_value(
			value,
			I2C_SETUP_TIME_LIMIT,
			DC_I2C_DDC1_SETUP,
			DC_I2C_DDC1_TIME_LIMIT);

		set_reg_field_value(
			value,
			1,
			DC_I2C_DDC1_SETUP,
			DC_I2C_DDC1_ENABLE);

		dm_write_reg(i2c_engine->base.ctx, addr, value);
	}

	/* Program HW priority
	 * set to High - interrupt software I2C at any time
	 * Enable restart of SW I2C that was interrupted by HW
	 * disable queuing of software while I2C is in use by HW */
	{
		value = dm_read_reg(i2c_engine->base.ctx,
				mmDC_I2C_ARBITRATION);

		set_reg_field_value(
			value,
			0,
			DC_I2C_ARBITRATION,
			DC_I2C_NO_QUEUED_SW_GO);

		set_reg_field_value(
			value,
			DC_I2C_ARBITRATION__DC_I2C_SW_PRIORITY_NORMAL,
			DC_I2C_ARBITRATION,
			DC_I2C_SW_PRIORITY);

		dm_write_reg(i2c_engine->base.ctx,
				mmDC_I2C_ARBITRATION, value);
	}

	return true;
}

static uint32_t get_speed(
	const struct i2c_engine *i2c_engine)
{
	const struct i2c_hw_engine_dce110 *engine = FROM_I2C_ENGINE(i2c_engine);

	const uint32_t addr = engine->addr.DC_I2C_DDCX_SPEED;

	uint32_t pre_scale = 0;

	uint32_t value = dm_read_reg(i2c_engine->base.ctx, addr);

	pre_scale = get_reg_field_value(
			value,
			DC_I2C_DDC1_SPEED,
			DC_I2C_DDC1_PRESCALE);

	/* [anaumov] it seems following is unnecessary */
	/*ASSERT(value.bits.DC_I2C_DDC1_PRESCALE);*/

	return pre_scale ?
		engine->reference_frequency / pre_scale :
		engine->base.default_speed;
}

static void set_speed(
	struct i2c_engine *i2c_engine,
	uint32_t speed)
{
	struct i2c_hw_engine_dce110 *engine = FROM_I2C_ENGINE(i2c_engine);

	if (speed) {
		const uint32_t addr = engine->addr.DC_I2C_DDCX_SPEED;

		uint32_t value = dm_read_reg(i2c_engine->base.ctx, addr);

		set_reg_field_value(
			value,
			engine->reference_frequency / speed,
			DC_I2C_DDC1_SPEED,
			DC_I2C_DDC1_PRESCALE);

		set_reg_field_value(
			value,
			2,
			DC_I2C_DDC1_SPEED,
			DC_I2C_DDC1_THRESHOLD);

		/*DCE11, HW add 100Khz support for I2c*/
		if (speed > 50) {
			set_reg_field_value(
				value,
				2,
				DC_I2C_DDC1_SPEED,
				DC_I2C_DDC1_START_STOP_TIMING_CNTL);
		} else {
			set_reg_field_value(
				value,
				1,
				DC_I2C_DDC1_SPEED,
				DC_I2C_DDC1_START_STOP_TIMING_CNTL);
		}

		dm_write_reg(i2c_engine->base.ctx, addr, value);
	}
}

static inline void reset_hw_engine(struct engine *engine)
{
	uint32_t value = dm_read_reg(engine->ctx, mmDC_I2C_CONTROL);

	set_reg_field_value(
		value,
		1,
		DC_I2C_CONTROL,
		DC_I2C_SOFT_RESET);

	set_reg_field_value(
		value,
		1,
		DC_I2C_CONTROL,
		DC_I2C_SW_STATUS_RESET);

	dm_write_reg(engine->ctx, mmDC_I2C_CONTROL, value);
}

static bool is_hw_busy(struct engine *engine)
{
	uint32_t i2c_sw_status = 0;

	uint32_t value = dm_read_reg(engine->ctx, mmDC_I2C_SW_STATUS);

	i2c_sw_status = get_reg_field_value(
			value,
			DC_I2C_SW_STATUS,
			DC_I2C_SW_STATUS);

	if (i2c_sw_status == DC_I2C_STATUS__DC_I2C_STATUS_IDLE)
		return false;

	reset_hw_engine(engine);

	value = dm_read_reg(engine->ctx, mmDC_I2C_SW_STATUS);

	i2c_sw_status = get_reg_field_value(
			value,
			DC_I2C_SW_STATUS,
			DC_I2C_SW_STATUS);

	return i2c_sw_status != DC_I2C_STATUS__DC_I2C_STATUS_IDLE;
}

/*
 * @brief
 * DC_GPIO_DDC MM register offsets
 */
static const uint32_t transaction_addr[] = {
	mmDC_I2C_TRANSACTION0,
	mmDC_I2C_TRANSACTION1,
	mmDC_I2C_TRANSACTION2,
	mmDC_I2C_TRANSACTION3
};

static bool process_transaction(
	struct i2c_hw_engine_dce110 *engine,
	struct i2c_request_transaction_data *request)
{
	uint32_t length = request->length;
	uint8_t *buffer = request->data;

	bool last_transaction = false;
	uint32_t value = 0;

	struct dc_context *ctx = NULL;

	ctx = engine->base.base.base.ctx;

	{
		const uint32_t addr =
			transaction_addr[engine->transaction_count];

		value = dm_read_reg(ctx, addr);

		set_reg_field_value(
			value,
			1,
			DC_I2C_TRANSACTION0,
			DC_I2C_STOP_ON_NACK0);

		set_reg_field_value(
			value,
			1,
			DC_I2C_TRANSACTION0,
			DC_I2C_START0);

		if ((engine->transaction_count == 3) ||
		(request->action == I2CAUX_TRANSACTION_ACTION_I2C_WRITE) ||
		(request->action & I2CAUX_TRANSACTION_ACTION_I2C_READ)) {

			set_reg_field_value(
				value,
				1,
				DC_I2C_TRANSACTION0,
				DC_I2C_STOP0);

			last_transaction = true;
		} else
			set_reg_field_value(
				value,
				0,
				DC_I2C_TRANSACTION0,
				DC_I2C_STOP0);

		set_reg_field_value(
			value,
			(0 != (request->action &
					I2CAUX_TRANSACTION_ACTION_I2C_READ)),
			DC_I2C_TRANSACTION0,
			DC_I2C_RW0);

		set_reg_field_value(
			value,
			length,
			DC_I2C_TRANSACTION0,
			DC_I2C_COUNT0);

		dm_write_reg(ctx, addr, value);
	}

	/* Write the I2C address and I2C data
	 * into the hardware circular buffer, one byte per entry.
	 * As an example, the 7-bit I2C slave address for CRT monitor
	 * for reading DDC/EDID information is 0b1010001.
	 * For an I2C send operation, the LSB must be programmed to 0;
	 * for I2C receive operation, the LSB must be programmed to 1. */

	{
		value = 0;

		set_reg_field_value(
			value,
			false,
			DC_I2C_DATA,
			DC_I2C_DATA_RW);

		set_reg_field_value(
			value,
			request->address,
			DC_I2C_DATA,
			DC_I2C_DATA);

		if (engine->transaction_count == 0) {
			set_reg_field_value(
				value,
				0,
				DC_I2C_DATA,
				DC_I2C_INDEX);

			/*enable index write*/
			set_reg_field_value(
				value,
				1,
				DC_I2C_DATA,
				DC_I2C_INDEX_WRITE);

			engine->buffer_used_write = 0;
		}

		dm_write_reg(ctx, mmDC_I2C_DATA, value);

		engine->buffer_used_write++;

		if (!(request->action & I2CAUX_TRANSACTION_ACTION_I2C_READ)) {

			set_reg_field_value(
				value,
				0,
				DC_I2C_DATA,
				DC_I2C_INDEX_WRITE);

			while (length) {

				set_reg_field_value(
					value,
					*buffer++,
					DC_I2C_DATA,
					DC_I2C_DATA);

				dm_write_reg(ctx, mmDC_I2C_DATA, value);

				engine->buffer_used_write++;
				--length;
			}
		}
	}

	++engine->transaction_count;
	engine->buffer_used_bytes += length + 1;

	return last_transaction;
}

static void execute_transaction(
	struct i2c_hw_engine_dce110 *engine)
{
	uint32_t value = 0;
	struct dc_context *ctx = NULL;

	ctx = engine->base.base.base.ctx;

	{
		const uint32_t addr = engine->addr.DC_I2C_DDCX_SETUP;

		value = dm_read_reg(ctx, addr);

		set_reg_field_value(
			value,
			0,
			DC_I2C_DDC1_SETUP,
			DC_I2C_DDC1_DATA_DRIVE_EN);

		set_reg_field_value(
			value,
			0,
			DC_I2C_DDC1_SETUP,
			DC_I2C_DDC1_CLK_DRIVE_EN);

		set_reg_field_value(
			value,
			0,
			DC_I2C_DDC1_SETUP,
			DC_I2C_DDC1_DATA_DRIVE_SEL);

		set_reg_field_value(
			value,
			0,
			DC_I2C_DDC1_SETUP,
			DC_I2C_DDC1_INTRA_TRANSACTION_DELAY);

		set_reg_field_value(
			value,
			0,
			DC_I2C_DDC1_SETUP,
			DC_I2C_DDC1_INTRA_BYTE_DELAY);

		dm_write_reg(ctx, addr, value);
	}

	{
		const uint32_t addr = mmDC_I2C_CONTROL;

		value = dm_read_reg(ctx, addr);

		set_reg_field_value(
			value,
			0,
			DC_I2C_CONTROL,
			DC_I2C_SOFT_RESET);

		set_reg_field_value(
			value,
			0,
			DC_I2C_CONTROL,
			DC_I2C_SW_STATUS_RESET);

		set_reg_field_value(
			value,
			0,
			DC_I2C_CONTROL,
			DC_I2C_SEND_RESET);

		set_reg_field_value(
			value,
			0,
			DC_I2C_CONTROL,
			DC_I2C_GO);

		set_reg_field_value(
			value,
			engine->transaction_count - 1,
			DC_I2C_CONTROL,
			DC_I2C_TRANSACTION_COUNT);

		dm_write_reg(ctx, addr, value);
	}

	/* start I2C transfer */
	{
		const uint32_t addr = mmDC_I2C_CONTROL;

		value	= dm_read_reg(ctx, addr);

		set_reg_field_value(
			value,
			1,
			DC_I2C_CONTROL,
			DC_I2C_GO);

		dm_write_reg(ctx, addr, value);
	}

	/* all transactions were executed and HW buffer became empty
	 * (even though it actually happens when status becomes DONE) */
	engine->transaction_count = 0;
	engine->buffer_used_bytes = 0;
}

static void submit_channel_request(
	struct i2c_engine *engine,
	struct i2c_request_transaction_data *request)
{
	request->status = I2C_CHANNEL_OPERATION_SUCCEEDED;

	if (!process_transaction(FROM_I2C_ENGINE(engine), request))
		return;

	if (is_hw_busy(&engine->base)) {
		request->status = I2C_CHANNEL_OPERATION_ENGINE_BUSY;
		return;
	}

	execute_transaction(FROM_I2C_ENGINE(engine));
}

static void process_channel_reply(
	struct i2c_engine *engine,
	struct i2c_reply_transaction_data *reply)
{
	uint32_t length = reply->length;
	uint8_t *buffer = reply->data;

	struct i2c_hw_engine_dce110 *i2c_hw_engine_dce110 =
		FROM_I2C_ENGINE(engine);

	uint32_t value = 0;

	/*set index*/
	set_reg_field_value(
		value,
		i2c_hw_engine_dce110->buffer_used_write,
		DC_I2C_DATA,
		DC_I2C_INDEX);

	set_reg_field_value(
		value,
		1,
		DC_I2C_DATA,
		DC_I2C_DATA_RW);

	set_reg_field_value(
		value,
		1,
		DC_I2C_DATA,
		DC_I2C_INDEX_WRITE);

	dm_write_reg(engine->base.ctx, mmDC_I2C_DATA, value);

	while (length) {
		/* after reading the status,
		 * if the I2C operation executed successfully
		 * (i.e. DC_I2C_STATUS_DONE = 1) then the I2C controller
		 * should read data bytes from I2C circular data buffer */

		value = dm_read_reg(engine->base.ctx, mmDC_I2C_DATA);

		*buffer++ = get_reg_field_value(
				value,
				DC_I2C_DATA,
				DC_I2C_DATA);

		--length;
	}
}

static enum i2c_channel_operation_result get_channel_status(
	struct i2c_engine *engine,
	uint8_t *returned_bytes)
{
	uint32_t i2c_sw_status = 0;
	uint32_t value = dm_read_reg(engine->base.ctx, mmDC_I2C_SW_STATUS);

	i2c_sw_status = get_reg_field_value(
			value,
			DC_I2C_SW_STATUS,
			DC_I2C_SW_STATUS);

	if (i2c_sw_status == DC_I2C_STATUS__DC_I2C_STATUS_USED_BY_SW)
		return I2C_CHANNEL_OPERATION_ENGINE_BUSY;
	else if (value & DC_I2C_SW_STATUS__DC_I2C_SW_STOPPED_ON_NACK_MASK)
		return I2C_CHANNEL_OPERATION_NO_RESPONSE;
	else if (value & DC_I2C_SW_STATUS__DC_I2C_SW_TIMEOUT_MASK)
		return I2C_CHANNEL_OPERATION_TIMEOUT;
	else if (value & DC_I2C_SW_STATUS__DC_I2C_SW_ABORTED_MASK)
		return I2C_CHANNEL_OPERATION_FAILED;
	else if (value & DC_I2C_SW_STATUS__DC_I2C_SW_DONE_MASK)
		return I2C_CHANNEL_OPERATION_SUCCEEDED;

	/*
	 * this is the case when HW used for communication, I2C_SW_STATUS
	 * could be zero
	 */
	return I2C_CHANNEL_OPERATION_SUCCEEDED;
}

static void destroy(
	struct i2c_engine **i2c_engine)
{
	struct i2c_hw_engine_dce110 *engine_dce110 =
			FROM_I2C_ENGINE(*i2c_engine);

	dal_i2c_hw_engine_destruct(&engine_dce110->base);

	dm_free(engine_dce110);

	*i2c_engine = NULL;
}

static const struct i2c_engine_funcs i2c_engine_funcs = {
	.destroy = destroy,
	.get_speed = get_speed,
	.set_speed = set_speed,
	.setup_engine = setup_engine,
	.submit_channel_request = submit_channel_request,
	.process_channel_reply = process_channel_reply,
	.get_channel_status = get_channel_status,
	.acquire_engine = dal_i2c_hw_engine_acquire_engine,
};

static bool construct(
	struct i2c_hw_engine_dce110 *engine_dce110,
	const struct i2c_hw_engine_dce110_create_arg *arg)
{
	if (!i2c_hw_engine_dce110_construct(engine_dce110, arg))
		return false;

	engine_dce110->base.base.funcs = &i2c_engine_funcs;

	return true;
}

struct i2c_engine *dal_i2c_hw_engine_dce112_create(
	const struct i2c_hw_engine_dce110_create_arg *arg)
{
	struct i2c_hw_engine_dce110 *engine_dce10;

	if (!arg) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	engine_dce10 = dm_alloc(sizeof(struct i2c_hw_engine_dce110));

	if (!engine_dce10) {
		ASSERT_CRITICAL(false);
		return NULL;
	}

	if (construct(engine_dce10, arg))
		return &engine_dce10->base.base;

	ASSERT_CRITICAL(false);

	dm_free(engine_dce10);

	return NULL;
}
