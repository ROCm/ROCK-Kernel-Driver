/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#include "amdgpu.h"
#include "vf_error.h"
/* Needs enum IDH_LOG_VF_ERROR, it is defined in both mxgpu_ai.h and mxgpu_vi.h. */
#include "mxgpu_ai.h"

#define AMDGPU_VF_ERROR_ENTRY_SIZE     32

/* struct error_entry - amdgpu VF error information. */
struct amdgpu_vf_error_buffer {
	int read_count;
	int write_count;
	uint16_t code[AMDGPU_VF_ERROR_ENTRY_SIZE];
	uint16_t flags[AMDGPU_VF_ERROR_ENTRY_SIZE];
	uint64_t data[AMDGPU_VF_ERROR_ENTRY_SIZE];
};

struct amdgpu_vf_error_buffer admgpu_vf_errors;

#if 0 /* amdgpu driver does not need following code, but we should keep them same as the code of AMD GIM driver */

struct error_text
{
	uint8_t arg_type;
	char* text;
};

enum error_data_type
{
	ERROR_DATA_ARG_NONE = 0,  // No error data
	ERROR_DATA_ARG_64,        // 64-bit
	ERROR_DATA_ARG_32_32,     // 32bit 32bit
	ERROR_DATA_ARG_16_16_32,  // 16bit 16bit 32bit
};

