/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include <linux/module.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "cikd.h"
#include "cik_sdma.h"
#include "gca/gfx_7_2_d.h"
#include "gca/gfx_7_2_enum.h"
#include "gca/gfx_7_2_sh_mask.h"
#include "oss/oss_2_0_d.h"
#include "oss/oss_2_0_sh_mask.h"
#include "gmc/gmc_7_1_d.h"
#include "gmc/gmc_7_1_sh_mask.h"

#define CIK_PIPE_PER_MEC	(4)

#define AMDKFD_SKIP_UNCOMPILED_CODE 1

enum {
	MAX_TRAPID = 8,		/* 3 bits in the bitfield. */
	MAX_WATCH_ADDRESSES = 4
};

enum {
	ADDRESS_WATCH_REG_ADDR_HI = 0,
	ADDRESS_WATCH_REG_ADDR_LO,
	ADDRESS_WATCH_REG_CNTL,
	ADDRESS_WATCH_REG_MAX
};

/*  not defined in the CI/KV reg file  */
enum {
	ADDRESS_WATCH_REG_CNTL_ATC_BIT = 0x10000000UL,
	ADDRESS_WATCH_REG_CNTL_DEFAULT_MASK = 0x00FFFFFF,
	ADDRESS_WATCH_REG_ADDLOW_MASK_EXTENTION = 0x03000000,
	/* extend the mask to 26 bits in order to match the low address field. */
	ADDRESS_WATCH_REG_ADDLOW_SHIFT = 6,
	ADDRESS_WATCH_REG_ADDHIGH_MASK = 0xFFFF
};

static const uint32_t watchRegs[MAX_WATCH_ADDRESSES * ADDRESS_WATCH_REG_MAX] = {
	mmTCP_WATCH0_ADDR_H, mmTCP_WATCH0_ADDR_L, mmTCP_WATCH0_CNTL,
	mmTCP_WATCH1_ADDR_H, mmTCP_WATCH1_ADDR_L, mmTCP_WATCH1_CNTL,
	mmTCP_WATCH2_ADDR_H, mmTCP_WATCH2_ADDR_L, mmTCP_WATCH2_CNTL,
	mmTCP_WATCH3_ADDR_H, mmTCP_WATCH3_ADDR_L, mmTCP_WATCH3_CNTL
};

union TCP_WATCH_CNTL_BITS {
	struct {
		uint32_t mask:24;
		uint32_t vmid:4;
		uint32_t atc:1;
		uint32_t mode:2;
		uint32_t valid:1;
	} bitfields, bits;
	uint32_t u32All;
	signed int i32All;
	float f32All;
};

struct kgd_mem {
	union {
		struct {
			struct amdgpu_bo *bo;
			uint64_t gpu_addr;
			void *cpu_ptr;
		} data1;
		struct {
			struct amdgpu_bo *bo;
			struct amdgpu_bo_va *bo_va;
		} data2;
	};
};
static int map_bo(struct amdgpu_device *adev, uint64_t va, void *vm, struct amdgpu_bo *bo, struct amdgpu_bo_va **bo_va);

static int alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
			void **mem_obj, uint64_t *gpu_addr,
			void **cpu_ptr);

static void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj);

static uint64_t get_vmem_size(struct kgd_dev *kgd);
static uint64_t get_gpu_clock_counter(struct kgd_dev *kgd);

static uint32_t get_max_engine_clock_in_mhz(struct kgd_dev *kgd);

static int create_process_vm(struct kgd_dev *kgd, void **vm);
static void destroy_process_vm(struct kgd_dev *kgd, void *vm);

static int create_process_gpumem(struct kgd_dev *kgd, uint64_t va, size_t size, void *vm, struct kgd_mem **mem);
static void destroy_process_gpumem(struct kgd_dev *kgd, struct kgd_mem *mem);

static uint32_t get_process_page_dir(void *vm);

static int open_graphic_handle(struct kgd_dev *kgd, uint64_t va, void *vm, int fd, uint32_t handle, struct kgd_mem **mem);

/*
 * Register access functions
 */

static void kgd_program_sh_mem_settings(struct kgd_dev *kgd, uint32_t vmid, uint32_t sh_mem_config,
		uint32_t sh_mem_ape1_base, uint32_t sh_mem_ape1_limit, uint32_t sh_mem_bases);
