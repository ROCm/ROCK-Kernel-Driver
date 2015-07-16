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

#include "dal_services.h"
#include "include/adapter_service_interface.h"
#include "include/i2caux_interface.h"
#include "include/ddc_service_interface.h"
#include "include/ddc_service_types.h"
#include "include/grph_object_id.h"
#include "include/dpcd_defs.h"
#include "include/logger_interface.h"
#include "ddc_i2caux_helper.h"
#include "ddc_service.h"

#define AUX_POWER_UP_WA_DELAY 500
#define I2C_OVER_AUX_DEFER_WA_DELAY 70

/* CV smart dongle slave address for retrieving supported HDTV modes*/
#define CV_SMART_DONGLE_ADDRESS 0x20
/* DVI-HDMI dongle slave address for retrieving dongle signature*/
#define DVI_HDMI_DONGLE_ADDRESS 0x68
static const int8_t dvi_hdmi_dongle_signature_str[] = "6140063500G";
struct dvi_hdmi_dongle_signature_data {
	int8_t vendor[3];/* "AMD" */
	uint8_t version[2];
	uint8_t size;
	int8_t id[11];/* "6140063500G"*/
};
/* DP-HDMI dongle slave address for retrieving dongle signature*/
#define DP_HDMI_DONGLE_ADDRESS 0x40
static const uint8_t dp_hdmi_dongle_signature_str[] = "DP-HDMI ADAPTOR";
#define DP_HDMI_DONGLE_SIGNATURE_EOT 0x04

struct dp_hdmi_dongle_signature_data {
	int8_t id[15];/* "DP-HDMI ADAPTOR"*/
	uint8_t eot;/* end of transmition '\x4' */
};

/* Address range from 0x00 to 0x1F.*/
#define DP_ADAPTOR_TYPE2_SIZE 0x20
#define DP_ADAPTOR_TYPE2_REG_ID 0x10
#define DP_ADAPTOR_TYPE2_REG_MAX_TMDS_CLK 0x1D
/* Identifies adaptor as Dual-mode adaptor */
#define DP_ADAPTOR_TYPE2_ID 0xA0
/* MHz*/
#define DP_ADAPTOR_TYPE2_MAX_TMDS_CLK 30
/* MHz*/
#define DP_ADAPTOR_TYPE2_MIN_TMDS_CLK 2

#define DDC_I2C_COMMAND_ENGINE I2C_COMMAND_ENGINE_SW

enum edid_read_result {
	EDID_READ_RESULT_EDID_MATCH = 0,
	EDID_READ_RESULT_EDID_MISMATCH,
	EDID_READ_RESULT_CHECKSUM_READ_ERR,
	EDID_READ_RESULT_VENDOR_READ_ERR
};

union dp_downstream_port_present {
	uint8_t byte;
	struct {
		uint8_t PORT_PRESENT:1;
		uint8_t PORT_TYPE:2;
		uint8_t FMT_CONVERSION:1;
		uint8_t DETAILED_CAPS:1;
		uint8_t RESERVED:3;
	} fields;
};

union ddc_wa {
	struct {
		uint32_t DP_SKIP_POWER_OFF:1;
		uint32_t DP_AUX_POWER_UP_WA_DELAY:1;
	} bits;
	uint32_t raw;
};

struct ddc_flags {
	uint8_t EDID_QUERY_DONE_ONCE:1;
	uint8_t IS_INTERNAL_DISPLAY:1;
	uint8_t FORCE_READ_REPEATED_START:1;
	uint8_t EDID_STRESS_READ:1;

};

struct ddc_service {
	struct ddc *ddc_pin;
	struct ddc_flags flags;
	union ddc_wa wa;
	enum ddc_transaction_type transaction_type;
	enum display_dongle_type dongle_type;
	struct dp_receiver_id_info dp_receiver_id_info;
	struct adapter_service *as;
	struct dal_context *ctx;

	uint32_t address;
	uint32_t edid_buf_len;
	uint8_t edid_buf[MAX_EDID_BUFFER_SIZE];
};

static bool construct(
	struct ddc_service *ddc_service,
	struct ddc_service_init_data *init_data)
{
	enum connector_id connector_id =
		dal_graphics_object_id_get_connector_id(init_data->id);

	ddc_service->ctx = init_data->ctx;
	ddc_service->as = init_data->as;
	ddc_service->ddc_pin = dal_adapter_service_obtain_ddc(
			init_data->as, init_data->id);

	ddc_service->flags.EDID_QUERY_DONE_ONCE = false;

	ddc_service->flags.FORCE_READ_REPEATED_START =
		dal_adapter_service_is_feature_supported(
			FEATURE_DDC_READ_FORCE_REPEATED_START);

	ddc_service->flags.EDID_STRESS_READ =
			dal_adapter_service_is_feature_supported(
				FEATURE_EDID_STRESS_READ);


	ddc_service->flags.IS_INTERNAL_DISPLAY =
		connector_id == CONNECTOR_ID_EDP ||
		connector_id == CONNECTOR_ID_LVDS;

	ddc_service->wa.raw = 0;
	return true;
}

struct ddc_service *dal_ddc_service_create(
	struct ddc_service_init_data *init_data)
{
	struct ddc_service *ddc_service;

	ddc_service = dal_alloc(sizeof(struct ddc_service));

	if (!ddc_service)
		return NULL;

	if (construct(ddc_service, init_data))
		return ddc_service;

	dal_free(ddc_service);
	return NULL;
}

static void destruct(struct ddc_service *ddc)
{
	if (ddc->ddc_pin)
		dal_adapter_service_release_ddc(ddc->as, ddc->ddc_pin);
}

