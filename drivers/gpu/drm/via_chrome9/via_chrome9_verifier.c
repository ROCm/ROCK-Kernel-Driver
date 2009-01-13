/*
* Copyright 2004 The Unichrome Project. All Rights Reserved.
* Copyright 2005 Thomas Hellstrom. All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sub license,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the
* next paragraph) shall be included in all copies or substantial portions
* of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
* THE AUTHOR(S), AND/OR THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES
* OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*
* This code was written using docs obtained under NDA from VIA Inc.
*
* Don't run this code directly on an AGP buffer. Due to cache problems it will
* be very slow.
*/

#include "via_chrome9_3d_reg.h"
#include "drmP.h"
#include "drm.h"
#include "via_chrome9_drm.h"
#include "via_chrome9_verifier.h"
#include "via_chrome9_drv.h"

#if VIA_CHROME9_VERIFY_ENABLE

enum verifier_state {
	state_command,
	state_header0,
	state_header1,
	state_header2,
	state_header3,
	state_header4,
	state_header5,
	state_header6,
	state_header7,
	state_error
};

enum hazard {
	no_check = 0,
	check_render_target_addr0,
	check_render_target_addr1,
	check_render_target_addr_mode,
	check_z_buffer_addr0,
	check_z_buffer_addr1,
	check_z_buffer_addr_mode,
	check_zocclusion_addr0,
	check_zocclusion_addr1,
	check_coarse_z_addr0,
	check_coarse_z_addr1,
	check_fvf_addr_mode,
	check_t_level0_facen_addr0,
	check_fence_cmd_addr0,
	check_fence_cmd_addr1,
	check_fence_cmd_addr2,
	forbidden_command
};

/*
 * Associates each hazard above with a possible multi-command
 * sequence. For example an address that is split over multiple
 * commands and that needs to be checked at the first command
 * that does not include any part of the address.
 */

static enum drm_via_chrome9_sequence seqs[] = {
	no_sequence,
	dest_address,
	dest_address,
	dest_address,
	z_address,
	z_address,
	z_address,
	zocclusion_address,
	zocclusion_address,
	coarse_z_address,
	coarse_z_address,
	fvf_address,
	tex_address,
	fence_cmd_address,
	fence_cmd_address,
	fence_cmd_address,
	no_sequence
};

struct hz_init {
	unsigned int code;
	enum hazard hz;
};
/* for atrribute other than context hazard detect */
static struct hz_init init_table1[] = {
	{0xcc, no_check},
	{0xcd, no_check},
	{0xce, no_check},
	{0xcf, no_check},
	{0xdd, no_check},
	{0xee, no_check},
	{0x00, no_check},
	{0x01, no_check},
	{0x10, check_z_buffer_addr0},
	{0x11, check_z_buffer_addr1},
	{0x12, check_z_buffer_addr_mode},
	{0x13, no_check},
	{0x14, no_check},
	{0x15, no_check},
	{0x16, no_check},
	{0x17, no_check},
	{0x18, no_check},
	{0x19, no_check},
	{0x1a, no_check},
	{0x1b, no_check},
	{0x1c, no_check},
	{0x1d, no_check},
	{0x1e, no_check},
	{0x1f, no_check},
	{0x20, no_check},
	{0x21, check_zocclusion_addr0},
	{0x22, check_zocclusion_addr1},
	{0x23, no_check},
	{0x24, no_check},
	{0x25, no_check},
	{0x26, no_check},
	{0x27, no_check},
	/* H5 only*/
	{0x28, no_check},
	{0x29, check_coarse_z_addr0},
	{0x2a, check_coarse_z_addr1},
	{0x33, no_check},
	{0x34, no_check},
	{0x35, no_check},
	{0x36, no_check},
	{0x37, no_check},
	{0x38, no_check},
	{0x39, no_check},
	{0x3A, no_check},
	{0x3B, no_check},
	{0x3C, no_check},
	{0x3D, no_check},
	{0x3E, no_check},
	{0x3F, no_check},
	/*render target check */
	{0x50, check_render_target_addr0},
	/* H5/H6 different */
	{0x51, check_render_target_addr_mode},
	{0x52, check_render_target_addr1},
	{0x53, no_check},
	{0x58, check_render_target_addr0},
	{0x59, check_render_target_addr_mode},
	{0x5a, check_render_target_addr1},
	{0x5b, no_check},
	{0x60, check_render_target_addr0},
	{0x61, check_render_target_addr_mode},
	{0x62, check_render_target_addr1},
	{0x63, no_check},
	{0x68, check_render_target_addr0},
	{0x69, check_render_target_addr_mode},
	{0x6a, check_render_target_addr1},
	{0x6b, no_check},
	{0x70, no_check},
	{0x71, no_check},
	{0x72, no_check},
	{0x73, no_check},
	{0x74, no_check},
	{0x75, no_check},
	{0x76, no_check},
	{0x77, no_check},
	{0x78, no_check},
	{0x80, no_check},
	{0x81, no_check},
	{0x82, no_check},
	{0x83, no_check},
	{0x84, no_check},
	{0x85, no_check},
	{0x86, no_check},
	{0x87, no_check},
	{0x88, no_check},
	{0x89, no_check},
	{0x8a, no_check},
	{0x90, no_check},
	{0x91, no_check},
	{0x92, no_check},
	{0x93, no_check},
	{0x94, no_check},
	{0x95, no_check},
	{0x96, no_check},
	{0x97, no_check},
	{0x98, no_check},
	{0x99, no_check},
	{0x9a, no_check},
	{0x9b, no_check},
	{0xaa, no_check}
};

