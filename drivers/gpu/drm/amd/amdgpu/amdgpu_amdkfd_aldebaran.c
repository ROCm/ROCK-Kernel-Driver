/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
 */
#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_amdkfd_arcturus.h"
#include "amdgpu_amdkfd_gfx_v9.h"
#include "gc/gc_9_4_2_offset.h"
#include "gc/gc_9_4_2_sh_mask.h"
#include "soc15.h"

static inline struct amdgpu_device *get_amdgpu_device(struct kgd_dev *kgd)
{
	return (struct amdgpu_device *)kgd;
}

/*
 * Aldebaran helper for wave launch stall requirements on debug trap setting.
 *
 * vmid:
 *   Target VMID to stall
 *
 * data:
 *   Used to check current SPI_GDBG_PER_VMID_CNTL settings.
 *
 * After wavefront launch has been stalled, allocated waves must drain SPI in
 * order for debug trap settings to take effect on those waves. This is
 * roughly a ~96 clock cycle wait on SPI where a read on
 * SPI_GDBG_PER_VMID_CNTL translates to ~32 clock cycles.
 * KGD_ALDEBARAN_WAVE_LAUNCH_SPI_DRAIN_LATENCY indicates the number of reads
 * required.
 *
 * NOTE: Assumes per-vmid SPI instance is SRBM locked.
 */
#define KGD_ALDEBARAN_WAVE_LAUNCH_SPI_DRAIN_LATENCY	3

static void kgd_aldebaran_stall_wave_launch(struct amdgpu_device *adev,
					    uint32_t vmid,
					    uint32_t data)
{
	int i;
	uint32_t stall_cur =
		REG_GET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, STALL_VMID);

	if (stall_cur)
		return;

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, STALL_VMID, 1);
	WREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL), data);

	for (i = 0; i < KGD_ALDEBARAN_WAVE_LAUNCH_SPI_DRAIN_LATENCY; i++)
		RREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL));
}

static void kgd_aldebaran_enable_debug_trap(struct kgd_dev *kgd,
					    uint32_t vmid)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t data;

	kgd_gfx_v9_lock_srbm(kgd, 0, 0, 0, vmid);

	data = RREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL));
	kgd_aldebaran_stall_wave_launch(adev, vmid, data);

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, 0);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE, 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL), data);

	kgd_gfx_v9_unlock_srbm(kgd);
}

static void kgd_aldebaran_disable_debug_trap(struct kgd_dev *kgd,
					     uint32_t vmid)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t data;

	kgd_gfx_v9_lock_srbm(kgd, 0, 0, 0, vmid);

	data = RREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL));
	kgd_aldebaran_stall_wave_launch(adev, vmid, data);

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, 0);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE, 0);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, STALL_VMID, 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL), data);

	kgd_gfx_v9_unlock_srbm(kgd);
}

static int kgd_aldebaran_set_wave_launch_trap_override(struct kgd_dev *kgd,
					uint32_t vmid,
					uint32_t trap_override,
					uint32_t trap_mask_bits,
					uint32_t trap_mask_request,
					uint32_t *trap_mask_prev,
					uint32_t *trap_mask_supported)

{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t data;

	kgd_gfx_v9_lock_srbm(kgd, 0, 0, 0, vmid);

	data = RREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL));
	kgd_aldebaran_stall_wave_launch(adev, vmid, data);

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, trap_mask_bits);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE,
								trap_override);
	WREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL), data);

	kgd_gfx_v9_unlock_srbm(kgd);

	return 0;
}

static void kgd_aldebaran_set_wave_launch_mode(struct kgd_dev *kgd,
					uint8_t wave_launch_mode,
					uint32_t vmid)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t data;
	bool is_stall_mode = wave_launch_mode == 4;

	kgd_gfx_v9_lock_srbm(kgd, 0, 0, 0, vmid);

	data = RREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL));
	kgd_aldebaran_stall_wave_launch(adev, vmid, data);

	if (is_stall_mode)
		goto out;

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, LAUNCH_MODE,
					wave_launch_mode);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, STALL_VMID, 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL), data);
out:
	kgd_gfx_v9_unlock_srbm(kgd);
}

int kgd_aldebaran_set_precise_mem_ops(struct kgd_dev *kgd, uint32_t vmid,
					bool enable)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t data = 0;

	kgd_gfx_v9_lock_srbm(kgd, 0, 0, 0, vmid);

	data = REG_SET_FIELD(
			data, SQ_DEBUG, SINGLE_MEMOP, enable ? 1 : 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, regSQ_DEBUG), data);

	kgd_gfx_v9_unlock_srbm(kgd);

	return 0;
}

const struct kfd2kgd_calls aldebaran_kfd2kgd = {
	.program_sh_mem_settings = kgd_gfx_v9_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_gfx_v9_set_pasid_vmid_mapping,
	.init_interrupts = kgd_gfx_v9_init_interrupts,
	.hqd_load = kgd_gfx_v9_hqd_load,
	.hiq_mqd_load = kgd_gfx_v9_hiq_mqd_load,
	.hqd_sdma_load = kgd_arcturus_hqd_sdma_load,
	.hqd_dump = kgd_gfx_v9_hqd_dump,
	.hqd_sdma_dump = kgd_arcturus_hqd_sdma_dump,
	.hqd_is_occupied = kgd_gfx_v9_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_arcturus_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_gfx_v9_hqd_destroy,
	.hqd_sdma_destroy = kgd_arcturus_hqd_sdma_destroy,
	.address_watch_disable = kgd_gfx_v9_address_watch_disable,
	.address_watch_execute = kgd_gfx_v9_address_watch_execute,
	.wave_control_execute = kgd_gfx_v9_wave_control_execute,
	.address_watch_get_offset = kgd_gfx_v9_address_watch_get_offset,
	.get_atc_vmid_pasid_mapping_info =
				kgd_gfx_v9_get_atc_vmid_pasid_mapping_info,
	.set_vm_context_page_table_base = kgd_gfx_v9_set_vm_context_page_table_base,
	.enable_debug_trap = kgd_aldebaran_enable_debug_trap,
	.disable_debug_trap = kgd_aldebaran_disable_debug_trap,
	.set_wave_launch_trap_override =
				kgd_aldebaran_set_wave_launch_trap_override,
	.set_wave_launch_mode = kgd_aldebaran_set_wave_launch_mode,
	.set_address_watch = kgd_gfx_v9_set_address_watch,
	.clear_address_watch = kgd_gfx_v9_clear_address_watch,
	.set_precise_mem_ops = kgd_aldebaran_set_precise_mem_ops,
	.get_iq_wait_times = kgd_gfx_v9_get_iq_wait_times,
	.build_grace_period_packet_info =
				kgd_gfx_v9_build_grace_period_packet_info,
};