void dal_ddc_service_destroy(struct ddc_service **ddc)
{
	if (!ddc || !*ddc) {
		BREAK_TO_DEBUGGER();
		return;
	}
	destruct(*ddc);
	dal_free(*ddc);
	*ddc = NULL;
}

enum ddc_service_type dal_ddc_service_get_type(struct ddc_service *ddc)
{
	return DDC_SERVICE_TYPE_CONNECTOR;
}

void dal_ddc_service_set_transaction_type(
	struct ddc_service *ddc,
	enum ddc_transaction_type type)
{
	ddc->transaction_type = type;
}

bool dal_ddc_service_is_in_aux_transaction_mode(struct ddc_service *ddc)
{
	switch (ddc->transaction_type) {
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX:
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX_WITH_DEFER:
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX_RETRY_DEFER:
		return true;
	default:
		break;
	}
	return false;
}

static uint32_t defer_delay_converter_wa(
	struct ddc_service *ddc,
	uint32_t defer_delay)
{
	struct dp_receiver_id_info dp_rec_info = {0};

	if (dal_ddc_service_get_dp_receiver_id_info(ddc, &dp_rec_info) &&
		(dp_rec_info.branch_id == DP_BRANCH_DEVICE_ID_4) &&
		!dal_strncmp(dp_rec_info.branch_name,
			DP_DVI_CONVERTER_ID_4,
			sizeof(dp_rec_info.branch_name)))
		return defer_delay > I2C_OVER_AUX_DEFER_WA_DELAY ?
			defer_delay : I2C_OVER_AUX_DEFER_WA_DELAY;

	return defer_delay;

}

#define DP_TRANSLATOR_DELAY 5

static uint32_t get_defer_delay(struct ddc_service *ddc)
{
	uint32_t defer_delay = 0;

	switch (ddc->transaction_type) {
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX:
		if ((DISPLAY_DONGLE_DP_VGA_CONVERTER == ddc->dongle_type) ||
			(DISPLAY_DONGLE_DP_DVI_CONVERTER == ddc->dongle_type) ||
			(DISPLAY_DONGLE_DP_HDMI_CONVERTER ==
				ddc->dongle_type)) {

			defer_delay = DP_TRANSLATOR_DELAY;

			defer_delay =
				defer_delay_converter_wa(ddc, defer_delay);

		} else /*sink has a delay different from an Active Converter*/
			defer_delay = 0;
		break;
	case DDC_TRANSACTION_TYPE_I2C_OVER_AUX_WITH_DEFER:
		defer_delay = DP_TRANSLATOR_DELAY;
		break;
	default:
		break;
	}
	return defer_delay;
}

static bool i2c_read(
	struct ddc_service *ddc,
	uint32_t address,
	uint8_t *buffer,
	uint32_t len)
{
	uint8_t offs_data = 0;
	struct i2c_payload payloads[2] = {
		{
		.write = true,
		.address = address,
		.length = 1,
		.data = &offs_data },
		{
		.write = false,
		.address = address,
		.length = len,
		.data = buffer } };

	struct i2c_command command = {
		.payloads = payloads,
		.number_of_payloads = 2,
		.engine = DDC_I2C_COMMAND_ENGINE,
		.speed = dal_adapter_service_get_sw_i2c_speed(ddc->as) };

	return dal_i2caux_submit_i2c_command(
		dal_adapter_service_get_i2caux(ddc->as),
		ddc->ddc_pin,
		&command);
}

static bool ddc_edid_cmp(uint8_t *lhs, uint8_t *rhs, int32_t len)
{
	for (; len > 0 && *lhs++ == *rhs++; --len)
		;
	return !len;
}

static bool retreive_edid_data_at_offset(
	struct ddc_service *ddc,
	uint8_t offset,
	uint8_t *buf,
	uint8_t len)
{
	if (dal_ddc_service_is_in_aux_transaction_mode(ddc)) {

		struct aux_payload payloads[2] = {
			{
			.i2c_over_aux = true,
			.write = true,
			.address = ddc->address,
			.length = 1,
			.data = &offset },
			{
			.i2c_over_aux = true,
			.write = false,
			.address = ddc->address,
			.length = len,
			.data = buf } };

		struct aux_command cmd = {
			.payloads = payloads,
			.number_of_payloads = 2,
			.defer_delay = get_defer_delay(ddc),
			.max_defer_write_retry = 0 };

		if (ddc->transaction_type ==
			DDC_TRANSACTION_TYPE_I2C_OVER_AUX_RETRY_DEFER)
			cmd.max_defer_write_retry = AUX_MAX_DEFER_WRITE_RETRY;

		return dal_i2caux_submit_aux_command(
			dal_adapter_service_get_i2caux(ddc->as),
			ddc->ddc_pin,
			&cmd);
	} else {
		struct i2c_payload payloads[2] = {
			{
			.write = true,
			.address = ddc->address,
			.length = 1,
			.data = &offset },
			{
			.write = false,
			.address = ddc->address,
			.length = len,
			.data = buf } };

		struct i2c_command cmd = {
			.payloads = payloads,
			.number_of_payloads = 2,
			.engine = DDC_I2C_COMMAND_ENGINE,
			.speed = dal_adapter_service_get_sw_i2c_speed(
				ddc->as) };

		return dal_i2caux_submit_i2c_command(
			dal_adapter_service_get_i2caux(ddc->as),
			ddc->ddc_pin,
			&cmd);
	}
}

