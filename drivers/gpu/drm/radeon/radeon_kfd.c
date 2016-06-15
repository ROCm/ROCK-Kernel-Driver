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
#include "radeon.h"
#include "cikd.h"
#include "cik_reg.h"
#include "radeon_kfd.h"
#include "radeon_ucode.h"
#include <linux/firmware.h>
#include "cik_structs.h"

#define CIK_PIPE_PER_MEC	(4)

static const uint32_t watchRegs[MAX_WATCH_ADDRESSES * ADDRESS_WATCH_REG_MAX] = {
	TCP_WATCH0_ADDR_H, TCP_WATCH0_ADDR_L, TCP_WATCH0_CNTL,
	TCP_WATCH1_ADDR_H, TCP_WATCH1_ADDR_L, TCP_WATCH1_CNTL,
	TCP_WATCH2_ADDR_H, TCP_WATCH2_ADDR_L, TCP_WATCH2_CNTL,
	TCP_WATCH3_ADDR_H, TCP_WATCH3_ADDR_L, TCP_WATCH3_CNTL
};

struct kgd_mem {
	union {
		struct {
			struct radeon_bo *bo;
			uint64_t gpu_addr;
			void *cpu_ptr;
		} data1;
		struct {
			struct mutex lock;
			struct radeon_bo *bo;
			struct radeon_bo_va *bo_va;
			bool mapped_to_gpu_memory;
		} data2;
	};
};
/* Helper functions*/
static int add_bo_to_vm(struct radeon_device *rdev, uint64_t va,
			struct radeon_vm *vm, struct radeon_bo *bo,
			struct radeon_bo_va **bo_va);
static int map_bo_to_gpuvm(struct radeon_device *rdev, struct radeon_bo *bo,
		struct radeon_bo_va *bo_va);
static int unmap_bo_from_gpuvm(struct radeon_device *rdev,
				struct radeon_bo_va *bo_va);
static void remove_bo_from_vm(struct radeon_device *rdev, struct radeon_bo *bo,
				struct radeon_bo_va *bo_va);

static int alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
			void **mem_obj, uint64_t *gpu_addr,
			void **cpu_ptr);

static void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj);

static void get_local_mem_info(struct kgd_dev *kgd,
		struct kfd_local_mem_info *mem_info);
static uint64_t get_gpu_clock_counter(struct kgd_dev *kgd);

static uint32_t get_max_engine_clock_in_mhz(struct kgd_dev *kgd);

static int create_process_vm(struct kgd_dev *kgd, void **vm);
static void destroy_process_vm(struct kgd_dev *kgd, void *vm);

static uint32_t get_process_page_dir(void *vm);

static int open_graphic_handle(struct kgd_dev *kgd, uint64_t va, void *vm, int fd, uint32_t handle, struct kgd_mem **mem);
static int map_memory_to_gpu(struct kgd_dev *kgd, struct kgd_mem *mem,
		void *vm);
static int unmap_memory_from_gpu(struct kgd_dev *kgd, struct kgd_mem *mem,
		void *vm);
static int alloc_memory_of_gpu(struct kgd_dev *kgd, uint64_t va, uint64_t size,
		void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr,
		struct kfd_process_device *pdd, uint32_t flags);
static int free_memory_of_gpu(struct kgd_dev *kgd, struct kgd_mem *mem);

static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type);

/*
 * Register access functions
 */

static void kgd_program_sh_mem_settings(struct kgd_dev *kgd, uint32_t vmid,
		uint32_t sh_mem_config,	uint32_t sh_mem_ape1_base,
		uint32_t sh_mem_ape1_limit, uint32_t sh_mem_bases);

static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
					unsigned int vmid);

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr);
static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id);
static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
		uint32_t queue_id, uint32_t __user *wptr,
		uint32_t page_table_base);
static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd);
static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id);

static int kgd_hqd_destroy(struct kgd_dev *kgd, uint32_t reset_type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id);
static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd);
static int kgd_hqd_sdma_destroy(struct kgd_dev *kgd, void *mqd,
				unsigned int timeout);
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

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd, uint8_t vmid);
static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
							uint8_t vmid);
static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid);
static void set_num_of_requests(struct kgd_dev *dev, uint8_t num_of_req);
static void get_cu_info(struct kgd_dev *kgd, struct kfd_cu_info *cu_info);
static int alloc_memory_of_scratch(struct kgd_dev *kgd,
					 uint64_t va, uint32_t vmid);
static int write_config_static_mem(struct kgd_dev *kgd, bool swizzle_enable,
		uint8_t element_size, uint8_t index_stride, uint8_t mtype);
static int mmap_bo(struct kgd_dev *kgd, struct vm_area_struct *vma);
static int map_gtt_bo_to_kernel(struct kgd_dev *kgd,
			struct kgd_mem *mem, void **kptr);
static void set_vm_context_page_table_base(struct kgd_dev *kgd, uint32_t vmid,
			uint32_t page_table_base);
struct kfd_process_device *get_pdd_from_buffer_object(struct kgd_dev *kgd,
		struct kgd_mem *mem);
static int return_bo_size(struct kgd_dev *kgd, struct kgd_mem *mem);

static const struct kfd2kgd_calls kfd2kgd = {
	.init_gtt_mem_allocation = alloc_gtt_mem,
	.free_gtt_mem = free_gtt_mem,
	.get_local_mem_info = get_local_mem_info,
	.get_gpu_clock_counter = get_gpu_clock_counter,
	.get_max_engine_clock_in_mhz = get_max_engine_clock_in_mhz,
	.create_process_vm = create_process_vm,
	.destroy_process_vm = destroy_process_vm,
	.get_process_page_dir = get_process_page_dir,
	.open_graphic_handle = open_graphic_handle,
	.program_sh_mem_settings = kgd_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_set_pasid_vmid_mapping,
	.init_pipeline = kgd_init_pipeline,
	.init_interrupts = kgd_init_interrupts,
	.hqd_load = kgd_hqd_load,
	.hqd_sdma_load = kgd_hqd_sdma_load,
	.hqd_is_occupied = kgd_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_hqd_destroy,
	.hqd_sdma_destroy = kgd_hqd_sdma_destroy,
	.address_watch_disable = kgd_address_watch_disable,
	.address_watch_execute = kgd_address_watch_execute,
	.wave_control_execute = kgd_wave_control_execute,
	.address_watch_get_offset = kgd_address_watch_get_offset,
	.get_atc_vmid_pasid_mapping_pasid = get_atc_vmid_pasid_mapping_pasid,
	.get_atc_vmid_pasid_mapping_valid = get_atc_vmid_pasid_mapping_valid,
	.write_vmid_invalidate_request = write_vmid_invalidate_request,
	.alloc_memory_of_gpu = alloc_memory_of_gpu,
	.free_memory_of_gpu = free_memory_of_gpu,
	.map_memory_to_gpu = map_memory_to_gpu,
	.unmap_memory_to_gpu = unmap_memory_from_gpu,
	.get_fw_version = get_fw_version,
	.set_num_of_requests = set_num_of_requests,
	.get_cu_info = get_cu_info,
	.alloc_memory_of_scratch = alloc_memory_of_scratch,
	.write_config_static_mem = write_config_static_mem,
	.mmap_bo = mmap_bo,
	.map_gtt_bo_to_kernel = map_gtt_bo_to_kernel,
	.get_pdd_from_buffer_object = get_pdd_from_buffer_object,
	.set_vm_context_page_table_base = set_vm_context_page_table_base,
	.return_bo_size = return_bo_size
};

