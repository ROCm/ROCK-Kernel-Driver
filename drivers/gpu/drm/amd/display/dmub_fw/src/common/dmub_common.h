/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#ifndef DMUB_COMMON_H_
#define DMUB_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_DRM_AMD_DC_DMUB
#include "os_types.h"
struct dmub_dc_cmd;

struct dmub {
	void *ctx;
	void (*reg_write)(struct dmub *dmub, uint32_t offset, uint32_t value);
	uint32_t (*reg_read)(struct dmub *dmub, uint32_t offset);
	void (*dequeque)();
};

#define mmRegWrite(offset, value)
#define mmRegRead(offset)
#endif

void process_ring_buffer_command(
		struct dmub *dmub,
		struct dmub_dc_cmd *dc_cmd);
void ring_buffer_command_dequeue(
		struct dmub_dc_cmd *dmub_cmd,
		union dmub_rb_cmd *cmd);

#ifdef __cplusplus
}
#endif

#endif /* DMUB_COMMON_H_ */