static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid, unsigned int vmid);
static int kgd_init_memory(struct kgd_dev *kgd);
static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id, uint32_t hpd_size, uint64_t hpd_gpu_addr);
static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id);
static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id, uint32_t queue_id, uint32_t __user *wptr);
static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd);
static bool kgd_hqd_is_occupies(struct kgd_dev *kgd, uint64_t queue_address, uint32_t pipe_id, uint32_t queue_id);
static bool kgd_hqd_sdma_is_occupies(struct kgd_dev *kgd, void *mqd);
static int kgd_hqd_destroy(struct kgd_dev *kgd, uint32_t reset_type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id);
static int kgd_hqd_sdma_destroy(struct kgd_dev *kgd, void *mqd,
				unsigned int timeout);
static int kgd_init_sdma_engines(struct kgd_dev *kgd);
static int kgd_address_watch_disable(struct kgd_dev *kgd);
static int kgd_address_watch_execute(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo);
static int kgd_wave_control_execute(struct kgd_dev *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd);
static uint32_t kgd_address_watch_get_offset(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset);

static bool read_atc_vmid_pasid_mapping_reg_valid_field(struct kgd_dev *kgd, uint8_t vmid);
static uint16_t read_atc_vmid_pasid_mapping_reg_pasid_field(struct kgd_dev *kgd, uint8_t vmid);
static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid);

static const struct kfd2kgd_calls kfd2kgd = {
	.init_gtt_mem_allocation = alloc_gtt_mem,
	.free_gtt_mem = free_gtt_mem,
	.get_vmem_size = get_vmem_size,
	.get_gpu_clock_counter = get_gpu_clock_counter,
	.get_max_engine_clock_in_mhz = get_max_engine_clock_in_mhz,
	.create_process_vm = create_process_vm,
	.destroy_process_vm = destroy_process_vm,
	.create_process_gpumem = create_process_gpumem,
	.destroy_process_gpumem = destroy_process_gpumem,
	.get_process_page_dir = get_process_page_dir,
	.open_graphic_handle = open_graphic_handle,
	.program_sh_mem_settings = kgd_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_set_pasid_vmid_mapping,
	.init_memory = kgd_init_memory,
	.init_pipeline = kgd_init_pipeline,
	.init_interrupts = kgd_init_interrupts,
	.init_sdma_engines = kgd_init_sdma_engines,
	.hqd_load = kgd_hqd_load,
	.hqd_sdma_load = kgd_hqd_sdma_load,
	.hqd_is_occupied = kgd_hqd_is_occupies,
	.hqd_sdma_is_occupies = kgd_hqd_sdma_is_occupies,
	.hqd_destroy = kgd_hqd_destroy,
	.hqd_sdma_destroy = kgd_hqd_sdma_destroy,
	.address_watch_disable = kgd_address_watch_disable,
	.address_watch_execute = kgd_address_watch_execute,
	.wave_control_execute = kgd_wave_control_execute,
	.address_watch_get_offset = kgd_address_watch_get_offset,
	.read_atc_vmid_pasid_mapping_reg_pasid_field = read_atc_vmid_pasid_mapping_reg_pasid_field,
	.read_atc_vmid_pasid_mapping_reg_valid_field = read_atc_vmid_pasid_mapping_reg_valid_field,
	.write_vmid_invalidate_request = write_vmid_invalidate_request
};

static const struct kgd2kfd_calls *kgd2kfd;

bool amdgpu_amdkfd_init(void)
{
	bool (*kgd2kfd_init_p)(unsigned, const struct kfd2kgd_calls*,
				const struct kgd2kfd_calls**);

	kgd2kfd_init_p = symbol_request(kgd2kfd_init);

	if (kgd2kfd_init_p == NULL)
		return false;

	if (!kgd2kfd_init_p(KFD_INTERFACE_VERSION, &kfd2kgd, &kgd2kfd)) {
		symbol_put(kgd2kfd_init);
		kgd2kfd = NULL;

		return false;
	}

	return true;
}

void amdgpu_amdkfd_fini(void)
{
	if (kgd2kfd) {
		kgd2kfd->exit();
		symbol_put(kgd2kfd_init);
	}
}

void amdgpu_amdkfd_device_probe(struct amdgpu_device *adev)
{
	if (kgd2kfd)
		adev->kfd = kgd2kfd->probe((struct kgd_dev *)adev, adev->pdev);
}

