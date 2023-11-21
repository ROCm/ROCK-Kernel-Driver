/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "umc_v12_0.h"
#include "amdgpu_ras.h"
#include "amdgpu_umc.h"
#include "amdgpu.h"
#include "umc/umc_12_0_0_offset.h"
#include "umc/umc_12_0_0_sh_mask.h"

const uint32_t
	umc_v12_0_channel_idx_tbl[]
			[UMC_V12_0_UMC_INSTANCE_NUM]
			[UMC_V12_0_CHANNEL_INSTANCE_NUM] = {
		{{3,   7,   11,  15,  2,   6,   10,  14},  {1,   5,   9,   13,  0,   4,   8,   12},
		 {19,  23,  27,  31,  18,  22,  26,  30},  {17,  21,  25,  29,  16,  20,  24,  28}},
		{{47,  43,  39,  35,  46,  42,  38,  34},  {45,  41,  37,  33,  44,  40,  36,  32},
		 {63,  59,  55,  51,  62,  58,  54,  50},  {61,  57,  53,  49,  60,  56,  52,  48}},
		{{79,  75,  71,  67,  78,  74,  70,  66},  {77,  73,  69,  65,  76,  72,  68,  64},
		 {95,  91,  87,  83,  94,  90,  86,  82},  {93,  89,  85,  81,  92,  88,  84,  80}},
		{{99,  103, 107, 111, 98,  102, 106, 110}, {97,  101, 105, 109, 96,  100, 104, 108},
		 {115, 119, 123, 127, 114, 118, 122, 126}, {113, 117, 121, 125, 112, 116, 120, 124}}
	};

/* mapping of MCA error address to normalized address */
static const uint32_t umc_v12_0_ma2na_mapping[] = {
	0,  5,  6,  8,  9,  14, 12, 13,
	10, 11, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, 26, 27, 28,
	24, 7,  29, 30,
};

static inline uint64_t get_umc_v12_0_reg_offset(struct amdgpu_device *adev,
					    uint32_t node_inst,
					    uint32_t umc_inst,
					    uint32_t ch_inst)
{
	uint32_t index = umc_inst * adev->umc.channel_inst_num + ch_inst;
	uint64_t cross_node_offset = (node_inst == 0) ? 0 : UMC_V12_0_CROSS_NODE_OFFSET;

	umc_inst = index / 4;
	ch_inst = index % 4;

	return adev->umc.channel_offs * ch_inst + UMC_V12_0_INST_DIST * umc_inst +
		UMC_V12_0_NODE_DIST * node_inst + cross_node_offset;
}

static int umc_v12_0_reset_error_count_per_channel(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	uint64_t odecc_err_cnt_addr;
	uint64_t umc_reg_offset =
		get_umc_v12_0_reg_offset(adev, node_inst, umc_inst, ch_inst);

	odecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_OdEccErrCnt);

	/* clear error count */
	WREG32_PCIE_EXT((odecc_err_cnt_addr + umc_reg_offset) * 4,
			UMC_V12_0_CE_CNT_INIT);

	return 0;
}

static void umc_v12_0_reset_error_count(struct amdgpu_device *adev)
{
	amdgpu_umc_loop_channels(adev,
		umc_v12_0_reset_error_count_per_channel, NULL);
}

bool umc_v12_0_is_uncorrectable_error(uint64_t mc_umc_status)
{
	return ((REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1) &&
		(REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, PCC) == 1 ||
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1 ||
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, TCC) == 1));
}

bool umc_v12_0_is_correctable_error(uint64_t mc_umc_status)
{
	return (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
		(REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, CECC) == 1 ||
		(REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UECC) == 1 &&
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 0) ||
		/* Identify data parity error in replay mode */
		((REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, ErrorCodeExt) == 0x5 ||
		REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, ErrorCodeExt) == 0xb) &&
		!(umc_v12_0_is_uncorrectable_error(mc_umc_status)))));
}

