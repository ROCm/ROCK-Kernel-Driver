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

/* amdgpu_amdkfd.h defines the private interface between amdgpu and amdkfd. */

#ifndef AMDGPU_AMDKFD_H_INCLUDED
#define AMDGPU_AMDKFD_H_INCLUDED

#include <linux/types.h>
#include <linux/workqueue.h>
#include <kgd_kfd_interface.h>
#include "amdgpu.h"

extern const struct kgd2kfd_calls *kgd2kfd;

struct amdgpu_device;

struct kfd_bo_va_list {
	struct list_head bo_list;
	struct amdgpu_bo_va *bo_va;
	void *kgd_dev;
	bool is_mapped;
	bool map_fail;
	uint64_t va;
};

struct kgd_mem {
	struct mutex lock;
	struct amdgpu_bo *bo;
	struct list_head bo_va_list;
	/* protected by amdkfd_process_info.lock */
	struct ttm_validate_buffer validate_list;
	struct ttm_validate_buffer resv_list;
	uint32_t domain;
	unsigned int mapped_to_gpu_memory;
	void *kptr;
	uint64_t va;
	unsigned int evicted; /* eviction counter */
	struct delayed_work work; /* for restore evicted mem */
	struct mm_struct *mm; /* for restore */

	uint32_t pte_flags;

	/* flags bitfield */
	bool no_substitute : 1;
	bool aql_queue     : 1;
	bool busy          : 1;
};

/* KFD Memory Eviction */
struct amdgpu_amdkfd_fence {
	struct fence base;
	void *mm;
	spinlock_t lock;
	char timeline_name[TASK_COMM_LEN];
};

struct amdgpu_amdkfd_fence *amdgpu_amdkfd_fence_create(u64 context,
						       void *mm);
bool amd_kfd_fence_check_mm(struct fence *f, void *mm);
struct amdgpu_amdkfd_fence *to_amdgpu_amdkfd_fence(struct fence *f);

struct amdkfd_process_info {
	/* List head of all VMs that belong to a KFD process */
	struct list_head vm_list_head;
	/* List head for all KFD BOs that belong to a KFD process. */
	struct list_head kfd_bo_list;
	/* Lock to protect kfd_bo_list */
	struct mutex lock;

	/* Number of VMs */
	unsigned int n_vms;
	/* Eviction Fence */
	struct amdgpu_amdkfd_fence *eviction_fence;
};

/* struct amdkfd_vm -
 * For Memory Eviction KGD requires a mechanism to keep track of all KFD BOs
 * belonging to a KFD process. All the VMs belonging to the same process point
 * to the same amdkfd_process_info.
 */
struct amdkfd_vm {
	/* Keep base as the first parameter for pointer compatibility between
	 * amdkfd_vm and amdgpu_vm.
	 */
	struct amdgpu_vm base;

	/* List node in amdkfd_process_info.vm_list_head*/
	struct list_head vm_list_node;

	struct amdgpu_device *adev;
	/* Points to the KFD process VM info*/
	struct amdkfd_process_info *process_info;
};

int amdgpu_amdkfd_init(void);
void amdgpu_amdkfd_fini(void);

bool amdgpu_amdkfd_load_interface(struct amdgpu_device *rdev);

void amdgpu_amdkfd_suspend(struct amdgpu_device *rdev);
int amdgpu_amdkfd_resume(struct amdgpu_device *rdev);
void amdgpu_amdkfd_interrupt(struct amdgpu_device *rdev,
			const void *ih_ring_entry);
void amdgpu_amdkfd_device_probe(struct amdgpu_device *rdev);
void amdgpu_amdkfd_device_init(struct amdgpu_device *rdev);
void amdgpu_amdkfd_device_fini(struct amdgpu_device *rdev);

int amdgpu_amdkfd_evict_mem(struct amdgpu_device *adev, struct kgd_mem *mem,
			    struct mm_struct *mm);
int amdgpu_amdkfd_schedule_restore_mem(struct amdgpu_device *adev,
				       struct kgd_mem *mem,
				       struct mm_struct *mm,
				       unsigned long delay);
void amdgpu_amdkfd_cancel_restore_mem(struct kgd_mem *mem);
int amdgpu_amdkfd_submit_ib(struct kgd_dev *kgd, enum kgd_engine_type engine,
				uint32_t vmid, uint64_t gpu_addr,
				uint32_t *ib_cmd, uint32_t ib_len);