static const struct error_text amdgim_error_vf [AMDGIM_ERROR_VF_MAX] =
{
	/* AMDGIM_ERROR_VF_GPU_INIT_FATAL_FAIL */      {ERROR_DATA_ARG_NONE, "Fatal error during GPU init."},
	/* AMDGIM_ERROR_VF_ATOMBIOS_INIT_FAIL */       {ERROR_DATA_ARG_NONE, "amdgpu_atombios_init failed."},
	/* AMDGIM_ERROR_VF_UNLOCATE_BIOS_ROM */        {ERROR_DATA_ARG_NONE, "Unable to locate a BIOS ROM."},
	/* AMDGIM_ERROR_VF_NO_VBIOS */                 {ERROR_DATA_ARG_NONE, "no vBIOS found"},
	/* AMDGIM_ERROR_VF_GPU_POST_ERROR */           {ERROR_DATA_ARG_NONE, "gpu post error."},

	/* AMDGIM_ERROR_VF_ATOMBIOS_GET_CLOCK_FAIL */  {ERROR_DATA_ARG_NONE, "amdgpu_atombios_get_clock_info failed."},
	/* AMDGIM_ERROR_VF_FENCE_INIT_FAIL */          {ERROR_DATA_ARG_NONE, "amdgpu_fence_driver_init failed."},
	/* AMDGIM_ERROR_VF_AMDGPU_INIT_FAIL */         {ERROR_DATA_ARG_NONE, "amdgpu_init failed."},
	/* AMDGIM_ERROR_VF_IB_INIT_FAIL */             {ERROR_DATA_ARG_64,   "IB initialization failed (%d)."},
	/* AMDGIM_ERROR_VF_AMDGPU_LATE_INIT_FAIL */    {ERROR_DATA_ARG_NONE, "amdgpu_late_init failed."},

	/* AMDGIM_ERROR_VF_ASIC_RESUME_FAIL */         {ERROR_DATA_ARG_64,   "asic resume failed (%d)."},
	/* AMDGIM_ERROR_VF_GPU_RESET_FAIL */           {ERROR_DATA_ARG_NONE, "GPU reset failed."},
	/* AMDGIM_ERROR_VF_MMSCH_INIT_FAIL */          {ERROR_DATA_ARG_64,   "failed to init MMSCH, mmVCE_MMSCH_VF_MAILBOX_RESP = 0x%x."},
	/* AMDGIM_ERROR_VF_UVD_NORESP_GIVEUP */        {ERROR_DATA_ARG_NONE, "UVD not responding, giving up."},
	/* AMDGIM_ERROR_VF_UVD_NORESP_RESET */         {ERROR_DATA_ARG_NONE, "UVD not responding, trying to reset the VCPU."},

	/* AMDGIM_ERROR_VF_LOAD_GFX_FIRMWARE_FAIL */   {ERROR_DATA_ARG_NONE, "Failed to load gfx firmware."},
	/* AMDGIM_ERROR_VF_MEC_BO_INIT_FAIL */         {ERROR_DATA_ARG_NONE, "Failed to init MEC BOs."},
	/* AMDGIM_ERROR_VF_ADD_DEV_TO_GENPD_FAIL */    {ERROR_DATA_ARG_NONE, "Failed to add dev to genpd."},
	/* AMDGIM_ERROR_VF_IH_WB_ALLOC_FAIL */         {ERROR_DATA_ARG_64,   "IH wptr_offs wb alloc failed (%d)."},
	/* AMDGIM_ERROR_VF_BO_ALLOC_K_FAIL */          {ERROR_DATA_ARG_64,   "Failed to allocate kernel bo (%d)."},

	/* AMDGIM_ERROR_VF_BO_RESERVE_FAIL */          {ERROR_DATA_ARG_64,   "Failed to reserve kernel bo (%d)."},
	/* AMDGIM_ERROR_VF_BO_PIN_FAIL */              {ERROR_DATA_ARG_64,   "Kernel bo pin failed (%d)."},
	/* AMDGIM_ERROR_VF_BO_MAP_FAIL */              {ERROR_DATA_ARG_64,   "Kernel bo map failed (%d)."},
	/* AMDGIM_ERROR_VF_RING_R_WB_ALLOC_FAIL */     {ERROR_DATA_ARG_64,   "Ring rptr_offs wb alloc failed (%d)."},
	/* AMDGIM_ERROR_VF_RING_W_WB_ALLOC_FAIL */     {ERROR_DATA_ARG_64,   "Ring wptr_offs wb alloc failed (%d)."},

	/* AMDGIM_ERROR_VF_RING_F_WB_ALLOC_FAIL */     {ERROR_DATA_ARG_64,   "Ring fence_offs wb alloc failed (%d)."},
	/* AMDGIM_ERROR_VF_RING_C_WB_ALLOC_FAIL */     {ERROR_DATA_ARG_64,   "Ring cond_exec_polling wb alloc failed (%d)."},
	/* AMDGIM_ERROR_VF_INIT_FENCE_FAIL */          {ERROR_DATA_ARG_64,   "failed initializing fences (%d)."},
	/* AMDGIM_ERROR_VF_RING_CREATE_FAIL */         {ERROR_DATA_ARG_64,   "Ring create failed (%d)."},
	/* AMDGIM_ERROR_VF_BO_ALLOC_M_FAIL */          {ERROR_DATA_ARG_64,   "Failed to allocate bo for manager (%d)."},

	/* AMDGIM_ERROR_VF_NO_BO_FOR_SA */             {ERROR_DATA_ARG_NONE, "No bo for sa manager."},
	/* AMDGIM_ERROR_VF_FW_ALLOC_FAIL */            {ERROR_DATA_ARG_64,   "Firmware buffer allocate failed (%d)."},
	/* AMDGIM_ERROR_VF_FW_RESERVE_FAIL */          {ERROR_DATA_ARG_64,   "Firmware buffer reserve failed (%d)."},
	/* AMDGIM_ERROR_VF_FW_PIN_FAIL */              {ERROR_DATA_ARG_64,   "Firmware buffer pin failed (%d)."},
	/* AMDGIM_ERROR_VF_FW_KMAP_FAIL */             {ERROR_DATA_ARG_64,   "Firmware buffer kmap failed (%d)."},

	/* AMDGIM_ERROR_VF_UVD_NOT_LOAD_FW */          {ERROR_DATA_ARG_NONE, "amdgpu_uvd: Can't load firmware."},
	/* AMDGIM_ERROR_VF_UVD_NOT_VALIDATE_FW */      {ERROR_DATA_ARG_NONE, "amdgpu_uvd: Can't validate firmware."},
	/* AMDGIM_ERROR_VF_ALLOC_UVD_BO_FAIL */        {ERROR_DATA_ARG_64,   "Failed to allocate UVD bo (%d)."},
	/* AMDGIM_ERROR_VF_VCE_NOT_LOAD_FW */          {ERROR_DATA_ARG_NONE, "amdgpu_vce: Can't load firmware."},
	/* AMDGIM_ERROR_VF_VCE_NOT_VALIDATE_FW */      {ERROR_DATA_ARG_NONE, "amdgpu_vce: Can't validate firmware."},

	/* AMDGIM_ERROR_VF_ALLOC_VCE_BO_FAIL */        {ERROR_DATA_ARG_64,   "Failed to allocate VCE bo (%d)."},
	/* AMDGIM_ERROR_VF_VCE_RESERVE_FAIL */         {ERROR_DATA_ARG_64,   "Failed to reserve VCE bo (%d)."},
	/* AMDGIM_ERROR_VF_VCE_KMAP_FAIL */            {ERROR_DATA_ARG_64,   "VCE kmap failed (%d)."},
	/* AMDGIM_ERROR_VF_NO_VRAM_FOR_GART */         {ERROR_DATA_ARG_NONE, "No VRAM object for PCIE GART."},
	/* AMDGIM_ERROR_VF_PSP_LOAD_FW_FAIL */         {ERROR_DATA_ARG_NONE, "PSP: Failed to load firmware."},

	/* AMDGIM_ERROR_VF_INIT_MMSCH_FAIL */          {ERROR_DATA_ARG_64,   "failed to init MMSCH, mmVCE_MMSCH_VF_MAILBOX_RESP = %x."},
	/* AMDGIM_ERROR_VF_GFX_LOAD_FW_FAIL */         {ERROR_DATA_ARG_NONE, "gfx: Failed to load firmware."},
	/* AMDGIM_ERROR_VF_NGG_CREATE_BUF_FAIL */      {ERROR_DATA_ARG_64,   "Failed to create NGG buffer (%d)."},
	/* AMDGIM_ERROR_VF_NGG_CREATE_PR_BUF_FAIL */   {ERROR_DATA_ARG_64,   "Failed to create Primitive Buffer (%d)."},
	/* AMDGIM_ERROR_VF_NGG_CREATE_PO_BUF_FAIL */   {ERROR_DATA_ARG_64,   "Failed to create Position Buffer (%d)."},

	/* AMDGIM_ERROR_VF_NGG_CREATE_CS_BUF_FAIL */   {ERROR_DATA_ARG_64,   "Failed to create Control Sideband Buffer (%d)."},
	/* AMDGIM_ERROR_VF_NGG_CREATE_PC_BUF_FAIL */   {ERROR_DATA_ARG_64,   "Failed to create Parameter Cache (%d)."},
	/* AMDGIM_ERROR_VF_BUFL_SIZE_INVALID */        {ERROR_DATA_ARG_64,   "Buffer size is invalid: %d"},
	/* AMDGIM_ERROR_VF_RLC_BO_INIT_FAIL */         {ERROR_DATA_ARG_64,   "Failed to init rlc BOs (%d)"},

	/* AMDGIM_ERROR_VF_TEST */                     {ERROR_DATA_ARG_64,   "This is error log collect test for VF component (test count %llu)."}
};