static void umc_v12_0_query_correctable_error_count(struct amdgpu_device *adev,
						   uint64_t umc_reg_offset,
						   unsigned long *error_count)
{
	uint64_t mc_umc_status;
	uint64_t mc_umc_status_addr;

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);

	/* Rely on MCUMC_STATUS for correctable error counter
	 * MCUMC_STATUS is a 64 bit register
	 */
	mc_umc_status =
		RREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4);

	if (umc_v12_0_is_correctable_error(mc_umc_status))
		*error_count += 1;
}

static void umc_v12_0_query_uncorrectable_error_count(struct amdgpu_device *adev,
						      uint64_t umc_reg_offset,
						      unsigned long *error_count)
{
	uint64_t mc_umc_status;
	uint64_t mc_umc_status_addr;

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);

	/* Check the MCUMC_STATUS. */
	mc_umc_status =
		RREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4);

	if (umc_v12_0_is_uncorrectable_error(mc_umc_status))
		*error_count += 1;
}

static int umc_v12_0_query_error_count(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	struct ras_err_data *err_data = (struct ras_err_data *)data;
	unsigned long ue_count = 0, ce_count = 0;

	/* NOTE: node_inst is converted by adev->umc.active_mask and the range is [0-3],
	 * which can be used as die ID directly */
	struct amdgpu_smuio_mcm_config_info mcm_info = {
		.socket_id = adev->smuio.funcs->get_socket_id(adev),
		.die_id = node_inst,
	};

	uint64_t umc_reg_offset =
		get_umc_v12_0_reg_offset(adev, node_inst, umc_inst, ch_inst);

	umc_v12_0_query_correctable_error_count(adev, umc_reg_offset, &ce_count);
	umc_v12_0_query_uncorrectable_error_count(adev, umc_reg_offset, &ue_count);

	amdgpu_ras_error_statistic_ue_count(err_data, &mcm_info, ue_count);
	amdgpu_ras_error_statistic_ce_count(err_data, &mcm_info, ce_count);

	return 0;
}

static void umc_v12_0_query_ras_error_count(struct amdgpu_device *adev,
					   void *ras_error_status)
{
	amdgpu_umc_loop_channels(adev,
		umc_v12_0_query_error_count, ras_error_status);

	umc_v12_0_reset_error_count(adev);
}

static bool umc_v12_0_bit_wise_xor(uint32_t val)
{
	bool result = 0;
	int i;

	for (i = 0; i < 32; i++)
		result = result ^ ((val >> i) & 0x1);

	return result;
}

