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

#ifndef DMUB_CMD__H
#define DMUB_CMD__H


#ifdef CONFIG_DRM_AMD_DC_DMUB
#include "os_types.h"
#else
#include "dmub_types.h"
#endif
/*
 * DMUB command definition
 */


#define DMUB_RB_MAX_ENTRY	16
#define DMUB_RB_CMD_SIZE	64
#define DMUB_RB_SIZE	(DMUB_RB_CMD_SIZE * DMUB_RB_MAX_ENTRY)

#ifdef DMUB_EMULATION

#else

#endif


enum dmub_cmd_type {
	DMUB_CMD__NULL,
	DMUB_CMD__REG_SEQ_READ_MODIFY_WRITE,
	DMUB_CMD__REG_SEQ_BURST_WRITE,
};


struct dmub_cmd_header {
	enum dmub_cmd_type type : 11;
	unsigned int payload_bytes : 5;  /* up to 60 bytes */
	unsigned int reserved : 16;
};

struct read_modify_write_sequence {
	uint32_t addr;
	uint32_t modify_mask;
	uint32_t modify_value;
};


/* read modify write
 *
 * 60 payload bytes can hold up to 5 sets of read modify writes,
 * each take 3 dwords.
 *
 * number of sequences = header.payload_bytes / sizeof(struct read_modify_write_sequence)
 *
 * modify_mask = 0xffff'ffff means all fields are going to be updated.  in this case
 * command parser will skip the read and we can use modify_mask = 0xffff'ffff as reg write
 */
#define DMUB_READ_MODIFY_WRITE_SEQ__MAX		5
struct dmub_rb_cmd_read_modify_write {
	struct dmub_cmd_header header;  // type = DMUB_CMD__REG_SEQ_READ_MODIFY_WRITE
	struct read_modify_write_sequence seq[DMUB_READ_MODIFY_WRITE_SEQ__MAX];
};


/* burst write
 *
 * support use case such as writing out LUTs.
 *
 * 60 payload bytes can hold up to 14 values to write to given address
 *
 * number of payload = header.payload_bytes / sizeof(struct read_modify_write_sequence)
 */
struct dmub_rb_cmd_burst_write {
	struct dmub_cmd_header header;  // type = DMUB_CMD__REG_SEQ_BURST_WRITE
	uint32_t addr;
	uint32_t write_values[14];
};

#define HEAD_BYTES sizeof(struct dmub_cmd_header) / 8

struct dmub_rb_cmd_common {
	struct dmub_cmd_header header;
	uint8_t cmd_buffer[DMUB_RB_CMD_SIZE - HEAD_BYTES];
};

union dmub_rb_cmd {
	struct dmub_rb_cmd_read_modify_write read_modify_write;
	struct dmub_rb_cmd_burst_write burst_write;
	struct dmub_rb_cmd_common cmd_common;
};

#endif /* DMUB_CMD__H */