static enum edid_read_result verify_edid_1x_signature(
	struct ddc_service *ddc)
{
	/*
	 * verify checksum and extension
	 */
	{
		uint8_t data[DDC_EDID1X_EXT_CNT_AND_CHECKSUM_LEN];

		if (!retreive_edid_data_at_offset(
			ddc,
			DDC_EDID1X_EXT_CNT_AND_CHECKSUM_OFFSET,
			data,
			DDC_EDID1X_EXT_CNT_AND_CHECKSUM_LEN))
			return EDID_READ_RESULT_CHECKSUM_READ_ERR;

		if (!ddc_edid_cmp(
			data,
			&ddc->edid_buf[DDC_EDID1X_EXT_CNT_AND_CHECKSUM_OFFSET],
			DDC_EDID1X_EXT_CNT_AND_CHECKSUM_LEN))
			return EDID_READ_RESULT_EDID_MISMATCH;
	}

	/*
	 * verify manufacture and product ID
	 */
	{
		uint8_t data[DDC_EDID1X_VENDORID_SIGNATURE_LEN];

		if (!retreive_edid_data_at_offset(
			ddc,
			DDC_EDID1X_VENDORID_SIGNATURE_OFFSET,
			data,
			DDC_EDID1X_VENDORID_SIGNATURE_LEN))
			return EDID_READ_RESULT_CHECKSUM_READ_ERR;

		if (!ddc_edid_cmp(
			data,
			&ddc->edid_buf[DDC_EDID1X_VENDORID_SIGNATURE_OFFSET],
			DDC_EDID1X_VENDORID_SIGNATURE_LEN))
			return EDID_READ_RESULT_EDID_MISMATCH;
	}
	return EDID_READ_RESULT_EDID_MATCH;
}

static enum edid_read_result verify_edid_20_signature(
	struct ddc_service *ddc)
{
	/*
	 * verify checksum and extension
	 */
	{
		uint8_t data[DDC_EDID20_CHECKSUM_LEN];

		if (!retreive_edid_data_at_offset(
			ddc,
			DDC_EDID20_CHECKSUM_OFFSET,
			data,
			DDC_EDID20_CHECKSUM_LEN))
			return EDID_READ_RESULT_CHECKSUM_READ_ERR;

		if (!ddc_edid_cmp(
			data,
			&ddc->edid_buf[DDC_EDID20_CHECKSUM_OFFSET],
			DDC_EDID20_CHECKSUM_LEN))
			return EDID_READ_RESULT_EDID_MISMATCH;
	}

	/*
	 * verify manufacture and product ID
	 */
	{
		uint8_t data[DDC_EDID20_VENDORID_SIGNATURE_LEN];

		if (!retreive_edid_data_at_offset(
			ddc,
			DDC_EDID20_VENDORID_SIGNATURE_OFFSET,
			data,
			DDC_EDID20_VENDORID_SIGNATURE_LEN))
			return EDID_READ_RESULT_CHECKSUM_READ_ERR;

		if (!ddc_edid_cmp(
			data,
			&ddc->edid_buf[DDC_EDID20_VENDORID_SIGNATURE_LEN],
			DDC_EDID20_VENDORID_SIGNATURE_LEN))
			return EDID_READ_RESULT_EDID_MISMATCH;
	}
	return EDID_READ_RESULT_EDID_MATCH;
}

static enum edid_read_result check_edid_the_same(struct ddc_service *ddc)
{
	enum edid_read_result ret = EDID_READ_RESULT_EDID_MISMATCH;

	if (ddc->edid_buf_len > DDC_EDID_20_SIGNATURE_OFFSET) {
		if (ddc->edid_buf[DDC_EDID_20_SIGNATURE_OFFSET] ==
			DDC_EDID_20_SIGNATURE)
			ret = verify_edid_20_signature(ddc);
		else
			ret = verify_edid_1x_signature(ddc);
	}

	return ret;
}

static uint8_t aux_read_edid_block(
	struct ddc_service *ddc,
	uint8_t address,
	uint8_t index,
	uint8_t *buf)
{
	struct aux_command cmd = {
		.payloads = NULL,
		.number_of_payloads = 0,
		.defer_delay = get_defer_delay(ddc),
		.max_defer_write_retry = 0 };

	uint8_t retrieved = 0;
	uint8_t base_offset =
		(index % DDC_EDID_BLOCKS_PER_SEGMENT) * DDC_EDID_BLOCK_SIZE;
	uint8_t segment = index / DDC_EDID_BLOCKS_PER_SEGMENT;

	for (retrieved = 0; retrieved < DDC_EDID_BLOCK_SIZE;
		retrieved += DEFAULT_AUX_MAX_DATA_SIZE) {

		uint8_t offset = base_offset + retrieved;

		struct aux_payload payloads[3] = {
			{
			.i2c_over_aux = true,
			.write = true,
			.address = DDC_EDID_SEGMENT_ADDRESS,
			.length = 1,
			.data = &segment },
			{
			.i2c_over_aux = true,
			.write = true,
			.address = address,
			.length = 1,
			.data = &offset },
			{
			.i2c_over_aux = true,
			.write = false,
			.address = address,
			.length = DEFAULT_AUX_MAX_DATA_SIZE,
			.data = &buf[retrieved] } };

		if (segment == 0) {
			cmd.payloads = &payloads[1];
			cmd.number_of_payloads = 2;
		} else {
			cmd.payloads = payloads;
			cmd.number_of_payloads = 3;
		}

		if (!dal_i2caux_submit_aux_command(
			dal_adapter_service_get_i2caux(ddc->as),
			ddc->ddc_pin,
			&cmd))
			/* cannot read, break*/
			break;
	}

	/* Reset segment to 0. Needed by some panels */
	if (0 != segment) {
		struct aux_payload payloads[1] = { {
			.i2c_over_aux = true,
			.write = true,
			.address = DDC_EDID_SEGMENT_ADDRESS,
			.length = 1,
			.data = &segment } };
		bool result = false;

		segment = 0;

		cmd.number_of_payloads = ARRAY_SIZE(payloads);
		cmd.payloads = payloads;

		result = dal_i2caux_submit_aux_command(
			dal_adapter_service_get_i2caux(ddc->as),
			ddc->ddc_pin,
			&cmd);

		if (false == result)
			dal_logger_write(
				ddc->ctx->logger,
				LOG_MAJOR_ERROR,
				LOG_MINOR_COMPONENT_DISPLAY_CAPABILITY_SERVICE,
				"%s: Writing of EDID Segment (0x30) failed!\n",
				__func__);
	}

	return retrieved;
}