/* for texture stage's hazard detect */
static struct hz_init init_table2[] = {
	{0xcc, no_check},
	{0xcd, no_check},
	{0xce, no_check},
	{0xcf, no_check},
	{0xdd, no_check},
	{0xee, no_check},
	{0x00, no_check},
	{0x01, no_check},
	{0x02, no_check},
	{0x03, no_check},
	{0x04, no_check},
	{0x05, no_check},
	/* H5/H6 diffent */
	{0x18, check_t_level0_facen_addr0},
	{0x20, no_check},
	{0x21, no_check},
	{0x22, no_check},
	{0x30, no_check},
	{0x50, no_check},
	{0x51, no_check},
	{0x9b, no_check},
};

/*Check for flexible vertex format */
static struct hz_init init_table3[] = {
	{0xcc, no_check},
	{0xcd, no_check},
	{0xce, no_check},
	{0xcf, no_check},
	{0xdd, no_check},
	{0xee, no_check},
	/* H5/H6 different */
	{0x00, check_fvf_addr_mode},
	{0x01, no_check},
	{0x02, no_check},
	{0x03, no_check},
	{0x04, no_check},
	{0x05, no_check},
	{0x08, no_check},
	{0x09, no_check},
	{0x0a, no_check},
	{0x0b, no_check},
	{0x0c, no_check},
	{0x0d, no_check},
	{0x0e, no_check},
	{0x0f, no_check},
	{0x10, no_check},
	{0x11, no_check},
	{0x12, no_check},
	{0x13, no_check},
	{0x14, no_check},
	{0x15, no_check},
	{0x16, no_check},
	{0x17, no_check},
	{0x18, no_check},
	{0x19, no_check},
	{0x1a, no_check},
	{0x1b, no_check},
	{0x1c, no_check},
	{0x1d, no_check},
	{0x1e, no_check},
	{0x1f, no_check},
	{0x20, no_check},
	{0x21, no_check},
	{0x22, no_check},
	{0x23, no_check},
	{0x24, no_check},
	{0x25, no_check},
	{0x26, no_check},
	{0x27, no_check},
	{0x28, no_check},
	{0x29, no_check},
	{0x2a, no_check},
	{0x2b, no_check},
	{0x2c, no_check},
	{0x2d, no_check},
	{0x2e, no_check},
	{0x2f, no_check},
	{0x40, no_check},
	{0x41, no_check},
	{0x42, no_check},
	{0x43, no_check},
	{0x44, no_check},
	{0x45, no_check},
	{0x46, no_check},
	{0x47, no_check},
	{0x48, no_check},
	{0x50, no_check},
	{0x51, no_check},
	{0x52, no_check},
	{0x60, no_check},
	{0x61, no_check},
	{0x62, no_check},
	{0x9b, no_check},
	{0xaa, no_check}
};
/*Check for 364 fence command id*/
static struct hz_init init_table4[] = {
	{0xcc, no_check},
	{0xcd, no_check},
	{0xce, no_check},
	{0xcf, no_check},
	{0xdd, no_check},
	{0xee, no_check},
	{0x00, no_check},
	{0x01, check_fence_cmd_addr0},
	{0x02, check_fence_cmd_addr1},
	{0x03, check_fence_cmd_addr2},
	{0x10, no_check},
	{0x11, no_check},
	{0x12, no_check},
	{0x13, no_check},
	{0x14, no_check},
	{0x18, no_check},
	{0x19, no_check},
	{0x1a, no_check},
	{0x1b, no_check},
	{0x1c, no_check},
	{0x20, no_check},
	{0xab, no_check},
	{0xaa, no_check}
};