static const struct kgd2kfd_calls *kgd2kfd;

static int return_bo_size(struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct radeon_bo *bo;

	BUG_ON(mem == NULL);

	bo = mem->data2.bo;
	return bo->tbo.mem.size;

}

struct kfd_process_device *get_pdd_from_buffer_object(struct kgd_dev *kgd,
		struct kgd_mem *mem)
{

	return mem->data2.bo->pdd;
}

int radeon_kfd_init(void)
{
	int ret;

#if defined(CONFIG_HSA_AMD_MODULE)
	int (*kgd2kfd_init_p)(unsigned, const struct kgd2kfd_calls**);

	kgd2kfd_init_p = symbol_request(kgd2kfd_init);

	if (kgd2kfd_init_p == NULL)
		return -ENOENT;

	ret = kgd2kfd_init_p(KFD_INTERFACE_VERSION, &kgd2kfd);
	if (ret) {
		symbol_put(kgd2kfd_init);
		kgd2kfd = NULL;
	}

#elif defined(CONFIG_HSA_AMD)
	ret = kgd2kfd_init(KFD_INTERFACE_VERSION, &kgd2kfd);
	if (ret)
		kgd2kfd = NULL;

#else
	ret = -ENOENT;
#endif

	return ret;
}

void radeon_kfd_fini(void)
{
	if (kgd2kfd) {
		kgd2kfd->exit();
		symbol_put(kgd2kfd_init);
	}
}

void radeon_kfd_device_probe(struct radeon_device *rdev)
{
	if (kgd2kfd)
		rdev->kfd = kgd2kfd->probe((struct kgd_dev *)rdev,
			rdev->pdev, &kfd2kgd);
}

void radeon_kfd_device_init(struct radeon_device *rdev)
{
	if (rdev->kfd) {
		struct kgd2kfd_shared_resources gpu_resources = {
			.compute_vmid_bitmap = 0xFF00,

			.first_compute_pipe = 1,
			.compute_pipe_count = 4 - 1,
			.gpuvm_size = (uint64_t)radeon_vm_size << 30
		};

		radeon_doorbell_get_kfd_info(rdev,
				&gpu_resources.doorbell_physical_address,
				&gpu_resources.doorbell_aperture_size,
				&gpu_resources.doorbell_start_offset);

		kgd2kfd->device_init(rdev->kfd, &gpu_resources);
	}
}

void radeon_kfd_device_fini(struct radeon_device *rdev)
{
	if (rdev->kfd) {
		kgd2kfd->device_exit(rdev->kfd);
		rdev->kfd = NULL;
	}
}

void radeon_kfd_interrupt(struct radeon_device *rdev, const void *ih_ring_entry)
{
	if (rdev->kfd)
		kgd2kfd->interrupt(rdev->kfd, ih_ring_entry);
}

void radeon_kfd_suspend(struct radeon_device *rdev)
{
	if (rdev->kfd)
		kgd2kfd->suspend(rdev->kfd);
}

int radeon_kfd_resume(struct radeon_device *rdev)
{
	int r = 0;

	if (rdev->kfd)
		r = kgd2kfd->resume(rdev->kfd);

	return r;
}

static int alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
			void **mem_obj, uint64_t *gpu_addr,
			void **cpu_ptr)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;
	struct kgd_mem **mem = (struct kgd_mem **) mem_obj;
	int r;

	BUG_ON(kgd == NULL);
	BUG_ON(gpu_addr == NULL);
	BUG_ON(cpu_ptr == NULL);

	*mem = kmalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if ((*mem) == NULL)
		return -ENOMEM;

	r = radeon_bo_create(rdev, size, PAGE_SIZE, true, RADEON_GEM_DOMAIN_GTT,
				RADEON_GEM_GTT_WC, NULL, NULL,
				&(*mem)->data1.bo);
	if (r) {
		dev_err(rdev->dev,
			"failed to allocate BO for amdkfd (%d)\n", r);
		return r;
	}

	/* map the buffer */
	r = radeon_bo_reserve((*mem)->data1.bo, true);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to reserve bo for amdkfd\n", r);
		goto allocate_mem_reserve_bo_failed;
	}

	r = radeon_bo_pin((*mem)->data1.bo, RADEON_GEM_DOMAIN_GTT,
				&(*mem)->data1.gpu_addr);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to pin bo for amdkfd\n", r);
		goto allocate_mem_pin_bo_failed;
	}
	*gpu_addr = (*mem)->data1.gpu_addr;

	r = radeon_bo_kmap((*mem)->data1.bo, &(*mem)->data1.cpu_ptr);
	if (r) {
		dev_err(rdev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", r);
		goto allocate_mem_kmap_bo_failed;
	}
	*cpu_ptr = (*mem)->data1.cpu_ptr;

	radeon_bo_unreserve((*mem)->data1.bo);

	return 0;

allocate_mem_kmap_bo_failed:
	radeon_bo_unpin((*mem)->data1.bo);
allocate_mem_pin_bo_failed:
	radeon_bo_unreserve((*mem)->data1.bo);
allocate_mem_reserve_bo_failed:
	radeon_bo_unref(&(*mem)->data1.bo);

	return r;
}

static void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj)
{
	struct kgd_mem *mem = (struct kgd_mem *) mem_obj;

	BUG_ON(mem == NULL);

	radeon_bo_reserve(mem->data1.bo, true);
	radeon_bo_kunmap(mem->data1.bo);
	radeon_bo_unpin(mem->data1.bo);
	radeon_bo_unreserve(mem->data1.bo);
	radeon_bo_unref(&(mem->data1.bo));
	kfree(mem);
}