static uint8_t i2c_read_edid_block(
	struct ddc_service *ddc,
	uint8_t address,
	uint8_t index,
	uint8_t *buf)
{
	bool ret = false;
	uint8_t offset = (index % DDC_EDID_BLOCKS_PER_SEGMENT) *
		DDC_EDID_BLOCK_SIZE;
	uint8_t segment = index / DDC_EDID_BLOCKS_PER_SEGMENT;

	struct i2c_command cmd = {
		.payloads = NULL,
		.number_of_payloads = 0,
		.engine = DDC_I2C_COMMAND_ENGINE,
		.speed = dal_adapter_service_get_sw_i2c_speed(ddc->as) };

	struct i2c_payload payloads[3] = {
		{
		.write = true,
		.address = DDC_EDID_SEGMENT_ADDRESS,
		.length = 1,
		.data = &segment },
		{
		.write = true,
		.address = address,
		.length = 1,
		.data = &offset },
		{
		.write = false,
		.address = address,
		.length = DDC_EDID_BLOCK_SIZE,
		.data = buf } };
/*
 * Some I2C engines don't handle stop/start between write-offset and read-data
 * commands properly. For those displays, we have to force the newer E-DDC
 * behavior of repeated-start which can be enabled by runtime parameter. */
/* Originally implemented for OnLive using NXP receiver chip */

	if (index == 0 && !ddc->flags.FORCE_READ_REPEATED_START) {
		/* base block, use use DDC2B, submit as 2 commands */
		cmd.payloads = &payloads[1];
		cmd.number_of_payloads = 1;

		if (dal_i2caux_submit_i2c_command(
			dal_adapter_service_get_i2caux(ddc->as),
			ddc->ddc_pin,
			&cmd)) {

			cmd.payloads = &payloads[2];
			cmd.number_of_payloads = 1;

			ret = dal_i2caux_submit_i2c_command(
				dal_adapter_service_get_i2caux(ddc->as),
				ddc->ddc_pin,
				&cmd);
		}

	} else {
		/*
		 * extension block use E-DDC, submit as 1 command
		 * or if repeated-start is forced by runtime parameter
		 */
		if (segment != 0) {
			/* include segment offset in command*/
			cmd.payloads = payloads;
			cmd.number_of_payloads = 3;
		} else {
			/* we are reading first segment,
			 * segment offset is not required */
			cmd.payloads = &payloads[1];
			cmd.number_of_payloads = 2;
		}

		ret = dal_i2caux_submit_i2c_command(
			dal_adapter_service_get_i2caux(ddc->as),
			ddc->ddc_pin,
			&cmd);
	}

	return ret ? DDC_EDID_BLOCK_SIZE : 0;
}

static uint32_t query_edid_block(
	struct ddc_service *ddc,
	uint8_t address,
	uint8_t index,
	uint8_t *buf,
	uint32_t size)
{
	uint32_t size_retrieved = 0;

	if (size < DDC_EDID_BLOCK_SIZE)
		return 0;

	if (dal_ddc_service_is_in_aux_transaction_mode(ddc)) {

		ASSERT(index < 2);
		size_retrieved =
			aux_read_edid_block(ddc, address, index, buf);
	} else {
		size_retrieved =
			i2c_read_edid_block(ddc, address, index, buf);
	}

	return size_retrieved;
}

#define DDC_DPCD_EDID_CHECKSUM_WRITE_ADDRESS 0x261
#define DDC_TEST_ACK_ADDRESS 0x260
#define DDC_DPCD_EDID_TEST_ACK 0x04
#define DDC_DPCD_EDID_TEST_MASK 0x04
#define DDC_DPCD_TEST_REQUEST_ADDRESS 0x218

static void write_dp_edid_checksum(
	struct ddc_service *ddc,
	uint8_t checksum)
{
	uint8_t dpcd_data;

	dal_ddc_service_read_dpcd_data(
		ddc,
		DDC_DPCD_TEST_REQUEST_ADDRESS,
		&dpcd_data,
		1);

	if (dpcd_data & DDC_DPCD_EDID_TEST_MASK) {

		dal_ddc_service_write_dpcd_data(
			ddc,
			DDC_DPCD_EDID_CHECKSUM_WRITE_ADDRESS,
			&checksum,
			1);

		dpcd_data = DDC_DPCD_EDID_TEST_ACK;

		dal_ddc_service_write_dpcd_data(
			ddc,
			DDC_TEST_ACK_ADDRESS,
			&dpcd_data,
			1);
	}
}