/*Check for 353 fence command id*/
static struct hz_init init_table5[] = {
	{0xcc, no_check},
	{0xcd, no_check},
	{0xce, no_check},
	{0xcf, no_check},
	{0xdd, no_check},
	{0xee, no_check},
	{0x00, no_check},
	{0x01, no_check},
	{0x02, no_check},
	{0x03, no_check},
	{0x04, check_fence_cmd_addr0},
	{0x05, check_fence_cmd_addr1},
	{0x06, no_check},
	{0x07, check_fence_cmd_addr2},
	{0x08, no_check},
	{0x09, no_check},
	{0x0a, no_check},
	{0x0b, no_check},
	{0x0c, no_check},
	{0x0d, no_check},
	{0x0e, no_check},
	{0x0f, no_check},
	{0x10, no_check},
	{0x11, no_check},
	{0x12, no_check},
	{0x18, no_check},
	{0x19, no_check},
	{0x1a, no_check},
	{0x30, no_check},
	{0x31, no_check},
	{0x32, no_check},
	{0x68, no_check},
	{0x69, no_check},
	{0x6a, no_check},
	{0x6b, no_check},
	{0xab, no_check},
	{0xaa, no_check}
};

static enum hazard init_table_01_00[256];
static enum hazard init_table_02_0n[256];
static enum hazard init_table_04_00[256];
static enum hazard init_table_11_364[256];
static enum hazard init_table_11_353[256];

/*Require fence command id location reside in the shadow system memory */
static inline int
check_fence_cmd_addr_range(struct drm_via_chrome9_state *seq,
	unsigned long fence_cmd_add, unsigned long size, struct drm_device *dev)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;
	if (!dev_priv->shadow_map.shadow)
		return -1;
	if ((fence_cmd_add < dev_priv->shadow_map.shadow->offset) ||
		(fence_cmd_add + size >
		dev_priv->shadow_map.shadow->offset +
		dev_priv->shadow_map.shadow->size))
		return -1;
	return 0;
}

/*
 * Currently we only catch the fence cmd's address, which will
 * access system memory inevitably.
 * NOTE:No care about AGP address.(we just think all AGP access are safe now).
 */

static inline int finish_current_sequence(struct drm_via_chrome9_state *cur_seq)
{
	switch (cur_seq->unfinished) {
	case fence_cmd_address:
		if (cur_seq->fence_need_check)
			if (check_fence_cmd_addr_range(cur_seq,
				cur_seq->fence_cmd_addr, 4, cur_seq->dev))
				return -EINVAL;
			break;
	default:
		break;
	}
	cur_seq->unfinished = no_sequence;
	return 0;
}
/* Only catch the cmd which potentially access the system memory, and treat all
 * the other cmds are safe.
 */
static inline int
investigate_hazard(uint32_t cmd, enum hazard hz,
	struct drm_via_chrome9_state *cur_seq)
{
	register uint32_t tmp;

	if (cur_seq->unfinished && (cur_seq->unfinished != seqs[hz])) {
		int ret = finish_current_sequence(cur_seq);
		if (ret)
			return ret;
	}