void amdgpu_amdkfd_device_init(struct amdgpu_device *adev)
{
	if (adev->kfd) {
		struct kgd2kfd_shared_resources gpu_resources = {
			.compute_vmid_bitmap = 0xFF00,

			.first_compute_pipe = 1,
			.compute_pipe_count = 8 - 1,
		};

		amdgpu_doorbell_get_kfd_info(adev,
				&gpu_resources.doorbell_physical_address,
				&gpu_resources.doorbell_aperture_size,
				&gpu_resources.doorbell_start_offset);

		kgd2kfd->device_init(adev->kfd, &gpu_resources);
	}
}

void amdgpu_amdkfd_device_fini(struct amdgpu_device *adev)
{
	if (adev->kfd) {
		kgd2kfd->device_exit(adev->kfd);
		adev->kfd = NULL;
	}
}

void amdgpu_amdkfd_interrupt(struct amdgpu_device *adev, const void *ih_ring_entry)
{
	if (adev->kfd)
		kgd2kfd->interrupt(adev->kfd, ih_ring_entry);
}

void amdgpu_amdkfd_suspend(struct amdgpu_device *adev)
{
	if (adev->kfd)
		kgd2kfd->suspend(adev->kfd);
}

int amdgpu_amdkfd_resume(struct amdgpu_device *adev)
{
	int r = 0;

	if (adev->kfd)
		r = kgd2kfd->resume(adev->kfd);

	return r;
}

static int alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
			void **mem_obj, uint64_t *gpu_addr,
			void **cpu_ptr)
{
	struct amdgpu_device *rdev = (struct amdgpu_device *)kgd;
	struct kgd_mem **mem = (struct kgd_mem **) mem_obj;
	int r;

	BUG_ON(kgd == NULL);
	BUG_ON(gpu_addr == NULL);
	BUG_ON(cpu_ptr == NULL);

	*mem = kmalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if ((*mem) == NULL)
		return -ENOMEM;

	r = amdgpu_bo_create(rdev, size, PAGE_SIZE, true, AMDGPU_GEM_DOMAIN_GTT,
				NULL, &(*mem)->data1.bo);
	if (r) {
		dev_err(rdev->dev,
			"failed to allocate BO for amdkfd (%d)\n", r);
		return r;
	}

	/* map the buffer */
	r = amdgpu_bo_reserve((*mem)->data1.bo, true);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to reserve bo for amdkfd\n", r);
		goto allocate_mem_reserve_bo_failed;
	}

	r = amdgpu_bo_pin((*mem)->data1.bo, AMDGPU_GEM_DOMAIN_GTT,
				&(*mem)->data1.gpu_addr);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to pin bo for amdkfd\n", r);
		goto allocate_mem_pin_bo_failed;
	}
	*gpu_addr = (*mem)->data1.gpu_addr;

	r = amdgpu_bo_kmap((*mem)->data1.bo, &(*mem)->data1.cpu_ptr);
	if (r) {
		dev_err(rdev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", r);
		goto allocate_mem_kmap_bo_failed;
	}
	*cpu_ptr = (*mem)->data1.cpu_ptr;

	amdgpu_bo_unreserve((*mem)->data1.bo);

	return 0;

allocate_mem_kmap_bo_failed:
	amdgpu_bo_unpin((*mem)->data1.bo);
allocate_mem_pin_bo_failed:
	amdgpu_bo_unreserve((*mem)->data1.bo);
allocate_mem_reserve_bo_failed:
	amdgpu_bo_unref(&(*mem)->data1.bo);

	return r;
}

static void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj)
{
	struct kgd_mem *mem = (struct kgd_mem *) mem_obj;

	BUG_ON(mem == NULL);

	amdgpu_bo_reserve(mem->data1.bo, true);
	amdgpu_bo_kunmap(mem->data1.bo);
	amdgpu_bo_unpin(mem->data1.bo);
	amdgpu_bo_unreserve(mem->data1.bo);
	amdgpu_bo_unref(&(mem->data1.bo));
	kfree(mem);
}

static uint64_t get_vmem_size(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;

	BUG_ON(kgd == NULL);

	return adev->mc.real_vram_size;
}

static uint64_t get_gpu_clock_counter(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;

	return adev->asic_funcs->get_gpu_clock_counter(adev);
}

static uint32_t get_max_engine_clock_in_mhz(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;

	/* The sclk is in quantas of 10kHz */
	return adev->pm.dpm.dyn_state.max_clock_voltage_on_ac.sclk / 100;
}

