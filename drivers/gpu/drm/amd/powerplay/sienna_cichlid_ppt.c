/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include <linux/firmware.h>
#include <linux/pci.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_internal.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v11_0.h"
#include "smu11_driver_if_sienna_cichlid.h"
#include "soc15_common.h"
#include "atom.h"
#include "sienna_cichlid_ppt.h"
#include "smu_v11_0_pptable.h"
#include "smu_v11_0_7_ppsmc.h"
#include "nbio/nbio_2_3_offset.h"
#include "nbio/nbio_2_3_sh_mask.h"

#include "asic_reg/mp/mp_11_0_sh_mask.h"

#define FEATURE_MASK(feature) (1ULL << feature)
#define SMC_DPM_FEATURE ( \
	FEATURE_MASK(FEATURE_DPM_PREFETCHER_BIT) | \
	FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT)     | \
	FEATURE_MASK(FEATURE_DPM_UCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_LINK_BIT)       | \
	FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT)     | \
	FEATURE_MASK(FEATURE_DPM_FCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_DCEFCLK_BIT))

#define MSG_MAP(msg, index) \
	[SMU_MSG_##msg] = {1, (index)}

static struct smu_11_0_cmn2aisc_mapping sienna_cichlid_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,			PPSMC_MSG_TestMessage),
	MSG_MAP(GetSmuVersion,			PPSMC_MSG_GetSmuVersion),
	MSG_MAP(GetDriverIfVersion,		PPSMC_MSG_GetDriverIfVersion),
	MSG_MAP(SetAllowedFeaturesMaskLow,	PPSMC_MSG_SetAllowedFeaturesMaskLow),
	MSG_MAP(SetAllowedFeaturesMaskHigh,	PPSMC_MSG_SetAllowedFeaturesMaskHigh),
	MSG_MAP(EnableAllSmuFeatures,		PPSMC_MSG_EnableAllSmuFeatures),
	MSG_MAP(DisableAllSmuFeatures,		PPSMC_MSG_DisableAllSmuFeatures),
	MSG_MAP(EnableSmuFeaturesLow,		PPSMC_MSG_EnableSmuFeaturesLow),
	MSG_MAP(EnableSmuFeaturesHigh,		PPSMC_MSG_EnableSmuFeaturesHigh),
	MSG_MAP(DisableSmuFeaturesLow,		PPSMC_MSG_DisableSmuFeaturesLow),
	MSG_MAP(DisableSmuFeaturesHigh,		PPSMC_MSG_DisableSmuFeaturesHigh),
	MSG_MAP(GetEnabledSmuFeaturesLow,       PPSMC_MSG_GetRunningSmuFeaturesLow),
	MSG_MAP(GetEnabledSmuFeaturesHigh,	PPSMC_MSG_GetRunningSmuFeaturesHigh),
	MSG_MAP(SetWorkloadMask,		PPSMC_MSG_SetWorkloadMask),
	MSG_MAP(SetPptLimit,			PPSMC_MSG_SetPptLimit),
	MSG_MAP(SetDriverDramAddrHigh,		PPSMC_MSG_SetDriverDramAddrHigh),
	MSG_MAP(SetDriverDramAddrLow,		PPSMC_MSG_SetDriverDramAddrLow),
	MSG_MAP(SetToolsDramAddrHigh,		PPSMC_MSG_SetToolsDramAddrHigh),
	MSG_MAP(SetToolsDramAddrLow,		PPSMC_MSG_SetToolsDramAddrLow),
	MSG_MAP(TransferTableSmu2Dram,		PPSMC_MSG_TransferTableSmu2Dram),
	MSG_MAP(TransferTableDram2Smu,		PPSMC_MSG_TransferTableDram2Smu),
	MSG_MAP(UseDefaultPPTable,		PPSMC_MSG_UseDefaultPPTable),
	MSG_MAP(EnterBaco,			PPSMC_MSG_EnterBaco),
	MSG_MAP(SetSoftMinByFreq,		PPSMC_MSG_SetSoftMinByFreq),
	MSG_MAP(SetSoftMaxByFreq,		PPSMC_MSG_SetSoftMaxByFreq),
	MSG_MAP(SetHardMinByFreq,		PPSMC_MSG_SetHardMinByFreq),
	MSG_MAP(SetHardMaxByFreq,		PPSMC_MSG_SetHardMaxByFreq),
	MSG_MAP(GetMinDpmFreq,			PPSMC_MSG_GetMinDpmFreq),
	MSG_MAP(GetMaxDpmFreq,			PPSMC_MSG_GetMaxDpmFreq),
	MSG_MAP(GetDpmFreqByIndex,		PPSMC_MSG_GetDpmFreqByIndex),
	MSG_MAP(SetGeminiMode,			PPSMC_MSG_SetGeminiMode),
	MSG_MAP(SetGeminiApertureHigh,		PPSMC_MSG_SetGeminiApertureHigh),
	MSG_MAP(SetGeminiApertureLow,		PPSMC_MSG_SetGeminiApertureLow),
	MSG_MAP(OverridePcieParameters,		PPSMC_MSG_OverridePcieParameters),
	MSG_MAP(ReenableAcDcInterrupt,		PPSMC_MSG_ReenableAcDcInterrupt),
	MSG_MAP(NotifyPowerSource,		PPSMC_MSG_NotifyPowerSource),
	MSG_MAP(SetUclkFastSwitch,		PPSMC_MSG_SetUclkFastSwitch),
	MSG_MAP(SetVideoFps,			PPSMC_MSG_SetVideoFps),
	MSG_MAP(PrepareMp1ForUnload,		PPSMC_MSG_PrepareMp1ForUnload),
	MSG_MAP(AllowGfxOff,			PPSMC_MSG_AllowGfxOff),
	MSG_MAP(DisallowGfxOff,			PPSMC_MSG_DisallowGfxOff),
	MSG_MAP(GetPptLimit,			PPSMC_MSG_GetPptLimit),
	MSG_MAP(GetDcModeMaxDpmFreq,		PPSMC_MSG_GetDcModeMaxDpmFreq),
	MSG_MAP(ExitBaco,			PPSMC_MSG_ExitBaco),
	MSG_MAP(PowerUpVcn,			PPSMC_MSG_PowerUpVcn),
	MSG_MAP(PowerDownVcn,			PPSMC_MSG_PowerDownVcn),
	MSG_MAP(PowerUpJpeg,			PPSMC_MSG_PowerUpJpeg),
	MSG_MAP(PowerDownJpeg,			PPSMC_MSG_PowerDownJpeg),
	MSG_MAP(BacoAudioD3PME,			PPSMC_MSG_BacoAudioD3PME),
	MSG_MAP(ArmD3,				PPSMC_MSG_ArmD3),
};

static struct smu_11_0_cmn2aisc_mapping sienna_cichlid_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(GFXCLK,		PPCLK_GFXCLK),
	CLK_MAP(SCLK,		PPCLK_GFXCLK),
	CLK_MAP(SOCCLK,		PPCLK_SOCCLK),
	CLK_MAP(FCLK,		PPCLK_FCLK),
	CLK_MAP(UCLK,		PPCLK_UCLK),
	CLK_MAP(MCLK,		PPCLK_UCLK),
	CLK_MAP(DCLK,		PPCLK_DCLK_0),
	CLK_MAP(DCLK1,		PPCLK_DCLK_0),
	CLK_MAP(VCLK,		PPCLK_VCLK_1),
	CLK_MAP(VCLK1,		PPCLK_VCLK_1),
	CLK_MAP(DCEFCLK,	PPCLK_DCEFCLK),
	CLK_MAP(DISPCLK,	PPCLK_DISPCLK),
	CLK_MAP(PIXCLK,		PPCLK_PIXCLK),
	CLK_MAP(PHYCLK,		PPCLK_PHYCLK),
};

static struct smu_11_0_cmn2aisc_mapping sienna_cichlid_feature_mask_map[SMU_FEATURE_COUNT] = {
	FEA_MAP(DPM_PREFETCHER),
	FEA_MAP(DPM_GFXCLK),
	FEA_MAP(DPM_GFX_GPO),
	FEA_MAP(DPM_UCLK),
	FEA_MAP(DPM_SOCCLK),
	FEA_MAP(DPM_MP0CLK),
	FEA_MAP(DPM_LINK),
	FEA_MAP(DPM_DCEFCLK),
	FEA_MAP(MEM_VDDCI_SCALING),
	FEA_MAP(MEM_MVDD_SCALING),
	FEA_MAP(DS_GFXCLK),
	FEA_MAP(DS_SOCCLK),
	FEA_MAP(DS_LCLK),
	FEA_MAP(DS_DCEFCLK),
	FEA_MAP(DS_UCLK),
	FEA_MAP(GFX_ULV),
	FEA_MAP(FW_DSTATE),
	FEA_MAP(GFXOFF),
	FEA_MAP(BACO),
	FEA_MAP(MM_DPM_PG),
	FEA_MAP(RSMU_SMN_CG),
	FEA_MAP(PPT),
	FEA_MAP(TDC),
	FEA_MAP(APCC_PLUS),
	FEA_MAP(GTHR),
	FEA_MAP(ACDC),
	FEA_MAP(VR0HOT),
	FEA_MAP(VR1HOT),
	FEA_MAP(FW_CTF),
	FEA_MAP(FAN_CONTROL),
	FEA_MAP(THERMAL),
	FEA_MAP(GFX_DCS),
	FEA_MAP(RM),
	FEA_MAP(LED_DISPLAY),
	FEA_MAP(GFX_SS),
	FEA_MAP(OUT_OF_BAND_MONITOR),
	FEA_MAP(TEMP_DEPENDENT_VMIN),
	FEA_MAP(MMHUB_PG),
	FEA_MAP(ATHUB_PG),
	FEA_MAP(APCC_DFLL),
};

static struct smu_11_0_cmn2aisc_mapping sienna_cichlid_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP(PPTABLE),
	TAB_MAP(WATERMARKS),
	TAB_MAP(AVFS_PSM_DEBUG),
	TAB_MAP(AVFS_FUSE_OVERRIDE),
	TAB_MAP(PMSTATUSLOG),
	TAB_MAP(SMU_METRICS),
	TAB_MAP(DRIVER_SMU_CONFIG),
	TAB_MAP(ACTIVITY_MONITOR_COEFF),
	TAB_MAP(OVERDRIVE),
	TAB_MAP(I2C_COMMANDS),
	TAB_MAP(PACE),
};

static struct smu_11_0_cmn2aisc_mapping sienna_cichlid_pwr_src_map[SMU_POWER_SOURCE_COUNT] = {
	PWR_MAP(AC),
	PWR_MAP(DC),
};

static struct smu_11_0_cmn2aisc_mapping sienna_cichlid_workload_map[PP_SMC_POWER_PROFILE_COUNT] = {
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT,	WORKLOAD_PPLIB_DEFAULT_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_FULLSCREEN3D,		WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_POWERSAVING,		WORKLOAD_PPLIB_POWER_SAVING_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VIDEO,		WORKLOAD_PPLIB_VIDEO_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VR,			WORKLOAD_PPLIB_VR_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_CUSTOM_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
};

static int sienna_cichlid_get_smu_msg_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_MSG_MAX_COUNT)
		return -EINVAL;

	mapping = sienna_cichlid_message_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int sienna_cichlid_get_smu_clk_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_CLK_COUNT)
		return -EINVAL;

	mapping = sienna_cichlid_clk_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int sienna_cichlid_get_smu_feature_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_FEATURE_COUNT)
		return -EINVAL;

	mapping = sienna_cichlid_feature_mask_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int sienna_cichlid_get_smu_table_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_TABLE_COUNT)
		return -EINVAL;

	mapping = sienna_cichlid_table_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int sienna_cichlid_get_pwr_src_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_POWER_SOURCE_COUNT)
		return -EINVAL;

	mapping = sienna_cichlid_pwr_src_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int sienna_cichlid_get_workload_type(struct smu_context *smu, enum PP_SMC_POWER_PROFILE profile)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (profile > PP_SMC_POWER_PROFILE_CUSTOM)
		return -EINVAL;

	mapping = sienna_cichlid_workload_map[profile];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int