int get_vf_error_text (uint32_t error_code, uint64_t error_data, char* error_msg, int buf_size)
{
	int error_catagory = AMDGIM_ERROR_CATAGORY(error_code);
	int error_sub_code = AMDGIM_ERROR_SUBCODE(error_code);

	if (AMDGIM_ERROR_CATEGORY_VF != error_catagory) {
		return 0;
	}
	if (error_sub_code >= AMDGIM_ERROR_VF_MAX) {
		return 0;
	}
	switch (amdgim_error_vf[error_sub_code].arg_type) {
	case ERROR_DATA_ARG_NONE:
		snprintf (error_msg, buf_size - 1, amdgim_error_vf[error_sub_code].text);
		break;
	case ERROR_DATA_ARG_64:
		snprintf (error_msg, buf_size - 1, amdgim_error_vf[error_sub_code].text,
				error_data);
		break;
	case ERROR_DATA_ARG_32_32:
		snprintf (error_msg, buf_size - 1, amdgim_error_vf[error_sub_code].text,
				(uint32_t)(error_data >> 32),
				(uint32_t)(error_data & 0xFFFFFFFF));
		break;
	case ERROR_DATA_ARG_16_16_32:
		snprintf (error_msg, buf_size - 1, amdgim_error_vf[error_sub_code].text,
				(uint16_t)(error_data >> 48),
				(uint16_t)((error_data >> 32) & 0xFFFF),
				(uint32_t)(error_data & 0xFFFFFFFF));
		break;
	default:
		return 0;
		break;
	}
	return strlen (error_msg);
}

#endif

void amdgpu_put_vf_error(uint16_t sub_error_code, uint16_t error_flags, uint64_t error_data)
{
	int index;
	uint16_t error_code = AMDGIM_ERROR_CODE(AMDGIM_ERROR_CATEGORY_VF, sub_error_code);

	index = admgpu_vf_errors.write_count % AMDGPU_VF_ERROR_ENTRY_SIZE;
	admgpu_vf_errors.code [index] = error_code;
	admgpu_vf_errors.flags [index] = error_flags;
	admgpu_vf_errors.data [index] = error_data;
	admgpu_vf_errors.write_count ++;
}


void amdgpu_trans_all_vf_error(struct amdgpu_device *adev)
{
	//u32 pf2vf_flags = 0;
	u32 data1, data2, data3;
	int index;

	if ((NULL == adev) || (!amdgpu_sriov_vf(adev)) || (!adev->virt.ops) || (!adev->virt.ops->trans_msg)){
		return;
	}
/*
 	TODO: Enable these code when pv2vf_info is merged
	AMDGPU_FW_VRAM_PF2VF_READ (adev, feature_flags, &pf2vf_flags);
	if (!(pf2vf_flags & AMDGIM_FEATURE_ERROR_LOG_COLLECT))
	{
		return;
	}
*/
	/* The errors are overlay of array, correct read_count as full. */
	if (admgpu_vf_errors.write_count - admgpu_vf_errors.read_count > AMDGPU_VF_ERROR_ENTRY_SIZE) {
		admgpu_vf_errors.read_count = admgpu_vf_errors.write_count - AMDGPU_VF_ERROR_ENTRY_SIZE;
	}

	while (admgpu_vf_errors.read_count < admgpu_vf_errors.write_count) {
		index =admgpu_vf_errors.read_count % AMDGPU_VF_ERROR_ENTRY_SIZE;
		data1 = AMDGIM_ERROR_CODE_FLAGS_TO_MAILBOX (admgpu_vf_errors.code[index], admgpu_vf_errors.flags[index]);
		data2 = admgpu_vf_errors.data[index] & 0xFFFFFFFF;
		data3 = (admgpu_vf_errors.data[index] >> 32) & 0xFFFFFFFF;

		adev->virt.ops->trans_msg(adev, IDH_LOG_VF_ERROR, data1, data2, data3);
		admgpu_vf_errors.read_count ++;
	}
}