static void umc_v12_0_convert_error_address(struct amdgpu_device *adev,
					    struct ras_err_data *err_data, uint64_t err_addr,
					    uint32_t ch_inst, uint32_t umc_inst,
					    uint32_t node_inst)
{
	uint32_t channel_index, i;
	uint64_t soc_pa, na, retired_page, column;
	uint32_t bank_hash0, bank_hash1, bank_hash2, bank_hash3, col, row, row_xor;
	uint32_t bank0, bank1, bank2, bank3, bank;

	bank_hash0 = (err_addr >> UMC_V12_0_MCA_B0_BIT) & 0x1ULL;
	bank_hash1 = (err_addr >> UMC_V12_0_MCA_B1_BIT) & 0x1ULL;
	bank_hash2 = (err_addr >> UMC_V12_0_MCA_B2_BIT) & 0x1ULL;
	bank_hash3 = (err_addr >> UMC_V12_0_MCA_B3_BIT) & 0x1ULL;
	col = (err_addr >> 1) & 0x1fULL;
	row = (err_addr >> 10) & 0x3fffULL;

	/* apply bank hash algorithm */
	bank0 =
		bank_hash0 ^ (UMC_V12_0_XOR_EN0 &
		(umc_v12_0_bit_wise_xor(col & UMC_V12_0_COL_XOR0) ^
		(umc_v12_0_bit_wise_xor(row & UMC_V12_0_ROW_XOR0))));
	bank1 =
		bank_hash1 ^ (UMC_V12_0_XOR_EN1 &
		(umc_v12_0_bit_wise_xor(col & UMC_V12_0_COL_XOR1) ^
		(umc_v12_0_bit_wise_xor(row & UMC_V12_0_ROW_XOR1))));
	bank2 =
		bank_hash2 ^ (UMC_V12_0_XOR_EN2 &
		(umc_v12_0_bit_wise_xor(col & UMC_V12_0_COL_XOR2) ^
		(umc_v12_0_bit_wise_xor(row & UMC_V12_0_ROW_XOR2))));
	bank3 =
		bank_hash3 ^ (UMC_V12_0_XOR_EN3 &
		(umc_v12_0_bit_wise_xor(col & UMC_V12_0_COL_XOR3) ^
		(umc_v12_0_bit_wise_xor(row & UMC_V12_0_ROW_XOR3))));

	bank = bank0 | (bank1 << 1) | (bank2 << 2) | (bank3 << 3);
	err_addr &= ~0x3c0ULL;
	err_addr |= (bank << UMC_V12_0_MCA_B0_BIT);

	na = 0x0;
	/* convert mca error address to normalized address */
	for (i = 1; i < ARRAY_SIZE(umc_v12_0_ma2na_mapping); i++)
		na |= ((err_addr >> i) & 0x1ULL) << umc_v12_0_ma2na_mapping[i];

	channel_index =
		adev->umc.channel_idx_tbl[node_inst * adev->umc.umc_inst_num *
			adev->umc.channel_inst_num +
			umc_inst * adev->umc.channel_inst_num +
			ch_inst];
	/* translate umc channel address to soc pa, 3 parts are included */
	soc_pa = ADDR_OF_32KB_BLOCK(na) |
		ADDR_OF_256B_BLOCK(channel_index) |
		OFFSET_IN_256B_BLOCK(na);

	/* the umc channel bits are not original values, they are hashed */
	UMC_V12_0_SET_CHANNEL_HASH(channel_index, soc_pa);

	/* clear [C3 C2] in soc physical address */
	soc_pa &= ~(0x3ULL << UMC_V12_0_PA_C2_BIT);
	/* clear [C4] in soc physical address */
	soc_pa &= ~(0x1ULL << UMC_V12_0_PA_C4_BIT);

	row_xor = row ^ (0x1ULL << 13);
	/* loop for all possibilities of [C4 C3 C2] */
	for (column = 0; column < UMC_V12_0_NA_MAP_PA_NUM; column++) {
		retired_page = soc_pa | ((column & 0x3) << UMC_V12_0_PA_C2_BIT);
		retired_page |= (((column & 0x4) >> 2) << UMC_V12_0_PA_C4_BIT);
		/* include column bit 0 and 1 */
		col &= 0x3;
		col |= (column << 2);
		dev_info(adev->dev,
			"Error Address(PA):0x%-10llx Row:0x%-4x Col:0x%-2x Bank:0x%x Channel:0x%x\n",
			retired_page, row, col, bank, channel_index);
		amdgpu_umc_fill_error_record(err_data, err_addr,
			retired_page, channel_index, umc_inst);

		/* shift R13 bit */
		retired_page ^= (0x1ULL << UMC_V12_0_PA_R13_BIT);
		dev_info(adev->dev,
			"Error Address(PA):0x%-10llx Row:0x%-4x Col:0x%-2x Bank:0x%x Channel:0x%x\n",
			retired_page, row_xor, col, bank, channel_index);
		amdgpu_umc_fill_error_record(err_data, err_addr,
			retired_page, channel_index, umc_inst);
	}
}