/*
 * Creates a VM context for HSA process
 */
static int create_process_vm(struct kgd_dev *kgd, void **vm)
{
	int ret;
	struct amdgpu_vm *new_vm;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	new_vm = kzalloc(sizeof(struct amdgpu_vm), GFP_KERNEL);
	if (new_vm == NULL)
		return -ENOMEM;

	/* Initialize the VM context, allocate the page directory and zero it */
	ret = amdgpu_vm_init(adev, new_vm);
	if (ret != 0) {
		/* Undo everything related to the new VM context */
		amdgpu_vm_fini(adev, new_vm);
		kfree(new_vm);
		new_vm = NULL;
	}

	/* Pin the PD directory*/
	amdgpu_bo_reserve(new_vm->page_directory, true);
	amdgpu_bo_pin(new_vm->page_directory, AMDGPU_GEM_DOMAIN_VRAM, NULL);
	amdgpu_bo_unreserve(new_vm->page_directory);
#if 0
	new_vm->pd_gpu_addr = amdgpu_bo_gpu_offset(new_vm->page_directory);
#endif
	*vm = (void *) new_vm;

	return ret;
}

/*
 * Destroys a VM context of HSA process
 */
static void destroy_process_vm(struct kgd_dev *kgd, void *vm)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	struct amdgpu_vm *rvm = (struct amdgpu_vm *) vm;

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	/* Unpin the PD directory*/
	amdgpu_bo_reserve(rvm->page_directory, true);
	amdgpu_bo_unpin(rvm->page_directory);
	amdgpu_bo_unreserve(rvm->page_directory);

	/* Release the VM context */
	amdgpu_vm_fini(adev, rvm);
	kfree(vm);
}

static int create_process_gpumem(struct kgd_dev *kgd, uint64_t va, size_t size,
				void *vm, struct kgd_mem **mem)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	int ret;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo *bo;

	BUG_ON(kgd == NULL);
	BUG_ON(size == 0);
	BUG_ON(mem == NULL);

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (*mem == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	/* Allocate buffer object on VRAM */
	ret = amdgpu_bo_create(adev, size, PAGE_SIZE, false,
				AMDGPU_GEM_DOMAIN_VRAM,
				NULL, &bo);
	if (ret != 0)
		goto err_bo_create;

	ret = map_bo(adev, va, vm, bo, &bo_va);
	if (ret != 0)
		goto err_map;

	(*mem)->data2.bo = bo;
	(*mem)->data2.bo_va = bo_va;
	return 0;

err_map:
	amdgpu_bo_unref(&bo);
err_bo_create:
	kfree(*mem);
err:
	return ret;
}

/* Destroys the GPU allocation and frees the kgd_mem structure */
static void destroy_process_gpumem(struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct amdgpu_vm *rvm;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);
	rvm = mem->data2.bo_va->vm;

	amdgpu_bo_reserve(mem->data2.bo, true);
	amdgpu_vm_bo_rmv(adev, mem->data2.bo_va);

#ifndef AMDKFD_SKIP_UNCOMPILED_CODE
	mutex_lock(&rvm->mutex);
	amdgpu_vm_clear_freed(adev, rvm);
	mutex_unlock(&rvm->mutex);
#endif

	amdgpu_bo_unpin(mem->data2.bo);
	amdgpu_bo_unreserve(mem->data2.bo);
	amdgpu_bo_unref(&mem->data2.bo);
	kfree(mem);
}

static uint32_t get_process_page_dir(void *vm)
{
#if 0
	struct amdgpu_vm *rvm = (struct amdgpu_vm *) vm;

	BUG_ON(vm == NULL);

	return rvm->pd_gpu_addr >> AMDGPU_GPU_PAGE_SHIFT;
#endif
	return 0;
}

static int open_graphic_handle(struct kgd_dev *kgd, uint64_t va, void *vm,
				int fd, uint32_t handle, struct kgd_mem **mem)
{
	return 0;
}

static inline struct amdgpu_device *get_amdgpu_device(struct kgd_dev *kgd)
{
	return (struct amdgpu_device *)kgd;
}

static void lock_srbm(struct kgd_dev *kgd, uint32_t mec, uint32_t pipe,
			uint32_t queue, uint32_t vmid)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t value = PIPEID(pipe) | MEID(mec) | VMID(vmid) | QUEUEID(queue);

	mutex_lock(&adev->srbm_mutex);
	WREG32(mmSRBM_GFX_CNTL, value);
}