static void edid_query(struct ddc_service *ddc)
{
	uint32_t bytes_read = 0;
	uint32_t ext_cnt = 0;

	uint8_t address;
	uint32_t i;

	for (address = DDC_EDID_ADDRESS_START;
		address <= DDC_EDID_ADDRESS_END; ++address) {

		bytes_read = query_edid_block(
			ddc,
			address,
			0,
			ddc->edid_buf,
			sizeof(ddc->edid_buf) - bytes_read);

		if (bytes_read != DDC_EDID_BLOCK_SIZE)
			continue;

		/* get the number of ext blocks*/
		ext_cnt = ddc->edid_buf[DDC_EDID_EXT_COUNT_OFFSET];

		/* EDID 2.0, need to read 1 more block because EDID2.0 is
		 * 256 byte in size*/
		if (ddc->edid_buf[DDC_EDID_20_SIGNATURE_OFFSET] ==
			DDC_EDID_20_SIGNATURE)
				ext_cnt = 1;

		for (i = 0; i < ext_cnt; i++) {
			/* read additional ext blocks accordingly */
			bytes_read += query_edid_block(
					ddc,
					address,
					i+1,
					&ddc->edid_buf[bytes_read],
					sizeof(ddc->edid_buf) - bytes_read);
		}

		/*this is special code path for DP compliance*/
		if (DDC_TRANSACTION_TYPE_I2C_OVER_AUX == ddc->transaction_type)
			write_dp_edid_checksum(
				ddc,
				ddc->edid_buf[(ext_cnt * DDC_EDID_BLOCK_SIZE) +
				DDC_EDID1X_CHECKSUM_OFFSET]);

		/*remembers the address where we fetch the EDID from
		 * for later signature check use */
		ddc->address = address;

		break;/* already read edid, done*/
	}

	ddc->edid_buf_len = bytes_read;
}

void dal_ddc_service_optimized_edid_query(struct ddc_service *ddc)
{
	enum edid_read_result error = EDID_READ_RESULT_EDID_MISMATCH;

	if (dal_adapter_service_is_feature_supported(
		FEATURE_EDID_STRESS_READ))
		/* different EDIDs, we need to query the new one */
		error = EDID_READ_RESULT_EDID_MISMATCH;
	else if (ddc->flags.IS_INTERNAL_DISPLAY &&
		ddc->flags.EDID_QUERY_DONE_ONCE)
		/* InternalDisplay includes LVDS, eDP and ALL-In-One
		 * system's display-video bios report deviceTag as Internal).*/
			return;
	else {
		error = check_edid_the_same(ddc);
		/* signature check result shows we have the same EDID
		connected, we can skip retrieving the full EDID */
		if (error == EDID_READ_RESULT_EDID_MATCH)
			return;
	}

	if (error == EDID_READ_RESULT_CHECKSUM_READ_ERR
		|| error == EDID_READ_RESULT_VENDOR_READ_ERR) {
		dal_memset(ddc->edid_buf, 0, sizeof(ddc->edid_buf));
		ddc->edid_buf_len = 0;
		dal_logger_write(ddc->ctx->logger,
			LOG_MAJOR_DCS,
			LOG_MINOR_DCS_EDID_EMULATOR,
			"EDID read error: %i. Skipping EDID query.\n", error);
	} else {
		/* retireve EDID*/
		edid_query(ddc);
		ddc->flags.EDID_QUERY_DONE_ONCE = true;
	}
}

uint32_t dal_ddc_service_get_edid_buf_len(struct ddc_service *ddc)
{
	return ddc->edid_buf_len;
}

const uint8_t *dal_ddc_service_get_edid_buf(struct ddc_service *ddc)
{
	return ddc->edid_buf;
}

bool dal_ddc_service_i2c_query_dp_dual_mode_adaptor(
	struct ddc_service *ddc,
	struct display_sink_capability *sink_cap)
{
	enum display_dongle_type dummy_dongle;
	/* We allow passing NULL pointer when caller only wants
	 * to know dongle presence*/
	enum display_dongle_type *dongle =
		(sink_cap ? &sink_cap->dongle_type : &dummy_dongle);
	uint8_t type2_dongle_buf[DP_ADAPTOR_TYPE2_SIZE];
	bool is_type2_dongle = false;
	bool is_valid_hdmi_signature = false;
	struct dp_hdmi_dongle_signature_data *dongle_signature;
	uint32_t i;

	/* Assume we have valid DP-HDMI dongle connected */
	*dongle = DISPLAY_DONGLE_DP_HDMI_DONGLE;

	/* Read DP-HDMI dongle I2c */
	if (!i2c_read(
		ddc,
		DP_HDMI_DONGLE_ADDRESS,
		type2_dongle_buf,
		sizeof(type2_dongle_buf))) {
		*dongle = DISPLAY_DONGLE_DP_DVI_DONGLE;
		return false;
	}

	/* Check if Type 2 dongle.*/
	/*If It is a Type 2 dongle, but we don't know yet
	 * is it DP-HDMI or DP-DVI */
	if (type2_dongle_buf[DP_ADAPTOR_TYPE2_REG_ID] == DP_ADAPTOR_TYPE2_ID)
		is_type2_dongle = true;

	dongle_signature =
		(struct dp_hdmi_dongle_signature_data *)type2_dongle_buf;

	/* Check EOT */
	if (dongle_signature->eot != DP_HDMI_DONGLE_SIGNATURE_EOT) {
		if (is_type2_dongle == false) {

			dal_logger_write(ddc->ctx->logger,
				LOG_MAJOR_DCS,
				LOG_MINOR_DCS_DONGLE_DETECTION,
				"Detected Type 1 DP-HDMI dongle (no valid HDMI signature EOT).\n");

			*dongle = DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE;
			return true;
		}
	}

	/* Assume there is a valid HDMI signature, if this is not the case,
	 * set this flag to false. */
	is_valid_hdmi_signature = true;

	/* Check signature */
	for (i = 0; i < sizeof(dongle_signature->id); ++i) {
		/* If its not the right signature,
		 * skip mismatch in subversion byte.*/
		if (dongle_signature->id[i] !=
			dp_hdmi_dongle_signature_str[i] && i != 3) {

			if (is_type2_dongle) {
				is_valid_hdmi_signature = false;
				break;
			}

			dal_logger_write(ddc->ctx->logger,
				LOG_MAJOR_DCS,
				LOG_MINOR_DCS_DONGLE_DETECTION,
				"Detected Type 1 DP-HDMI dongle (no valid HDMI signature EOT).\n");

			*dongle = DISPLAY_DONGLE_DP_HDMI_MISMATCHED_DONGLE;
			return true;

		}
	}

