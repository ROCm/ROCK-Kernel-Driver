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

#ifndef __DAL_GRPH_OBJECT_DEFS_H__
#define __DAL_GRPH_OBJECT_DEFS_H__

#include "grph_object_id.h"

/* ********************************************************************
 * ********************************************************************
 *
 *  These defines shared between All Graphics Objects
 *
 * ********************************************************************
 * ********************************************************************
 */

/* HPD unit id - HW direct translation */
enum hpd_source_id {
	HPD_SOURCEID1 = 0,
	HPD_SOURCEID2,
	HPD_SOURCEID3,
	HPD_SOURCEID4,
	HPD_SOURCEID5,
	HPD_SOURCEID6,

	HPD_SOURCEID_COUNT,
	HPD_SOURCEID_UNKNOWN
};

/* DDC unit id - HW direct translation */
enum channel_id {
	CHANNEL_ID_UNKNOWN = 0,
	CHANNEL_ID_DDC1,
	CHANNEL_ID_DDC2,
	CHANNEL_ID_DDC3,
	CHANNEL_ID_DDC4,
	CHANNEL_ID_DDC5,
	CHANNEL_ID_DDC6,
	CHANNEL_ID_DDC_VGA,
	CHANNEL_ID_I2C_PAD,
	CHANNEL_ID_COUNT
};

#define DECODE_CHANNEL_ID(ch_id) \
	(ch_id) == CHANNEL_ID_DDC1 ? "CHANNEL_ID_DDC1" : \
	(ch_id) == CHANNEL_ID_DDC2 ? "CHANNEL_ID_DDC2" : \
	(ch_id) == CHANNEL_ID_DDC3 ? "CHANNEL_ID_DDC3" : \
	(ch_id) == CHANNEL_ID_DDC4 ? "CHANNEL_ID_DDC4" : \
	(ch_id) == CHANNEL_ID_DDC5 ? "CHANNEL_ID_DDC5" : \
	(ch_id) == CHANNEL_ID_DDC6 ? "CHANNEL_ID_DDC6" : \
	(ch_id) == CHANNEL_ID_DDC_VGA ? "CHANNEL_ID_DDC_VGA" : \
	(ch_id) == CHANNEL_ID_I2C_PAD ? "CHANNEL_ID_I2C_PAD" : "Invalid"

enum transmitter {
	TRANSMITTER_UNKNOWN = (-1L),
	TRANSMITTER_UNIPHY_A,
	TRANSMITTER_UNIPHY_B,
	TRANSMITTER_UNIPHY_C,
	TRANSMITTER_UNIPHY_D,
	TRANSMITTER_UNIPHY_E,
	TRANSMITTER_UNIPHY_F,
	TRANSMITTER_NUTMEG_CRT,
	TRANSMITTER_TRAVIS_CRT,
	TRANSMITTER_TRAVIS_LCD,
	TRANSMITTER_UNIPHY_G,
	TRANSMITTER_COUNT
};

enum physical_phy_id {
	PHY_ID_UNKNOWN = (-1L),
	PHY_ID_0,
	PHY_ID_1,
	PHY_ID_2,
	PHY_ID_3,
	PHY_ID_4,
	PHY_ID_5,
	PHY_ID_6,
	PHY_ID_7,
	PHY_ID_8,
	PHY_ID_9,
	PHY_ID_COUNT
};

/* Generic source of the synchronisation input/output signal */
/* Can be used for flow control, stereo sync, timing sync, frame sync, etc */
enum sync_source {
	SYNC_SOURCE_NONE = 0,

	/* Source based on controllers */
	SYNC_SOURCE_CONTROLLER0,
	SYNC_SOURCE_CONTROLLER1,
	SYNC_SOURCE_CONTROLLER2,
	SYNC_SOURCE_CONTROLLER3,
	SYNC_SOURCE_CONTROLLER4,
	SYNC_SOURCE_CONTROLLER5,

	/* Source based on GSL group */
	SYNC_SOURCE_GSL_GROUP0,
	SYNC_SOURCE_GSL_GROUP1,
	SYNC_SOURCE_GSL_GROUP2,

	/* Source based on GSL IOs */
	/* These IOs normally used as GSL input/output */
	SYNC_SOURCE_GSL_IO_FIRST,
	SYNC_SOURCE_GSL_IO_GENLOCK_CLOCK = SYNC_SOURCE_GSL_IO_FIRST,
	SYNC_SOURCE_GSL_IO_GENLOCK_VSYNC,
	SYNC_SOURCE_GSL_IO_SWAPLOCK_A,
	SYNC_SOURCE_GSL_IO_SWAPLOCK_B,
	SYNC_SOURCE_GSL_IO_LAST = SYNC_SOURCE_GSL_IO_SWAPLOCK_B,