static void unlock_srbm(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	WREG32(mmSRBM_GFX_CNTL, 0);
	mutex_unlock(&adev->srbm_mutex);
}

static void acquire_queue(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t queue_id)
{
	uint32_t mec = (++pipe_id / CIK_PIPE_PER_MEC) + 1;
	uint32_t pipe = (pipe_id % CIK_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, queue_id, 0);
}

static void release_queue(struct kgd_dev *kgd)
{
	unlock_srbm(kgd);
}

static void kgd_program_sh_mem_settings(struct kgd_dev *kgd, uint32_t vmid,
					uint32_t sh_mem_config,
					uint32_t sh_mem_ape1_base,
					uint32_t sh_mem_ape1_limit,
					uint32_t sh_mem_bases)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	lock_srbm(kgd, 0, 0, 0, vmid);

	WREG32(mmSH_MEM_CONFIG, sh_mem_config);
	WREG32(mmSH_MEM_APE1_BASE, sh_mem_ape1_base);
	WREG32(mmSH_MEM_APE1_LIMIT, sh_mem_ape1_limit);
	WREG32(mmSH_MEM_BASES, sh_mem_bases);

	unlock_srbm(kgd);
}

static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
					unsigned int vmid)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	/*
	 * We have to assume that there is no outstanding mapping.
	 * The ATC_VMID_PASID_MAPPING_UPDATE_STATUS bit could be 0 because a mapping
	 * is in progress or because a mapping finished and the SW cleared it.
	 * So the protocol is to always wait & clear.
	 */
	uint32_t pasid_mapping = (pasid == 0) ? 0 : (uint32_t)pasid | ATC_VMID0_PASID_MAPPING__VALID_MASK;

	WREG32(mmATC_VMID0_PASID_MAPPING + vmid, pasid_mapping);

	while (!(RREG32(mmATC_VMID_PASID_MAPPING_UPDATE_STATUS) & (1U << vmid)))
		cpu_relax();
	WREG32(mmATC_VMID_PASID_MAPPING_UPDATE_STATUS, 1U << vmid);

	return 0;
}

static int kgd_init_memory(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	/*
	 * Configure apertures:
	 * LDS:         0x60000000'00000000 - 0x60000001'00000000 (4GB)
	 * Scratch:     0x60000001'00000000 - 0x60000002'00000000 (4GB)
	 * GPUVM:       0x60010000'00000000 - 0x60020000'00000000 (1TB)
	 */
	int i;
	uint32_t sh_mem_bases = PRIVATE_BASE(0x6000) | SHARED_BASE(0x6000);

	for (i = 8; i < 16; i++) {
		uint32_t sh_mem_config;

		lock_srbm(kgd, 0, 0, 0, i);

		sh_mem_config = SH_MEM_ALIGNMENT_MODE_UNALIGNED << SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT;
		sh_mem_config |= MTYPE_NONCACHED << SH_MEM_CONFIG__DEFAULT_MTYPE__SHIFT;

		WREG32(mmSH_MEM_CONFIG, sh_mem_config);

		WREG32(mmSH_MEM_BASES, sh_mem_bases);

		/* Scratch aperture is not supported for now. */
		WREG32(mmSH_STATIC_MEM_CONFIG, 0);

		/* APE1 disabled for now. */
		WREG32(mmSH_MEM_APE1_BASE, 1);
		WREG32(mmSH_MEM_APE1_LIMIT, 0);

		unlock_srbm(kgd);
	}

	return 0;
}

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	uint32_t mec = (++pipe_id / CIK_PIPE_PER_MEC) + 1;
	uint32_t pipe = (pipe_id % CIK_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, 0, 0);
	WREG32(mmCP_HPD_EOP_BASE_ADDR, lower_32_bits(hpd_gpu_addr >> 8));
	WREG32(mmCP_HPD_EOP_BASE_ADDR_HI, upper_32_bits(hpd_gpu_addr >> 8));
	WREG32(mmCP_HPD_EOP_VMID, 0);
	WREG32(mmCP_HPD_EOP_CONTROL, hpd_size);
	unlock_srbm(kgd);

	return 0;
}