void get_local_mem_info(struct kgd_dev *kgd,
			struct kfd_local_mem_info *mem_info)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;

	BUG_ON(kgd == NULL);

	memset(mem_info, 0, sizeof(*mem_info));
	mem_info->local_mem_size_public = rdev->mc.visible_vram_size;
			mem_info->local_mem_size_private =
					rdev->mc.real_vram_size -
					rdev->mc.visible_vram_size;
	mem_info->vram_width = rdev->mc.vram_width;
	mem_info->mem_clk_max = radeon_dpm_get_mclk(rdev, false);
}

static uint64_t get_gpu_clock_counter(struct kgd_dev *kgd)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;

	return rdev->asic->get_gpu_clock_counter(rdev);
}

static uint32_t get_max_engine_clock_in_mhz(struct kgd_dev *kgd)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;

	/* The sclk is in quantas of 10kHz */
	return rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.sclk / 100;
}

/*
 * Creates a VM context for HSA process
 */
static int create_process_vm(struct kgd_dev *kgd, void **vm)
{
	int ret;
	struct radeon_vm *new_vm;
	struct radeon_device *rdev = (struct radeon_device *) kgd;

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	new_vm = kzalloc(sizeof(struct radeon_vm), GFP_KERNEL);
	if (new_vm == NULL)
		return -ENOMEM;

	/* Initialize the VM context, allocate the page directory and zero it */
	ret = radeon_vm_init(rdev, new_vm);
	if (ret != 0) {
		/* Undo everything related to the new VM context */
		radeon_vm_fini(rdev, new_vm);
		kfree(new_vm);
		new_vm = NULL;
	}

	*vm = (void *) new_vm;

	pr_debug("Created process vm with address %p\n", *vm);

	return ret;
}

/*
 * Destroys a VM context of HSA process
 */
static void destroy_process_vm(struct kgd_dev *kgd, void *vm)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	struct radeon_vm *rvm = (struct radeon_vm *) vm;

	BUG_ON(kgd == NULL);
	BUG_ON(vm == NULL);

	pr_debug("Destroying process vm with address %p\n", vm);

	/* Release the VM context */
	radeon_vm_fini(rdev, rvm);
	kfree(vm);
}

static uint32_t get_process_page_dir(void *vm)
{
	struct radeon_vm *rvm = (struct radeon_vm *) vm;
	struct radeon_vm_id *vm_id;

	BUG_ON(rvm == NULL);

	vm_id = &rvm->ids[CAYMAN_RING_TYPE_CP1_INDEX];

	return vm_id->pd_gpu_addr >> RADEON_GPU_PAGE_SHIFT;
}

static int open_graphic_handle(struct kgd_dev *kgd, uint64_t va, void *vm,
				int fd, uint32_t handle, struct kgd_mem **mem)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	int ret;
	struct radeon_bo_va *bo_va;
	struct radeon_bo *bo;
	struct file *filp;
	struct drm_gem_object *gem_obj;

	BUG_ON(kgd == NULL);
	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);
	BUG_ON(vm == NULL);

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (!*mem) {
		ret = -ENOMEM;
		goto err;
	}
	mutex_init(&(*mem)->data2.lock);

	/* Translate fd to file */
	rcu_read_lock();
	filp = fcheck(fd);
	rcu_read_unlock();

	BUG_ON(filp == NULL);

	/* Get object by handle*/
	gem_obj = drm_gem_object_lookup(filp->private_data, handle);
	BUG_ON(gem_obj == NULL);

	/* No need to increment GEM refcount*/
	drm_gem_object_unreference(gem_obj);

	bo = gem_to_radeon_bo(gem_obj);

	/* Inc TTM refcount*/
	ttm_bo_reference(&bo->tbo);

	ret = add_bo_to_vm(rdev, va, vm, bo, &bo_va);
	if (ret != 0)
		goto err_map;

	/* The allocated BO, PD and appropriate PTs are pinned, virtual to MC address mapping created */
	ret = map_bo_to_gpuvm(rdev, bo, bo_va);
	if (ret != 0)
		goto err_failed_to_pin_bo;

	(*mem)->data2.bo = bo;
	(*mem)->data2.bo_va = bo_va;
	return 0;

err_failed_to_pin_bo:
	remove_bo_from_vm(rdev, bo, bo_va);
err_map:
	radeon_bo_unref(&bo);
	kfree(*mem);
err:
	return ret;
}

static inline struct radeon_device *get_radeon_device(struct kgd_dev *kgd)
{
	return (struct radeon_device *)kgd;
}

static void write_register(struct kgd_dev *kgd, uint32_t offset, uint32_t value)
{
	struct radeon_device *rdev = get_radeon_device(kgd);

	writel(value, (void __iomem *)(rdev->rmmio + offset));
}

static uint32_t read_register(struct kgd_dev *kgd, uint32_t offset)
{
	struct radeon_device *rdev = get_radeon_device(kgd);

	return readl((void __iomem *)(rdev->rmmio + offset));
}

static void lock_srbm(struct kgd_dev *kgd, uint32_t mec, uint32_t pipe,
			uint32_t queue, uint32_t vmid)
{
	struct radeon_device *rdev = get_radeon_device(kgd);
	uint32_t value = PIPEID(pipe) | MEID(mec) | VMID(vmid) | QUEUEID(queue);

	mutex_lock(&rdev->srbm_mutex);
	write_register(kgd, SRBM_GFX_CNTL, value);
}

static void unlock_srbm(struct kgd_dev *kgd)
{
	struct radeon_device *rdev = get_radeon_device(kgd);

	write_register(kgd, SRBM_GFX_CNTL, 0);
	mutex_unlock(&rdev->srbm_mutex);
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
	lock_srbm(kgd, 0, 0, 0, vmid);

	write_register(kgd, SH_MEM_CONFIG, sh_mem_config);
	write_register(kgd, SH_MEM_APE1_BASE, sh_mem_ape1_base);
	write_register(kgd, SH_MEM_APE1_LIMIT, sh_mem_ape1_limit);
	write_register(kgd, SH_MEM_BASES, sh_mem_bases);

	unlock_srbm(kgd);
}