	switch (hz) {
	case check_render_target_addr0:
		tmp = ((cmd >> 24) - 0x50) >> 3;
		cur_seq->unfinished = dest_address;
		cur_seq->render_target_addr[tmp] = cmd << 8;
		break;
	case check_render_target_addr1:
		cur_seq->unfinished = dest_address;
		tmp = ((cmd >> 24) - 0x50) >> 3;
		cur_seq->render_target_pitch[tmp] = (cmd & 0x000001FF) >> 5;
		break;
	case check_render_target_addr_mode:
		cur_seq->unfinished = dest_address;
		if (!cur_seq->agp)
			if (((cmd & 0x00300000) >> 20) == 2) {
				DRM_ERROR("Attempt to place \
					render target in system memory\n");
				return -EINVAL;
			}
		break;
	case check_z_buffer_addr0:
		cur_seq->unfinished = z_address;
		break;
	case check_z_buffer_addr1:
		cur_seq->unfinished = z_address;
		if ((cmd & 0x00000003) == 2) {
			DRM_ERROR("Attempt to place \
					Z buffer in system memory\n");
			return -EINVAL;
		}
		break;
	case check_z_buffer_addr_mode:
		cur_seq->unfinished = z_address;
		if (((cmd & 0x00000060) >> 5) == 2) {
			DRM_ERROR("Attempt to place \
					stencil buffer in system memory\n");
			return -EINVAL;
		}
		break;
	case check_zocclusion_addr0:
		cur_seq->unfinished = zocclusion_address;
		break;
	case check_zocclusion_addr1:
		cur_seq->unfinished = zocclusion_address;
		if (((cmd & 0x00c00000) >> 22) == 2) {
			DRM_ERROR("Attempt to access system memory\n");
			return -EINVAL;
		}
		break;
	case check_coarse_z_addr0:
		cur_seq->unfinished = coarse_z_address;
		if (((cmd & 0x00300000) >> 20) == 2)
			return -EINVAL;
		break;
	case check_coarse_z_addr1:
		cur_seq->unfinished = coarse_z_address;
		break;
	case check_fvf_addr_mode:
		cur_seq->unfinished = fvf_address;
		if (!cur_seq->agp)
			if (((cmd & 0x0000c000) >> 14) == 2) {
				DRM_ERROR("Attempt to place \
					fvf buffer in system memory\n");
			return -EINVAL;
			}
		break;
	case check_t_level0_facen_addr0:
		cur_seq->unfinished = tex_address;
		if (!cur_seq->agp)
			if ((cmd & 0x00000003) == 2 ||
				((cmd & 0x0000000c) >> 2) == 2 ||
				((cmd & 0x00000030) >> 4) == 2 ||
				((cmd & 0x000000c0) >> 6) == 2 ||
				((cmd & 0x0000c000) >> 14) == 2 ||
				((cmd & 0x00030000) >> 16) == 2) {
				DRM_ERROR("Attempt to place \
					texture buffer in system memory\n");
			return -EINVAL;
			}
		break;
	case check_fence_cmd_addr0:
		cur_seq->unfinished = fence_cmd_address;
		if (cur_seq->agp)
			cur_seq->fence_cmd_addr =
			(cur_seq->fence_cmd_addr & 0xFF000000) |
			(cmd & 0x00FFFFFF);
		else
			cur_seq->fence_cmd_addr =
			(cur_seq->fence_cmd_addr & 0x00FFFFFF) |
			((cmd & 0x000000FF) << 24);
		break;
	case check_fence_cmd_addr1:
		cur_seq->unfinished = fence_cmd_address;
		if (!cur_seq->agp)
			cur_seq->fence_cmd_addr =
			(cur_seq->fence_cmd_addr & 0xFF000000) |
			(cmd & 0x00FFFFFF);
		break;
	case check_fence_cmd_addr2:
		cur_seq->unfinished = fence_cmd_address;
		if (cmd & 0x00040000)
			cur_seq->fence_need_check = 1;
		else
			cur_seq->fence_need_check = 0;
		break;
	default:
		/*We think all the other cmd are safe.*/
		return 0;
	}
	return 0;
}

static inline int verify_mmio_address(uint32_t address)
{
	if ((address > 0x3FF) && (address < 0xC00)) {
		DRM_ERROR("Invalid VIDEO DMA command. "
			  "Attempt to access 3D- or command burst area.\n");
		return 1;
	} else if ((address > 0xDFF) && (address < 0x1200)) {
		DRM_ERROR("Invalid VIDEO DMA command. "
			  "Attempt to access PCI DMA area.\n");
		return 1;
	} else if ((address > 0x1DFF) && (address < 0x2200)) {
		DRM_ERROR("Invalid VIDEO DMA command. "
			  "Attempt to access CBU ROTATE SPACE registers.\n");
		return 1;
	} else if ((address > 0x23FF) && (address < 0x3200)) {
		DRM_ERROR("Invalid VIDEO DMA command. "
			  "Attempt to access PCI DMA2 area..\n");
		return 1;
	} else if (address > 0x33FF) {
		DRM_ERROR("Invalid VIDEO DMA command. "
			  "Attempt to access VGA registers.\n");
		return 1;
	}
	return 0;
}