static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t mec;
	uint32_t pipe;

	mec = (++pipe_id / CIK_PIPE_PER_MEC) + 1;
	pipe = (pipe_id % CIK_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, 0, 0);

	WREG32(mmCPC_INT_CNTL, CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK);

	unlock_srbm(kgd);

	return 0;
}

static int kgd_init_sdma_engines(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t value;

	value = RREG32(mmSDMA0_CNTL);
	value |= SDMA0_CNTL__AUTO_CTXSW_ENABLE_MASK;
	WREG32(mmSDMA0_CNTL, value);

	value = RREG32(mmSDMA1_CNTL);
	value |= SDMA1_CNTL__AUTO_CTXSW_ENABLE_MASK;
	WREG32(mmSDMA1_CNTL, value);

	return 0;
}

static inline uint32_t get_sdma_base_addr(struct cik_sdma_rlc_registers *m)
{
	uint32_t retval;

	retval = m->sdma_engine_id * SDMA1_REGISTER_OFFSET +
			m->sdma_queue_id * KFD_CIK_SDMA_QUEUE_OFFSET;
	pr_err("kfd: sdma base address: 0x%x\n", retval);

	return retval;
}

static inline struct cik_mqd *get_mqd(void *mqd)
{
	return (struct cik_mqd *)mqd;
}

static inline struct cik_sdma_rlc_registers *get_sdma_mqd(void *mqd)
{
	return (struct cik_sdma_rlc_registers *)mqd;
}

static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t wptr_shadow, is_wptr_shadow_valid;
	struct cik_mqd *m;

	m = get_mqd(mqd);

	is_wptr_shadow_valid = !get_user(wptr_shadow, wptr);

	acquire_queue(kgd, pipe_id, queue_id);
	WREG32(mmCP_MQD_BASE_ADDR, m->cp_mqd_base_addr_lo);
	WREG32(mmCP_MQD_BASE_ADDR_HI, m->cp_mqd_base_addr_hi);
	WREG32(mmCP_MQD_CONTROL, m->cp_mqd_control);

	WREG32(mmCP_HQD_PQ_BASE, m->cp_hqd_pq_base_lo);
	WREG32(mmCP_HQD_PQ_BASE_HI, m->cp_hqd_pq_base_hi);
	WREG32(mmCP_HQD_PQ_CONTROL, m->cp_hqd_pq_control);

	WREG32(mmCP_HQD_IB_CONTROL, m->cp_hqd_ib_control);
	WREG32(mmCP_HQD_IB_BASE_ADDR, m->cp_hqd_ib_base_addr_lo);
	WREG32(mmCP_HQD_IB_BASE_ADDR_HI, m->cp_hqd_ib_base_addr_hi);

	WREG32(mmCP_HQD_IB_RPTR, m->cp_hqd_ib_rptr);

	WREG32(mmCP_HQD_PERSISTENT_STATE, m->cp_hqd_persistent_state);
	WREG32(mmCP_HQD_SEMA_CMD, m->cp_hqd_sema_cmd);
	WREG32(mmCP_HQD_MSG_TYPE, m->cp_hqd_msg_type);

	WREG32(mmCP_HQD_ATOMIC0_PREOP_LO, m->cp_hqd_atomic0_preop_lo);
	WREG32(mmCP_HQD_ATOMIC0_PREOP_HI, m->cp_hqd_atomic0_preop_hi);
	WREG32(mmCP_HQD_ATOMIC1_PREOP_LO, m->cp_hqd_atomic1_preop_lo);
	WREG32(mmCP_HQD_ATOMIC1_PREOP_HI, m->cp_hqd_atomic1_preop_hi);

	WREG32(mmCP_HQD_PQ_RPTR_REPORT_ADDR, m->cp_hqd_pq_rptr_report_addr_lo);
	WREG32(mmCP_HQD_PQ_RPTR_REPORT_ADDR_HI, m->cp_hqd_pq_rptr_report_addr_hi);
	WREG32(mmCP_HQD_PQ_RPTR, m->cp_hqd_pq_rptr);

	WREG32(mmCP_HQD_PQ_WPTR_POLL_ADDR, m->cp_hqd_pq_wptr_poll_addr_lo);
	WREG32(mmCP_HQD_PQ_WPTR_POLL_ADDR_HI, m->cp_hqd_pq_wptr_poll_addr_hi);

	WREG32(mmCP_HQD_PQ_DOORBELL_CONTROL, m->cp_hqd_pq_doorbell_control);

	WREG32(mmCP_HQD_VMID, m->cp_hqd_vmid);

	WREG32(mmCP_HQD_QUANTUM, m->cp_hqd_quantum);

	WREG32(mmCP_HQD_PIPE_PRIORITY, m->cp_hqd_pipe_priority);
	WREG32(mmCP_HQD_QUEUE_PRIORITY, m->cp_hqd_queue_priority);

	WREG32(mmCP_HQD_IQ_RPTR, m->cp_hqd_iq_rptr);

	if (is_wptr_shadow_valid)
		WREG32(mmCP_HQD_PQ_WPTR, wptr_shadow);

	WREG32(mmCP_HQD_ACTIVE, m->cp_hqd_active);
	release_queue(kgd);

	return 0;
}