	/* Source based on regular IOs */
	SYNC_SOURCE_IO_FIRST,
	SYNC_SOURCE_IO_GENERIC_A = SYNC_SOURCE_IO_FIRST,
	SYNC_SOURCE_IO_GENERIC_B,
	SYNC_SOURCE_IO_GENERIC_C,
	SYNC_SOURCE_IO_GENERIC_D,
	SYNC_SOURCE_IO_GENERIC_E,
	SYNC_SOURCE_IO_GENERIC_F,
	SYNC_SOURCE_IO_HPD1,
	SYNC_SOURCE_IO_HPD2,
	SYNC_SOURCE_IO_HSYNC_A,
	SYNC_SOURCE_IO_VSYNC_A,
	SYNC_SOURCE_IO_HSYNC_B,
	SYNC_SOURCE_IO_VSYNC_B,
	SYNC_SOURCE_IO_LAST = SYNC_SOURCE_IO_VSYNC_B,

	/* Misc. flow control sources */
	SYNC_SOURCE_DUAL_GPU_PIN
};

enum trigger_edge {
	TRIGGER_EDGE_RISING = 0,
	TRIGGER_EDGE_FALLING,
	TRIGGER_EDGE_BOTH,
	TRIGGER_EDGE_DEFAULT
};

/* Parameters to enable CRTC trigger */
struct trigger_params {
	enum sync_source source;
	enum trigger_edge edge;
};

/* CRTC Static Screen event triggers */
struct static_screen_events {
	union {
		/* event mask to enable/disable various
		   trigger sources for static screen detection */
		struct {
			/* Force event high */
			uint32_t FRAME_START:1;
			/* Cursor register change */
			uint32_t CURSOR_MOVE:1;
			/* Memory write to any client other than MCIF */
			uint32_t MEM_WRITE:1;
			/* Memory write to hit memory region 0 */
			uint32_t MEM_REGION0_WRITE:1;
			/* Memory write to hit memory region 1 */
			uint32_t MEM_REGION1_WRITE:1;
			/* Memory write to hit memory region 2 */
			uint32_t MEM_REGION2_WRITE:1;
			/* Memory write to hit memory region 3 */
			uint32_t MEM_REGION3_WRITE:1;
			/* Graphics Surface Update Pending */
			uint32_t GFX_UPDATE:1;
			/* Overlay Surface Update Pending */
			uint32_t OVL_UPDATE:1;
			/* Compressed surface invalidated in FBC */
			uint32_t INVALIDATE_FBC_SURFACE:1;
			/* Register pending update in any double buffered
			register group in the display pipe
			(i.e. Blender, DCP, or SCL) */
			uint32_t REG_PENDING_UPDATE:1;
			/* Crtc_trig_a: Based on signal from any other CRTC */
			uint32_t CRTC_TRIG_A:1;
			/* Crtc_trig_b: Based on signal from any other CRTC */
			uint32_t CRTC_TRIG_B:1;
			/* Readback of CRTC nominal vertical count register
			by driver  indicates that OS may be trying to change
			mode or contents of the display therefore need to
			switch to higher refresh rate */
			uint32_t READBACK_NOMINAL_VERTICAL:1;
			/* Readback of CRTC dynamic vertical count register
			by driver  indicates that OS may be trying to change
			mode or contents of the display therefore need to
			switch to higher refresh rate */
			uint32_t READBACK_DYNAMIC_VERTICAL:1;
			/* Reserved */
			uint32_t RESERVED:1;
		} bits;
		uint32_t u_all;
	};
};

/*
 * ***************************************************************
 * ********************* Register programming sequences ********
 * ***************************************************************
 */

#define IO_REGISTER_SEQUENCE_MAX_LENGTH 5

/*
 *****************************************************************************
 *                             struct io_register
 *****************************************************************************
 * Generic struct for read/write register or GPIO.
 * It allows controlling only some bit section of register, rather then the
 * whole one.
 * For write operation should be used as following:
 *   1. data  = READ(Base + RegisterOffset)
 *   2. data &= ANDMask
 *   3. data |= ORMask
 *   4. WRITE(Base + RegisterOffset, data)
 *
 * Note: In case of regular register, ANDMask will be typically 0.
 *	In case of GPIO, ANDMask will have typically all bits set
 *	except the specific GPIO bit.
 *
 * For read operation should be used as following:
 *   1. data   = READ(Base + RegisterOffset)
 *   2. data  &= ANDMask
 *   3. data >>= BitShift
 *
 * Note: In case of regular register, ANDMask will be typically 0xFFFFFFFF.
 *	In case of GPIO, ANDMask will have typically only specific GPIO bit set
 *
 * Note: Base Address is not exposed in this structure due to
 *	security consideration.
 */

/* Sequence ID - uniqly defines sequence on single adapter */

struct fbc_info {
	bool fbc_enable;
	bool lpt_enable;
};

#endif