static inline int is_dummy_cmd(uint32_t cmd)
{
	if ((cmd & INV_DUMMY_MASK) == 0xCC000000 ||
			(cmd & INV_DUMMY_MASK) == 0xCD000000 ||
			(cmd & INV_DUMMY_MASK) == 0xCE000000 ||
			(cmd & INV_DUMMY_MASK) == 0xCF000000 ||
			(cmd & INV_DUMMY_MASK) == 0xDD000000)
			return 1;
	return 0;
}

static inline int
verify_2d_tail(uint32_t const **buffer, const uint32_t *buf_end,
	uint32_t dwords)
{
	const uint32_t *buf = *buffer;

	if (buf_end - buf < dwords) {
		DRM_ERROR("Illegal termination of 2d command.\n");
		return 1;
	}

	while (dwords--) {
		if (!is_dummy_cmd(*buf++)) {
			DRM_ERROR("Illegal 2d command tail.\n");
			return 1;
		}
	}

	*buffer = buf;
	return 0;
}

static inline int
verify_video_tail(uint32_t const **buffer, const uint32_t *buf_end,
		  uint32_t dwords)
{
	const uint32_t *buf = *buffer;

	if (buf_end - buf < dwords) {
		DRM_ERROR("Illegal termination of video command.\n");
		return 1;
	}
	while (dwords--) {
		if (*buf && !is_dummy_cmd(*buf)) {
			DRM_ERROR("Illegal video command tail.\n");
			return 1;
		}
		buf++;
	}
	*buffer = buf;
	return 0;
}

static inline enum verifier_state
via_chrome9_check_header0(uint32_t const **buffer, const uint32_t *buf_end)
{
	const uint32_t *buf = *buffer;
	uint32_t cmd, qword, dword;

	qword = *(buf+1);
	buf += 4;
	dword = qword << 1;

	if (buf_end - buf < dword)
		return state_error;

	while (qword-- > 0) {
		cmd = *buf;
		/* Is this consition too restrict? */
		if ((cmd & 0xFFFF) > 0x1FF) {
			DRM_ERROR("Invalid header0 command io address 0x%x \
				Attempt to access non-2D mmio area.\n", cmd);
			return state_error;
		}
		buf += 2;
	}

	if ((dword & 3) && verify_2d_tail(&buf, buf_end, 4 - (dword & 0x3)))
		return state_error;

	*buffer = buf;
	return state_command;
}

static inline enum verifier_state
via_chrome9_check_header1(uint32_t const **buffer, const uint32_t *buf_end)
{
	uint32_t dword;
	const uint32_t *buf = *buffer;

	dword = *(buf + 1);
	buf += 4;

	if (buf + dword > buf_end)
		return state_error;

	buf += dword;

	if ((dword & 0x3) && verify_2d_tail(&buf, buf_end, 4 - (dword & 0x3)))
		return state_error;

	*buffer = buf;
	return state_command;
}

