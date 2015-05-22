/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
*/

#ifndef _ACP_GFX_IF_H
#define _ACP_GFX_IF_H

#include <linux/types.h>
#include "cgs_linux.h"
#include "cgs_common.h"
#include "amd_acp.h"

struct amd_acp_private {
	/* The public struture is first, so that pointers can be cast
	 * between the public and private structure */
	struct amd_acp_device public;

	/* private elements not expose through the bus interface */
	void *cgs_device;
	unsigned acp_version_major, acp_version_minor;
};

int amd_acp_hw_init(void *cgs_device,
		    unsigned acp_version_major, unsigned acp_version_minor,
		    struct amd_acp_private **apriv);
int amd_acp_hw_fini(struct amd_acp_private *apriv);
void amd_acp_suspend(struct amd_acp_private *acp_private);
void amd_acp_resume(struct amd_acp_private *acp_private);

#endif /* _ACP_GFX_IF_H */