	if (sink_cap && is_type2_dongle) {

		uint32_t max_tmds_clk =
			type2_dongle_buf[DP_ADAPTOR_TYPE2_REG_MAX_TMDS_CLK];

		max_tmds_clk = max_tmds_clk * 2 + max_tmds_clk / 2;

		if (max_tmds_clk < DP_ADAPTOR_TYPE2_MAX_TMDS_CLK &&
			max_tmds_clk > DP_ADAPTOR_TYPE2_MIN_TMDS_CLK) {

			if (is_valid_hdmi_signature == true)
				*dongle = DISPLAY_DONGLE_DP_HDMI_DONGLE;
			else
				*dongle = DISPLAY_DONGLE_DP_DVI_DONGLE;

			/* Multiply by 1000 to convert to kHz. */
			sink_cap->max_hdmi_pixel_clock =
				max_tmds_clk * 1000;
		} else
			dal_logger_write(ddc->ctx->logger,
				LOG_MAJOR_DCS,
				LOG_MINOR_DCS_DONGLE_DETECTION,
				"Invalid Maximum TMDS clock");

	}

	if (is_type2_dongle == false && is_valid_hdmi_signature == true)
		dal_logger_write(ddc->ctx->logger,
			LOG_MAJOR_DCS,
			LOG_MINOR_DCS_DONGLE_DETECTION,
			"Detected Type 1 DP-HDMI dongle.\n");

	return true;
}

void dal_ddc_service_retrieve_dpcd_data(
	struct ddc_service *ddc,
	struct av_sync_data *av_sync_data)
{
	const uint32_t size = DPCD_ADDRESS_AUDIO_DELAY_INSERT3 -
		DPCD_ADDRESS_AV_GRANULARITY + 1;
	uint8_t av_sync_caps[size];

	if (ddc->dp_receiver_id_info.dpcd_rev < DCS_DPCD_REV_12)
		return;

	dal_ddc_service_read_dpcd_data(
		ddc, DPCD_ADDRESS_AV_GRANULARITY, av_sync_caps, size);

	av_sync_data->av_granularity = av_sync_caps[0];
	av_sync_data->aud_dec_lat1 = av_sync_caps[1];
	av_sync_data->aud_dec_lat2 = av_sync_caps[2];
	av_sync_data->aud_pp_lat1 = av_sync_caps[3];
	av_sync_data->aud_pp_lat2 = av_sync_caps[4];
	av_sync_data->vid_inter_lat = av_sync_caps[5];
	av_sync_data->vid_prog_lat = av_sync_caps[6];
	av_sync_data->aud_del_ins1 = av_sync_caps[8];
	av_sync_data->aud_del_ins2 = av_sync_caps[9];
	av_sync_data->aud_del_ins3 = av_sync_caps[10];
}

static void get_active_converter_info(
	struct ddc_service *ddc,
	uint8_t data,
	struct display_sink_capability *sink_cap)
{
	union dp_downstream_port_present ds_port = { .byte = data };

	/* decode converter info*/
	if (!ds_port.fields.PORT_PRESENT) {

		ddc->dongle_type = DISPLAY_DONGLE_NONE;
		return;
	}
	switch (ds_port.fields.PORT_TYPE) {
	case DOWNSTREAM_VGA:
		sink_cap->dongle_type = DISPLAY_DONGLE_DP_VGA_CONVERTER;
		break;
	case DOWNSTREAM_DVI_HDMI:
		/* At this point we don't know is it DVI or HDMI,
		 * assume DVI.*/
		sink_cap->dongle_type = DISPLAY_DONGLE_DP_DVI_CONVERTER;
		break;
	default:
		sink_cap->dongle_type = DISPLAY_DONGLE_NONE;
		break;
	}

	if (sink_cap->dpcd_revision >= DCS_DPCD_REV_11) {
		uint8_t det_caps[4];
		union dwnstream_port_caps_byte0 *port_caps =
			(union dwnstream_port_caps_byte0 *)det_caps;
		dal_ddc_service_read_dpcd_data(ddc,
			DPCD_ADDRESS_DWN_STRM_PORT0_CAPS, det_caps,
			sizeof(det_caps));

		switch (port_caps->bits.DWN_STRM_PORTX_TYPE) {
		case DOWN_STREAM_DETAILED_VGA:
			sink_cap->dongle_type =
				DISPLAY_DONGLE_DP_VGA_CONVERTER;
			break;
		case DOWN_STREAM_DETAILED_DVI:
			sink_cap->dongle_type =
				DISPLAY_DONGLE_DP_DVI_CONVERTER;
			break;
		case DOWN_STREAM_DETAILED_HDMI:
			sink_cap->dongle_type =
				DISPLAY_DONGLE_DP_HDMI_CONVERTER;

			if (ds_port.fields.DETAILED_CAPS) {

				union dwnstream_port_caps_byte3_hdmi
					hdmi_caps = {.raw = det_caps[3] };

				sink_cap->is_dp_hdmi_s3d_converter =
					hdmi_caps.bits.FRAME_SEQ_TO_FRAME_PACK;
			}
			break;
		}
	}
	ddc->dongle_type = sink_cap->dongle_type;
}


enum {
	DP_SINK_CAP_SIZE =
		DPCD_ADDRESS_EDP_CONFIG_CAP - DPCD_ADDRESS_DPCD_REV + 1
};