static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
					unsigned int vmid)
{
	/*
	 * We have to assume that there is no outstanding mapping.
	 * The ATC_VMID_PASID_MAPPING_UPDATE_STATUS bit could be 0
	 * because a mapping is in progress or because a mapping finished and
	 * the SW cleared it.
	 * So the protocol is to always wait & clear.
	 */
	uint32_t pasid_mapping = (pasid == 0) ? 0 : (uint32_t)pasid | 
					ATC_VMID_PASID_MAPPING_VALID_MASK;

	write_register(kgd, ATC_VMID0_PASID_MAPPING + vmid*sizeof(uint32_t),
			pasid_mapping);

	while (!(read_register(kgd, ATC_VMID_PASID_MAPPING_UPDATE_STATUS) &
								(1U << vmid)))
		cpu_relax();
	write_register(kgd, ATC_VMID_PASID_MAPPING_UPDATE_STATUS, 1U << vmid);

	/* Mapping vmid to pasid also for IH block */
	write_register(kgd, IH_VMID_0_LUT + vmid * sizeof(uint32_t),
			pasid_mapping);

	return 0;
}

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr)
{
	uint32_t mec = (pipe_id / CIK_PIPE_PER_MEC) + 1;
	uint32_t pipe = (pipe_id % CIK_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, 0, 0);
	write_register(kgd, CP_HPD_EOP_BASE_ADDR,
			lower_32_bits(hpd_gpu_addr >> 8));
	write_register(kgd, CP_HPD_EOP_BASE_ADDR_HI,
			upper_32_bits(hpd_gpu_addr >> 8));
	write_register(kgd, CP_HPD_EOP_VMID, 0);
	write_register(kgd, CP_HPD_EOP_CONTROL, hpd_size);
	unlock_srbm(kgd);

	return 0;
}

static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id)
{
	uint32_t mec;
	uint32_t pipe;

	mec = (++pipe_id / CIK_PIPE_PER_MEC) + 1;
	pipe = (pipe_id % CIK_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, 0, 0);

	write_register(kgd, CPC_INT_CNTL,
			TIME_STAMP_INT_ENABLE | OPCODE_ERROR_INT_ENABLE);

	unlock_srbm(kgd);

	return 0;
}

static inline uint32_t get_sdma_base_addr(struct cik_sdma_rlc_registers *m)
{
	uint32_t retval;

	retval = m->sdma_engine_id * SDMA1_REGISTER_OFFSET +
			m->sdma_queue_id * KFD_CIK_SDMA_QUEUE_OFFSET;

	pr_debug("kfd: sdma base address: 0x%x\n", retval);

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
		uint32_t queue_id, uint32_t __user *wptr,
		uint32_t page_table_base)
{
	uint32_t wptr_shadow, is_wptr_shadow_valid;
	struct cik_mqd *m;

	m = get_mqd(mqd);

	is_wptr_shadow_valid = !get_user(wptr_shadow, wptr);

	acquire_queue(kgd, pipe_id, queue_id);

	write_register(kgd, COMPUTE_STATIC_THREAD_MGMT_SE0,
			m->compute_static_thread_mgmt_se0);
	write_register(kgd, COMPUTE_STATIC_THREAD_MGMT_SE1,
			m->compute_static_thread_mgmt_se1);
	write_register(kgd, COMPUTE_STATIC_THREAD_MGMT_SE2,
			m->compute_static_thread_mgmt_se2);
	write_register(kgd, COMPUTE_STATIC_THREAD_MGMT_SE3,
			m->compute_static_thread_mgmt_se3);

	write_register(kgd, CP_MQD_BASE_ADDR, m->cp_mqd_base_addr_lo);
	write_register(kgd, CP_MQD_BASE_ADDR_HI, m->cp_mqd_base_addr_hi);
	write_register(kgd, CP_MQD_CONTROL, m->cp_mqd_control);

	write_register(kgd, CP_HQD_PQ_BASE, m->cp_hqd_pq_base_lo);
	write_register(kgd, CP_HQD_PQ_BASE_HI, m->cp_hqd_pq_base_hi);
	write_register(kgd, CP_HQD_PQ_CONTROL, m->cp_hqd_pq_control);

	write_register(kgd, CP_HQD_IB_CONTROL, m->cp_hqd_ib_control);
	write_register(kgd, CP_HQD_IB_BASE_ADDR, m->cp_hqd_ib_base_addr_lo);
	write_register(kgd, CP_HQD_IB_BASE_ADDR_HI, m->cp_hqd_ib_base_addr_hi);

	write_register(kgd, CP_HQD_IB_RPTR, m->cp_hqd_ib_rptr);

	write_register(kgd, CP_HQD_PERSISTENT_STATE,
			m->cp_hqd_persistent_state);
	write_register(kgd, CP_HQD_SEMA_CMD, m->cp_hqd_sema_cmd);
	write_register(kgd, CP_HQD_MSG_TYPE, m->cp_hqd_msg_type);

	write_register(kgd, CP_HQD_ATOMIC0_PREOP_LO,
			m->cp_hqd_atomic0_preop_lo);

	write_register(kgd, CP_HQD_ATOMIC0_PREOP_HI,
			m->cp_hqd_atomic0_preop_hi);

	write_register(kgd, CP_HQD_ATOMIC1_PREOP_LO,
			m->cp_hqd_atomic1_preop_lo);

	write_register(kgd, CP_HQD_ATOMIC1_PREOP_HI,
			m->cp_hqd_atomic1_preop_hi);

	write_register(kgd, CP_HQD_PQ_RPTR_REPORT_ADDR,
			m->cp_hqd_pq_rptr_report_addr_lo);

	write_register(kgd, CP_HQD_PQ_RPTR_REPORT_ADDR_HI,
			m->cp_hqd_pq_rptr_report_addr_hi);

	write_register(kgd, CP_HQD_PQ_RPTR, m->cp_hqd_pq_rptr);

	write_register(kgd, CP_HQD_PQ_WPTR_POLL_ADDR,
			m->cp_hqd_pq_wptr_poll_addr_lo);

	write_register(kgd, CP_HQD_PQ_WPTR_POLL_ADDR_HI,
			m->cp_hqd_pq_wptr_poll_addr_hi);

	write_register(kgd, CP_HQD_PQ_DOORBELL_CONTROL,
			m->cp_hqd_pq_doorbell_control);

	write_register(kgd, CP_HQD_VMID, m->cp_hqd_vmid);

	write_register(kgd, CP_HQD_QUANTUM, m->cp_hqd_quantum);

	write_register(kgd, CP_HQD_PIPE_PRIORITY, m->cp_hqd_pipe_priority);
	write_register(kgd, CP_HQD_QUEUE_PRIORITY, m->cp_hqd_queue_priority);

	write_register(kgd, CP_HQD_IQ_RPTR, m->cp_hqd_iq_rptr);

	if (is_wptr_shadow_valid)
		write_register(kgd, CP_HQD_PQ_WPTR, wptr_shadow);

	write_register(kgd, CP_HQD_ACTIVE, m->cp_hqd_active);
	release_queue(kgd);

	return 0;
}