static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	WREG32(sdma_base_addr + mmSDMA0_RLC0_VIRTUAL_ADDR, m->sdma_rlc_virtual_addr);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_BASE, m->sdma_rlc_rb_base);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_BASE_HI, m->sdma_rlc_rb_base_hi);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_ADDR_LO, m->sdma_rlc_rb_rptr_addr_lo);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_ADDR_HI, m->sdma_rlc_rb_rptr_addr_hi);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_DOORBELL, m->sdma_rlc_doorbell);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL, m->sdma_rlc_rb_cntl);

	return 0;
}

static bool kgd_hqd_is_occupies(struct kgd_dev *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t act;
	bool retval = false;
	uint32_t low, high;

	acquire_queue(kgd, pipe_id, queue_id);
	act = RREG32(mmCP_HQD_ACTIVE);
	if (act) {
		low = lower_32_bits(queue_address >> 8);
		high = upper_32_bits(queue_address >> 8);

		if (low == RREG32(mmCP_HQD_PQ_BASE) &&
				high == RREG32(mmCP_HQD_PQ_BASE_HI))
			retval = true;
	}
	release_queue(kgd);
	return retval;
}

static bool kgd_hqd_sdma_is_occupies(struct kgd_dev *kgd, void *mqd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	sdma_rlc_rb_cntl = RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL);

	if (sdma_rlc_rb_cntl & SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK)
		return true;

	return false;
}

static int kgd_hqd_destroy(struct kgd_dev *kgd, uint32_t reset_type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t temp;

	acquire_queue(kgd, pipe_id, queue_id);
	WREG32(mmCP_HQD_PQ_DOORBELL_CONTROL, 0);

	WREG32(mmCP_HQD_DEQUEUE_REQUEST, reset_type);

	while (true) {
		temp = RREG32(mmCP_HQD_ACTIVE);
		if (temp & CP_HQD_ACTIVE__ACTIVE__SHIFT)
			break;
		if (timeout == 0) {
			pr_err("kfd: cp queue preemption time out (%dms)\n",
				temp);
			return -ETIME;
		}
		msleep(20);
		timeout -= 20;
	}

	release_queue(kgd);
	return 0;
}

static int kgd_hqd_sdma_destroy(struct kgd_dev *kgd, void *mqd,
				unsigned int timeout)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;
	uint32_t temp;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	temp = RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL);
	temp = temp & ~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK;
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL, temp);

	while (true) {
		temp = RREG32(sdma_base_addr + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (temp & SDMA0_STATUS_REG__RB_CMD_IDLE__SHIFT)
			break;
		if (timeout == 0)
			return -ETIME;
		msleep(20);
		timeout -= 20;
	}

	WREG32(sdma_base_addr + mmSDMA0_RLC0_DOORBELL, 0);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR, 0);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_WPTR, 0);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_BASE, 0);

	return 0;
}

static int kgd_address_watch_disable(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	union TCP_WATCH_CNTL_BITS cntl;
	unsigned int i;

	cntl.u32All = 0;

	cntl.bitfields.valid = 0;
	cntl.bitfields.mask = ADDRESS_WATCH_REG_CNTL_DEFAULT_MASK;
	cntl.bitfields.atc = 1;

	/* Turning off this address until we set all the registers */
	for (i = 0; i < MAX_WATCH_ADDRESSES; i++)
		WREG32(watchRegs[i * ADDRESS_WATCH_REG_MAX + ADDRESS_WATCH_REG_CNTL],
			cntl.u32All);

	return 0;
}

