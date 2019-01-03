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

#ifndef DMUB_DC__H
#define DMUB_DC__H

#include "dmub_cmd.h"


struct dmub_dc_cmd {
	const void *dm;
};


struct dmub_offload_funcs {
	struct dmub_dc_cmd dmub_cmd;

	void (*queue_dmub_cmd)(struct dmub_dc_cmd *dmub_cmd, struct dmub_cmd_header *cmd);
	void (*execute_dmub_queue)(struct dmub_dc_cmd *dmub_cmd);
	void (*wait_queue_idle)(struct dmub_dc_cmd *dmub_cmd);
};

struct dc_reg_helper_state {
	bool gather_in_progress;

	union dmub_rb_cmd cmd_data;
	unsigned int reg_seq_count;
};


#endif /* DMUB_DC__H */