sienna_cichlid_get_allowed_feature_mask(struct smu_context *smu,
				  uint32_t *feature_mask, uint32_t num)
{
	struct amdgpu_device *adev = smu->adev;

	if (num > 2)
		return -EINVAL;

	memset(feature_mask, 0, sizeof(uint32_t) * num);

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_PREFETCHER_BIT)
				| FEATURE_MASK(FEATURE_DPM_FCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_SOCCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_DCEFCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_FCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_UCLK_BIT)
				| FEATURE_MASK(FEATURE_FW_DSTATE_BIT)
				| FEATURE_MASK(FEATURE_DF_CSTATE_BIT)
				| FEATURE_MASK(FEATURE_RSMU_SMN_CG_BIT)
				| FEATURE_MASK(FEATURE_GFX_SS_BIT)
				| FEATURE_MASK(FEATURE_VR0HOT_BIT)
				| FEATURE_MASK(FEATURE_PPT_BIT)
				| FEATURE_MASK(FEATURE_TDC_BIT)
				| FEATURE_MASK(FEATURE_BACO_BIT)
				| FEATURE_MASK(FEATURE_APCC_DFLL_BIT)
				| FEATURE_MASK(FEATURE_FW_CTF_BIT)
				| FEATURE_MASK(FEATURE_FAN_CONTROL_BIT)
				| FEATURE_MASK(FEATURE_THERMAL_BIT)
				| FEATURE_MASK(FEATURE_OUT_OF_BAND_MONITOR_BIT);

	if (adev->pm.pp_feature & PP_SCLK_DPM_MASK) {
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT);
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_GFX_GPO_BIT);
	}

	if (adev->pm.pp_feature & PP_MCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_UCLK_BIT)
					| FEATURE_MASK(FEATURE_MEM_VDDCI_SCALING_BIT)
					| FEATURE_MASK(FEATURE_MEM_MVDD_SCALING_BIT);

	if (adev->pm.pp_feature & PP_PCIE_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_LINK_BIT);

	if (adev->pm.pp_feature & PP_DCEFCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_DCEFCLK_BIT);

	if (adev->pm.pp_feature & PP_SOCCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT);

	if (adev->pm.pp_feature & PP_ULV_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_ULV_BIT);

	if (adev->pm.pp_feature & PP_SCLK_DEEP_SLEEP_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_GFXCLK_BIT);

	if (adev->pm.pp_feature & PP_GFXOFF_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFXOFF_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_ATHUB)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_ATHUB_PG_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_MMHUB)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_MMHUB_PG_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_VCN ||
	    smu->adev->pg_flags & AMD_PG_SUPPORT_JPEG)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_MM_DPM_PG_BIT);

	return 0;
}

static int sienna_cichlid_check_powerplay_table(struct smu_context *smu)
{
	return 0;
}

static int sienna_cichlid_append_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *smc_pptable = table_context->driver_pptable;
	struct atom_smc_dpm_info_v4_9 *smc_dpm_table;
	int index, ret;
	int i;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    smc_dpm_info);

	ret = smu_get_atom_data_table(smu, index, NULL, NULL, NULL,
				      (uint8_t **)&smc_dpm_table);
	if (ret)
		return ret;

	memcpy(smc_pptable->I2cControllers, smc_dpm_table->I2cControllers,
	       sizeof(I2cControllerConfig_t) * NUM_I2C_CONTROLLERS);

	/* SVI2 Board Parameters */
	smc_pptable->VddGfxVrMapping = smc_dpm_table->VddGfxVrMapping;
	smc_pptable->VddSocVrMapping = smc_dpm_table->VddSocVrMapping;
	smc_pptable->VddMem0VrMapping = smc_dpm_table->VddMem0VrMapping;
	smc_pptable->VddMem1VrMapping = smc_dpm_table->VddMem1VrMapping;
	smc_pptable->GfxUlvPhaseSheddingMask = smc_dpm_table->GfxUlvPhaseSheddingMask;
	smc_pptable->SocUlvPhaseSheddingMask = smc_dpm_table->SocUlvPhaseSheddingMask;
	smc_pptable->VddciUlvPhaseSheddingMask = smc_dpm_table->VddciUlvPhaseSheddingMask;
	smc_pptable->MvddUlvPhaseSheddingMask = smc_dpm_table->MvddUlvPhaseSheddingMask;

	/* Telemetry Settings */
	smc_pptable->GfxMaxCurrent = smc_dpm_table->GfxMaxCurrent;
	smc_pptable->GfxOffset = smc_dpm_table->GfxOffset;
	smc_pptable->Padding_TelemetryGfx = smc_dpm_table->Padding_TelemetryGfx;
	smc_pptable->SocMaxCurrent = smc_dpm_table->SocMaxCurrent;
	smc_pptable->SocOffset = smc_dpm_table->SocOffset;
	smc_pptable->Padding_TelemetrySoc = smc_dpm_table->Padding_TelemetrySoc;
	smc_pptable->Mem0MaxCurrent = smc_dpm_table->Mem0MaxCurrent;
	smc_pptable->Mem0Offset = smc_dpm_table->Mem0Offset;
	smc_pptable->Padding_TelemetryMem0 = smc_dpm_table->Padding_TelemetryMem0;
	smc_pptable->Mem1MaxCurrent = smc_dpm_table->Mem1MaxCurrent;
	smc_pptable->Mem1Offset = smc_dpm_table->Mem1Offset;
	smc_pptable->Padding_TelemetryMem1 = smc_dpm_table->Padding_TelemetryMem1;
	smc_pptable->MvddRatio = smc_dpm_table->MvddRatio;

	/* GPIO Settings */
	smc_pptable->AcDcGpio = smc_dpm_table->AcDcGpio;
	smc_pptable->AcDcPolarity = smc_dpm_table->AcDcPolarity;
	smc_pptable->VR0HotGpio = smc_dpm_table->VR0HotGpio;
	smc_pptable->VR0HotPolarity = smc_dpm_table->VR0HotPolarity;
	smc_pptable->VR1HotGpio = smc_dpm_table->VR1HotGpio;
	smc_pptable->VR1HotPolarity = smc_dpm_table->VR1HotPolarity;
	smc_pptable->GthrGpio = smc_dpm_table->GthrGpio;
	smc_pptable->GthrPolarity = smc_dpm_table->GthrPolarity;

	/* LED Display Settings */
	smc_pptable->LedPin0 = smc_dpm_table->LedPin0;
	smc_pptable->LedPin1 = smc_dpm_table->LedPin1;
	smc_pptable->LedPin2 = smc_dpm_table->LedPin2;
	smc_pptable->LedEnableMask = smc_dpm_table->LedEnableMask;
	smc_pptable->LedPcie = smc_dpm_table->LedPcie;
	smc_pptable->LedError = smc_dpm_table->LedError;
	smc_pptable->LedSpare1[0] = smc_dpm_table->LedSpare1[0];
	smc_pptable->LedSpare1[1] = smc_dpm_table->LedSpare1[1];

	/* GFXCLK PLL Spread Spectrum */
	smc_pptable->PllGfxclkSpreadEnabled = smc_dpm_table->PllGfxclkSpreadEnabled;
	smc_pptable->PllGfxclkSpreadPercent = smc_dpm_table->PllGfxclkSpreadPercent;
	smc_pptable->PllGfxclkSpreadFreq = smc_dpm_table->PllGfxclkSpreadFreq;

	/* GFXCLK DFLL Spread Spectrum */
	smc_pptable->DfllGfxclkSpreadEnabled = smc_dpm_table->DfllGfxclkSpreadEnabled;
	smc_pptable->DfllGfxclkSpreadPercent = smc_dpm_table->DfllGfxclkSpreadPercent;
	smc_pptable->DfllGfxclkSpreadFreq = smc_dpm_table->DfllGfxclkSpreadFreq;

	/* UCLK Spread Spectrum */
	smc_pptable->UclkSpreadEnabled = smc_dpm_table->UclkSpreadEnabled;
	smc_pptable->UclkSpreadPercent = smc_dpm_table->UclkSpreadPercent;
	smc_pptable->UclkSpreadFreq = smc_dpm_table->UclkSpreadFreq;

	/* FCLK Spred Spectrum */
	smc_pptable->FclkSpreadEnabled = smc_dpm_table->FclkSpreadEnabled;
	smc_pptable->FclkSpreadPercent = smc_dpm_table->FclkSpreadPercent;
	smc_pptable->FclkSpreadFreq = smc_dpm_table->FclkSpreadFreq;

	/* Memory Config */
	smc_pptable->MemoryChannelEnabled = smc_dpm_table->MemoryChannelEnabled;
	smc_pptable->DramBitWidth = smc_dpm_table->DramBitWidth;
	smc_pptable->PaddingMem1[0] = smc_dpm_table->PaddingMem1[0];
	smc_pptable->PaddingMem1[1] = smc_dpm_table->PaddingMem1[1];
	smc_pptable->PaddingMem1[2] = smc_dpm_table->PaddingMem1[2];

	/* Total board power */
	smc_pptable->TotalBoardPower = smc_dpm_table->TotalBoardPower;
	smc_pptable->BoardPowerPadding = smc_dpm_table->BoardPowerPadding;

	/* XGMI Training */
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++) {
		smc_pptable->XgmiLinkSpeed[i] = smc_dpm_table->XgmiLinkSpeed[i];
		smc_pptable->XgmiLinkWidth[i] = smc_dpm_table->XgmiLinkWidth[i];
		smc_pptable->XgmiFclkFreq[i] = smc_dpm_table->XgmiFclkFreq[i];
		smc_pptable->XgmiSocVoltage[i] = smc_dpm_table->XgmiSocVoltage[i];
	}

	return 0;
}

static int sienna_cichlid_store_powerplay_table(struct smu_context *smu)
{
	struct smu_11_0_powerplay_table *powerplay_table = NULL;
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_baco_context *smu_baco = &smu->smu_baco;

	if (!table_context->power_play_table)
		return -EINVAL;

	powerplay_table = table_context->power_play_table;

	memcpy(table_context->driver_pptable, &powerplay_table->smc_pptable,
	       sizeof(PPTable_t));

	table_context->thermal_controller_type = powerplay_table->thermal_controller_type;

	mutex_lock(&smu_baco->mutex);
	if (powerplay_table->platform_caps & SMU_11_0_PP_PLATFORM_CAP_BACO ||
	    powerplay_table->platform_caps & SMU_11_0_PP_PLATFORM_CAP_MACO)
		smu_baco->platform_support = true;
	mutex_unlock(&smu_baco->mutex);

	return 0;
}

static int sienna_cichlid_tables_init(struct smu_context *smu, struct smu_table *tables)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	SMU_TABLE_INIT(tables, SMU_TABLE_PPTABLE, sizeof(PPTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_OVERDRIVE, sizeof(OverDriveTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU11_TOOL_SIZE,
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_ACTIVITY_MONITOR_COEFF,
		       sizeof(DpmActivityMonitorCoeffInt_t), PAGE_SIZE,
	               AMDGPU_GEM_DOMAIN_VRAM);

	smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		return -ENOMEM;
	smu_table->metrics_time = 0;

	smu_table->watermarks_table = kzalloc(sizeof(Watermarks_t), GFP_KERNEL);
	if (!smu_table->watermarks_table)
		return -ENOMEM;

	return 0;
}

static int sienna_cichlid_get_metrics_table(struct smu_context *smu,
				    SmuMetrics_t *metrics_table)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	int ret = 0;

	mutex_lock(&smu->metrics_lock);
	if (!smu_table->metrics_time || time_after(jiffies, smu_table->metrics_time + msecs_to_jiffies(100))) {
		ret = smu_update_table(smu, SMU_TABLE_SMU_METRICS, 0,
				(void *)smu_table->metrics_table, false);
		if (ret) {
			pr_info("Failed to export SMU metrics table!\n");
			mutex_unlock(&smu->metrics_lock);
			return ret;
		}
		smu_table->metrics_time = jiffies;
	}

	memcpy(metrics_table, smu_table->metrics_table, sizeof(SmuMetrics_t));
	mutex_unlock(&smu->metrics_lock);

	return ret;
}

static int sienna_cichlid_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	if (smu_dpm->dpm_context)
		return -EINVAL;

	smu_dpm->dpm_context = kzalloc(sizeof(struct smu_11_0_dpm_context),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;

	smu_dpm->dpm_context_size = sizeof(struct smu_11_0_dpm_context);

	return 0;
}