static int umc_v12_0_query_error_address(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	uint64_t mc_umc_status_addr;
	uint64_t mc_umc_status, err_addr;
	uint64_t mc_umc_addrt0;
	struct ras_err_data *err_data = (struct ras_err_data *)data;
	uint64_t umc_reg_offset =
		get_umc_v12_0_reg_offset(adev, node_inst, umc_inst, ch_inst);

	mc_umc_status_addr =
		SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_STATUST0);

	mc_umc_status = RREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4);

	if (mc_umc_status == 0)
		return 0;

	if (!err_data->err_addr) {
		/* clear umc status */
		WREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4, 0x0ULL);

		return 0;
	}

	/* calculate error address if ue error is detected */
	if (REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, Val) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, AddrV) == 1 &&
	    REG_GET_FIELD(mc_umc_status, MCA_UMC_UMC0_MCUMC_STATUST0, UC) == 1) {

		mc_umc_addrt0 =
			SOC15_REG_OFFSET(UMC, 0, regMCA_UMC_UMC0_MCUMC_ADDRT0);

		err_addr = RREG64_PCIE_EXT((mc_umc_addrt0 + umc_reg_offset) * 4);

		err_addr = REG_GET_FIELD(err_addr, MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr);

		umc_v12_0_convert_error_address(adev, err_data, err_addr,
					ch_inst, umc_inst, node_inst);
	}

	/* clear umc status */
	WREG64_PCIE_EXT((mc_umc_status_addr + umc_reg_offset) * 4, 0x0ULL);

	return 0;
}

static void umc_v12_0_query_ras_error_address(struct amdgpu_device *adev,
					     void *ras_error_status)
{
	amdgpu_umc_loop_channels(adev,
		umc_v12_0_query_error_address, ras_error_status);
}

static int umc_v12_0_err_cnt_init_per_channel(struct amdgpu_device *adev,
					uint32_t node_inst, uint32_t umc_inst,
					uint32_t ch_inst, void *data)
{
	uint32_t odecc_cnt_sel;
	uint64_t odecc_cnt_sel_addr, odecc_err_cnt_addr;
	uint64_t umc_reg_offset =
		get_umc_v12_0_reg_offset(adev, node_inst, umc_inst, ch_inst);

	odecc_cnt_sel_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_OdEccCntSel);
	odecc_err_cnt_addr =
		SOC15_REG_OFFSET(UMC, 0, regUMCCH0_OdEccErrCnt);

	odecc_cnt_sel = RREG32_PCIE_EXT((odecc_cnt_sel_addr + umc_reg_offset) * 4);

	/* set ce error interrupt type to APIC based interrupt */
	odecc_cnt_sel = REG_SET_FIELD(odecc_cnt_sel, UMCCH0_OdEccCntSel,
					OdEccErrInt, 0x1);
	WREG32_PCIE_EXT((odecc_cnt_sel_addr + umc_reg_offset) * 4, odecc_cnt_sel);

	/* set error count to initial value */
	WREG32_PCIE_EXT((odecc_err_cnt_addr + umc_reg_offset) * 4, UMC_V12_0_CE_CNT_INIT);

	return 0;
}

static void umc_v12_0_err_cnt_init(struct amdgpu_device *adev)
{
	amdgpu_umc_loop_channels(adev,
		umc_v12_0_err_cnt_init_per_channel, NULL);
}

static bool umc_v12_0_query_ras_poison_mode(struct amdgpu_device *adev)
{
	/*
	 * Force return true, because regUMCCH0_EccCtrl
	 * is not accessible from host side
	 */
	return true;
}

const struct amdgpu_ras_block_hw_ops umc_v12_0_ras_hw_ops = {
	.query_ras_error_count = umc_v12_0_query_ras_error_count,
	.query_ras_error_address = umc_v12_0_query_ras_error_address,
};

struct amdgpu_umc_ras umc_v12_0_ras = {
	.ras_block = {
		.hw_ops = &umc_v12_0_ras_hw_ops,
	},
	.err_cnt_init = umc_v12_0_err_cnt_init,
	.query_ras_poison_mode = umc_v12_0_query_ras_poison_mode,
};
