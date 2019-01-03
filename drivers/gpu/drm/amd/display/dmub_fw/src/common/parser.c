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
#include "dmub_dc.h"
#include "..\..\dc\dmub_cmd.h"
#include "dmub_common.h"

static void read_modify_write(struct dmub *dmub, union dmub_rb_cmd *cmd)
{
	int32_t reg_count =
			cmd->read_modify_write.header.payload_bytes / sizeof(struct read_modify_write_sequence);
	int32_t i;

	for (i = 0; i < reg_count; i++) {
		uint32_t addr = cmd->read_modify_write.seq[i].addr;
		uint32_t mask = cmd->read_modify_write.seq[i].modify_mask;
		uint32_t value = cmd->read_modify_write.seq[i].modify_value;
		uint32_t reg_val = dmub->reg_read(dmub, addr);

		reg_val = (reg_val & ~mask) | value;

		dmub->reg_write(dmub, addr, reg_val);
	}
}

void process_ring_buffer_command(
		struct dmub *dmub,
		struct dmub_dc_cmd *dc_cmd)
{
	union dmub_rb_cmd cmd;

	ring_buffer_command_dequeue(dc_cmd, &cmd);

	switch (cmd.cmd_common.header.type) {
	case DMUB_CMD__REG_SEQ_READ_MODIFY_WRITE:
		read_modify_write(dmub, &cmd);
		break;
	case DMUB_CMD__REG_SEQ_BURST_WRITE:
		break;
	default:
		break;
	}
}

