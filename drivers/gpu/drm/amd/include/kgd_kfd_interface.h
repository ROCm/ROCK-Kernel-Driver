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

/*
 * This file defines the private interface between the
 * AMD kernel graphics drivers and the AMD KFD.
 */

#ifndef KGD_KFD_INTERFACE_H_INCLUDED
#define KGD_KFD_INTERFACE_H_INCLUDED

#include <linux/types.h>
#include <linux/mm_types.h>
#include <linux/scatterlist.h>

struct pci_dev;

#define KFD_INTERFACE_VERSION 1

struct kfd_dev;
struct kgd_dev;

struct kgd_mem;
struct kfd_process_device;
struct amdgpu_bo;

enum kfd_preempt_type {
	KFD_PREEMPT_TYPE_WAVEFRONT_DRAIN = 0,
	KFD_PREEMPT_TYPE_WAVEFRONT_RESET,
};

struct kfd_vm_fault_info {
	uint64_t	page_addr;
	uint32_t	vmid;
	uint32_t	mc_id;
	uint32_t	status;
	bool		prot_valid;
	bool		prot_read;
	bool		prot_write;
	bool		prot_exec;
};

struct kfd_cu_info {
	uint32_t num_shader_engines;
	uint32_t num_shader_arrays_per_engine;
	uint32_t num_cu_per_sh;
	uint32_t cu_active_number;
	uint32_t cu_ao_mask;
	uint32_t simd_per_cu;
	uint32_t max_waves_per_simd;
	uint32_t wave_front_size;
	uint32_t max_scratch_slots_per_cu;
	uint32_t lds_size;
	uint32_t cu_bitmap[4][4];
};

/* For getting GPU local memory information from KGD */
struct kfd_local_mem_info {
	uint64_t local_mem_size_private;
	uint64_t local_mem_size_public;
	uint32_t vram_width;
	uint32_t mem_clk_max;
};

enum kgd_memory_pool {
	KGD_POOL_SYSTEM_CACHEABLE = 1,
	KGD_POOL_SYSTEM_WRITECOMBINE = 2,
	KGD_POOL_FRAMEBUFFER = 3,
};

enum kgd_engine_type {
	KGD_ENGINE_PFP = 1,
	KGD_ENGINE_ME,
	KGD_ENGINE_CE,
	KGD_ENGINE_MEC1,
	KGD_ENGINE_MEC2,
	KGD_ENGINE_RLC,
	KGD_ENGINE_SDMA1,
	KGD_ENGINE_SDMA2,
	KGD_ENGINE_MAX
};

struct kgd2kfd_shared_resources {
	/* Bit n == 1 means VMID n is available for KFD. */
	unsigned int compute_vmid_bitmap;

	/* Compute pipes are counted starting from MEC0/pipe0 as 0. */
	unsigned int first_compute_pipe;

	/* Number of MEC pipes available for KFD. */
	unsigned int compute_pipe_count;

	/* Base address of doorbell aperture. */
	phys_addr_t doorbell_physical_address;

	/* Size in bytes of doorbell aperture. */
	size_t doorbell_aperture_size;

	/* Number of bytes at start of aperture reserved for KGD. */
	size_t doorbell_start_offset;

	/* GPUVM address space size in bytes */
	uint64_t gpuvm_size;
};

/*
 * Allocation flag domains currently only VRAM and GTT domain supported
 */
#define ALLOC_MEM_FLAGS_VRAM			(1 << 0)
#define ALLOC_MEM_FLAGS_GTT				(1 << 1)
#define ALLOC_MEM_FLAGS_USERPTR			(1 << 2)

/*
 * Allocation flags attributes/access options.
 */
#define ALLOC_MEM_FLAGS_NONPAGED		(1 << 31)
#define ALLOC_MEM_FLAGS_READONLY		(1 << 30)
#define ALLOC_MEM_FLAGS_PUBLIC			(1 << 29)
#define ALLOC_MEM_FLAGS_NO_SUBSTITUTE	(1 << 28)
#define ALLOC_MEM_FLAGS_AQL_QUEUE_MEM	(1 << 27)
#define ALLOC_MEM_FLAGS_EXECUTE_ACCESS	(1 << 26)