static int sienna_cichlid_set_default_dpm_table(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	PPTable_t *driver_ppt = NULL;
	int i;

        driver_ppt = table_context->driver_pptable;

        dpm_context->dpm_tables.soc_table.min = driver_ppt->FreqTableSocclk[0];
        dpm_context->dpm_tables.soc_table.max = driver_ppt->FreqTableSocclk[NUM_SOCCLK_DPM_LEVELS - 1];

        dpm_context->dpm_tables.gfx_table.min = driver_ppt->FreqTableGfx[0];
        dpm_context->dpm_tables.gfx_table.max = driver_ppt->FreqTableGfx[NUM_GFXCLK_DPM_LEVELS - 1];

        dpm_context->dpm_tables.uclk_table.min = driver_ppt->FreqTableUclk[0];
        dpm_context->dpm_tables.uclk_table.max = driver_ppt->FreqTableUclk[NUM_UCLK_DPM_LEVELS - 1];

        dpm_context->dpm_tables.vclk_table.min = driver_ppt->FreqTableVclk[0];
        dpm_context->dpm_tables.vclk_table.max = driver_ppt->FreqTableVclk[NUM_VCLK_DPM_LEVELS - 1];

        dpm_context->dpm_tables.dclk_table.min = driver_ppt->FreqTableDclk[0];
        dpm_context->dpm_tables.dclk_table.max = driver_ppt->FreqTableDclk[NUM_DCLK_DPM_LEVELS - 1];

        dpm_context->dpm_tables.dcef_table.min = driver_ppt->FreqTableDcefclk[0];
        dpm_context->dpm_tables.dcef_table.max = driver_ppt->FreqTableDcefclk[NUM_DCEFCLK_DPM_LEVELS - 1];

        dpm_context->dpm_tables.pixel_table.min = driver_ppt->FreqTablePixclk[0];
        dpm_context->dpm_tables.pixel_table.max = driver_ppt->FreqTablePixclk[NUM_PIXCLK_DPM_LEVELS - 1];

        dpm_context->dpm_tables.display_table.min = driver_ppt->FreqTableDispclk[0];
        dpm_context->dpm_tables.display_table.max = driver_ppt->FreqTableDispclk[NUM_DISPCLK_DPM_LEVELS - 1];

        dpm_context->dpm_tables.phy_table.min = driver_ppt->FreqTablePhyclk[0];
        dpm_context->dpm_tables.phy_table.max = driver_ppt->FreqTablePhyclk[NUM_PHYCLK_DPM_LEVELS - 1];

	for (i = 0; i < MAX_PCIE_CONF; i++) {
		dpm_context->dpm_tables.pcie_table.pcie_gen[i] = driver_ppt->PcieGenSpeed[i];
		dpm_context->dpm_tables.pcie_table.pcie_lane[i] = driver_ppt->PcieLaneCount[i];
	}

	return 0;
}

static int sienna_cichlid_dpm_set_uvd_enable(struct smu_context *smu, bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	if (enable) {
		/* vcn dpm on is a prerequisite for vcn power gate messages */
		if (smu_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn, 0, NULL);
			if (ret)
				return ret;
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn, 0x10000, NULL);
			if (ret)
				return ret;
		}
		power_gate->vcn_gated = false;
	} else {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerDownVcn, 0, NULL);
			if (ret)
				return ret;
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerDownVcn, 0x10000, NULL);
			if (ret)
				return ret;
		}
		power_gate->vcn_gated = true;
	}

	return ret;
}

static int sienna_cichlid_dpm_set_jpeg_enable(struct smu_context *smu, bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	if (enable) {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerUpJpeg, 0, NULL);
			if (ret)
				return ret;
		}
		power_gate->jpeg_gated = false;
	} else {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_MM_DPM_PG_BIT)) {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerDownJpeg, 0, NULL);
			if (ret)
				return ret;
		}
		power_gate->jpeg_gated = true;
	}

	return ret;
}

static int sienna_cichlid_get_current_clk_freq_by_table(struct smu_context *smu,
				       enum smu_clk_type clk_type,
				       uint32_t *value)
{
	int ret = 0, clk_id = 0;
	SmuMetrics_t metrics;

	ret = sienna_cichlid_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0)
		return clk_id;

	*value = metrics.CurrClock[clk_id];

	return ret;
}

static bool sienna_cichlid_is_support_fine_grained_dpm(struct smu_context *smu, enum smu_clk_type clk_type)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	DpmDescriptor_t *dpm_desc = NULL;
	uint32_t clk_index = 0;

	clk_index = smu_clk_get_index(smu, clk_type);
	dpm_desc = &pptable->DpmDescriptor[clk_index];

	/* 0 - Fine grained DPM, 1 - Discrete DPM */
	return dpm_desc->SnapToDiscrete == 0 ? true : false;
}

static int sienna_cichlid_print_clk_levels(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_11_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	PPTable_t *pptable = (PPTable_t *)table_context->driver_pptable;
	int i, size = 0, ret = 0;
	uint32_t cur_value = 0, value = 0, count = 0;
	uint32_t freq_values[3] = {0};
	uint32_t mark_index = 0;
	uint32_t gen_speed, lane_width;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
	case SMU_SOCCLK:
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_FCLK:
	case SMU_DCEFCLK:
		ret = smu_get_current_clk_freq(smu, clk_type, &cur_value);
		if (ret)
			goto print_clk_out;

		/* 10KHz -> MHz */
		cur_value = cur_value / 100;

		/* no need to disable gfxoff when retrieving the current gfxclk */
		if ((clk_type == SMU_GFXCLK) || (clk_type == SMU_SCLK))
			amdgpu_gfx_off_ctrl(adev, false);

		ret = smu_get_dpm_level_count(smu, clk_type, &count);
		if (ret)
			goto print_clk_out;

		if (!sienna_cichlid_is_support_fine_grained_dpm(smu, clk_type)) {
			for (i = 0; i < count; i++) {
				ret = smu_get_dpm_freq_by_index(smu, clk_type, i, &value);
				if (ret)
					goto print_clk_out;

				size += sprintf(buf + size, "%d: %uMhz %s\n", i, value,
						cur_value == value ? "*" : "");
			}
		} else {
			ret = smu_get_dpm_freq_by_index(smu, clk_type, 0, &freq_values[0]);
			if (ret)
				goto print_clk_out;
			ret = smu_get_dpm_freq_by_index(smu, clk_type, count - 1, &freq_values[2]);
			if (ret)
				goto print_clk_out;

			freq_values[1] = cur_value;
			mark_index = cur_value == freq_values[0] ? 0 :
				     cur_value == freq_values[2] ? 2 : 1;
			if (mark_index != 1)
				freq_values[1] = (freq_values[0] + freq_values[2]) / 2;

			for (i = 0; i < 3; i++) {
				size += sprintf(buf + size, "%d: %uMhz %s\n", i, freq_values[i],
						i == mark_index ? "*" : "");
			}

		}
		break;
	case SMU_PCIE:
		gen_speed = (RREG32_PCIE(smnPCIE_LC_SPEED_CNTL) &
			     PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK)
			>> PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT;
		lane_width = (RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL) &
			      PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK)
			>> PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT;
		for (i = 0; i < NUM_LINK_LEVELS; i++)
			size += sprintf(buf + size, "%d: %s %s %dMhz %s\n", i,
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 0) ? "2.5GT/s," :
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 1) ? "5.0GT/s," :
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 2) ? "8.0GT/s," :
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 3) ? "16.0GT/s," : "",
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 1) ? "x1" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 2) ? "x2" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 3) ? "x4" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 4) ? "x8" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 5) ? "x12" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 6) ? "x16" : "",
					pptable->LclkFreq[i],
					(gen_speed == dpm_context->dpm_tables.pcie_table.pcie_gen[i]) &&
					(lane_width == dpm_context->dpm_tables.pcie_table.pcie_lane[i]) ?
					"*" : "");
		break;
	default:
		break;
	}

print_clk_out:
	if ((clk_type == SMU_GFXCLK) || (clk_type == SMU_SCLK))
		amdgpu_gfx_off_ctrl(adev, true);

	return size;
}

static int sienna_cichlid_force_clk_levels(struct smu_context *smu,
				   enum smu_clk_type clk_type, uint32_t mask)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0, size = 0;
	uint32_t soft_min_level = 0, soft_max_level = 0, min_freq = 0, max_freq = 0;

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	if ((clk_type == SMU_GFXCLK) || (clk_type == SMU_SCLK))
		amdgpu_gfx_off_ctrl(adev, false);

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
	case SMU_SOCCLK:
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_DCEFCLK:
	case SMU_FCLK:
		/* There is only 2 levels for fine grained DPM */
		if (sienna_cichlid_is_support_fine_grained_dpm(smu, clk_type)) {
			soft_max_level = (soft_max_level >= 1 ? 1 : 0);
			soft_min_level = (soft_min_level >= 1 ? 1 : 0);
		}

		ret = smu_get_dpm_freq_by_index(smu, clk_type, soft_min_level, &min_freq);
		if (ret)
			goto forec_level_out;

		ret = smu_get_dpm_freq_by_index(smu, clk_type, soft_max_level, &max_freq);
		if (ret)
			goto forec_level_out;

		ret = smu_set_soft_freq_range(smu, clk_type, min_freq, max_freq, false);
		if (ret)
			goto forec_level_out;
		break;
	default:
		break;
	}

forec_level_out:
	if ((clk_type == SMU_GFXCLK) || (clk_type == SMU_SCLK))
		amdgpu_gfx_off_ctrl(adev, true);

	return size;
}

static int sienna_cichlid_populate_umd_state_clk(struct smu_context *smu)
{
	int ret = 0;
	uint32_t min_sclk_freq = 0, min_mclk_freq = 0;

	ret = smu_get_dpm_freq_range(smu, SMU_SCLK, &min_sclk_freq, NULL, false);
	if (ret)
		return ret;

	smu->pstate_sclk = min_sclk_freq * 100;

	ret = smu_get_dpm_freq_range(smu, SMU_MCLK, &min_mclk_freq, NULL, false);
	if (ret)
		return ret;

	smu->pstate_mclk = min_mclk_freq * 100;

	return ret;
}

static int sienna_cichlid_pre_display_config_changed(struct smu_context *smu)
{
	int ret = 0;
	uint32_t max_freq = 0;

	/* Sienna_Cichlid do not support to change display num currently */
	return 0;
#if 0
	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays, 0, NULL);
	if (ret)
		return ret;
#endif

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_get_dpm_freq_range(smu, SMU_UCLK, NULL, &max_freq, false);
		if (ret)
			return ret;
		ret = smu_set_hard_freq_range(smu, SMU_UCLK, 0, max_freq);
		if (ret)
			return ret;
	}

	return ret;
}

static int sienna_cichlid_display_config_changed(struct smu_context *smu)
{
	int ret = 0;

	if ((smu->watermarks_bitmap & WATERMARKS_EXIST) &&
	    smu_feature_is_supported(smu, SMU_FEATURE_DPM_DCEFCLK_BIT) &&
	    smu_feature_is_supported(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
#if 0
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays,
						  smu->display_config->num_display,
						  NULL);
#endif
		if (ret)
			return ret;
	}

	return ret;
}

static int sienna_cichlid_force_dpm_limit_value(struct smu_context *smu, bool highest)
{
	int ret = 0, i = 0;
	uint32_t min_freq, max_freq, force_freq;
	enum smu_clk_type clk_type;

	enum smu_clk_type clks[] = {
		SMU_GFXCLK,
		SMU_MCLK,
		SMU_SOCCLK,
	};

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		clk_type = clks[i];
		ret = smu_get_dpm_freq_range(smu, clk_type, &min_freq, &max_freq, false);
		if (ret)
			return ret;

		force_freq = highest ? max_freq : min_freq;
		ret = smu_set_soft_freq_range(smu, clk_type, force_freq, force_freq, false);
		if (ret)
			return ret;
	}

	return ret;
}

static int sienna_cichlid_unforce_dpm_levels(struct smu_context *smu)
{
	int ret = 0, i = 0;
	uint32_t min_freq, max_freq;
	enum smu_clk_type clk_type;

	enum smu_clk_type clks[] = {
		SMU_GFXCLK,
		SMU_MCLK,
		SMU_SOCCLK,
	};

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		clk_type = clks[i];
		ret = smu_get_dpm_freq_range(smu, clk_type, &min_freq, &max_freq, false);
		if (ret)
			return ret;

		ret = smu_set_soft_freq_range(smu, clk_type, min_freq, max_freq, false);
		if (ret)
			return ret;
	}

	return ret;
}

static int sienna_cichlid_get_gpu_power(struct smu_context *smu, uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = sienna_cichlid_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	*value = metrics.AverageSocketPower << 8;

	return 0;
}