static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd)
{
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_VIRTUAL_ADDR,
			m->sdma_rlc_virtual_addr);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_BASE,
			m->sdma_rlc_rb_base);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_BASE_HI,
			m->sdma_rlc_rb_base_hi);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_RPTR_ADDR_LO,
			m->sdma_rlc_rb_rptr_addr_lo);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_RPTR_ADDR_HI,
			m->sdma_rlc_rb_rptr_addr_hi);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_DOORBELL,
			m->sdma_rlc_doorbell);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_CNTL,
			m->sdma_rlc_rb_cntl);

	return 0;
}

static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id)
{
	uint32_t act;
	bool retval = false;
	uint32_t low, high;

	acquire_queue(kgd, pipe_id, queue_id);
	act = read_register(kgd, CP_HQD_ACTIVE);
	if (act) {
		low = lower_32_bits(queue_address >> 8);
		high = upper_32_bits(queue_address >> 8);

		if (low == read_register(kgd, CP_HQD_PQ_BASE) &&
				high == read_register(kgd, CP_HQD_PQ_BASE_HI))
			retval = true;
	}
	release_queue(kgd);
	return retval;
}

static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd)
{
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	sdma_rlc_rb_cntl = read_register(kgd,
					sdma_base_addr + SDMA0_RLC0_RB_CNTL);

	if (sdma_rlc_rb_cntl & SDMA_RB_ENABLE)
		return true;

	return false;
}

static int kgd_hqd_destroy(struct kgd_dev *kgd, uint32_t reset_type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id)
{
	uint32_t temp;

	acquire_queue(kgd, pipe_id, queue_id);
	write_register(kgd, CP_HQD_PQ_DOORBELL_CONTROL, 0);

	write_register(kgd, CP_HQD_DEQUEUE_REQUEST, reset_type);

	while (true) {
		temp = read_register(kgd, CP_HQD_ACTIVE);
		if (temp & 0x1)
			break;
		if (timeout == 0) {
			pr_err("kfd: cp queue preemption time out (%dms)\n",
				temp);
			release_queue(kgd);
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
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;
	uint32_t temp;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	temp = read_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_CNTL);
	temp = temp & ~SDMA_RB_ENABLE;
	write_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_CNTL, temp);

	while (true) {
		temp = read_register(kgd, sdma_base_addr +
						SDMA0_RLC0_CONTEXT_STATUS);
		if (temp & SDMA_RLC_IDLE)
			break;
		if (timeout == 0)
			return -ETIME;
		msleep(20);
		timeout -= 20;
	}

	write_register(kgd, sdma_base_addr + SDMA0_RLC0_DOORBELL, 0);
	write_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_RPTR, 0);
	write_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_WPTR, 0);
	write_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_BASE, 0);

	return 0;
}

static int kgd_address_watch_disable(struct kgd_dev *kgd)
{
	union TCP_WATCH_CNTL_BITS cntl;
	unsigned int i;

	cntl.u32All = 0;

	cntl.bitfields.valid = 0;
	cntl.bitfields.mask = ADDRESS_WATCH_REG_CNTL_DEFAULT_MASK;
	cntl.bitfields.atc = 1;

	/* Turning off this address until we set all the registers */
	for (i = 0; i < MAX_WATCH_ADDRESSES; i++)
		write_register(kgd,
				watchRegs[i * ADDRESS_WATCH_REG_MAX +
				          ADDRESS_WATCH_REG_CNTL],
				cntl.u32All);

	return 0;
}

static int kgd_address_watch_execute(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo)
{
	union TCP_WATCH_CNTL_BITS cntl;

	cntl.u32All = cntl_val;

	/* Turning off this watch point until we set all the registers */
	cntl.bitfields.valid = 0;
	write_register(kgd,
			watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
			          ADDRESS_WATCH_REG_CNTL],
			cntl.u32All);

	write_register(kgd,
			watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
			          ADDRESS_WATCH_REG_ADDR_HI],
			addr_hi);

	write_register(kgd,
			watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
			          ADDRESS_WATCH_REG_ADDR_LO],
			addr_lo);

	/* Enable the watch point */
	cntl.bitfields.valid = 1;

	write_register(kgd,
			watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
			          ADDRESS_WATCH_REG_CNTL],
			cntl.u32All);

	return 0;
}

static int kgd_wave_control_execute(struct kgd_dev *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd)
{
	struct radeon_device *rdev = get_radeon_device(kgd);
	uint32_t data;

	mutex_lock(&rdev->grbm_idx_mutex);

	write_register(kgd, GRBM_GFX_INDEX, gfx_index_val);
	write_register(kgd, SQ_CMD, sq_cmd);

	/*  Restore the GRBM_GFX_INDEX register  */

	data = INSTANCE_BROADCAST_WRITES | SH_BROADCAST_WRITES |
		SE_BROADCAST_WRITES;

	write_register(kgd, GRBM_GFX_INDEX, data);

	mutex_unlock(&rdev->grbm_idx_mutex);

	return 0;
}

static uint32_t kgd_address_watch_get_offset(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset)
{
	return (
	(watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX + reg_offset]) >> 2
	);
}

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd, uint8_t vmid)
{
	uint32_t reg;
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	reg = RREG32(ATC_VMID0_PASID_MAPPING + vmid*4);
	return reg & ATC_VMID_PASID_MAPPING_VALID_MASK;
}

static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
							uint8_t vmid)
{
	uint32_t reg;
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	reg = RREG32(ATC_VMID0_PASID_MAPPING + vmid*4);
	return reg & ATC_VMID_PASID_MAPPING_PASID_MASK;
}

static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	return WREG32(VM_INVALIDATE_REQUEST, 1 << vmid);
}

static int add_bo_to_vm(struct radeon_device *rdev, uint64_t va,
			struct radeon_vm *rvm, struct radeon_bo *bo,
			struct radeon_bo_va **bo_va)
{
	int ret;

	BUG_ON(va == 0);

	radeon_bo_reserve(bo, true);

	/* Add BO to VM internal data structures*/
	*bo_va = radeon_vm_bo_add(rdev, rvm, bo);
	if (*bo_va == NULL) {
		ret = -EINVAL;
		pr_err("amdkfd: Failed to add BO object to VM. ret == %d\n",
				ret);
		goto err_vmadd;
	}