/**
 * struct kfd2kgd_calls
 *
 * @init_gtt_mem_allocation: Allocate a buffer on the gart aperture.
 * The buffer can be used for mqds, hpds, kernel queue, fence and runlists
 *
 * @free_gtt_mem: Frees a buffer that was allocated on the gart aperture
 *
 * @get_local_mem_info: Retrieves information about GPU local memory
 *
 * @get_gpu_clock_counter: Retrieves GPU clock counter
 *
 * @get_max_engine_clock_in_mhz: Retrieves maximum GPU clock in MHz
 *
 * @program_sh_mem_settings: A function that should initiate the memory
 * properties such as main aperture memory type (cache / non cached) and
 * secondary aperture base address, size and memory type.
 * This function is used only for no cp scheduling mode.
 *
 * @set_pasid_vmid_mapping: Exposes pasid/vmid pair to the H/W for no cp
 * scheduling mode. Only used for no cp scheduling mode.
 *
 * @init_pipeline: Initialized the compute pipelines.
 *
 * @hqd_load: Loads the mqd structure to a H/W hqd slot. used only for no cp
 * sceduling mode.
 *
 * @hqd_sdma_load: Loads the SDMA mqd structure to a H/W SDMA hqd slot.
 * used only for no HWS mode. If mm is passed in, its mmap_sem must be
 * read-locked.
 *
 * @hqd_dump: Dumps CPC HQD registers to an array of address-value pairs.
 * Array is allocated with kmalloc, needs to be freed with kfree by caller.
 *
 * @hqd_sdma_dump: Dumps SDMA HQD registers to an array of address-value pairs.
 * Array is allocated with kmalloc, needs to be freed with kfree by caller.
 *
 * @hqd_is_occupies: Checks if a hqd slot is occupied.
 *
 * @hqd_destroy: Destructs and preempts the queue assigned to that hqd slot.
 *
 * @hqd_sdma_is_occupied: Checks if an SDMA hqd slot is occupied.
 *
 * @hqd_sdma_destroy: Destructs and preempts the SDMA queue assigned to that
 * SDMA hqd slot.
 *
 * @map_memory_to_gpu: Allocates and pins BO, PD and all related PTs
 *
 * @unmap_memory_to_gpu: Releases and unpins BO, PD and all related PTs
 *
 * @get_fw_version: Returns FW versions from the header
 *
 * @set_num_of_requests: Sets number of Peripheral Page Request (PPR) sent to
 * IOMMU when address translation failed
 *
 * @get_cu_info: Retrieves activated cu info
 *
 * @get_dmabuf_info: Returns information about a dmabuf if it was
 * created by the GPU driver
 *
 * @import_dmabuf: Imports a DMA buffer, creating a new kgd_mem object
 * Supports only DMA buffers created by GPU driver on the same GPU
 *
 * This structure contains function pointers to services that the kgd driver
 * provides to amdkfd driver.
 *
 */
struct kfd2kgd_calls {
	int (*init_gtt_mem_allocation)(struct kgd_dev *kgd, size_t size,
					void **mem_obj, uint64_t *gpu_addr,
					void **cpu_ptr);

	void (*free_gtt_mem)(struct kgd_dev *kgd, void *mem_obj);

	void(*get_local_mem_info)(struct kgd_dev *kgd,
			struct kfd_local_mem_info *mem_info);
	uint64_t (*get_gpu_clock_counter)(struct kgd_dev *kgd);

	uint32_t (*get_max_engine_clock_in_mhz)(struct kgd_dev *kgd);

	int (*create_process_vm)(struct kgd_dev *kgd, void **vm);
	void (*destroy_process_vm)(struct kgd_dev *kgd, void *vm);

	int (*create_process_gpumem)(struct kgd_dev *kgd, uint64_t va, size_t size, void *vm, struct kgd_mem **mem);
	void (*destroy_process_gpumem)(struct kgd_dev *kgd, struct kgd_mem *mem);

	uint32_t (*get_process_page_dir)(void *vm);

	int (*open_graphic_handle)(struct kgd_dev *kgd, uint64_t va, void *vm, int fd, uint32_t handle, struct kgd_mem **mem);

	/* Register access functions */
	void (*program_sh_mem_settings)(struct kgd_dev *kgd, uint32_t vmid,
			uint32_t sh_mem_config,	uint32_t sh_mem_ape1_base,
			uint32_t sh_mem_ape1_limit, uint32_t sh_mem_bases);

	int (*set_pasid_vmid_mapping)(struct kgd_dev *kgd, unsigned int pasid,
					unsigned int vmid);

	int (*init_pipeline)(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr);

	int (*init_interrupts)(struct kgd_dev *kgd, uint32_t pipe_id);
	

	int (*hqd_load)(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
				uint32_t queue_id, uint32_t __user *wptr);

	int (*hqd_sdma_load)(struct kgd_dev *kgd, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm);

	int (*hqd_dump)(struct kgd_dev *kgd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs);

	int (*hqd_sdma_dump)(struct kgd_dev *kgd,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs);

	bool (*hqd_is_occupied)(struct kgd_dev *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id);

	int (*hqd_destroy)(struct kgd_dev *kgd, uint32_t reset_type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id);

	bool (*hqd_sdma_is_occupied)(struct kgd_dev *kgd, void *mqd);

	int (*hqd_sdma_destroy)(struct kgd_dev *kgd, void *mqd,
				unsigned int timeout);
				