static int sienna_cichlid_get_current_activity_percent(struct smu_context *smu,
					       enum amd_pp_sensors sensor,
					       uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = sienna_cichlid_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		*value = metrics.AverageGfxActivity;
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		*value = metrics.AverageUclkActivity;
		break;
	default:
		pr_err("Invalid sensor for retrieving clock activity\n");
		return -EINVAL;
	}

	return 0;
}

static bool sienna_cichlid_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	uint32_t feature_mask[2];
	unsigned long feature_enabled;
	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	feature_enabled = (unsigned long)((uint64_t)feature_mask[0] |
			   ((uint64_t)feature_mask[1] << 32));
	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int sienna_cichlid_get_fan_speed_rpm(struct smu_context *smu,
				    uint32_t *speed)
{
	SmuMetrics_t metrics;
	int ret = 0;

	if (!speed)
		return -EINVAL;

	ret = sienna_cichlid_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	*speed = metrics.CurrFanSpeed;

	return ret;
}

static int sienna_cichlid_get_fan_speed_percent(struct smu_context *smu,
					uint32_t *speed)
{
	int ret = 0;
	uint32_t percent = 0;
	uint32_t current_rpm;
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	ret = sienna_cichlid_get_fan_speed_rpm(smu, &current_rpm);
	if (ret)
		return ret;

	percent = current_rpm * 100 / pptable->FanMaximumRpm;
	*speed = percent > 100 ? 100 : percent;

	return ret;
}

static int sienna_cichlid_get_power_profile_mode(struct smu_context *smu, char *buf)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	uint32_t i, size = 0;
	int16_t workload_type = 0;
	static const char *profile_name[] = {
					"BOOTUP_DEFAULT",
					"3D_FULL_SCREEN",
					"POWER_SAVING",
					"VIDEO",
					"VR",
					"COMPUTE",
					"CUSTOM"};
	static const char *title[] = {
			"PROFILE_INDEX(NAME)",
			"CLOCK_TYPE(NAME)",
			"FPS",
			"MinFreqType",
			"MinActiveFreqType",
			"MinActiveFreq",
			"BoosterFreqType",
			"BoosterFreq",
			"PD_Data_limit_c",
			"PD_Data_error_coeff",
			"PD_Data_error_rate_coeff"};
	int result = 0;

	if (!buf)
		return -EINVAL;

	size += sprintf(buf + size, "%16s %s %s %s %s %s %s %s %s %s %s\n",
			title[0], title[1], title[2], title[3], title[4], title[5],
			title[6], title[7], title[8], title[9], title[10]);

	for (i = 0; i <= PP_SMC_POWER_PROFILE_CUSTOM; i++) {
		/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
		workload_type = smu_workload_get_type(smu, i);
		if (workload_type < 0)
			return -EINVAL;

		result = smu_update_table(smu,
					  SMU_TABLE_ACTIVITY_MONITOR_COEFF, workload_type,
					  (void *)(&activity_monitor), false);
		if (result) {
			pr_err("[%s] Failed to get activity monitor!", __func__);
			return result;
		}

		size += sprintf(buf + size, "%2d %14s%s:\n",
			i, profile_name[i], (i == smu->power_profile_mode) ? "*" : " ");

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			0,
			"GFXCLK",
			activity_monitor.Gfx_FPS,
			activity_monitor.Gfx_MinFreqStep,
			activity_monitor.Gfx_MinActiveFreqType,
			activity_monitor.Gfx_MinActiveFreq,
			activity_monitor.Gfx_BoosterFreqType,
			activity_monitor.Gfx_BoosterFreq,
			activity_monitor.Gfx_PD_Data_limit_c,
			activity_monitor.Gfx_PD_Data_error_coeff,
			activity_monitor.Gfx_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			1,
			"SOCCLK",
			activity_monitor.Fclk_FPS,
			activity_monitor.Fclk_MinFreqStep,
			activity_monitor.Fclk_MinActiveFreqType,
			activity_monitor.Fclk_MinActiveFreq,
			activity_monitor.Fclk_BoosterFreqType,
			activity_monitor.Fclk_BoosterFreq,
			activity_monitor.Fclk_PD_Data_limit_c,
			activity_monitor.Fclk_PD_Data_error_coeff,
			activity_monitor.Fclk_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			2,
			"MEMLK",
			activity_monitor.Mem_FPS,
			activity_monitor.Mem_MinFreqStep,
			activity_monitor.Mem_MinActiveFreqType,
			activity_monitor.Mem_MinActiveFreq,
			activity_monitor.Mem_BoosterFreqType,
			activity_monitor.Mem_BoosterFreq,
			activity_monitor.Mem_PD_Data_limit_c,
			activity_monitor.Mem_PD_Data_error_coeff,
			activity_monitor.Mem_PD_Data_error_rate_coeff);
	}

	return size;
}

static int sienna_cichlid_set_power_profile_mode(struct smu_context *smu, long *input, uint32_t size)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	int workload_type, ret = 0;

	smu->power_profile_mode = input[size];

	if (smu->power_profile_mode > PP_SMC_POWER_PROFILE_CUSTOM) {
		pr_err("Invalid power profile mode %d\n", smu->power_profile_mode);
		return -EINVAL;
	}

	if (smu->power_profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {

		ret = smu_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF, WORKLOAD_PPLIB_CUSTOM_BIT,
				       (void *)(&activity_monitor), false);
		if (ret) {
			pr_err("[%s] Failed to get activity monitor!", __func__);
			return ret;
		}

		switch (input[0]) {
		case 0: /* Gfxclk */
			activity_monitor.Gfx_FPS = input[1];
			activity_monitor.Gfx_MinFreqStep = input[2];
			activity_monitor.Gfx_MinActiveFreqType = input[3];
			activity_monitor.Gfx_MinActiveFreq = input[4];
			activity_monitor.Gfx_BoosterFreqType = input[5];
			activity_monitor.Gfx_BoosterFreq = input[6];
			activity_monitor.Gfx_PD_Data_limit_c = input[7];
			activity_monitor.Gfx_PD_Data_error_coeff = input[8];
			activity_monitor.Gfx_PD_Data_error_rate_coeff = input[9];
			break;
		case 1: /* Socclk */
			activity_monitor.Fclk_FPS = input[1];
			activity_monitor.Fclk_MinFreqStep = input[2];
			activity_monitor.Fclk_MinActiveFreqType = input[3];
			activity_monitor.Fclk_MinActiveFreq = input[4];
			activity_monitor.Fclk_BoosterFreqType = input[5];
			activity_monitor.Fclk_BoosterFreq = input[6];
			activity_monitor.Fclk_PD_Data_limit_c = input[7];
			activity_monitor.Fclk_PD_Data_error_coeff = input[8];
			activity_monitor.Fclk_PD_Data_error_rate_coeff = input[9];
			break;
		case 2: /* Memlk */
			activity_monitor.Mem_FPS = input[1];
			activity_monitor.Mem_MinFreqStep = input[2];
			activity_monitor.Mem_MinActiveFreqType = input[3];
			activity_monitor.Mem_MinActiveFreq = input[4];
			activity_monitor.Mem_BoosterFreqType = input[5];
			activity_monitor.Mem_BoosterFreq = input[6];
			activity_monitor.Mem_PD_Data_limit_c = input[7];
			activity_monitor.Mem_PD_Data_error_coeff = input[8];
			activity_monitor.Mem_PD_Data_error_rate_coeff = input[9];
			break;
		}

		ret = smu_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF, WORKLOAD_PPLIB_CUSTOM_BIT,
				       (void *)(&activity_monitor), true);
		if (ret) {
			pr_err("[%s] Failed to set activity monitor!", __func__);
			return ret;
		}
	}

	/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
	workload_type = smu_workload_get_type(smu, smu->power_profile_mode);
	if (workload_type < 0)
		return -EINVAL;
	smu_send_smc_msg_with_param(smu, SMU_MSG_SetWorkloadMask,
				    1 << workload_type, NULL);

	return ret;
}

static int sienna_cichlid_get_profiling_clk_mask(struct smu_context *smu,
					 enum amd_dpm_forced_level level,
					 uint32_t *sclk_mask,
					 uint32_t *mclk_mask,
					 uint32_t *soc_mask)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;
	uint32_t level_count = 0;

	if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) {
		if (sclk_mask)
			*sclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
		if (mclk_mask)
			*mclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
		if(sclk_mask) {
			amdgpu_gfx_off_ctrl(adev, false);
			ret = smu_get_dpm_level_count(smu, SMU_SCLK, &level_count);
			amdgpu_gfx_off_ctrl(adev, true);
			if (ret)
				return ret;
			*sclk_mask = level_count - 1;
		}

		if(mclk_mask) {
			ret = smu_get_dpm_level_count(smu, SMU_MCLK, &level_count);
			if (ret)
				return ret;
			*mclk_mask = level_count - 1;
		}

		if(soc_mask) {
			ret = smu_get_dpm_level_count(smu, SMU_SOCCLK, &level_count);
			if (ret)
				return ret;
			*soc_mask = level_count - 1;
		}
	}

	return ret;
}

static int sienna_cichlid_notify_smc_display_config(struct smu_context *smu)
{
	struct smu_clocks min_clocks = {0};
	struct pp_display_clock_request clock_req;
	int ret = 0;

	min_clocks.dcef_clock = smu->display_config->min_dcef_set_clk;
	min_clocks.dcef_clock_in_sr = smu->display_config->min_dcef_deep_sleep_set_clk;
	min_clocks.memory_clock = smu->display_config->min_mem_set_clock;

	if (smu_feature_is_supported(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = min_clocks.dcef_clock * 10;

		ret = smu_v11_0_display_clock_voltage_request(smu, &clock_req);
		if (!ret) {
			if (smu_feature_is_supported(smu, SMU_FEATURE_DS_DCEFCLK_BIT)) {
				ret = smu_send_smc_msg_with_param(smu,
								  SMU_MSG_SetMinDeepSleepDcefclk,
								  min_clocks.dcef_clock_in_sr/100,
								  NULL);
				if (ret) {
					pr_err("Attempt to set divider for DCEFCLK Failed!");
					return ret;
				}
			}
		} else {
			pr_info("Attempt to set Hard Min for DCEFCLK Failed!");
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_set_hard_freq_range(smu, SMU_UCLK, min_clocks.memory_clock/100, 0);
		if (ret) {
			pr_err("[%s] Set hard min uclk failed!", __func__);
			return ret;
		}
	}

	return 0;
}

static int sienna_cichlid_set_watermarks_table(struct smu_context *smu,
				       void *watermarks, struct
				       dm_pp_wm_sets_with_clock_ranges_soc15
				       *clock_ranges)
{
	int i;
	int ret = 0;
	Watermarks_t *table = watermarks;

	if (!table || !clock_ranges)
		return -EINVAL;

	if (clock_ranges->num_wm_dmif_sets > 4 ||
	    clock_ranges->num_wm_mcif_sets > 4)
                return -EINVAL;

        for (i = 0; i < clock_ranges->num_wm_dmif_sets; i++) {
		table->WatermarkRow[1][i].MinClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_dcfclk_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MaxClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_dcfclk_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MinUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_mem_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MaxUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_mem_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].WmSetting = (uint8_t)
				clock_ranges->wm_dmif_clocks_ranges[i].wm_set_id;
        }

	for (i = 0; i < clock_ranges->num_wm_mcif_sets; i++) {
		table->WatermarkRow[0][i].MinClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_socclk_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MaxClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_socclk_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MinUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_mem_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MaxUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_mem_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].WmSetting = (uint8_t)
				clock_ranges->wm_mcif_clocks_ranges[i].wm_set_id;
        }

	smu->watermarks_bitmap |= WATERMARKS_EXIST;

	if (!(smu->watermarks_bitmap & WATERMARKS_LOADED)) {
		ret = smu_write_watermarks_table(smu);
		if (ret) {
			pr_err("Failed to update WMTABLE!");
			return ret;
		}
		smu->watermarks_bitmap |= WATERMARKS_LOADED;
	}

	return 0;
}

static int sienna_cichlid_thermal_get_temperature(struct smu_context *smu,
					     enum amd_pp_sensors sensor,
					     uint32_t *value)
{
	SmuMetrics_t metrics;
	int ret = 0;

	if (!value)
		return -EINVAL;

	ret = sienna_cichlid_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		*value = metrics.TemperatureHotspot *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		*value = metrics.TemperatureEdge *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		*value = metrics.TemperatureMem *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	default:
		pr_err("Invalid sensor for retrieving temp\n");
		return -EINVAL;
	}

	return 0;
}