	/*
	 * Set virtual address for the allocation, allocate PTs, if needed,
	 * and zero them
	 */
	ret = radeon_vm_bo_set_addr(rdev, *bo_va, va,
			RADEON_VM_PAGE_READABLE | RADEON_VM_PAGE_WRITEABLE);
	if (ret != 0) {
		pr_err("amdkfd: Failed to set virtual address for BO. ret == %d\n",
				ret);
		pr_debug("va == 0x%08llx\n", va);
		goto err_vmsetaddr;
	}

	return 0;

err_vmsetaddr:
	radeon_vm_bo_rmv(rdev, *bo_va);
	mutex_lock(&rvm->mutex);
	radeon_vm_clear_freed(rdev, rvm);
	mutex_unlock(&rvm->mutex);
	/* Don't fall through to unreserve because the BO was already
	   unreserved by radeon_vm_bo_set_addr. */
	return ret;
err_vmadd:
	radeon_bo_unreserve(bo);
	return ret;
}

static void remove_bo_from_vm(struct radeon_device *rdev, struct radeon_bo *bo,
				struct radeon_bo_va *bo_va)
{
	radeon_bo_reserve(bo, true);
	radeon_vm_bo_rmv(rdev, bo_va);
	radeon_bo_unreserve(bo);
}


static int try_pin_bo(struct radeon_bo *bo, uint64_t *mc_address, bool resv)
{
	int ret;

	if (resv) {
		ret = radeon_bo_reserve(bo, true);
		if (ret != 0)
			return ret;
	}

	ret = radeon_bo_pin(bo, RADEON_GEM_DOMAIN_VRAM, mc_address);
	if (ret != 0) {
		if (resv)
			radeon_bo_unreserve(bo);
		return ret;
	}

	if (resv)
		radeon_bo_unreserve(bo);

	return 0;
}

static int unpin_bo(struct radeon_bo *bo, bool resv)
{
	int ret;

	if (resv) {
		ret = radeon_bo_reserve(bo, true);
		if (ret != 0)
			return ret;
	}

	ret = radeon_bo_unpin(bo);
	if (ret != 0) {
		if (resv)
			radeon_bo_unreserve(bo);
		return ret;
	}

	if (resv)
		radeon_bo_unreserve(bo);

	return 0;
}


static int try_pin_pts(struct radeon_bo_va *bo_va, bool resv)
{
	int ret;
	uint64_t pt_idx, start, last, failed;
	struct radeon_vm *vm;

	vm = bo_va->vm;
	start = bo_va->it.start >> radeon_vm_block_size;
	last = bo_va->it.last >> radeon_vm_block_size;

	pr_debug("start PT index %llu  last PT index %llu\n", start, last);

	/* walk over the address space and pin the page tables BOs*/
	for (pt_idx = start; pt_idx <= last; pt_idx++) {
		ret = try_pin_bo(vm->page_tables[pt_idx].bo, NULL, resv);
		if (ret != 0) {
			failed = pt_idx;
			goto err;
		}
	}

	return 0;

err:
	/* Unpin all already pinned BOs*/
	if (failed > 0) {
		for (pt_idx = start; pt_idx <= failed - 1; pt_idx++)
			unpin_bo(vm->page_tables[pt_idx].bo, resv);
	}
	return ret;
}

static void unpin_pts(struct radeon_bo_va *bo_va, struct radeon_vm *vm,
			bool resv)
{
	uint64_t pt_idx, start, last;

	start = bo_va->it.start >> radeon_vm_block_size;
	last = bo_va->it.last >> radeon_vm_block_size;

	pr_debug("start PT index %llu  last PT index %llu\n", start, last);

	/* walk over the address space and unpin the page tables BOs*/
	for (pt_idx = start; pt_idx <= last; pt_idx++)
		unpin_bo(vm->page_tables[pt_idx].bo, resv);

}

static int map_bo_to_gpuvm(struct radeon_device *rdev, struct radeon_bo *bo,
		struct radeon_bo_va *bo_va)
{
	struct radeon_vm_id *vm_id;
	struct radeon_vm *vm;
	int ret;
	struct radeon_bo_list *vm_bos, *lobj;
	struct ww_acquire_ctx ticket;
	struct list_head list;

	INIT_LIST_HEAD(&list);

	vm = bo_va->vm;

	/* Pin BO*/
	ret = try_pin_bo(bo, NULL, true);
	if (ret != 0) {
		pr_err("amdkfd: Failed to pin BO\n");
		return ret;
	}

	vm_bos = radeon_vm_get_bos(rdev, vm, &list);
	if (!vm_bos) {
		pr_err("amdkfd: Failed to get bos from vm\n");
		goto err_failed_to_get_bos;
	}

	ret = ttm_eu_reserve_buffers(&ticket, &list, false, NULL);
	if (ret) {
		pr_err("amdkfd: Failed to reserve buffers in ttm\n");
		goto err_failed_to_ttm_reserve;
	}

	/* Pin PTs */
	ret = try_pin_pts(bo_va, false);
	if (ret != 0) {
		pr_err("amdkfd: Failed to pin PTs\n");
		goto err_failed_to_pin_pts;
	}

	/* Pin the PD directory*/
	vm_id = &vm->ids[CAYMAN_RING_TYPE_CP1_INDEX];
	ret = try_pin_bo(vm->page_directory, &vm_id->pd_gpu_addr, false);
	if (ret != 0) {
		pr_err("amdkfd: Failed to pin PD\n");
		goto err_failed_to_pin_pd;
	}

	mutex_lock(&vm->mutex);

	/* Update the page directory */
	ret = radeon_vm_update_page_directory(rdev, vm);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_update_page_directory\n");
		goto err_failed_to_update_pd;
	}

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	ret = radeon_vm_clear_freed(rdev, vm);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_clear_freed\n");
		goto err_failed_vm_clear_freed;
	}

	/* Update the page tables  */
	ret = radeon_vm_bo_update(rdev, bo_va, &bo->tbo.mem);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_bo_update\n");
		goto err_failed_to_update_pts;
	}

	ret = radeon_vm_clear_invalids(rdev, vm);
	if (ret != 0) {
		pr_err("amdkfd: Failed to radeon_vm_clear_invalids\n");
		goto err_failed_to_vm_clear_invalids;
	}

	mutex_unlock(&vm->mutex);

	list_for_each_entry(lobj, &list, tv.head) {
		struct radeon_bo *bo = lobj->robj;
		ret = ttm_bo_wait(&bo->tbo, false, false);
		if (ret != 0) {
			pr_err("amdkfd: Failed to wait for PT/PD update (err == %d)\n",
					ret);
			goto err_failed_to_wait_pt_pd_update;
		}
	}

	ttm_eu_backoff_reservation(&ticket, &list);
	drm_free_large(vm_bos);

	return 0;