	int (*address_watch_disable)(struct kgd_dev *kgd);
	int (*address_watch_execute)(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo);
	int (*wave_control_execute)(struct kgd_dev *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd);
	uint32_t (*address_watch_get_offset)(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset);
	bool (*get_atc_vmid_pasid_mapping_valid)(
					struct kgd_dev *kgd,
					uint8_t vmid);
	uint16_t (*get_atc_vmid_pasid_mapping_pasid)(
					struct kgd_dev *kgd,
					uint8_t vmid);
	void (*write_vmid_invalidate_request)(struct kgd_dev *kgd,
					uint8_t vmid);
	int (*alloc_memory_of_gpu)(struct kgd_dev *kgd, uint64_t va,
			uint64_t size, void *vm,
			struct kgd_mem **mem, uint64_t *offset,
			void **kptr, struct kfd_process_device *pdd,
			uint32_t flags);
	int (*free_memory_of_gpu)(struct kgd_dev *kgd, struct kgd_mem *mem);
	int (*map_memory_to_gpu)(struct kgd_dev *kgd, struct kgd_mem *mem,
			void *vm);
	int (*unmap_memory_to_gpu)(struct kgd_dev *kgd, struct kgd_mem *mem,
			void *vm);

	uint16_t (*get_fw_version)(struct kgd_dev *kgd,
				enum kgd_engine_type type);

	void (*set_num_of_requests)(struct kgd_dev *kgd,
			uint8_t num_of_requests);
	int (*alloc_memory_of_scratch)(struct kgd_dev *kgd,
			uint64_t va, uint32_t vmid);
	int (*write_config_static_mem)(struct kgd_dev *kgd, bool swizzle_enable,
		uint8_t element_size, uint8_t index_stride, uint8_t mtype);
	void (*get_cu_info)(struct kgd_dev *kgd,
			struct kfd_cu_info *cu_info);
	int (*mmap_bo)(struct kgd_dev *kgd, struct vm_area_struct *vma);
	int (*map_gtt_bo_to_kernel)(struct kgd_dev *kgd,
			struct kgd_mem *mem, void **kptr);
	void (*set_vm_context_page_table_base)(struct kgd_dev *kgd, uint32_t vmid,
			uint32_t page_table_base);
	struct kfd_process_device* (*get_pdd_from_buffer_object)
		(struct kgd_dev *kgd, struct kgd_mem *mem);
	int (*return_bo_size)(struct kgd_dev *kgd, struct kgd_mem *mem);

	int (*pin_get_sg_table_bo)(struct kgd_dev *kgd,
			struct kgd_mem *mem, uint64_t offset,
			uint64_t size, struct sg_table **ret_sg);
	void (*unpin_put_sg_table_bo)(struct kgd_mem *mem,
			struct sg_table *sg);

	int (*get_dmabuf_info)(struct kgd_dev *kgd, int dma_buf_fd,
			       struct kgd_dev **dma_buf_kgd, uint64_t *bo_size,
			       void *metadata_buffer, size_t buffer_size,
			       uint32_t *metadata_size, uint32_t *flags);
	int (*import_dmabuf)(struct kgd_dev *kgd, int dma_buf_fd, uint64_t va,
			     void *vm, struct kgd_mem **mem, uint64_t *size);

	int (*get_vm_fault_info)(struct kgd_dev *kgd,
			struct kfd_vm_fault_info *info);

};

/**
 * struct kgd2kfd_calls
 *
 * @exit: Notifies amdkfd that kgd module is unloaded
 *
 * @probe: Notifies amdkfd about a probe done on a device in the kgd driver.
 *
 * @device_init: Initialize the newly probed device (if it is a device that
 * amdkfd supports)
 *
 * @device_exit: Notifies amdkfd about a removal of a kgd device
 *
 * @suspend: Notifies amdkfd about a suspend action done to a kgd device
 *
 * @resume: Notifies amdkfd about a resume action done to a kgd device
 *
 * @quiesce_mm: Quiesce all user queue access to specified MM address space
 *
 * @resume_mm: Resume user queue access to specified MM address space
 *
 * This structure contains function callback pointers so the kgd driver
 * will notify to the amdkfd about certain status changes.
 *
 */
struct kgd2kfd_calls {
	void (*exit)(void);
	struct kfd_dev* (*probe)(struct kgd_dev *kgd, struct pci_dev *pdev,
		const struct kfd2kgd_calls *f2g);
	bool (*device_init)(struct kfd_dev *kfd,
			const struct kgd2kfd_shared_resources *gpu_resources);
	void (*device_exit)(struct kfd_dev *kfd);
	void (*interrupt)(struct kfd_dev *kfd, const void *ih_ring_entry);
	void (*suspend)(struct kfd_dev *kfd);
	int (*resume)(struct kfd_dev *kfd);
	int (*evict_bo)(struct kfd_dev *dev, void *ptr);
	int (*restore)(struct kfd_dev *kfd);
	int (*quiesce_mm)(struct kfd_dev *kfd, struct mm_struct *mm);
	int (*resume_mm)(struct kfd_dev *kfd, struct mm_struct *mm);
};

int kgd2kfd_init(unsigned interface_version,
		const struct kgd2kfd_calls **g2f);

#endif /* KGD_KFD_INTERFACE_H_INCLUDED */