static int sienna_cichlid_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size)
{
	int ret = 0;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;

	if(!data || !size)
		return -EINVAL;

	mutex_lock(&smu->sensor_lock);
	switch (sensor) {
	case AMDGPU_PP_SENSOR_MAX_FAN_RPM:
		*(uint32_t *)data = pptable->FanMaximumRpm;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = sienna_cichlid_get_current_activity_percent(smu, sensor, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = sienna_cichlid_get_gpu_power(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = sienna_cichlid_thermal_get_temperature(smu, sensor, (uint32_t *)data);
		*size = 4;
		break;
	default:
		ret = smu_v11_0_read_sensor(smu, sensor, data, size);
	}
	mutex_unlock(&smu->sensor_lock);

	return ret;
}

static int sienna_cichlid_get_uclk_dpm_states(struct smu_context *smu, uint32_t *clocks_in_khz, uint32_t *num_states)
{
	uint32_t num_discrete_levels = 0;
	uint16_t *dpm_levels = NULL;
	uint16_t i = 0;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *driver_ppt = NULL;

	if (!clocks_in_khz || !num_states || !table_context->driver_pptable)
		return -EINVAL;

	driver_ppt = table_context->driver_pptable;
	num_discrete_levels = driver_ppt->DpmDescriptor[PPCLK_UCLK].NumDiscreteLevels;
	dpm_levels = driver_ppt->FreqTableUclk;

	if (num_discrete_levels == 0 || dpm_levels == NULL)
		return -EINVAL;

	*num_states = num_discrete_levels;
	for (i = 0; i < num_discrete_levels; i++) {
		/* convert to khz */
		*clocks_in_khz = (*dpm_levels) * 1000;
		clocks_in_khz++;
		dpm_levels++;
	}

	return 0;
}

static int sienna_cichlid_set_performance_level(struct smu_context *smu,
					enum amd_dpm_forced_level level);

static int sienna_cichlid_set_standard_performance_level(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;
	uint32_t sclk_freq = 0, uclk_freq = 0;

	switch (adev->asic_type) {
	/* TODO: need to set specify clk value by asic type, not support yet*/
	default:
		/* by default, this is same as auto performance level */
		return sienna_cichlid_set_performance_level(smu, AMD_DPM_FORCED_LEVEL_AUTO);
	}

	ret = smu_set_soft_freq_range(smu, SMU_SCLK, sclk_freq, sclk_freq, false);
	if (ret)
		return ret;
	ret = smu_set_soft_freq_range(smu, SMU_UCLK, uclk_freq, uclk_freq, false);
	if (ret)
		return ret;

	return ret;
}

static int sienna_cichlid_set_peak_performance_level(struct smu_context *smu)
{
	int ret = 0;

	/* TODO: not support yet*/
	return ret;
}

static int sienna_cichlid_set_performance_level(struct smu_context *smu,
					enum amd_dpm_forced_level level)
{
	int ret = 0;
	uint32_t sclk_mask, mclk_mask, soc_mask;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = smu_force_dpm_limit_value(smu, true);
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = smu_force_dpm_limit_value(smu, false);
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = smu_unforce_dpm_levels(smu);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
		ret = sienna_cichlid_set_standard_performance_level(smu);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
		ret = smu_get_profiling_clk_mask(smu, level,
						 &sclk_mask,
						 &mclk_mask,
						 &soc_mask);
		if (ret)
			return ret;
		smu_force_clk_levels(smu, SMU_SCLK, 1 << sclk_mask, false);
		smu_force_clk_levels(smu, SMU_MCLK, 1 << mclk_mask, false);
		smu_force_clk_levels(smu, SMU_SOCCLK, 1 << soc_mask, false);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		ret = sienna_cichlid_set_peak_performance_level(smu);
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}
	return ret;
}

static int sienna_cichlid_get_thermal_temperature_range(struct smu_context *smu,
						struct smu_temperature_range *range)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_powerplay_table *powerplay_table = table_context->power_play_table;

	if (!range || !powerplay_table)
		return -EINVAL;

	range->max = powerplay_table->software_shutdown_temp *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return 0;
}

static int sienna_cichlid_display_disable_memory_clock_switch(struct smu_context *smu,
						bool disable_memory_clock_switch)
{
	int ret = 0;
	struct smu_11_0_max_sustainable_clocks *max_sustainable_clocks =
		(struct smu_11_0_max_sustainable_clocks *)
			smu->smu_table.max_sustainable_clocks;
	uint32_t min_memory_clock = smu->hard_min_uclk_req_from_dal;
	uint32_t max_memory_clock = max_sustainable_clocks->uclock;

	if(smu->disable_uclk_switch == disable_memory_clock_switch)
		return 0;

	if(disable_memory_clock_switch)
		ret = smu_set_hard_freq_range(smu, SMU_UCLK, max_memory_clock, 0);
	else
		ret = smu_set_hard_freq_range(smu, SMU_UCLK, min_memory_clock, 0);

	if(!ret)
		smu->disable_uclk_switch = disable_memory_clock_switch;

	return ret;
}

static uint32_t sienna_cichlid_get_pptable_power_limit(struct smu_context *smu)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	return pptable->SocketPowerLimitAc[PPT_THROTTLER_PPT0];
}

static int sienna_cichlid_get_power_limit(struct smu_context *smu,
				     uint32_t *limit,
				     bool cap)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	uint32_t asic_default_power_limit = 0;
	int ret = 0;
	int power_src;

	if (!smu->power_limit) {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_PPT_BIT)) {
			power_src = smu_power_get_index(smu, SMU_POWER_SOURCE_AC);
			if (power_src < 0)
				return -EINVAL;

			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetPptLimit,
							  power_src << 16, &asic_default_power_limit);
			if (ret) {
				pr_err("[%s] get PPT limit failed!", __func__);
				return ret;
			}
		} else {
			/* the last hope to figure out the ppt limit */
			if (!pptable) {
				pr_err("Cannot get PPT limit due to pptable missing!");
				return -EINVAL;
			}
			asic_default_power_limit =
				pptable->SocketPowerLimitAc[PPT_THROTTLER_PPT0];
		}

		smu->power_limit = asic_default_power_limit;
	}

	if (cap)
		*limit = smu_v11_0_get_max_power_limit(smu);
	else
		*limit = smu->power_limit;

	return 0;
}

static int sienna_cichlid_update_pcie_parameters(struct smu_context *smu,
					 uint32_t pcie_gen_cap,
					 uint32_t pcie_width_cap)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	int ret, i;
	uint32_t smu_pcie_arg;

	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_11_0_dpm_context *dpm_context = smu_dpm->dpm_context;

	for (i = 0; i < NUM_LINK_LEVELS; i++) {
		smu_pcie_arg = (i << 16) |
			((pptable->PcieGenSpeed[i] <= pcie_gen_cap) ?
					(pptable->PcieGenSpeed[i] << 8) :
					(pcie_gen_cap << 8)) |
			((pptable->PcieLaneCount[i] <= pcie_width_cap) ?
					pptable->PcieLaneCount[i] :
					pcie_width_cap);

		ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_OverridePcieParameters,
					  smu_pcie_arg,
					  NULL);

		if (ret)
			return ret;

		if (pptable->PcieGenSpeed[i] > pcie_gen_cap)
			dpm_context->dpm_tables.pcie_table.pcie_gen[i] = pcie_gen_cap;
		if (pptable->PcieLaneCount[i] > pcie_width_cap)
			dpm_context->dpm_tables.pcie_table.pcie_lane[i] = pcie_width_cap;
	}

	return 0;
}

int sienna_cichlid_get_dpm_ultimate_freq(struct smu_context *smu,
				enum smu_clk_type clk_type,
				uint32_t *min, uint32_t *max)
{
	struct amdgpu_device *adev = smu->adev;
	int ret;

	if (clk_type == SMU_GFXCLK)
		amdgpu_gfx_off_ctrl(adev, false);
	ret = smu_v11_0_get_dpm_ultimate_freq(smu, clk_type, min, max);
	if (clk_type == SMU_GFXCLK)
		amdgpu_gfx_off_ctrl(adev, true);

	return ret;
}

int sienna_cichlid_set_soft_freq_limited_range(struct smu_context *smu,
				      enum smu_clk_type clk_type,
				      uint32_t min, uint32_t max)
{
	struct amdgpu_device *adev = smu->adev;
	int ret;

	if (clk_type == SMU_GFXCLK)
		amdgpu_gfx_off_ctrl(adev, false);
	ret = smu_v11_0_set_soft_freq_limited_range(smu, clk_type, min, max);
	if (clk_type == SMU_GFXCLK)
		amdgpu_gfx_off_ctrl(adev, true);

	return ret;
}

static bool sienna_cichlid_is_baco_supported(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t val;

	if (!smu_v11_0_baco_is_support(smu))
		return false;

	val = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP0);
	return (val & RCC_BIF_STRAP0__STRAP_PX_CAPABLE_MASK) ? true : false;
}

