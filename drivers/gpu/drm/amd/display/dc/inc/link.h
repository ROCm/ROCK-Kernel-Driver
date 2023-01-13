/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __DC_LINK_H__
#define __DC_LINK_H__

/* FILE POLICY AND INTENDED USAGE:
 *
 * This header declares link functions exposed to dc. All functions must have
 * "link_" as prefix. For example link_run_my_function. This header is strictly
 * private in dc and should never be included in other header files. dc
 * components should include this header in their .c files in order to access
 * functions in link folder. This file should never include any header files in
 * link folder. If there is a need to expose a function declared in one of
 * header files in side link folder, you need to move the function declaration
 * into this file and prefix it with "link_".
 */
#include "core_types.h"
#include "dc_link.h"

struct gpio *link_get_hpd_gpio(struct dc_bios *dcb,
		struct graphics_object_id link_id,
		struct gpio_service *gpio_service);

struct ddc_service_init_data {
	struct graphics_object_id id;
	struct dc_context *ctx;
	struct dc_link *link;
	bool is_dpia_link;
};

struct ddc_service *link_create_ddc_service(
		struct ddc_service_init_data *ddc_init_data);

void link_destroy_ddc_service(struct ddc_service **ddc);

bool link_is_in_aux_transaction_mode(struct ddc_service *ddc);

bool link_query_ddc_data(
		struct ddc_service *ddc,
		uint32_t address,
		uint8_t *write_buf,
		uint32_t write_size,
		uint8_t *read_buf,
		uint32_t read_size);


/* Attempt to submit an aux payload, retrying on timeouts, defers, and busy
 * states as outlined in the DP spec.  Returns true if the request was
 * successful.
 *
 * NOTE: The function requires explicit mutex on DM side in order to prevent
 * potential race condition. DC components should call the dpcd read/write
 * function in dm_helpers in order to access dpcd safely
 */
bool link_aux_transfer_with_retries_no_mutex(struct ddc_service *ddc,
		struct aux_payload *payload);

uint32_t link_get_aux_defer_delay(struct ddc_service *ddc);


#endif /* __DC_LINK_HPD_H__ */
