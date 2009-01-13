/*
* Copyright 2004 The Unichrome Project. All Rights Reserved.
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
* THE UNICHROME PROJECT, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
* OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
* THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
* Author: Scott Fang 2008.
*/

#ifndef _via_chrome9_VERIFIER_H_
#define _via_chrome9_VERIFIER_H_

#define VIA_CHROME9_VERIFY_ENABLE 1

enum  drm_via_chrome9_sequence {
	no_sequence = 0,
	z_address,
	dest_address,
	tex_address,
	zocclusion_address,
	coarse_z_address,
	fvf_address,
	fence_cmd_address
};

struct drm_via_chrome9_state {
	uint32_t texture_index;
	uint32_t render_target_addr[4];
	uint32_t render_target_pitch[4];
	uint32_t vb_addr;
	uint32_t fence_cmd_addr;
	uint32_t fence_need_check;
	enum drm_via_chrome9_sequence unfinished;
	int agp_texture;
	int multitex;
	struct drm_device *dev;
	int agp;
	const uint32_t *buf_start;
};

extern int via_chrome9_verify_command_stream(const uint32_t *buf,
	unsigned int size, struct drm_device *dev, int agp);
void via_chrome9_init_command_verifier(void);

#endif