static void sienna_cichlid_dump_pptable(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;
	int i;

	pr_info("Dumped PPTable:\n");

	pr_info("Version = 0x%08x\n", pptable->Version);
	pr_info("FeaturesToRun[0] = 0x%08x\n", pptable->FeaturesToRun[0]);
	pr_info("FeaturesToRun[1] = 0x%08x\n", pptable->FeaturesToRun[1]);

	for (i = 0; i < PPT_THROTTLER_COUNT; i++) {
		pr_info("SocketPowerLimitAc[%d] = 0x%x\n", i, pptable->SocketPowerLimitAc[i]);
		pr_info("SocketPowerLimitAcTau[%d] = 0x%x\n", i, pptable->SocketPowerLimitAcTau[i]);
		pr_info("SocketPowerLimitDc[%d] = 0x%x\n", i, pptable->SocketPowerLimitDc[i]);
		pr_info("SocketPowerLimitDcTau[%d] = 0x%x\n", i, pptable->SocketPowerLimitDcTau[i]);
	}

	for (i = 0; i < TDC_THROTTLER_COUNT; i++) {
		pr_info("TdcLimit[%d] = 0x%x\n", i, pptable->TdcLimit[i]);
		pr_info("TdcLimitTau[%d] = 0x%x\n", i, pptable->TdcLimitTau[i]);
	}

	for (i = 0; i < TEMP_COUNT; i++) {
		pr_info("TemperatureLimit[%d] = 0x%x\n", i, pptable->TemperatureLimit[i]);
	}

	pr_info("FitLimit = 0x%x\n", pptable->FitLimit);
	pr_info("TotalPowerConfig = 0x%x\n", pptable->TotalPowerConfig);
	pr_info("TotalPowerPadding[0] = 0x%x\n", pptable->TotalPowerPadding[0]);
	pr_info("TotalPowerPadding[1] = 0x%x\n", pptable->TotalPowerPadding[1]);
	pr_info("TotalPowerPadding[2] = 0x%x\n", pptable->TotalPowerPadding[2]);

	pr_info("ApccPlusResidencyLimit = 0x%x\n", pptable->ApccPlusResidencyLimit);
	for (i = 0; i < NUM_SMNCLK_DPM_LEVELS; i++) {
		pr_info("SmnclkDpmFreq[%d] = 0x%x\n", i, pptable->SmnclkDpmFreq[i]);
		pr_info("SmnclkDpmVoltage[%d] = 0x%x\n", i, pptable->SmnclkDpmVoltage[i]);
	}
	pr_info("PaddingAPCC[0] = 0x%x\n", pptable->PaddingAPCC[0]);
	pr_info("PaddingAPCC[1] = 0x%x\n", pptable->PaddingAPCC[1]);
	pr_info("PaddingAPCC[2] = 0x%x\n", pptable->PaddingAPCC[2]);
	pr_info("PaddingAPCC[3] = 0x%x\n", pptable->PaddingAPCC[3]);

	pr_info("ThrottlerControlMask = 0x%x\n", pptable->ThrottlerControlMask);

	pr_info("FwDStateMask = 0x%x\n", pptable->FwDStateMask);

	pr_info("UlvVoltageOffsetSoc = 0x%x\n", pptable->UlvVoltageOffsetSoc);
	pr_info("UlvVoltageOffsetGfx = 0x%x\n", pptable->UlvVoltageOffsetGfx);
	pr_info("MinVoltageUlvGfx = 0x%x\n", pptable->MinVoltageUlvGfx);
	pr_info("MinVoltageUlvSoc = 0x%x\n", pptable->MinVoltageUlvSoc);

	pr_info("SocLIVmin = 0x%x\n", pptable->SocLIVmin);
	pr_info("PaddingLIVmin = 0x%x\n", pptable->PaddingLIVmin);

	pr_info("GceaLinkMgrIdleThreshold = 0x%x\n", pptable->GceaLinkMgrIdleThreshold);
	pr_info("paddingRlcUlvParams[0] = 0x%x\n", pptable->paddingRlcUlvParams[0]);
	pr_info("paddingRlcUlvParams[1] = 0x%x\n", pptable->paddingRlcUlvParams[1]);
	pr_info("paddingRlcUlvParams[2] = 0x%x\n", pptable->paddingRlcUlvParams[2]);

	pr_info("MinVoltageGfx = 0x%x\n", pptable->MinVoltageGfx);
	pr_info("MinVoltageSoc = 0x%x\n", pptable->MinVoltageSoc);
	pr_info("MaxVoltageGfx = 0x%x\n", pptable->MaxVoltageGfx);
	pr_info("MaxVoltageSoc = 0x%x\n", pptable->MaxVoltageSoc);

	pr_info("LoadLineResistanceGfx = 0x%x\n", pptable->LoadLineResistanceGfx);
	pr_info("LoadLineResistanceSoc = 0x%x\n", pptable->LoadLineResistanceSoc);

	pr_info("VDDGFX_TVmin = 0x%x\n", pptable->VDDGFX_TVmin);
	pr_info("VDDSOC_TVmin = 0x%x\n", pptable->VDDSOC_TVmin);
	pr_info("VDDGFX_Vmin_HiTemp = 0x%x\n", pptable->VDDGFX_Vmin_HiTemp);
	pr_info("VDDGFX_Vmin_LoTemp = 0x%x\n", pptable->VDDGFX_Vmin_LoTemp);
	pr_info("VDDSOC_Vmin_HiTemp = 0x%x\n", pptable->VDDSOC_Vmin_HiTemp);
	pr_info("VDDSOC_Vmin_LoTemp = 0x%x\n", pptable->VDDSOC_Vmin_LoTemp);
	pr_info("VDDGFX_TVminHystersis = 0x%x\n", pptable->VDDGFX_TVminHystersis);
	pr_info("VDDSOC_TVminHystersis = 0x%x\n", pptable->VDDSOC_TVminHystersis);

	pr_info("[PPCLK_GFXCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_GFXCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_GFXCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_GFXCLK].Padding,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_GFXCLK].Padding16);

	pr_info("[PPCLK_SOCCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_SOCCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_SOCCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_SOCCLK].Padding,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_SOCCLK].Padding16);

	pr_info("[PPCLK_UCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_UCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_UCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_UCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_UCLK].Padding,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_UCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_UCLK].Padding16);

	pr_info("[PPCLK_FCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_FCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_FCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_FCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_FCLK].Padding,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_FCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_FCLK].Padding16);

	pr_info("[PPCLK_DCLK_0]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_DCLK_0].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DCLK_0].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DCLK_0].Padding,
			pptable->DpmDescriptor[PPCLK_DCLK_0].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DCLK_0].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_DCLK_0].SsFmin,
			pptable->DpmDescriptor[PPCLK_DCLK_0].Padding16);

	pr_info("[PPCLK_VCLK_0]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_VCLK_0].VoltageMode,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_VCLK_0].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_VCLK_0].Padding,
			pptable->DpmDescriptor[PPCLK_VCLK_0].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_VCLK_0].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_VCLK_0].SsFmin,
			pptable->DpmDescriptor[PPCLK_VCLK_0].Padding16);

	pr_info("[PPCLK_DCLK_1]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_DCLK_1].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DCLK_1].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DCLK_1].Padding,
			pptable->DpmDescriptor[PPCLK_DCLK_1].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DCLK_1].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_DCLK_1].SsFmin,
			pptable->DpmDescriptor[PPCLK_DCLK_1].Padding16);

	pr_info("[PPCLK_VCLK_1]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_VCLK_1].VoltageMode,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_VCLK_1].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_VCLK_1].Padding,
			pptable->DpmDescriptor[PPCLK_VCLK_1].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_VCLK_1].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_VCLK_1].SsFmin,
			pptable->DpmDescriptor[PPCLK_VCLK_1].Padding16);

	pr_info("FreqTableGfx\n");
	for (i = 0; i < NUM_GFXCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = 0x%x\n", i, pptable->FreqTableGfx[i]);

	pr_info("FreqTableVclk\n");
	for (i = 0; i < NUM_VCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = 0x%x\n", i, pptable->FreqTableVclk[i]);

	pr_info("FreqTableDclk\n");
	for (i = 0; i < NUM_DCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = 0x%x\n", i, pptable->FreqTableDclk[i]);

	pr_info("FreqTableSocclk\n");
	for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = 0x%x\n", i, pptable->FreqTableSocclk[i]);

	pr_info("FreqTableUclk\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = 0x%x\n", i, pptable->FreqTableUclk[i]);

	pr_info("FreqTableFclk\n");
	for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = 0x%x\n", i, pptable->FreqTableFclk[i]);

	pr_info("Paddingclks[0] = 0x%x\n",  pptable->Paddingclks[0]);
	pr_info("Paddingclks[1] = 0x%x\n",  pptable->Paddingclks[1]);
	pr_info("Paddingclks[2] = 0x%x\n",  pptable->Paddingclks[2]);
	pr_info("Paddingclks[3] = 0x%x\n",  pptable->Paddingclks[3]);
	pr_info("Paddingclks[4] = 0x%x\n",  pptable->Paddingclks[4]);
	pr_info("Paddingclks[5] = 0x%x\n",  pptable->Paddingclks[5]);
	pr_info("Paddingclks[6] = 0x%x\n",  pptable->Paddingclks[6]);
	pr_info("Paddingclks[7] = 0x%x\n",  pptable->Paddingclks[7]);
	pr_info("Paddingclks[8] = 0x%x\n",  pptable->Paddingclks[8]);
	pr_info("Paddingclks[9] = 0x%x\n",  pptable->Paddingclks[9]);
	pr_info("Paddingclks[10] = 0x%x\n", pptable->Paddingclks[10]);
	pr_info("Paddingclks[11] = 0x%x\n", pptable->Paddingclks[11]);
	pr_info("Paddingclks[12] = 0x%x\n", pptable->Paddingclks[12]);
	pr_info("Paddingclks[13] = 0x%x\n", pptable->Paddingclks[13]);
	pr_info("Paddingclks[14] = 0x%x\n", pptable->Paddingclks[14]);
	pr_info("Paddingclks[15] = 0x%x\n", pptable->Paddingclks[15]);

	pr_info("DcModeMaxFreq\n");
	pr_info("  .PPCLK_GFXCLK = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_GFXCLK]);
	pr_info("  .PPCLK_SOCCLK = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_SOCCLK]);
	pr_info("  .PPCLK_UCLK   = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_UCLK]);
	pr_info("  .PPCLK_FCLK   = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_FCLK]);
	pr_info("  .PPCLK_DCLK_0 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_DCLK_0]);
	pr_info("  .PPCLK_VCLK_0 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_VCLK_0]);
	pr_info("  .PPCLK_DCLK_1 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_DCLK_1]);
	pr_info("  .PPCLK_VCLK_1 = 0x%x\n", pptable->DcModeMaxFreq[PPCLK_VCLK_1]);

	pr_info("FreqTableUclkDiv\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->FreqTableUclkDiv[i]);

	pr_info("FclkBoostFreq = 0x%x\n", pptable->FclkBoostFreq);
	pr_info("FclkParamPadding = 0x%x\n", pptable->FclkParamPadding);

	pr_info("Mp0clkFreq\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->Mp0clkFreq[i]);

	pr_info("Mp0DpmVoltage\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->Mp0DpmVoltage[i]);

	pr_info("MemVddciVoltage\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->MemVddciVoltage[i]);

	pr_info("MemMvddVoltage\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->MemMvddVoltage[i]);

	pr_info("GfxclkFgfxoffEntry = 0x%x\n", pptable->GfxclkFgfxoffEntry);
	pr_info("GfxclkFinit = 0x%x\n", pptable->GfxclkFinit);
	pr_info("GfxclkFidle = 0x%x\n", pptable->GfxclkFidle);
	pr_info("GfxclkSource = 0x%x\n", pptable->GfxclkSource);
	pr_info("GfxclkPadding = 0x%x\n", pptable->GfxclkPadding);

	pr_info("GfxGpoSubFeatureMask = 0x%x\n", pptable->GfxGpoSubFeatureMask);

	pr_info("GfxGpoEnabledWorkPolicyMask = 0x%x\n", pptable->GfxGpoEnabledWorkPolicyMask);
	pr_info("GfxGpoDisabledWorkPolicyMask = 0x%x\n", pptable->GfxGpoDisabledWorkPolicyMask);
	pr_info("GfxGpoPadding[0] = 0x%x\n", pptable->GfxGpoPadding[0]);
	pr_info("GfxGpoVotingAllow = 0x%x\n", pptable->GfxGpoVotingAllow);
	pr_info("GfxGpoPadding32[0] = 0x%x\n", pptable->GfxGpoPadding32[0]);
	pr_info("GfxGpoPadding32[1] = 0x%x\n", pptable->GfxGpoPadding32[1]);
	pr_info("GfxGpoPadding32[2] = 0x%x\n", pptable->GfxGpoPadding32[2]);
	pr_info("GfxGpoPadding32[3] = 0x%x\n", pptable->GfxGpoPadding32[3]);
	pr_info("GfxDcsFopt = 0x%x\n", pptable->GfxDcsFopt);
	pr_info("GfxDcsFclkFopt = 0x%x\n", pptable->GfxDcsFclkFopt);
	pr_info("GfxDcsUclkFopt = 0x%x\n", pptable->GfxDcsUclkFopt);

	pr_info("DcsGfxOffVoltage = 0x%x\n", pptable->DcsGfxOffVoltage);
	pr_info("DcsMinGfxOffTime = 0x%x\n", pptable->DcsMinGfxOffTime);
	pr_info("DcsMaxGfxOffTime = 0x%x\n", pptable->DcsMaxGfxOffTime);
	pr_info("DcsMinCreditAccum = 0x%x\n", pptable->DcsMinCreditAccum);
	pr_info("DcsExitHysteresis = 0x%x\n", pptable->DcsExitHysteresis);
	pr_info("DcsTimeout = 0x%x\n", pptable->DcsTimeout);

	pr_info("DcsParamPadding[0] = 0x%x\n", pptable->DcsParamPadding[0]);
	pr_info("DcsParamPadding[1] = 0x%x\n", pptable->DcsParamPadding[1]);
	pr_info("DcsParamPadding[2] = 0x%x\n", pptable->DcsParamPadding[2]);
	pr_info("DcsParamPadding[3] = 0x%x\n", pptable->DcsParamPadding[3]);
	pr_info("DcsParamPadding[4] = 0x%x\n", pptable->DcsParamPadding[4]);

	pr_info("FlopsPerByteTable\n");
	for (i = 0; i < RLC_PACE_TABLE_NUM_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->FlopsPerByteTable[i]);

	pr_info("LowestUclkReservedForUlv = 0x%x\n", pptable->LowestUclkReservedForUlv);
	pr_info("vddingMem[0] = 0x%x\n", pptable->PaddingMem[0]);
	pr_info("vddingMem[1] = 0x%x\n", pptable->PaddingMem[1]);
	pr_info("vddingMem[2] = 0x%x\n", pptable->PaddingMem[2]);

	pr_info("UclkDpmPstates\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->UclkDpmPstates[i]);

	pr_info("UclkDpmSrcFreqRange\n");
	pr_info("  .Fmin = 0x%x\n",
		pptable->UclkDpmSrcFreqRange.Fmin);
	pr_info("  .Fmax = 0x%x\n",
		pptable->UclkDpmSrcFreqRange.Fmax);
	pr_info("UclkDpmTargFreqRange\n");
	pr_info("  .Fmin = 0x%x\n",
		pptable->UclkDpmTargFreqRange.Fmin);
	pr_info("  .Fmax = 0x%x\n",
		pptable->UclkDpmTargFreqRange.Fmax);
	pr_info("UclkDpmMidstepFreq = 0x%x\n", pptable->UclkDpmMidstepFreq);
	pr_info("UclkMidstepPadding = 0x%x\n", pptable->UclkMidstepPadding);

	pr_info("PcieGenSpeed\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->PcieGenSpeed[i]);

	pr_info("PcieLaneCount\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->PcieLaneCount[i]);

	pr_info("LclkFreq\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->LclkFreq[i]);

	pr_info("FanStopTemp = 0x%x\n", pptable->FanStopTemp);
	pr_info("FanStartTemp = 0x%x\n", pptable->FanStartTemp);

	pr_info("FanGain\n");
	for (i = 0; i < TEMP_COUNT; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->FanGain[i]);

	pr_info("FanPwmMin = 0x%x\n", pptable->FanPwmMin);
	pr_info("FanAcousticLimitRpm = 0x%x\n", pptable->FanAcousticLimitRpm);
	pr_info("FanThrottlingRpm = 0x%x\n", pptable->FanThrottlingRpm);
	pr_info("FanMaximumRpm = 0x%x\n", pptable->FanMaximumRpm);
	pr_info("MGpuFanBoostLimitRpm = 0x%x\n", pptable->MGpuFanBoostLimitRpm);
	pr_info("FanTargetTemperature = 0x%x\n", pptable->FanTargetTemperature);
	pr_info("FanTargetGfxclk = 0x%x\n", pptable->FanTargetGfxclk);
	pr_info("FanPadding16 = 0x%x\n", pptable->FanPadding16);
	pr_info("FanTempInputSelect = 0x%x\n", pptable->FanTempInputSelect);
	pr_info("FanPadding = 0x%x\n", pptable->FanPadding);
	pr_info("FanZeroRpmEnable = 0x%x\n", pptable->FanZeroRpmEnable);
	pr_info("FanTachEdgePerRev = 0x%x\n", pptable->FanTachEdgePerRev);

	pr_info("FuzzyFan_ErrorSetDelta = 0x%x\n", pptable->FuzzyFan_ErrorSetDelta);
	pr_info("FuzzyFan_ErrorRateSetDelta = 0x%x\n", pptable->FuzzyFan_ErrorRateSetDelta);
	pr_info("FuzzyFan_PwmSetDelta = 0x%x\n", pptable->FuzzyFan_PwmSetDelta);
	pr_info("FuzzyFan_Reserved = 0x%x\n", pptable->FuzzyFan_Reserved);

	pr_info("OverrideAvfsGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_GFX]);
	pr_info("OverrideAvfsGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_SOC]);
	pr_info("dBtcGbGfxDfllModelSelect = 0x%x\n", pptable->dBtcGbGfxDfllModelSelect);
	pr_info("Padding8_Avfs = 0x%x\n", pptable->Padding8_Avfs);

	pr_info("qAvfsGb[AVFS_VOLTAGE_GFX]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].a,
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].b,
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].c);
	pr_info("qAvfsGb[AVFS_VOLTAGE_SOC]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].a,
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].b,
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].c);
	pr_info("dBtcGbGfxPll{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxPll.a,
			pptable->dBtcGbGfxPll.b,
			pptable->dBtcGbGfxPll.c);
	pr_info("dBtcGbGfxAfll{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxDfll.a,
			pptable->dBtcGbGfxDfll.b,
			pptable->dBtcGbGfxDfll.c);
	pr_info("dBtcGbSoc{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbSoc.a,
			pptable->dBtcGbSoc.b,
			pptable->dBtcGbSoc.c);
	pr_info("qAgingGb[AVFS_VOLTAGE_GFX]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].m,
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].b);
	pr_info("qAgingGb[AVFS_VOLTAGE_SOC]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].m,
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].b);

	pr_info("PiecewiseLinearDroopIntGfxDfll\n");
	for (i = 0; i < NUM_PIECE_WISE_LINEAR_DROOP_MODEL_VF_POINTS; i++) {
		pr_info("		Fset[%d] = 0x%x\n",
			i, pptable->PiecewiseLinearDroopIntGfxDfll.Fset[i]);
		pr_info("		Vdroop[%d] = 0x%x\n",
			i, pptable->PiecewiseLinearDroopIntGfxDfll.Vdroop[i]);
	}

	pr_info("qStaticVoltageOffset[AVFS_VOLTAGE_GFX]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].c);
	pr_info("qStaticVoltageOffset[AVFS_VOLTAGE_SOC]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].c);

	pr_info("DcTol[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_GFX]);
	pr_info("DcTol[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_SOC]);

	pr_info("DcBtcEnabled[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_GFX]);
	pr_info("DcBtcEnabled[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_SOC]);
	pr_info("Padding8_GfxBtc[0] = 0x%x\n", pptable->Padding8_GfxBtc[0]);
	pr_info("Padding8_GfxBtc[1] = 0x%x\n", pptable->Padding8_GfxBtc[1]);

	pr_info("DcBtcMin[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_GFX]);
	pr_info("DcBtcMin[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_SOC]);
	pr_info("DcBtcMax[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_GFX]);
	pr_info("DcBtcMax[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_SOC]);

	pr_info("DcBtcGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_GFX]);
	pr_info("DcBtcGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_SOC]);

	pr_info("XgmiDpmPstates\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->XgmiDpmPstates[i]);
	pr_info("XgmiDpmSpare[0] = 0x%02x\n", pptable->XgmiDpmSpare[0]);
	pr_info("XgmiDpmSpare[1] = 0x%02x\n", pptable->XgmiDpmSpare[1]);

	pr_info("DebugOverrides = 0x%x\n", pptable->DebugOverrides);
	pr_info("ReservedEquation0{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation0.a,
			pptable->ReservedEquation0.b,
			pptable->ReservedEquation0.c);
	pr_info("ReservedEquation1{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation1.a,
			pptable->ReservedEquation1.b,
			pptable->ReservedEquation1.c);
	pr_info("ReservedEquation2{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation2.a,
			pptable->ReservedEquation2.b,
			pptable->ReservedEquation2.c);
	pr_info("ReservedEquation3{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation3.a,
			pptable->ReservedEquation3.b,
			pptable->ReservedEquation3.c);

	pr_info("SkuReserved[0] = 0x%x\n", pptable->SkuReserved[0]);
	pr_info("SkuReserved[1] = 0x%x\n", pptable->SkuReserved[1]);
	pr_info("SkuReserved[2] = 0x%x\n", pptable->SkuReserved[2]);
	pr_info("SkuReserved[3] = 0x%x\n", pptable->SkuReserved[3]);
	pr_info("SkuReserved[4] = 0x%x\n", pptable->SkuReserved[4]);
	pr_info("SkuReserved[5] = 0x%x\n", pptable->SkuReserved[5]);
	pr_info("SkuReserved[6] = 0x%x\n", pptable->SkuReserved[6]);
	pr_info("SkuReserved[7] = 0x%x\n", pptable->SkuReserved[7]);
	pr_info("SkuReserved[8] = 0x%x\n", pptable->SkuReserved[8]);
	pr_info("SkuReserved[9] = 0x%x\n", pptable->SkuReserved[9]);
	pr_info("SkuReserved[10] = 0x%x\n", pptable->SkuReserved[10]);
	pr_info("SkuReserved[11] = 0x%x\n", pptable->SkuReserved[11]);
	pr_info("SkuReserved[12] = 0x%x\n", pptable->SkuReserved[12]);
	pr_info("SkuReserved[13] = 0x%x\n", pptable->SkuReserved[13]);
	pr_info("SkuReserved[14] = 0x%x\n", pptable->SkuReserved[14]);

	pr_info("GamingClk[0] = 0x%x\n", pptable->GamingClk[0]);
	pr_info("GamingClk[1] = 0x%x\n", pptable->GamingClk[1]);
	pr_info("GamingClk[2] = 0x%x\n", pptable->GamingClk[2]);
	pr_info("GamingClk[3] = 0x%x\n", pptable->GamingClk[3]);
	pr_info("GamingClk[4] = 0x%x\n", pptable->GamingClk[4]);
	pr_info("GamingClk[5] = 0x%x\n", pptable->GamingClk[5]);

	for (i = 0; i < NUM_I2C_CONTROLLERS; i++) {
		pr_info("I2cControllers[%d]:\n", i);
		pr_info("                   .Enabled = 0x%x\n",
				pptable->I2cControllers[i].Enabled);
		pr_info("                   .Speed = 0x%x\n",
				pptable->I2cControllers[i].Speed);
		pr_info("                   .SlaveAddress = 0x%x\n",
				pptable->I2cControllers[i].SlaveAddress);
		pr_info("                   .ControllerPort = 0x%x\n",
				pptable->I2cControllers[i].ControllerPort);
		pr_info("                   .ControllerName = 0x%x\n",
				pptable->I2cControllers[i].ControllerName);
		pr_info("                   .ThermalThrottler = 0x%x\n",
				pptable->I2cControllers[i].ThermalThrotter);
		pr_info("                   .I2cProtocol = 0x%x\n",
				pptable->I2cControllers[i].I2cProtocol);
		pr_info("                   .PaddingConfig = 0x%x\n",
				pptable->I2cControllers[i].PaddingConfig);
	}

	pr_info("GpioScl = 0x%x\n", pptable->GpioScl);
	pr_info("GpioSda = 0x%x\n", pptable->GpioSda);
	pr_info("FchUsbPdSlaveAddr = 0x%x\n", pptable->FchUsbPdSlaveAddr);
	pr_info("I2cSpare[0] = 0x%x\n", pptable->I2cSpare[0]);

	pr_info("Board Parameters:\n");
	pr_info("VddGfxVrMapping = 0x%x\n", pptable->VddGfxVrMapping);
	pr_info("VddSocVrMapping = 0x%x\n", pptable->VddSocVrMapping);
	pr_info("VddMem0VrMapping = 0x%x\n", pptable->VddMem0VrMapping);
	pr_info("VddMem1VrMapping = 0x%x\n", pptable->VddMem1VrMapping);
	pr_info("GfxUlvPhaseSheddingMask = 0x%x\n", pptable->GfxUlvPhaseSheddingMask);
	pr_info("SocUlvPhaseSheddingMask = 0x%x\n", pptable->SocUlvPhaseSheddingMask);
	pr_info("VddciUlvPhaseSheddingMask = 0x%x\n", pptable->VddciUlvPhaseSheddingMask);
	pr_info("MvddUlvPhaseSheddingMask = 0x%x\n", pptable->MvddUlvPhaseSheddingMask);

	pr_info("GfxMaxCurrent = 0x%x\n", pptable->GfxMaxCurrent);
	pr_info("GfxOffset = 0x%x\n", pptable->GfxOffset);
	pr_info("Padding_TelemetryGfx = 0x%x\n", pptable->Padding_TelemetryGfx);

	pr_info("SocMaxCurrent = 0x%x\n", pptable->SocMaxCurrent);
	pr_info("SocOffset = 0x%x\n", pptable->SocOffset);
	pr_info("Padding_TelemetrySoc = 0x%x\n", pptable->Padding_TelemetrySoc);

	pr_info("Mem0MaxCurrent = 0x%x\n", pptable->Mem0MaxCurrent);
	pr_info("Mem0Offset = 0x%x\n", pptable->Mem0Offset);
	pr_info("Padding_TelemetryMem0 = 0x%x\n", pptable->Padding_TelemetryMem0);

	pr_info("Mem1MaxCurrent = 0x%x\n", pptable->Mem1MaxCurrent);
	pr_info("Mem1Offset = 0x%x\n", pptable->Mem1Offset);
	pr_info("Padding_TelemetryMem1 = 0x%x\n", pptable->Padding_TelemetryMem1);

	pr_info("MvddRatio = 0x%x\n", pptable->MvddRatio);

	pr_info("AcDcGpio = 0x%x\n", pptable->AcDcGpio);
	pr_info("AcDcPolarity = 0x%x\n", pptable->AcDcPolarity);
	pr_info("VR0HotGpio = 0x%x\n", pptable->VR0HotGpio);
	pr_info("VR0HotPolarity = 0x%x\n", pptable->VR0HotPolarity);
	pr_info("VR1HotGpio = 0x%x\n", pptable->VR1HotGpio);
	pr_info("VR1HotPolarity = 0x%x\n", pptable->VR1HotPolarity);
	pr_info("GthrGpio = 0x%x\n", pptable->GthrGpio);
	pr_info("GthrPolarity = 0x%x\n", pptable->GthrPolarity);
	pr_info("LedPin0 = 0x%x\n", pptable->LedPin0);
	pr_info("LedPin1 = 0x%x\n", pptable->LedPin1);
	pr_info("LedPin2 = 0x%x\n", pptable->LedPin2);
	pr_info("LedEnableMask = 0x%x\n", pptable->LedEnableMask);
	pr_info("LedPcie = 0x%x\n", pptable->LedPcie);
	pr_info("LedError = 0x%x\n", pptable->LedError);
	pr_info("LedSpare1[0] = 0x%x\n", pptable->LedSpare1[0]);
	pr_info("LedSpare1[1] = 0x%x\n", pptable->LedSpare1[1]);

	pr_info("PllGfxclkSpreadEnabled = 0x%x\n", pptable->PllGfxclkSpreadEnabled);
	pr_info("PllGfxclkSpreadPercent = 0x%x\n", pptable->PllGfxclkSpreadPercent);
	pr_info("PllGfxclkSpreadFreq = 0x%x\n",    pptable->PllGfxclkSpreadFreq);

	pr_info("DfllGfxclkSpreadEnabled = 0x%x\n", pptable->DfllGfxclkSpreadEnabled);
	pr_info("DfllGfxclkSpreadPercent = 0x%x\n", pptable->DfllGfxclkSpreadPercent);
	pr_info("DfllGfxclkSpreadFreq = 0x%x\n",    pptable->DfllGfxclkSpreadFreq);

	pr_info("UclkSpreadEnabled = 0x%x\n", pptable->UclkSpreadEnabled);
	pr_info("UclkSpreadPercent = 0x%x\n", pptable->UclkSpreadPercent);
	pr_info("UclkSpreadFreq = 0x%x\n", pptable->UclkSpreadFreq);

	pr_info("FclkSpreadEnabled = 0x%x\n", pptable->FclkSpreadEnabled);
	pr_info("FclkSpreadPercent = 0x%x\n", pptable->FclkSpreadPercent);
	pr_info("FclkSpreadFreq = 0x%x\n", pptable->FclkSpreadFreq);

	pr_info("MemoryChannelEnabled = 0x%x\n", pptable->MemoryChannelEnabled);
	pr_info("DramBitWidth = 0x%x\n", pptable->DramBitWidth);
	pr_info("PaddingMem1[0] = 0x%x\n", pptable->PaddingMem1[0]);
	pr_info("PaddingMem1[1] = 0x%x\n", pptable->PaddingMem1[1]);
	pr_info("PaddingMem1[2] = 0x%x\n", pptable->PaddingMem1[2]);

	pr_info("TotalBoardPower = 0x%x\n", pptable->TotalBoardPower);
	pr_info("BoardPowerPadding = 0x%x\n", pptable->BoardPowerPadding);

	pr_info("XgmiLinkSpeed\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->XgmiLinkSpeed[i]);
	pr_info("XgmiLinkWidth\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->XgmiLinkWidth[i]);
	pr_info("XgmiFclkFreq\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->XgmiFclkFreq[i]);
	pr_info("XgmiSocVoltage\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		pr_info("  .[%d] = 0x%x\n", i, pptable->XgmiSocVoltage[i]);

	pr_info("HsrEnabled = 0x%x\n", pptable->HsrEnabled);
	pr_info("VddqOffEnabled = 0x%x\n", pptable->VddqOffEnabled);
	pr_info("PaddingUmcFlags[0] = 0x%x\n", pptable->PaddingUmcFlags[0]);
	pr_info("PaddingUmcFlags[1] = 0x%x\n", pptable->PaddingUmcFlags[1]);

	pr_info("BoardReserved[0] = 0x%x\n", pptable->BoardReserved[0]);
	pr_info("BoardReserved[1] = 0x%x\n", pptable->BoardReserved[1]);
	pr_info("BoardReserved[2] = 0x%x\n", pptable->BoardReserved[2]);
	pr_info("BoardReserved[3] = 0x%x\n", pptable->BoardReserved[3]);
	pr_info("BoardReserved[4] = 0x%x\n", pptable->BoardReserved[4]);
	pr_info("BoardReserved[5] = 0x%x\n", pptable->BoardReserved[5]);
	pr_info("BoardReserved[6] = 0x%x\n", pptable->BoardReserved[6]);
	pr_info("BoardReserved[7] = 0x%x\n", pptable->BoardReserved[7]);
	pr_info("BoardReserved[8] = 0x%x\n", pptable->BoardReserved[8]);
	pr_info("BoardReserved[9] = 0x%x\n", pptable->BoardReserved[9]);
	pr_info("BoardReserved[10] = 0x%x\n", pptable->BoardReserved[10]);
	pr_info("BoardReserved[11] = 0x%x\n", pptable->BoardReserved[11]);
	pr_info("BoardReserved[12] = 0x%x\n", pptable->BoardReserved[12]);
	pr_info("BoardReserved[13] = 0x%x\n", pptable->BoardReserved[13]);
	pr_info("BoardReserved[14] = 0x%x\n", pptable->BoardReserved[14]);

	pr_info("MmHubPadding[0] = 0x%x\n", pptable->MmHubPadding[0]);
	pr_info("MmHubPadding[1] = 0x%x\n", pptable->MmHubPadding[1]);
	pr_info("MmHubPadding[2] = 0x%x\n", pptable->MmHubPadding[2]);
	pr_info("MmHubPadding[3] = 0x%x\n", pptable->MmHubPadding[3]);
	pr_info("MmHubPadding[4] = 0x%x\n", pptable->MmHubPadding[4]);
	pr_info("MmHubPadding[5] = 0x%x\n", pptable->MmHubPadding[5]);
	pr_info("MmHubPadding[6] = 0x%x\n", pptable->MmHubPadding[6]);
	pr_info("MmHubPadding[7] = 0x%x\n", pptable->MmHubPadding[7]);
}

static const struct pptable_funcs sienna_cichlid_ppt_funcs = {
	.tables_init = sienna_cichlid_tables_init,
	.alloc_dpm_context = sienna_cichlid_allocate_dpm_context,
	.store_powerplay_table = sienna_cichlid_store_powerplay_table,
	.check_powerplay_table = sienna_cichlid_check_powerplay_table,
	.append_powerplay_table = sienna_cichlid_append_powerplay_table,
	.get_smu_msg_index = sienna_cichlid_get_smu_msg_index,
	.get_smu_clk_index = sienna_cichlid_get_smu_clk_index,
	.get_smu_feature_index = sienna_cichlid_get_smu_feature_index,
	.get_smu_table_index = sienna_cichlid_get_smu_table_index,
	.get_smu_power_index = sienna_cichlid_get_pwr_src_index,
	.get_workload_type = sienna_cichlid_get_workload_type,
	.get_allowed_feature_mask = sienna_cichlid_get_allowed_feature_mask,
	.set_default_dpm_table = sienna_cichlid_set_default_dpm_table,
	.dpm_set_uvd_enable = sienna_cichlid_dpm_set_uvd_enable,
	.dpm_set_jpeg_enable = sienna_cichlid_dpm_set_jpeg_enable,
	.get_current_clk_freq_by_table = sienna_cichlid_get_current_clk_freq_by_table,
	.print_clk_levels = sienna_cichlid_print_clk_levels,
	.force_clk_levels = sienna_cichlid_force_clk_levels,
	.populate_umd_state_clk = sienna_cichlid_populate_umd_state_clk,
	.pre_display_config_changed = sienna_cichlid_pre_display_config_changed,
	.display_config_changed = sienna_cichlid_display_config_changed,
	.notify_smc_display_config = sienna_cichlid_notify_smc_display_config,
	.force_dpm_limit_value = sienna_cichlid_force_dpm_limit_value,
	.unforce_dpm_levels = sienna_cichlid_unforce_dpm_levels,
	.is_dpm_running = sienna_cichlid_is_dpm_running,
	.get_fan_speed_percent = sienna_cichlid_get_fan_speed_percent,
	.get_fan_speed_rpm = sienna_cichlid_get_fan_speed_rpm,
	.get_power_profile_mode = sienna_cichlid_get_power_profile_mode,
	.set_power_profile_mode = sienna_cichlid_set_power_profile_mode,
	.get_profiling_clk_mask = sienna_cichlid_get_profiling_clk_mask,
	.set_watermarks_table = sienna_cichlid_set_watermarks_table,
	.read_sensor = sienna_cichlid_read_sensor,
	.get_uclk_dpm_states = sienna_cichlid_get_uclk_dpm_states,
	.set_performance_level = sienna_cichlid_set_performance_level,
	.get_thermal_temperature_range = sienna_cichlid_get_thermal_temperature_range,
	.display_disable_memory_clock_switch = sienna_cichlid_display_disable_memory_clock_switch,
	.get_power_limit = sienna_cichlid_get_power_limit,
	.update_pcie_parameters = sienna_cichlid_update_pcie_parameters,
	.dump_pptable = sienna_cichlid_dump_pptable,
	.init_microcode = smu_v11_0_init_microcode,
	.load_microcode = smu_v11_0_load_microcode,
	.init_smc_tables = smu_v11_0_init_smc_tables,
	.fini_smc_tables = smu_v11_0_fini_smc_tables,
	.init_power = smu_v11_0_init_power,
	.fini_power = smu_v11_0_fini_power,
	.check_fw_status = smu_v11_0_check_fw_status,
	.setup_pptable = smu_v11_0_setup_pptable,
	.get_vbios_bootup_values = smu_v11_0_get_vbios_bootup_values,
	.get_clk_info_from_vbios = smu_v11_0_get_clk_info_from_vbios,
	.check_pptable = smu_v11_0_check_pptable,
	.parse_pptable = smu_v11_0_parse_pptable,
	.populate_smc_tables = smu_v11_0_populate_smc_pptable,
	.check_fw_version = smu_v11_0_check_fw_version,
	.write_pptable = smu_v11_0_write_pptable,
	.set_min_dcef_deep_sleep = smu_v11_0_set_min_dcef_deep_sleep,
	.set_driver_table_location = smu_v11_0_set_driver_table_location,
	.set_tool_table_location = smu_v11_0_set_tool_table_location,
	.notify_memory_pool_location = smu_v11_0_notify_memory_pool_location,
	.system_features_control = smu_v11_0_system_features_control,
	.send_smc_msg_with_param = smu_v11_0_send_msg_with_param,
	.init_display_count = smu_v11_0_init_display_count,
	.set_allowed_mask = smu_v11_0_set_allowed_mask,
	.get_enabled_mask = smu_v11_0_get_enabled_mask,
	.notify_display_change = smu_v11_0_notify_display_change,
	.set_power_limit = smu_v11_0_set_power_limit,
	.get_current_clk_freq = smu_v11_0_get_current_clk_freq,
	.init_max_sustainable_clocks = smu_v11_0_init_max_sustainable_clocks,
	.enable_thermal_alert = smu_v11_0_enable_thermal_alert,
	.disable_thermal_alert = smu_v11_0_disable_thermal_alert,
	.set_deep_sleep_dcefclk = smu_v11_0_set_deep_sleep_dcefclk,
	.display_clock_voltage_request = smu_v11_0_display_clock_voltage_request,
	.get_fan_control_mode = smu_v11_0_get_fan_control_mode,
	.set_fan_control_mode = smu_v11_0_set_fan_control_mode,
	.set_fan_speed_percent = smu_v11_0_set_fan_speed_percent,
	.set_fan_speed_rpm = smu_v11_0_set_fan_speed_rpm,
	.set_xgmi_pstate = smu_v11_0_set_xgmi_pstate,
	.gfx_off_control = smu_v11_0_gfx_off_control,
	.register_irq_handler = smu_v11_0_register_irq_handler,
	.set_azalia_d3_pme = smu_v11_0_set_azalia_d3_pme,
	.get_max_sustainable_clocks_by_dc = smu_v11_0_get_max_sustainable_clocks_by_dc,
	.baco_is_support= sienna_cichlid_is_baco_supported,
	.baco_get_state = smu_v11_0_baco_get_state,
	.baco_set_state = smu_v11_0_baco_set_state,
	.baco_enter = smu_v11_0_baco_enter,
	.baco_exit = smu_v11_0_baco_exit,
	.get_dpm_ultimate_freq = sienna_cichlid_get_dpm_ultimate_freq,
	.set_soft_freq_limited_range = sienna_cichlid_set_soft_freq_limited_range,
	.override_pcie_parameters = smu_v11_0_override_pcie_parameters,
	.get_pptable_power_limit = sienna_cichlid_get_pptable_power_limit,
};

void sienna_cichlid_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &sienna_cichlid_ppt_funcs;
}