static int kgd_address_watch_execute(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	union TCP_WATCH_CNTL_BITS cntl;

	cntl.u32All = cntl_val;

	/* Turning off this watch point until we set all the registers */
	cntl.bitfields.valid = 0;
	WREG32(watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX + ADDRESS_WATCH_REG_CNTL],
		cntl.u32All);

	WREG32(watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX + ADDRESS_WATCH_REG_ADDR_HI],
		addr_hi);

	WREG32(watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX + ADDRESS_WATCH_REG_ADDR_LO],
		addr_lo);

	/* Enable the watch point */
	cntl.bitfields.valid = 1;

	WREG32(watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX + ADDRESS_WATCH_REG_CNTL],
		cntl.u32All);

	return 0;
}

static int kgd_wave_control_execute(struct kgd_dev *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t data;

	mutex_lock(&adev->grbm_idx_mutex);

	WREG32(mmGRBM_GFX_INDEX, gfx_index_val);
	WREG32(mmSQ_CMD, sq_cmd);

	/*  Restore the GRBM_GFX_INDEX register  */

	data = GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK |
		GRBM_GFX_INDEX__SH_BROADCAST_WRITES_MASK |
		GRBM_GFX_INDEX__SE_BROADCAST_WRITES_MASK;

	WREG32(mmGRBM_GFX_INDEX, data);

	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

static uint32_t kgd_address_watch_get_offset(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset)
{
	return watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX + reg_offset];
}

static bool read_atc_vmid_pasid_mapping_reg_valid_field(struct kgd_dev *kgd,
							uint8_t vmid)
{
	uint32_t reg;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	reg = RREG32(mmATC_VMID0_PASID_MAPPING + vmid);
	return reg & ATC_VMID0_PASID_MAPPING__VALID_MASK;
}

static uint16_t read_atc_vmid_pasid_mapping_reg_pasid_field(struct kgd_dev *kgd,
								uint8_t vmid)
{
	uint32_t reg;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	reg = RREG32(mmATC_VMID0_PASID_MAPPING + vmid);
	return reg & ATC_VMID0_PASID_MAPPING__VALID_MASK;
}

static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	WREG32(mmVM_INVALIDATE_REQUEST, 1 << vmid);
}

static int map_bo(struct amdgpu_device *adev, uint64_t va, void *vm, struct amdgpu_bo *bo, struct amdgpu_bo_va **bo_va)
{
	struct amdgpu_vm *rvm = (struct amdgpu_vm *) vm;
	int ret;

	BUG_ON(va == 0);
	BUG_ON(vm == NULL);

	/* Pin bo */
	amdgpu_bo_reserve(bo, true);
	ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_VRAM, NULL);
	if (ret != 0) {
		ret = -EINVAL;
		goto err;
	}

	/* Add the allocation to the VM context */
	*bo_va = amdgpu_vm_bo_add(adev, rvm, bo);
	if (bo_va == NULL) {
		ret = -EINVAL;
		goto err_vmadd;
	}

	/* Set virtual address for the allocation, allocate PTs, if needed, and zero them */
	ret = amdgpu_vm_bo_set_addr(adev, *bo_va, va, AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE);
	if (ret != 0)
		goto err_vmsetaddr;

	mutex_lock(&rvm->mutex);

	/* Update the page tables  */
	ret = amdgpu_vm_bo_update(adev, *bo_va, &bo->tbo.mem);
	if (ret != 0)
		goto err_update_pt;

	/* Update the page directory */
	ret = amdgpu_vm_update_page_directory(adev, rvm);
	if (ret != 0)
		goto err_update_pd;

	mutex_unlock(&rvm->mutex);

	/* Wait for the page table update to complete. */
#if 0
	ret = amdgpu_fence_wait(rvm->fence, true);
#endif
	if (ret != 0)
		goto err_wait_fence;

	amdgpu_bo_unreserve(bo);

	return 0;

err_wait_fence:
err_update_pt:
err_update_pd:
	mutex_unlock(&rvm->mutex);
err_vmsetaddr:
	amdgpu_vm_bo_rmv(adev, *bo_va);
#ifndef AMDKFD_SKIP_UNCOMPILED_CODE
	mutex_lock(&rvm->mutex);
	amdgpu_vm_clear_freed(adev, rvm);
	mutex_unlock(&rvm->mutex);
#endif
err_vmadd:
	amdgpu_bo_unpin(bo);
	amdgpu_bo_unreserve(bo);
err:
	return ret;
}