err_failed_to_wait_pt_pd_update:
	mutex_lock(&vm->mutex);
err_failed_to_vm_clear_invalids:
	radeon_vm_bo_update(rdev, bo_va, NULL);
err_failed_to_update_pts:
err_failed_vm_clear_freed:
err_failed_to_update_pd:
	mutex_unlock(&vm->mutex);
	unpin_bo(vm->page_directory, false);
err_failed_to_pin_pd:
	unpin_pts(bo_va, vm, false);
err_failed_to_pin_pts:
	ttm_eu_backoff_reservation(&ticket, &list);
err_failed_to_ttm_reserve:
	drm_free_large(vm_bos);
err_failed_to_get_bos:
	unpin_bo(bo, true);

	return ret;
}

static int unmap_bo_from_gpuvm(struct radeon_device *rdev,
				struct radeon_bo_va *bo_va)
{
	struct radeon_vm *vm;
	int ret;
	struct ttm_validate_buffer tv;
	struct radeon_bo_list *vm_bos;
	struct ww_acquire_ctx ticket;
	struct list_head list;

	INIT_LIST_HEAD(&list);

	vm = bo_va->vm;
	tv.bo = &bo_va->bo->tbo;
	tv.shared = true;
	list_add(&tv.head, &list);

	vm_bos = radeon_vm_get_bos(rdev, vm, &list);
	if (!vm_bos) {
		pr_err("amdkfd: Failed to get bos from vm\n");
		ret = -ENOMEM;
		goto err_failed_to_get_bos;
	}

	ret = ttm_eu_reserve_buffers(&ticket, &list, false, NULL);
	if (ret) {
		pr_err("amdkfd: Failed to reserve buffers in ttm\n");
		goto err_failed_to_ttm_reserve;
	}

	mutex_lock(&vm->mutex);

	/*
	 * The previously "released" BOs are really released and their VAs are
	 * removed from PT. This function is called here because it requires
	 * the radeon_vm::mutex to be locked and PT to be reserved
	 */
	radeon_vm_clear_freed(rdev, vm);

	/* Update the page tables - Remove the mapping from bo_va */
	radeon_vm_bo_update(rdev, bo_va, NULL);

	radeon_vm_clear_invalids(rdev, vm);

	mutex_unlock(&vm->mutex);

	ttm_eu_backoff_reservation(&ticket, &list);
	drm_free_large(vm_bos);

	return 0;

err_failed_to_ttm_reserve:
	drm_free_large(vm_bos);
err_failed_to_get_bos:
	return ret;
}

static int write_config_static_mem(struct kgd_dev *kgd, bool swizzle_enable,
		uint8_t element_size, uint8_t index_stride, uint8_t mtype)
{
	uint32_t reg;
	struct radeon_device *rdev = (struct radeon_device *) kgd;

	reg = swizzle_enable << SH_STATIC_MEM_CONFIG__SWIZZLE_ENABLE__SHIFT |
		element_size << SH_STATIC_MEM_CONFIG__ELEMENT_SIZE__SHIFT |
		index_stride << SH_STATIC_MEM_CONFIG__INDEX_STRIDE__SHIFT |
		index_stride << SH_STATIC_MEM_CONFIG__PRIVATE_MTYPE__SHIFT;

	WREG32(SH_STATIC_MEM_CONFIG, reg);
	return 0;
}
static int alloc_memory_of_scratch(struct kgd_dev *kgd,
					 uint64_t va, uint32_t vmid)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;

	lock_srbm(kgd, 0, 0, 0, vmid);
	WREG32(SH_HIDDEN_PRIVATE_BASE_VMID, va);
	unlock_srbm(kgd);

	return 0;
}

static int alloc_memory_of_gpu(struct kgd_dev *kgd, uint64_t va, uint64_t size,
		void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr,
		struct kfd_process_device *pdd, uint32_t flags)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	int ret;
	struct radeon_bo_va *bo_va;
	struct radeon_bo *bo;

	BUG_ON(kgd == NULL);
	BUG_ON(size == 0);
	BUG_ON(mem == NULL);
	BUG_ON(vm == NULL);

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (*mem == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	mutex_init(&(*mem)->data2.lock);

	/* Allocate buffer object on VRAM */
	ret = radeon_bo_create(rdev, size, PAGE_SIZE, false,
				RADEON_GEM_DOMAIN_VRAM,
				RADEON_GEM_NO_CPU_ACCESS, NULL, NULL, &bo);
	if (ret != 0) {
		pr_err("amdkfd: Failed to create BO object on VRAM. ret == %d\n",
				ret);
		goto err_bo_create;
	}

	pr_debug("Created BO on VRAM with size %llu bytes\n", size);
	bo->pdd = pdd;
	ret = add_bo_to_vm(rdev, va, vm, bo, &bo_va);
	if (ret != 0)
		goto err_map;

	pr_debug("Set BO to VA %p\n", (void *) va);

	(*mem)->data2.bo = bo;
	(*mem)->data2.bo_va = bo_va;
	(*mem)->data2.mapped_to_gpu_memory = 0;

	return 0;

err_map:
	radeon_bo_unref(&bo);
err_bo_create:
	kfree(*mem);
err:
	return ret;

}

static int free_memory_of_gpu(struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	mutex_lock(&mem->data2.lock);

	if (mem->data2.mapped_to_gpu_memory == 1) {
		pr_debug("BO with VA %p, size %lu bytes is mapped to GPU. Need to unmap it before release\n",
		(void *) (mem->data2.bo_va->it.start * RADEON_GPU_PAGE_SIZE),
		mem->data2.bo->tbo.mem.size);
		mutex_unlock(&mem->data2.lock);
		unmap_memory_from_gpu(kgd, mem, NULL);
	} else
		mutex_unlock(&mem->data2.lock);
	/* lock is not needed after this, since mem is unused and will
	 * be freed anyway */

	pr_debug("Releasing BO with VA %p, size %lu bytes\n",
		(void *) (mem->data2.bo_va->it.start * RADEON_GPU_PAGE_SIZE),
		mem->data2.bo->tbo.mem.size);

	/* Remove from VM internal data structures */
	remove_bo_from_vm(rdev, mem->data2.bo, mem->data2.bo_va);

	/* Free the BO*/
	radeon_bo_unref(&mem->data2.bo);
	kfree(mem);

	return 0;
}

