/* Copyright 2012-15 Advanced Micro Devices, Inc.*/

#include "dal_services_types.h"
#include "include/dal_interface.h"

#include "vid.h"
#include "amdgpu.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"



static const u32 crtc_offsets[] =
{
	CRTC0_REGISTER_OFFSET,
	CRTC1_REGISTER_OFFSET,
	CRTC2_REGISTER_OFFSET,
	CRTC3_REGISTER_OFFSET,
	CRTC4_REGISTER_OFFSET,
	CRTC5_REGISTER_OFFSET,
	CRTC6_REGISTER_OFFSET
};


void dal_stop_mc_access(struct amdgpu_device *adev,
				     struct amdgpu_mode_mc_save *save)
{
	u32 crtc_enabled, tmp;
	int i;

	save->vga_render_control = RREG32(mmVGA_RENDER_CONTROL);
	save->vga_hdp_control = RREG32(mmVGA_HDP_CONTROL);

	/* disable VGA render */
	tmp = RREG32(mmVGA_RENDER_CONTROL);
	tmp = REG_SET_FIELD(tmp, VGA_RENDER_CONTROL, VGA_VSTATUS_CNTL, 0);
	WREG32(mmVGA_RENDER_CONTROL, tmp);

	/* blank the display controllers */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		crtc_enabled = REG_GET_FIELD(RREG32(mmCRTC_CONTROL + crtc_offsets[i]),
					     CRTC_CONTROL, CRTC_MASTER_EN);
		if (crtc_enabled) {
#if 0
			u32 frame_count;
			int j;

			save->crtc_enabled[i] = true;
			tmp = RREG32(mmCRTC_BLANK_CONTROL + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, CRTC_BLANK_CONTROL, CRTC_BLANK_DATA_EN) == 0) {
				amdgpu_display_vblank_wait(adev, i);
				WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 1);
				tmp = REG_SET_FIELD(tmp, CRTC_BLANK_CONTROL, CRTC_BLANK_DATA_EN, 1);
				WREG32(mmCRTC_BLANK_CONTROL + crtc_offsets[i], tmp);
				WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 0);
			}
			/* wait for the next frame */
			frame_count = amdgpu_display_vblank_get_counter(adev, i);
			for (j = 0; j < adev->usec_timeout; j++) {
				if (amdgpu_display_vblank_get_counter(adev, i) != frame_count)
					break;
				udelay(1);
			}
			tmp = RREG32(mmGRPH_UPDATE + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, GRPH_UPDATE, GRPH_UPDATE_LOCK) == 0) {
				tmp = REG_SET_FIELD(tmp, GRPH_UPDATE, GRPH_UPDATE_LOCK, 1);
				WREG32(mmGRPH_UPDATE + crtc_offsets[i], tmp);
			}
			tmp = RREG32(mmCRTC_MASTER_UPDATE_LOCK + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, CRTC_MASTER_UPDATE_LOCK, MASTER_UPDATE_LOCK) == 0) {
				tmp = REG_SET_FIELD(tmp, CRTC_MASTER_UPDATE_LOCK, MASTER_UPDATE_LOCK, 1);
				WREG32(mmCRTC_MASTER_UPDATE_LOCK + crtc_offsets[i], tmp);
			}
#else
			/* XXX this is a hack to avoid strange behavior with EFI on certain systems */
			WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 1);
			tmp = RREG32(mmCRTC_CONTROL + crtc_offsets[i]);
			tmp = REG_SET_FIELD(tmp, CRTC_CONTROL, CRTC_MASTER_EN, 0);
			WREG32(mmCRTC_CONTROL + crtc_offsets[i], tmp);
			WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 0);
			save->crtc_enabled[i] = false;
			/* ***** */
#endif
		} else {
			save->crtc_enabled[i] = false;
		}
	}
}

void dal_resume_mc_access(struct amdgpu_device *adev,
				       struct amdgpu_mode_mc_save *save)
{
	u32 tmp, frame_count;
	int i, j;

	/* update crtc base addresses */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS_HIGH + crtc_offsets[i],
		       upper_32_bits(adev->mc.vram_start));
		WREG32(mmGRPH_SECONDARY_SURFACE_ADDRESS_HIGH + crtc_offsets[i],
		       upper_32_bits(adev->mc.vram_start));
		WREG32(mmGRPH_PRIMARY_SURFACE_ADDRESS + crtc_offsets[i],
		       (u32)adev->mc.vram_start);
		WREG32(mmGRPH_SECONDARY_SURFACE_ADDRESS + crtc_offsets[i],
		       (u32)adev->mc.vram_start);

		if (save->crtc_enabled[i]) {
			tmp = RREG32(mmCRTC_MASTER_UPDATE_MODE + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, CRTC_MASTER_UPDATE_MODE, MASTER_UPDATE_MODE) != 3) {
				tmp = REG_SET_FIELD(tmp, CRTC_MASTER_UPDATE_MODE, MASTER_UPDATE_MODE, 3);
				WREG32(mmCRTC_MASTER_UPDATE_MODE + crtc_offsets[i], tmp);
			}
			tmp = RREG32(mmGRPH_UPDATE + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, GRPH_UPDATE, GRPH_UPDATE_LOCK)) {
				tmp = REG_SET_FIELD(tmp, GRPH_UPDATE, GRPH_UPDATE_LOCK, 0);
				WREG32(mmGRPH_UPDATE + crtc_offsets[i], tmp);
			}
			tmp = RREG32(mmCRTC_MASTER_UPDATE_LOCK + crtc_offsets[i]);
			if (REG_GET_FIELD(tmp, CRTC_MASTER_UPDATE_LOCK, MASTER_UPDATE_LOCK)) {
				tmp = REG_SET_FIELD(tmp, CRTC_MASTER_UPDATE_LOCK, MASTER_UPDATE_LOCK, 0);
				WREG32(mmCRTC_MASTER_UPDATE_LOCK + crtc_offsets[i], tmp);
			}
			for (j = 0; j < adev->usec_timeout; j++) {
				tmp = RREG32(mmGRPH_UPDATE + crtc_offsets[i]);
				if (REG_GET_FIELD(tmp, GRPH_UPDATE, GRPH_SURFACE_UPDATE_PENDING) == 0)
					break;
				udelay(1);
			}
			tmp = RREG32(mmCRTC_BLANK_CONTROL + crtc_offsets[i]);
			tmp = REG_SET_FIELD(tmp, CRTC_BLANK_CONTROL, CRTC_BLANK_DATA_EN, 0);
			WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 1);
			WREG32(mmCRTC_BLANK_CONTROL + crtc_offsets[i], tmp);
			WREG32(mmCRTC_UPDATE_LOCK + crtc_offsets[i], 0);
			/* wait for the next frame */
			frame_count = amdgpu_display_vblank_get_counter(adev, i);
			for (j = 0; j < adev->usec_timeout; j++) {
				if (amdgpu_display_vblank_get_counter(adev, i) != frame_count)
					break;
				udelay(1);
			}
		}
	}

	WREG32(mmVGA_MEMORY_BASE_ADDRESS_HIGH, upper_32_bits(adev->mc.vram_start));
	WREG32(mmVGA_MEMORY_BASE_ADDRESS, lower_32_bits(adev->mc.vram_start));

	/* Unlock vga access */
	WREG32(mmVGA_HDP_CONTROL, save->vga_hdp_control);
	mdelay(1);
	WREG32(mmVGA_RENDER_CONTROL, save->vga_render_control);
}