bool dal_ddc_service_aux_query_dp_sink_capability(
	struct ddc_service *ddc,
	struct display_sink_capability *sink_cap)
{
	/* We allow passing NULL pointer when caller only wants
	 * to know converter presence */
	struct display_sink_capability dummy_sink_cap;
	union dp_downstream_port_present ds_port = { 0 };
	struct dp_device_vendor_id dp_id;
	struct dp_sink_hw_fw_revision revision;
	uint8_t buf_caps[DP_SINK_CAP_SIZE] = { 0 };
	uint8_t dp_data;

	if (!sink_cap)
		sink_cap = &dummy_sink_cap;

	/* Some receiver needs 5ms to power up, not DP compliant*/
	if (ddc->wa.bits.DP_SKIP_POWER_OFF) {
		/* DP receiver has probably powered down AUX communication
		 when powering down data link (not-compliant), power it up*/
		uint32_t retry = 0;
		uint8_t power_state = 1;

		/* up to 4 tries to let DP receiver wake up*/
		while (DDC_RESULT_SUCESSFULL !=
			dal_ddc_service_write_dpcd_data(
			ddc,
			DPCD_ADDRESS_POWER_STATE,
			&power_state,
			sizeof(power_state)) && retry++ < 4)
			;
	}

	/* Some mDP->VGA dongle needs 500ms to power up from S4 */
	if (ddc->wa.bits.DP_AUX_POWER_UP_WA_DELAY)
		dal_sleep_in_milliseconds(AUX_POWER_UP_WA_DELAY);

	if (DDC_RESULT_SUCESSFULL !=
		dal_ddc_service_read_dpcd_data(
			ddc, DPCD_ADDRESS_DPCD_REV, buf_caps, DP_SINK_CAP_SIZE))
		return false;

	/*This for DP1.1 CTS core1.2 test. TE will check if Source read 0x200.
	 Current AMD implementation reads EDID to decide if display connected
	 to active dongle. Value of 0x200 is not really used by AMD driver.*/

	dal_ddc_service_read_dpcd_data(
		ddc, DPCD_ADDRESS_SINK_COUNT, &dp_data, sizeof(dp_data));

	sink_cap->downstrm_sink_count = dp_data;

	/* parse caps*/
	/* TODO: for DP1.2, SW driver may need parser number of downstream
	 * port DPCD offset 0x7  DOWN_STREAM_PORT_COUNT */
	sink_cap->dpcd_revision = buf_caps[DPCD_ADDRESS_DPCD_REV];
	sink_cap->dp_link_rate = buf_caps[DPCD_ADDRESS_MAX_LINK_RATE];
	sink_cap->dp_link_lane_count =
		buf_caps[DPCD_ADDRESS_MAX_LANE_COUNT] & 0x1F;
	sink_cap->ss_supported =
		buf_caps[DPCD_ADDRESS_MAX_DOWNSPREAD] & 0x01 ?
			LINK_SPREAD_05_DOWNSPREAD_30KHZ : LINK_SPREAD_DISABLED;

	ds_port.byte = buf_caps[DPCD_ADDRESS_DOWNSTREAM_PORT_PRESENT];

	/* interpret converter (downstream port) caps*/
	get_active_converter_info(ddc, ds_port.byte, sink_cap);

	/* get active dongles (converters) id for specific workarounds*/

	/* initialize m_dpRecevierInfo to zero */
	dal_memset(
		&ddc->dp_receiver_id_info, 0, sizeof(ddc->dp_receiver_id_info));

	ddc->dp_receiver_id_info.dpcd_rev = sink_cap->dpcd_revision;

	ddc->dp_receiver_id_info.dongle_type = sink_cap->dongle_type;

	/* read IEEE sink device id */
	dal_ddc_service_read_dpcd_data(
		ddc,
		DPCD_ADDRESS_SINK_DEVICE_ID_START,
		(uint8_t *)&dp_id,
		sizeof(dp_id));

	/* cache the sink_id */
	ddc->dp_receiver_id_info.sink_id =
		(dp_id.ieee_oui[0] << 16) +
		(dp_id.ieee_oui[1] << 8) +
		dp_id.ieee_oui[2];

	/* cache the sink_dev_name */
	dal_memmove(
		ddc->dp_receiver_id_info.sink_id_str,
		dp_id.ieee_device_id,
		sizeof(ddc->dp_receiver_id_info.sink_id_str));

	/* read IEEE sink hw/fw revision*/
	dal_ddc_service_read_dpcd_data(
		ddc,
		DPCD_ADDRESS_SINK_REVISION_START,
		(uint8_t *)&revision,
		sizeof(revision));

	/* cache the sink hw revision */
	ddc->dp_receiver_id_info.sink_hw_revision = revision.ieee_hw_rev;

	/* cache the sink fw revision */
	dal_memmove(
		ddc->dp_receiver_id_info.sink_fw_revision,
		revision.ieee_fw_rev,
		sizeof(ddc->dp_receiver_id_info.sink_fw_revision));

	/* read IEEE branch device id */
	dal_ddc_service_read_dpcd_data(
		ddc,
		DPCD_ADDRESS_BRANCH_DEVICE_ID_START,
		(uint8_t *)&dp_id,
		sizeof(dp_id));

	/* extract IEEE id
	 cache the branchDevId and branchDevName*/
	ddc->dp_receiver_id_info.branch_id =
		(dp_id.ieee_oui[0] << 16) +
		(dp_id.ieee_oui[1] << 8) +
		dp_id.ieee_oui[2];

	dal_memmove(
		ddc->dp_receiver_id_info.branch_name,
		dp_id.ieee_device_id,
		sizeof(ddc->dp_receiver_id_info.branch_name));

	switch (ddc->dp_receiver_id_info.branch_id) {
	case DP_BRANCH_DEVICE_ID_5:
		sink_cap->downstrm_sink_count_valid = true;
		break;
	default:
		break;
	}

