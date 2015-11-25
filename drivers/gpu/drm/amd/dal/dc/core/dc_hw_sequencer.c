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
 * Authors: AMD
 *
 */
#include "dc_services.h"
#include "core_types.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/dce110_hw_sequencer.h"
#endif

bool dc_construct_hw_sequencer(
				struct adapter_service *adapter_serv,
				struct dc *dc)
{
	enum dce_version dce_ver = dal_adapter_service_get_dce_version(adapter_serv);

	switch (dce_ver)
	{
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		return dce110_hw_sequencer_construct(dc);
#endif
	default:
		break;
	}

	return false;
}