static inline enum verifier_state
via_chrome9_check_header2(uint32_t const **buffer,
	const uint32_t *buf_end, struct drm_via_chrome9_state *hc_state)
{
	uint32_t cmd1, cmd2;
	enum hazard hz;
	const uint32_t *buf = *buffer;
	const enum hazard *hz_table;

	if ((buf_end - buf) < 4) {
		DRM_ERROR
		    ("Illegal termination of DMA HALCYON_HEADER2 sequence.\n");
		return state_error;
	}
	cmd1 = *buf & 0x0000FFFF;
	cmd2 = *++buf & 0x0000FFFF;
	if (((cmd1 != INV_REG_CR_BEGIN) && (cmd1 != INV_REG_3D_BEGIN)) ||
		((cmd2 != INV_REG_CR_TRANS) && (cmd2 != INV_REG_3D_TRANS))) {
		DRM_ERROR
		    ("Illegal IO address of DMA HALCYON_HEADER2 sequence.\n");
		return state_error;
	}
	/* Advance to get paratype and subparatype */
	cmd1 = *++buf & 0xFFFF0000;

	switch (cmd1) {
	case INV_ParaType_Attr:
		buf += 2;
		hz_table = init_table_01_00;
		break;
	case (INV_ParaType_Tex | (INV_SubType_Tex0 << 24)):
	case (INV_ParaType_Tex | (INV_SubType_Tex1 << 24)):
	case (INV_ParaType_Tex | (INV_SubType_Tex2 << 24)):
	case (INV_ParaType_Tex | (INV_SubType_Tex3 << 24)):
	case (INV_ParaType_Tex | (INV_SubType_Tex4 << 24)):
	case (INV_ParaType_Tex | (INV_SubType_Tex5 << 24)):
	case (INV_ParaType_Tex | (INV_SubType_Tex6 << 24)):
	case (INV_ParaType_Tex | (INV_SubType_Tex7 << 24)):
		buf += 2;
		hc_state->texture_index = (cmd1 & INV_ParaSubType_MASK) >> 24;
		hz_table = init_table_02_0n;
		break;
	case INV_ParaType_FVF:
		buf += 2;
		hz_table = init_table_04_00;
		break;
	case INV_ParaType_CR:
		buf += 2;
		if (hc_state->agp)
			hz_table = init_table_11_364;
		else
			hz_table = init_table_11_353;
		break;
	case INV_ParaType_Dummy:
		buf += 2;
		while ((buf < buf_end) && !is_agp_header(*buf))
			if (!is_dummy_cmd(*buf))
				return state_error;
			else
				buf++;

		if ((buf_end > buf) && ((buf_end - buf) & 0x3))
			return state_error;
		return state_command;
	/* We think cases below are all safe. So we feedback only when these
	these cmd has another header there.
	*/
	case INV_ParaType_Vdata:
	case (INV_ParaType_Tex |
		((INV_SubType_Tex0 | INV_SubType_TexSample) << 24)):
	case (INV_ParaType_Tex |
		((INV_SubType_Tex1 | INV_SubType_TexSample) << 24)):
	case (INV_ParaType_Tex |
		((INV_SubType_Tex2 | INV_SubType_TexSample) << 24)):
	case (INV_ParaType_Tex |
		((INV_SubType_Tex3 | INV_SubType_TexSample) << 24)):
	case (INV_ParaType_Tex |
		((INV_SubType_Tex4 | INV_SubType_TexSample) << 24)):
	case (INV_ParaType_Tex |
		((INV_SubType_Tex5 | INV_SubType_TexSample) << 24)):
	case (INV_ParaType_Tex |
		((INV_SubType_Tex6 | INV_SubType_TexSample) << 24)):
	case (INV_ParaType_Tex |
		((INV_SubType_Tex7 | INV_SubType_TexSample) << 24)):
	case (INV_ParaType_Tex | (INV_SubType_General << 24)):
	case INV_ParaType_Pal:
	case INV_ParaType_PreCR:
	case INV_ParaType_Cfg:
	default:
		buf += 2;
		while ((buf < buf_end) && !is_agp_header(*buf))
			buf++;
		*buffer = buf;
		return state_command;
	}

	while (buf < buf_end && !is_agp_header(*buf)) {
		cmd1 = *buf++;
		hz = hz_table[cmd1 >> 24];
		if (hz) {
			if (investigate_hazard(cmd1, hz, hc_state))
				return state_error;
		} else if (hc_state->unfinished &&
			finish_current_sequence(hc_state))
			return state_error;

	}
	if (hc_state->unfinished && finish_current_sequence(hc_state))
		return state_error;
	*buffer = buf;
	return state_command;
}

static inline enum verifier_state
via_chrome9_check_header3(uint32_t const **buffer,
	const uint32_t *buf_end)
{
	const uint32_t *buf = *buffer;

	buf += 4;
	while (buf < buf_end && !is_agp_header(*buf))
		buf += 4;

	*buffer = buf;
	return state_command;
}


static inline enum verifier_state
via_chrome9_check_vheader4(uint32_t const **buffer,
	const uint32_t *buf_end)
{
	uint32_t data;
	const uint32_t *buf = *buffer;

	if (buf_end - buf < 4) {
		DRM_ERROR("Illegal termination of video header4 command\n");
		return state_error;
	}

	data = *buf++ & ~INV_AGPHeader_MASK;
	if (verify_mmio_address(data))
		return state_error;

	data = *buf;
	buf += 2;

	if (*buf++ != 0x00000000) {
		DRM_ERROR("Illegal header4 header data\n");
		return state_error;
	}

	if (buf_end - buf < data)
		return state_error;
	buf += data;

	if ((data & 3) && verify_video_tail(&buf, buf_end, 4 - (data & 3)))
		return state_error;
	*buffer = buf;
	return state_command;

}