	/* translate receiver capabilities*/
	if (sink_cap->dp_link_spead != LINK_SPREAD_DISABLED)
		sink_cap->ss_supported = true;

	return true;
}

bool dal_ddc_service_query_ddc_data(
	struct ddc_service *ddc,
	uint32_t address,
	uint8_t *write_buf,
	uint32_t write_size,
	uint8_t *read_buf,
	uint32_t read_size)
{
	bool ret;
	uint32_t payload_size =
		dal_ddc_service_is_in_aux_transaction_mode(ddc) ?
			DEFAULT_AUX_MAX_DATA_SIZE : EDID_SEGMENT_SIZE;

	uint32_t write_payloads =
		(write_size + payload_size - 1) / payload_size;

	uint32_t read_payloads =
		(read_size + payload_size - 1) / payload_size;

	uint32_t payloads_num = write_payloads + read_payloads;

	if (write_size > EDID_SEGMENT_SIZE || read_size > EDID_SEGMENT_SIZE)
		return false;

	/*TODO: len of payload data for i2c and aux is uint8!!!!,
	 *  but we want to read 256 over i2c!!!!*/
	if (dal_ddc_service_is_in_aux_transaction_mode(ddc)) {

		struct aux_payloads *payloads =
			dal_ddc_aux_payloads_create(payloads_num);

		struct aux_command command = {
			.payloads = dal_ddc_aux_payloads_get(payloads),
			.number_of_payloads = 0,
			.defer_delay = get_defer_delay(ddc),
			.max_defer_write_retry = 0 };

		dal_ddc_aux_payloads_add(
			payloads, address, write_size, write_buf, true);

		dal_ddc_aux_payloads_add(
			payloads, address, read_size, read_buf, false);

		command.number_of_payloads =
			dal_ddc_aux_payloads_get_count(payloads);

		ret = dal_i2caux_submit_aux_command(
				dal_adapter_service_get_i2caux(ddc->as),
				ddc->ddc_pin,
				&command);

		dal_ddc_aux_payloads_destroy(&payloads);

	} else {
		struct i2c_payloads *payloads =
			dal_ddc_i2c_payloads_create(payloads_num);

		struct i2c_command command = {
			.payloads = dal_ddc_i2c_payloads_get(payloads),
			.number_of_payloads = 0,
			.engine = DDC_I2C_COMMAND_ENGINE,
			.speed =
				dal_adapter_service_get_sw_i2c_speed(ddc->as) };

		dal_ddc_i2c_payloads_add(
			payloads, address, write_size, write_buf, true);

		dal_ddc_i2c_payloads_add(
			payloads, address, read_size, read_buf, false);

		command.number_of_payloads =
			dal_ddc_i2c_payloads_get_count(payloads);

		ret = dal_i2caux_submit_i2c_command(
				dal_adapter_service_get_i2caux(ddc->as),
				ddc->ddc_pin,
				&command);

		dal_ddc_i2c_payloads_destroy(&payloads);
	}

	return ret;
}

bool dal_ddc_service_get_dp_receiver_id_info(
	struct ddc_service *ddc,
	struct dp_receiver_id_info *info)
{
	if (!info)
		return false;

	*info = ddc->dp_receiver_id_info;
	return true;
}

enum ddc_result dal_ddc_service_read_dpcd_data(
	struct ddc_service *ddc,
	uint32_t address,
	uint8_t *data,
	uint32_t len)
{
	struct aux_payload read_payload = {
		.i2c_over_aux = false,
		.write = false,
		.address = address,
		.length = len,
		.data = data,
	};
	struct aux_command command = {
		.payloads = &read_payload,
		.number_of_payloads = 1,
		.defer_delay = 0,
		.max_defer_write_retry = 0,
	};

	if (len > DEFAULT_AUX_MAX_DATA_SIZE) {
		BREAK_TO_DEBUGGER();
		return DDC_RESULT_FAILED_INVALID_OPERATION;
	}

	if (dal_i2caux_submit_aux_command(
		dal_adapter_service_get_i2caux(ddc->as),
		ddc->ddc_pin,
		&command))
		return DDC_RESULT_SUCESSFULL;

	return DDC_RESULT_FAILED_OPERATION;
}

enum ddc_result dal_ddc_service_write_dpcd_data(
	struct ddc_service *ddc,
	uint32_t address,
	const uint8_t *data,
	uint32_t len)
{
	struct aux_payload write_payload = {
		.i2c_over_aux = false,
		.write = true,
		.address = address,
		.length = len,
		.data = (uint8_t *)data,
	};
	struct aux_command command = {
		.payloads = &write_payload,
		.number_of_payloads = 1,
		.defer_delay = 0,
		.max_defer_write_retry = 0,
	};

	if (len > DEFAULT_AUX_MAX_DATA_SIZE) {
		BREAK_TO_DEBUGGER();
		return DDC_RESULT_FAILED_INVALID_OPERATION;
	}

	if (dal_i2caux_submit_aux_command(
		dal_adapter_service_get_i2caux(ddc->as),
		ddc->ddc_pin,
		&command))
		return DDC_RESULT_SUCESSFULL;

	return DDC_RESULT_FAILED_OPERATION;
}

/*test only function*/
void dal_ddc_service_set_ddc_pin(
	struct ddc_service *ddc_service,
	struct ddc *ddc)
{
	ddc_service->ddc_pin = ddc;
}

struct ddc *dal_ddc_service_get_ddc_pin(struct ddc_service *ddc_service)
{
	return ddc_service->ddc_pin;
}


void dal_ddc_service_reset_dp_receiver_id_info(struct ddc_service *ddc_service)
{
	dal_memset(&ddc_service->dp_receiver_id_info,
		0, sizeof(struct dp_receiver_id_info));
}