int amdgpu_amdkfd_gpuvm_restore_process_bos(void *process_info);
struct kfd2kgd_calls *amdgpu_amdkfd_gfx_7_get_functions(void);
struct kfd2kgd_calls *amdgpu_amdkfd_gfx_8_0_get_functions(void);
int amdgpu_amdkfd_copy_mem_to_mem(struct kgd_dev *kgd, struct kgd_mem *src_mem,
		uint64_t src_offset, struct kgd_mem *dst_mem,
		uint64_t dest_offset, uint64_t size, struct fence **f,
		uint64_t *actual_size);

bool amdgpu_amdkfd_is_kfd_vmid(struct amdgpu_device *adev,
			u32 vmid);

/* Shared API */
int map_bo(struct amdgpu_device *rdev, uint64_t va, void *vm,
		struct amdgpu_bo *bo, struct amdgpu_bo_va **bo_va);
int alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
			void **mem_obj, uint64_t *gpu_addr,
			void **cpu_ptr);
void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj);
void get_local_mem_info(struct kgd_dev *kgd,
			struct kfd_local_mem_info *mem_info);
uint64_t get_gpu_clock_counter(struct kgd_dev *kgd);

uint32_t get_max_engine_clock_in_mhz(struct kgd_dev *kgd);
void get_cu_info(struct kgd_dev *kgd, struct kfd_cu_info *cu_info);
int amdgpu_amdkfd_get_dmabuf_info(struct kgd_dev *kgd, int dma_buf_fd,
				  struct kgd_dev **dmabuf_kgd,
				  uint64_t *bo_size, void *metadata_buffer,
				  size_t buffer_size, uint32_t *metadata_size,
				  uint32_t *flags);

bool read_user_wptr(struct mm_struct *mm, uint32_t __user *wptr,
		    uint32_t *wptr_val);

/* GPUVM API */
int amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		struct kgd_dev *kgd, uint64_t va, uint64_t size,
		void *vm, struct kgd_mem **mem,
		uint64_t *offset, void **kptr,
		uint32_t flags);
int amdgpu_amdkfd_gpuvm_free_memory_of_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm);
int amdgpu_amdkfd_gpuvm_map_memory_to_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm);
int amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm);

int amdgpu_amdkfd_gpuvm_create_process_vm(struct kgd_dev *kgd, void **vm,
					  void **process_info);
void amdgpu_amdkfd_gpuvm_destroy_process_vm(struct kgd_dev *kgd, void *vm);

uint32_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *vm);

int amdgpu_amdkfd_gpuvm_get_vm_fault_info(struct kgd_dev *kgd,
					      struct kfd_vm_fault_info *info);

int amdgpu_amdkfd_gpuvm_mmap_bo(
		struct kgd_dev *kgd, struct vm_area_struct *vma);

int amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(struct kgd_dev *kgd,
		struct kgd_mem *mem, void **kptr);

int amdgpu_amdkfd_gpuvm_pin_get_sg_table(struct kgd_dev *kgd,
		struct kgd_mem *mem, uint64_t offset,
		uint64_t size, struct sg_table **ret_sg);
void amdgpu_amdkfd_gpuvm_unpin_put_sg_table(
		struct kgd_mem *mem, struct sg_table *sg);
int amdgpu_amdkfd_gpuvm_import_dmabuf(struct kgd_dev *kgd,
				      struct dma_buf *dmabuf,
				      uint64_t va, void *vm,
				      struct kgd_mem **mem, uint64_t *size,
				      uint64_t *mmap_offset);
int amdgpu_amdkfd_gpuvm_export_dmabuf(struct kgd_dev *kgd, void *vm,
				      struct kgd_mem *mem,
				      struct dma_buf **dmabuf);
int amdgpu_amdkfd_gpuvm_evict_mem(struct kgd_mem *mem, struct mm_struct *mm);
int amdgpu_amdkfd_gpuvm_restore_mem(struct kgd_mem *mem, struct mm_struct *mm);

void amdgpu_amdkfd_gpuvm_init_mem_limits(void);
void amdgpu_amdkfd_unreserve_system_memory_limit(struct amdgpu_bo *bo);
#endif /* AMDGPU_AMDKFD_H_INCLUDED */

