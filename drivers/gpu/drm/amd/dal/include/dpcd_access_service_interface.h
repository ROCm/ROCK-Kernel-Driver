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

#ifdef __DPCD_ACCESS_SERVICE_INTERFACE_HPP__
#define __DPCD_ACCESS_SERVICE_INTERFACE_HPP__

/* DDC service transaction error codes
 * depends on transaction status
 */
enum ddc_result {
	DDCRESULT_UNKNOWN = 0,
	DDCRESULT_SUCESSFULL,
	DDCRESULT_FAILEDCHANNELBUSY,
	DDCRESULT_FAILEDTIMEOUT,
	DDCRESULT_FAILEDPROTOCOLERROR,
	DDCRESULT_FAILEDNACK,
	DDCRESULT_FAILEDINCOMPLETE,
	DDCRESULT_FAILEDOPERATION,
	DDCRESULT_FAILEDINVALIDOPERATION,
	DDCRESULT_FAILEDBUFFEROVERFLOW
};

enum {
	MaxNativeAuxTransactionSize = 16
};

struct display_sink_capability;

/* TO DO: below functions can be moved to ddc_service (think about it)*/
enum ddc_result dal_ddc_read_dpcd_data(
		uint32_t address,
		unsigned char *data,
		uint32_t size);

enum ddc_result dal_ddc_write_dpcd_data(
		uint32_t address,
		const unsigned char *data uint32_t size);

bool dal_aux_query_dp_sink_capability(display_sink_capability *sink_cap);
bool start_gtc_sync(void);
bool stop_gtc_sync(void);

#endif /*__DPCD_ACCESS_SERVICE_INTERFACE_HPP__*/