static inline enum verifier_state
via_chrome9_check_vheader5(uint32_t const **buffer, const uint32_t *buf_end)
{
	uint32_t data;
	const uint32_t *buf = *buffer;
	uint32_t i;

	if (buf_end - buf < 4) {
		DRM_ERROR("Illegal termination of video header5 command\n");
		return state_error;
	}

	data = *++buf;
	buf += 2;

	if (*buf++ != 0x00000000) {
		DRM_ERROR("Illegal header5 header data\n");
		return state_error;
	}
	if ((buf_end - buf) < (data << 1)) {
		DRM_ERROR("Illegal termination of video header5 command\n");
		return state_error;
	}
	for (i = 0; i < data; ++i) {
		if (verify_mmio_address(*buf++))
			return state_error;
		buf++;
	}
	data <<= 1;
	if ((data & 3) && verify_video_tail(&buf, buf_end, 4 - (data & 3)))
		return state_error;
	*buffer = buf;
	return state_command;
}

int
via_chrome9_verify_command_stream(const uint32_t *buf,
	unsigned int size, struct drm_device *dev, int agp)
{

	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_state *hc_state = &dev_priv->hc_state;
	struct drm_via_chrome9_state saved_state = *hc_state;
	uint32_t cmd;
	const uint32_t *buf_end = buf + (size >> 2);
	enum verifier_state state = state_command;

	hc_state->dev = dev;
	hc_state->unfinished = no_sequence;
	hc_state->agp = agp;

	while (buf < buf_end) {

		switch (state) {
		case state_header0:
			state = via_chrome9_check_header0(&buf, buf_end);
			break;
		case state_header1:
			state = via_chrome9_check_header1(&buf, buf_end);
			break;
		case state_header2:
			state = via_chrome9_check_header2(&buf,
				buf_end, hc_state);
			break;
		case state_header3:
			state = via_chrome9_check_header3(&buf, buf_end);
			break;
		case state_header4:
			state = via_chrome9_check_vheader4(&buf, buf_end);
			break;
		case state_header5:
			state = via_chrome9_check_vheader5(&buf, buf_end);
			break;
		case state_header6:
		case state_header7:
			DRM_ERROR("Unimplemented Header 6/7 command.\n");
			state = state_error;
			break;
		case state_command:
			cmd = *buf;
			if (INV_AGPHeader2 == (cmd & INV_AGPHeader_MASK))
				state = state_header2;
			else if (INV_AGPHeader1 == (cmd & INV_AGPHeader_MASK))
				state = state_header1;
			else if (INV_AGPHeader5 == (cmd & INV_AGPHeader_MASK))
				state = state_header5;
			else if (INV_AGPHeader6 == (cmd & INV_AGPHeader_MASK))
				state = state_header6;
			else if (INV_AGPHeader3 == (cmd & INV_AGPHeader_MASK))
				state = state_header3;
			else if (INV_AGPHeader4 == (cmd & INV_AGPHeader_MASK))
				state = state_header4;
			else if (INV_AGPHeader7 == (cmd & INV_AGPHeader_MASK))
				state = state_header7;
			else if (INV_AGPHeader0 == (cmd & INV_AGPHeader_MASK))
				state = state_header0;
			else {
				DRM_ERROR("Invalid command sequence\n");
				state = state_error;
			}
			break;
		case state_error:
		default:
			*hc_state = saved_state;
			return -EINVAL;
		}
	}
	if (state == state_error) {
		*hc_state = saved_state;
		return -EINVAL;
	}
	return 0;
}


static void
setup_hazard_table(struct hz_init init_table[],
enum hazard table[], int size)
{
	int i;

	for (i = 0; i < 256; ++i)
		table[i] = forbidden_command;

	for (i = 0; i < size; ++i)
		table[init_table[i].code] = init_table[i].hz;
}

void via_chrome9_init_command_verifier(void)
{
	setup_hazard_table(init_table1, init_table_01_00,
			   sizeof(init_table1) / sizeof(struct hz_init));
	setup_hazard_table(init_table2, init_table_02_0n,
			   sizeof(init_table2) / sizeof(struct hz_init));
	setup_hazard_table(init_table3, init_table_04_00,
			   sizeof(init_table3) / sizeof(struct hz_init));
	setup_hazard_table(init_table4, init_table_11_364,
			   sizeof(init_table4) / sizeof(struct hz_init));
	setup_hazard_table(init_table5, init_table_11_353,
			   sizeof(init_table5) / sizeof(struct hz_init));
}

#endif