static int map_memory_to_gpu(struct kgd_dev *kgd, struct kgd_mem *mem, void *vm)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	int ret;
	struct radeon_bo_va *bo_va;
	struct radeon_bo *bo;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	mutex_lock(&mem->data2.lock);

	bo = mem->data2.bo;
	bo_va = mem->data2.bo_va;

	if (mem->data2.mapped_to_gpu_memory == 1) {
		pr_debug("BO with VA %p, size %lu bytes already mapped to GPU memory\n",
		(void *) (mem->data2.bo_va->it.start * RADEON_GPU_PAGE_SIZE),
		mem->data2.bo->tbo.mem.size);
		mutex_unlock(&mem->data2.lock);
		return 0;
	}

	pr_debug("Mapping BO with VA %p, size %lu bytes to GPU memory\n",
		(void *) (mem->data2.bo_va->it.start * RADEON_GPU_PAGE_SIZE),
		mem->data2.bo->tbo.mem.size);

	/*
	 * We need to pin the allocated BO, PD and appropriate PTs and to
	 * create a mapping of virtual to MC address
	 */
	ret = map_bo_to_gpuvm(rdev, bo, bo_va);
	if (ret != 0) {
		pr_err("amdkfd: Failed to map radeon bo to gpuvm\n");
		mutex_unlock(&mem->data2.lock);
		return ret;
	}

	mem->data2.mapped_to_gpu_memory = 1;

	mutex_unlock(&mem->data2.lock);

	return ret;
}

static int unmap_memory_from_gpu(struct kgd_dev *kgd, struct kgd_mem *mem,
		void *vm)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	struct radeon_bo_va *bo_va;
	int ret = 0;

	BUG_ON(kgd == NULL);
	BUG_ON(mem == NULL);

	mutex_lock(&mem->data2.lock);

	if (mem->data2.mapped_to_gpu_memory == 0) {
		pr_debug("Unmapping BO with VA %p, size %lu bytes from GPU memory is unnecessary\n",
		(void *) (mem->data2.bo_va->it.start * RADEON_GPU_PAGE_SIZE),
		mem->data2.bo->tbo.mem.size);
		mutex_lock(&mem->data2.lock);
		return 0;
	}

	pr_debug("Unmapping BO with VA %p, size %lu bytes from GPU memory\n",
		(void *) (mem->data2.bo_va->it.start * RADEON_GPU_PAGE_SIZE),
		mem->data2.bo->tbo.mem.size);

	bo_va = mem->data2.bo_va;

	/* Unpin the PD directory*/
	unpin_bo(bo_va->vm->page_directory, true);

	/* Unpin PTs */
	unpin_pts(bo_va, bo_va->vm, true);

	/* Unpin BO*/
	unpin_bo(mem->data2.bo, true);

	ret = unmap_bo_from_gpuvm(rdev, bo_va);

	mem->data2.mapped_to_gpu_memory = 0;

	mutex_unlock(&mem->data2.lock);

	return ret;
}

static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	const union radeon_firmware_header *hdr;

	BUG_ON(kgd == NULL || rdev->mec_fw == NULL);

	switch (type) {
	case KGD_ENGINE_PFP:
		hdr = (const union radeon_firmware_header *) rdev->pfp_fw->data;
		break;

	case KGD_ENGINE_ME:
		hdr = (const union radeon_firmware_header *) rdev->me_fw->data;
		break;

	case KGD_ENGINE_CE:
		hdr = (const union radeon_firmware_header *) rdev->ce_fw->data;
		break;

	case KGD_ENGINE_MEC1:
		hdr = (const union radeon_firmware_header *) rdev->mec_fw->data;
		break;

	case KGD_ENGINE_MEC2:
		hdr = (const union radeon_firmware_header *)
							rdev->mec2_fw->data;
		break;

	case KGD_ENGINE_RLC:
		hdr = (const union radeon_firmware_header *) rdev->rlc_fw->data;
		break;

	case KGD_ENGINE_SDMA1:
		hdr = (const union radeon_firmware_header *)
							rdev->sdma_fw->data;
		break;

	default:
		return 0;
	}

	if (hdr == NULL)
		return 0;

	/* Only 12 bit in use*/
	return hdr->common.ucode_version;
}

static void set_num_of_requests(struct kgd_dev *dev, uint8_t num_of_req)
{
	uint32_t value;

	value = read_register(dev, ATC_ATS_DEBUG);
	value &= ~NUM_REQUESTS_AT_ERR_MASK;
	value |= NUM_REQUESTS_AT_ERR(num_of_req);

	write_register(dev, ATC_ATS_DEBUG, value);
}

static void get_cu_info(struct kgd_dev *kgd, struct kfd_cu_info *cu_info)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	struct radeon_cu_info rcu_info;

	memset(cu_info, 0, sizeof(*cu_info));
	if (sizeof(cu_info->cu_bitmap) != sizeof(rcu_info.bitmap))
		return;
	if (rdev->asic->get_cu_info == NULL)
		return;

	memset(&rcu_info, 0, sizeof(rcu_info));
	rdev->asic->get_cu_info(rdev, &rcu_info);
	cu_info->cu_active_number = rcu_info.number;
	cu_info->cu_ao_mask = rcu_info.ao_cu_mask;
	memcpy(&cu_info->cu_bitmap[0], &rcu_info.bitmap[0], sizeof(rcu_info.bitmap));
	cu_info->num_shader_engines = rdev->config.cik.max_shader_engines;
	cu_info->num_shader_arrays_per_engine = rdev->config.cik.max_sh_per_se;
	cu_info->num_cu_per_sh = rdev->config.cik.max_cu_per_sh;
	cu_info->simd_per_cu = rcu_info.simd_per_cu;
	cu_info->max_waves_per_simd = rcu_info.max_waves_per_simd;
	cu_info->wave_front_size = rcu_info.wave_front_size;
	cu_info->max_scratch_slots_per_cu = rcu_info.max_scratch_slots_per_cu;
	cu_info->lds_size = rcu_info.lds_size;
}

static int mmap_bo(struct kgd_dev *kgd, struct vm_area_struct *vma)
{
	return 0;
}

static int map_gtt_bo_to_kernel(struct kgd_dev *kgd,
			struct kgd_mem *mem, void **kptr)
{
	return 0;
}

static void set_vm_context_page_table_base(struct kgd_dev *kgd, uint32_t vmid,
			uint32_t page_table_base)
{
	struct radeon_device *rdev = get_radeon_device(kgd);

	if (vmid < 8 || vmid > 15) {
		pr_err("amdkfd: trying to set page table base for wrong VMID\n");
		return;
	}
	WREG32(VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + vmid - 8, page_table_base);
}


