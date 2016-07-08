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
#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_ucode.h"
#include "amdgpu_amdkfd_gfx_v8.h"
#include "gca/gfx_8_0_sh_mask.h"
#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_enum.h"
#include "oss/oss_3_0_sh_mask.h"
#include "oss/oss_3_0_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "vi_structs.h"
#include "vid.h"

#define VI_PIPE_PER_MEC		(4)
#define VI_QUEUES_PER_PIPE_MEC	(8)

enum hqd_dequeue_request_type {
	NO_ACTION = 0,
	DRAIN_PIPE,
	RESET_WAVES,
	SAVE_WAVES
};

static const uint32_t watchRegs[MAX_WATCH_ADDRESSES * ADDRESS_WATCH_REG_MAX] = {
	mmTCP_WATCH0_ADDR_H, mmTCP_WATCH0_ADDR_L, mmTCP_WATCH0_CNTL,
	mmTCP_WATCH1_ADDR_H, mmTCP_WATCH1_ADDR_L, mmTCP_WATCH1_CNTL,
	mmTCP_WATCH2_ADDR_H, mmTCP_WATCH2_ADDR_L, mmTCP_WATCH2_CNTL,
	mmTCP_WATCH3_ADDR_H, mmTCP_WATCH3_ADDR_L, mmTCP_WATCH3_CNTL
};


struct vi_sdma_mqd;

static int create_process_gpumem(struct kgd_dev *kgd, uint64_t va, size_t size,
		void *vm, struct kgd_mem **mem);
static void destroy_process_gpumem(struct kgd_dev *kgd, struct kgd_mem *mem);

static int open_graphic_handle(struct kgd_dev *kgd, uint64_t va, void *vm,
				int fd, uint32_t handle, struct kgd_mem **mem);

static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type);

/*
 * Register access functions
 */

static void kgd_program_sh_mem_settings(struct kgd_dev *kgd, uint32_t vmid,
		uint32_t sh_mem_config,
		uint32_t sh_mem_ape1_base, uint32_t sh_mem_ape1_limit,
		uint32_t sh_mem_bases);
static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
		unsigned int vmid);
static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
		uint32_t hpd_size, uint64_t hpd_gpu_addr);
static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id);
static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr);
static int kgd_hqd_dump(struct kgd_dev *kgd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs);
static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm);
static int kgd_hqd_sdma_dump(struct kgd_dev *kgd,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs);
static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
		uint32_t pipe_id, uint32_t queue_id);
static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd);
static int kgd_hqd_destroy(struct kgd_dev *kgd,
				enum kfd_preempt_type reset_type,
				unsigned int utimeout, uint32_t pipe_id,
				uint32_t queue_id);
static int kgd_hqd_sdma_destroy(struct kgd_dev *kgd, void *mqd,
				unsigned int utimeout);
static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid);
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

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd,
		uint8_t vmid);
static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
		uint8_t vmid);
static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid);
static void set_num_of_requests(struct kgd_dev *kgd,
			uint8_t num_of_requests);
static int alloc_memory_of_scratch(struct kgd_dev *kgd,
				 uint64_t va, uint32_t vmid);
static int write_config_static_mem(struct kgd_dev *kgd, bool swizzle_enable,
		uint8_t element_size, uint8_t index_stride, uint8_t mtype);
static void set_vm_context_page_table_base(struct kgd_dev *kgd, uint32_t vmid,
		uint32_t page_table_base);

static const struct kfd2kgd_calls kfd2kgd = {
	.init_gtt_mem_allocation = alloc_gtt_mem,
	.free_gtt_mem = free_gtt_mem,
	.get_local_mem_info = get_local_mem_info,
	.get_gpu_clock_counter = get_gpu_clock_counter,
	.get_max_engine_clock_in_mhz = get_max_engine_clock_in_mhz,
	.create_process_vm = amdgpu_amdkfd_gpuvm_create_process_vm,
	.destroy_process_vm = amdgpu_amdkfd_gpuvm_destroy_process_vm,
	.create_process_gpumem = create_process_gpumem,
	.destroy_process_gpumem = destroy_process_gpumem,
	.get_process_page_dir = amdgpu_amdkfd_gpuvm_get_process_page_dir,
	.open_graphic_handle = open_graphic_handle,
	.program_sh_mem_settings = kgd_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_set_pasid_vmid_mapping,
	.init_pipeline = kgd_init_pipeline,
	.init_interrupts = kgd_init_interrupts,
	.hqd_load = kgd_hqd_load,
	.hqd_sdma_load = kgd_hqd_sdma_load,
	.hqd_dump = kgd_hqd_dump,
	.hqd_sdma_dump = kgd_hqd_sdma_dump,
	.hqd_is_occupied = kgd_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_hqd_destroy,
	.hqd_sdma_destroy = kgd_hqd_sdma_destroy,
	.address_watch_disable = kgd_address_watch_disable,
	.address_watch_execute = kgd_address_watch_execute,
	.wave_control_execute = kgd_wave_control_execute,
	.address_watch_get_offset = kgd_address_watch_get_offset,
	.get_atc_vmid_pasid_mapping_pasid =
			get_atc_vmid_pasid_mapping_pasid,
	.get_atc_vmid_pasid_mapping_valid =
			get_atc_vmid_pasid_mapping_valid,
	.write_vmid_invalidate_request = write_vmid_invalidate_request,
	.alloc_memory_of_gpu = amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu,
	.free_memory_of_gpu = amdgpu_amdkfd_gpuvm_free_memory_of_gpu,
	.map_memory_to_gpu = amdgpu_amdkfd_gpuvm_map_memory_to_gpu,
	.unmap_memory_to_gpu = amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu,
	.get_fw_version = get_fw_version,
	.set_num_of_requests = set_num_of_requests,
	.get_cu_info = get_cu_info,
	.alloc_memory_of_scratch = alloc_memory_of_scratch,
	.write_config_static_mem = write_config_static_mem,
	.mmap_bo = amdgpu_amdkfd_gpuvm_mmap_bo,
	.map_gtt_bo_to_kernel = amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel,
	.set_vm_context_page_table_base = set_vm_context_page_table_base,
	.get_pdd_from_buffer_object =
			amdgpu_amdkfd_gpuvm_get_pdd_from_buffer_object,
	.return_bo_size = amdgpu_amdkfd_gpuvm_return_bo_size,
	.pin_get_sg_table_bo = amdgpu_amdkfd_gpuvm_pin_get_sg_table,
	.unpin_put_sg_table_bo = amdgpu_amdkfd_gpuvm_unpin_put_sg_table,
	.get_dmabuf_info = amdgpu_amdkfd_get_dmabuf_info,
	.import_dmabuf = amdgpu_amdkfd_gpuvm_import_dmabuf,
	.get_vm_fault_info = amdgpu_amdkfd_gpuvm_get_vm_fault_info,
	.submit_ib = amdgpu_amdkfd_submit_ib
};

struct kfd2kgd_calls *amdgpu_amdkfd_gfx_8_0_get_functions()
{
	return (struct kfd2kgd_calls *)&kfd2kgd;
}

static int create_process_gpumem(struct kgd_dev *kgd, uint64_t va, size_t size,
				void *vm, struct kgd_mem **mem)
{
	return 0;
}

/* Destroys the GPU allocation and frees the kgd_mem structure */
static void destroy_process_gpumem(struct kgd_dev *kgd, struct kgd_mem *mem)
{

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
	uint32_t mec = (++pipe_id / VI_PIPE_PER_MEC) + 1;
	uint32_t pipe = (pipe_id % VI_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, queue_id, 0);
}

static uint32_t get_queue_mask(uint32_t pipe_id, uint32_t queue_id)
{
	/* assumes that pipe0 is used by graphics and that the correct
	 * MEC is selected by acquire_queue already
	 */
	unsigned bit = ((pipe_id+1) * VI_QUEUES_PER_PIPE_MEC + queue_id) & 31;

	return ((uint32_t)1) << bit;
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
	 * The ATC_VMID_PASID_MAPPING_UPDATE_STATUS bit could be 0 because
	 * a mapping is in progress or because a mapping finished
	 * and the SW cleared it.
	 * So the protocol is to always wait & clear.
	 */
	uint32_t pasid_mapping = (pasid == 0) ? 0 : (uint32_t)pasid |
			ATC_VMID0_PASID_MAPPING__VALID_MASK;

	WREG32(mmATC_VMID0_PASID_MAPPING + vmid, pasid_mapping);

	while (!(RREG32(mmATC_VMID_PASID_MAPPING_UPDATE_STATUS) & (1U << vmid)))
		cpu_relax();
	WREG32(mmATC_VMID_PASID_MAPPING_UPDATE_STATUS, 1U << vmid);

	/* Mapping vmid to pasid also for IH block */
	WREG32(mmIH_VMID_0_LUT + vmid, pasid_mapping);

	return 0;
}

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr)
{
	return 0;
}

static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t mec;
	uint32_t pipe;

	mec = (++pipe_id / VI_PIPE_PER_MEC) + 1;
	pipe = (pipe_id % VI_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, 0, 0);

	WREG32(mmCPC_INT_CNTL, CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK);

	unlock_srbm(kgd);

	return 0;
}

static inline uint32_t get_sdma_base_addr(struct vi_sdma_mqd *m)
{
	uint32_t retval;

	retval = m->sdma_engine_id * SDMA1_REGISTER_OFFSET +
		m->sdma_queue_id * KFD_VI_SDMA_QUEUE_OFFSET;
	pr_debug("kfd: sdma base address: 0x%x\n", retval);

	return retval;
}

static inline struct vi_mqd *get_mqd(void *mqd)
{
	return (struct vi_mqd *)mqd;
}

static inline struct vi_sdma_mqd *get_sdma_mqd(void *mqd)
{
	return (struct vi_sdma_mqd *)mqd;
}

static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct vi_mqd *m;

	m = get_mqd(mqd);

	acquire_queue(kgd, pipe_id, queue_id);

	WREG32(mmCOMPUTE_STATIC_THREAD_MGMT_SE0,
		m->compute_static_thread_mgmt_se0);
	WREG32(mmCOMPUTE_STATIC_THREAD_MGMT_SE1,
		m->compute_static_thread_mgmt_se1);
	WREG32(mmCOMPUTE_STATIC_THREAD_MGMT_SE2,
		m->compute_static_thread_mgmt_se2);
	WREG32(mmCOMPUTE_STATIC_THREAD_MGMT_SE3,
		m->compute_static_thread_mgmt_se3);

	WREG32(mmCP_MQD_CONTROL, m->cp_mqd_control);
	WREG32(mmCP_MQD_BASE_ADDR, m->cp_mqd_base_addr_lo);
	WREG32(mmCP_MQD_BASE_ADDR_HI, m->cp_mqd_base_addr_hi);

	WREG32(mmCP_HQD_VMID, m->cp_hqd_vmid);
	WREG32(mmCP_HQD_PQ_BASE, m->cp_hqd_pq_base_lo);
	WREG32(mmCP_HQD_PQ_BASE_HI, m->cp_hqd_pq_base_hi);
	WREG32(mmCP_HQD_PQ_RPTR_REPORT_ADDR, m->cp_hqd_pq_rptr_report_addr_lo);
	WREG32(mmCP_HQD_PQ_RPTR_REPORT_ADDR_HI,
			m->cp_hqd_pq_rptr_report_addr_hi);
	WREG32(mmCP_HQD_PQ_CONTROL, m->cp_hqd_pq_control);
	WREG32(mmCP_HQD_PQ_DOORBELL_CONTROL, m->cp_hqd_pq_doorbell_control);
	WREG32(mmCP_HQD_PQ_RPTR, m->cp_hqd_pq_rptr);

	if (wptr) {
		/* Don't read wptr with get_user because the user
		 * context may not be accessible (if this function
		 * runs in a work queue). Instead trigger a one-shot
		 * polling read from memory in the CP. This assumes
		 * that wptr is GPU-accessible in the queue's VMID via
		 * ATC or SVM. WPTR==RPTR before starting the poll so
		 * the CP starts fetching new commands from the right
		 * place.
		 */
		WREG32(mmCP_HQD_PQ_WPTR, m->cp_hqd_pq_rptr);
		WREG32(mmCP_HQD_PQ_WPTR_POLL_ADDR, (uint32_t)(uint64_t)wptr);
		WREG32(mmCP_HQD_PQ_WPTR_POLL_ADDR_HI,
		       (uint32_t)((uint64_t)wptr >> 32));
		WREG32(mmCP_PQ_WPTR_POLL_CNTL1,
		       get_queue_mask(pipe_id, queue_id));
	} else
		WREG32(mmCP_HQD_PQ_WPTR, 0);

	WREG32(mmCP_HQD_PERSISTENT_STATE, m->cp_hqd_persistent_state);
	WREG32(mmCP_HQD_PIPE_PRIORITY, m->cp_hqd_pipe_priority);
	WREG32(mmCP_HQD_QUEUE_PRIORITY, m->cp_hqd_queue_priority);
	WREG32(mmCP_HQD_QUANTUM, m->cp_hqd_quantum);

	WREG32(mmCP_HQD_EOP_BASE_ADDR, m->cp_hqd_eop_base_addr_lo);
	WREG32(mmCP_HQD_EOP_BASE_ADDR_HI, m->cp_hqd_eop_base_addr_hi);
	WREG32(mmCP_HQD_EOP_CONTROL, m->cp_hqd_eop_control);
	WREG32(mmCP_HQD_EOP_RPTR, m->cp_hqd_eop_rptr);
	WREG32(mmCP_HQD_EOP_WPTR, m->cp_hqd_eop_wptr);
	WREG32(mmCP_HQD_EOP_EVENTS, m->cp_hqd_eop_done_events);

	WREG32(mmCP_HQD_CTX_SAVE_BASE_ADDR_LO, m->cp_hqd_ctx_save_base_addr_lo);
	WREG32(mmCP_HQD_CTX_SAVE_BASE_ADDR_HI, m->cp_hqd_ctx_save_base_addr_hi);
	WREG32(mmCP_HQD_CTX_SAVE_CONTROL, m->cp_hqd_ctx_save_control);
	WREG32(mmCP_HQD_CNTL_STACK_OFFSET, m->cp_hqd_cntl_stack_offset);
	WREG32(mmCP_HQD_CNTL_STACK_SIZE, m->cp_hqd_cntl_stack_size);
	WREG32(mmCP_HQD_WG_STATE_OFFSET, m->cp_hqd_wg_state_offset);
	WREG32(mmCP_HQD_CTX_SAVE_SIZE, m->cp_hqd_ctx_save_size);

	WREG32(mmCP_HQD_IB_CONTROL, m->cp_hqd_ib_control);

	WREG32(mmCP_HQD_DEQUEUE_REQUEST, m->cp_hqd_dequeue_request);
	WREG32(mmCP_HQD_ERROR, m->cp_hqd_error);
	WREG32(mmCP_HQD_EOP_WPTR_MEM, m->cp_hqd_eop_wptr_mem);
	WREG32(mmCP_HQD_EOP_DONES, m->cp_hqd_eop_dones);

	WREG32(mmCP_HQD_ACTIVE, m->cp_hqd_active);

	release_queue(kgd);

	return 0;
}

static int kgd_hqd_dump(struct kgd_dev *kgd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t i = 0, reg;
#define HQD_N_REGS (54+4)
#define DUMP_REG(addr) do {				\
		if (WARN_ON_ONCE(i >= HQD_N_REGS))	\
			break;				\
		(*dump)[i][0] = (addr) << 2;		\
		(*dump)[i++][1] = RREG32(addr);		\
	} while (0)

	*dump = kmalloc(HQD_N_REGS*2*sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	acquire_queue(kgd, pipe_id, queue_id);

	DUMP_REG(mmCOMPUTE_STATIC_THREAD_MGMT_SE0);
	DUMP_REG(mmCOMPUTE_STATIC_THREAD_MGMT_SE1);
	DUMP_REG(mmCOMPUTE_STATIC_THREAD_MGMT_SE2);
	DUMP_REG(mmCOMPUTE_STATIC_THREAD_MGMT_SE3);

	for (reg = mmCP_MQD_BASE_ADDR; reg <= mmCP_HQD_EOP_DONES; reg++)
		DUMP_REG(reg);

	release_queue(kgd);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct vi_sdma_mqd *m;
	uint32_t sdma_base_addr;
	uint32_t temp, timeout = 2000;
	uint32_t data;
	bool wptr_valid = false;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL,
		m->sdmax_rlcx_rb_cntl & (~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK));

	while (true) {
		temp = RREG32(sdma_base_addr + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (temp & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (timeout == 0)
			return -ETIME;
		msleep(10);
		timeout -= 10;
	}
	if (m->sdma_engine_id) {
		data = RREG32(mmSDMA1_GFX_CONTEXT_CNTL);
		data = REG_SET_FIELD(data, SDMA1_GFX_CONTEXT_CNTL,
				RESUME_CTX, 0);
		WREG32(mmSDMA1_GFX_CONTEXT_CNTL, data);
	} else {
		data = RREG32(mmSDMA0_GFX_CONTEXT_CNTL);
		data = REG_SET_FIELD(data, SDMA0_GFX_CONTEXT_CNTL,
				RESUME_CTX, 0);
		WREG32(mmSDMA0_GFX_CONTEXT_CNTL, data);
	}

	WREG32(sdma_base_addr + mmSDMA0_RLC0_DOORBELL, m->sdmax_rlcx_doorbell);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR, m->sdmax_rlcx_rb_rptr);

	if (mm && mm == current->mm)
		wptr_valid = !get_user(data, wptr);
	else if (mm) {
		struct vm_area_struct *vma;

		vma = find_vma(mm, (unsigned long)wptr);
		if (vma && vma->vm_start <= (unsigned long)wptr &&
		    vma->vm_ops && vma->vm_ops->access)
			wptr_valid = (sizeof(data) == vma->vm_ops->access(
					      vma, (unsigned long)wptr,
					      &data, sizeof(data), 0));
	}
	if (wptr_valid)
		WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_WPTR, data);
	else
		WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_WPTR,
		       m->sdmax_rlcx_rb_rptr);

	WREG32(sdma_base_addr + mmSDMA0_RLC0_VIRTUAL_ADDR, m->sdmax_rlcx_virtual_addr);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_BASE, m->sdmax_rlcx_rb_base);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_BASE_HI, m->sdmax_rlcx_rb_base_hi);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_ADDR_LO, m->sdmax_rlcx_rb_rptr_addr_lo);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_ADDR_HI, m->sdmax_rlcx_rb_rptr_addr_hi);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL, m->sdmax_rlcx_rb_cntl);

	return 0;
}

static int kgd_hqd_sdma_dump(struct kgd_dev *kgd,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t sdma_offset = engine_id * SDMA1_REGISTER_OFFSET +
		queue_id * KFD_VI_SDMA_QUEUE_OFFSET;
	uint32_t i = 0, reg;
#undef HQD_N_REGS
#define HQD_N_REGS (19+4+2+3+7)

	*dump = kmalloc(HQD_N_REGS*2*sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	for (reg = mmSDMA0_RLC0_RB_CNTL; reg <= mmSDMA0_RLC0_DOORBELL; reg++)
		DUMP_REG(sdma_offset + reg);
	for (reg = mmSDMA0_RLC0_VIRTUAL_ADDR; reg <= mmSDMA0_RLC0_WATERMARK;
	     reg++)
		DUMP_REG(sdma_offset + reg);
	for (reg = mmSDMA0_RLC0_CSA_ADDR_LO; reg <= mmSDMA0_RLC0_CSA_ADDR_HI;
	     reg++)
		DUMP_REG(sdma_offset + reg);
	for (reg = mmSDMA0_RLC0_IB_SUB_REMAIN; reg <= mmSDMA0_RLC0_DUMMY_REG;
	     reg++)
		DUMP_REG(sdma_offset + reg);
	for (reg = mmSDMA0_RLC0_MIDCMD_DATA0; reg <= mmSDMA0_RLC0_MIDCMD_CNTL;
	     reg++)
		DUMP_REG(sdma_offset + reg);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
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

static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct vi_sdma_mqd *m;
	uint32_t sdma_base_addr;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	sdma_rlc_rb_cntl = RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL);

	if (sdma_rlc_rb_cntl & SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK)
		return true;

	return false;
}

static int kgd_hqd_destroy(struct kgd_dev *kgd,
				enum kfd_preempt_type reset_type,
				unsigned int utimeout, uint32_t pipe_id,
				uint32_t queue_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t temp;
	enum hqd_dequeue_request_type type;
	unsigned long flags, end_jiffies;
	int retry;

	acquire_queue(kgd, pipe_id, queue_id);

	switch (reset_type) {
	case KFD_PREEMPT_TYPE_WAVEFRONT_DRAIN:
		type = DRAIN_PIPE;
		break;
	case KFD_PREEMPT_TYPE_WAVEFRONT_RESET:
		type = RESET_WAVES;
		break;
	default:
		type = DRAIN_PIPE;
		break;
	}

	/* Workaround: If IQ timer is active and the wait time is close to or
	 * equal to 0, dequeueing is not safe. Wait until either the wait time
	 * is larger or timer is cleared. Also, ensure that IQ_REQ_PEND is
	 * cleared before continuing. Also, ensure wait times are set to at
	 * least 0x3.
	 */
	local_irq_save(flags);
	preempt_disable();
	retry = 5000; /* wait for 500 usecs at maximum */
	while (true) {
		temp = RREG32(mmCP_HQD_IQ_TIMER);
		if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, PROCESSING_IQ)) {
			pr_debug("HW is processing IQ\n");
			goto loop;
		}
		if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, ACTIVE)) {
			if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, RETRY_TYPE)
					== 3) /* SEM-rearm is safe */
				break;
			/* Wait time 3 is safe for CP, but our MMIO read/write
			 * time is close to 1 microsecond, so check for 10 to
			 * leave more buffer room
			 */
			if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, WAIT_TIME)
					>= 10)
				break;
			pr_debug("IQ timer is active\n");
		} else
			break;
	loop:
		if (!retry) {
			pr_err("kfd: CP HQD IQ timer status time out\n");
			break;
		}
		ndelay(100);
		--retry;
	}
	retry = 1000;
	while (true) {
		temp = RREG32(mmCP_HQD_DEQUEUE_REQUEST);
		if (!(temp & CP_HQD_DEQUEUE_REQUEST__IQ_REQ_PEND_MASK))
			break;
		pr_debug("Dequeue request is pending\n");

		if (!retry) {
			pr_err("kfd: CP HQD dequeue request time out\n");
			break;
		}
		ndelay(100);
		--retry;
	}
	local_irq_restore(flags);
	preempt_enable();

	WREG32(mmCP_HQD_DEQUEUE_REQUEST, type);

	end_jiffies = (utimeout * HZ / 1000) + jiffies;
	while (true) {
		temp = RREG32(mmCP_HQD_ACTIVE);
		if (!(temp & CP_HQD_ACTIVE__ACTIVE_MASK))
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("kfd: cp queue preemption time out.\n");
			release_queue(kgd);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	release_queue(kgd);
	return 0;
}

static int kgd_hqd_sdma_destroy(struct kgd_dev *kgd, void *mqd,
				unsigned int utimeout)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct vi_sdma_mqd *m;
	uint32_t sdma_base_addr;
	uint32_t temp;
	unsigned long end_jiffies = (utimeout * HZ / 1000) + jiffies;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	temp = RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL);
	temp = temp & ~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK;
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL, temp);

	while (true) {
		temp = RREG32(sdma_base_addr + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (temp & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies))
			return -ETIME;
		usleep_range(500, 1000);
	}

	WREG32(sdma_base_addr + mmSDMA0_RLC0_DOORBELL, 0);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL,
		RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL) |
		SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK);

	m->sdmax_rlcx_rb_rptr = RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR);

	return 0;
}

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd,
							uint8_t vmid)
{
	uint32_t reg;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	reg = RREG32(mmATC_VMID0_PASID_MAPPING + vmid);
	return reg & ATC_VMID0_PASID_MAPPING__VALID_MASK;
}

static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
								uint8_t vmid)
{
	uint32_t reg;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	reg = RREG32(mmATC_VMID0_PASID_MAPPING + vmid);
	return reg & ATC_VMID0_PASID_MAPPING__PASID_MASK;
}

static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	WREG32(mmVM_INVALIDATE_REQUEST, 1 << vmid);
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
	uint32_t data = 0;

	mutex_lock(&adev->grbm_idx_mutex);

	WREG32(mmGRBM_GFX_INDEX, gfx_index_val);
	WREG32(mmSQ_CMD, sq_cmd);

	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		INSTANCE_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SH_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SE_BROADCAST_WRITES, 1);

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

static int write_config_static_mem(struct kgd_dev *kgd, bool swizzle_enable,
		uint8_t element_size, uint8_t index_stride, uint8_t mtype)
{
	uint32_t reg;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	reg = swizzle_enable << SH_STATIC_MEM_CONFIG__SWIZZLE_ENABLE__SHIFT |
		element_size << SH_STATIC_MEM_CONFIG__ELEMENT_SIZE__SHIFT |
		index_stride << SH_STATIC_MEM_CONFIG__INDEX_STRIDE__SHIFT |
		mtype << SH_STATIC_MEM_CONFIG__PRIVATE_MTYPE__SHIFT;

	WREG32(mmSH_STATIC_MEM_CONFIG, reg);
	return 0;
}
static int alloc_memory_of_scratch(struct kgd_dev *kgd,
				 uint64_t va, uint32_t vmid)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	lock_srbm(kgd, 0, 0, 0, vmid);
	WREG32(mmSH_HIDDEN_PRIVATE_BASE_VMID, va);
	unlock_srbm(kgd);

	return 0;
}

static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	const union amdgpu_firmware_header *hdr;

	BUG_ON(kgd == NULL);

	switch (type) {
	case KGD_ENGINE_PFP:
		hdr = (const union amdgpu_firmware_header *)
							adev->gfx.pfp_fw->data;
		break;

	case KGD_ENGINE_ME:
		hdr = (const union amdgpu_firmware_header *)
							adev->gfx.me_fw->data;
		break;

	case KGD_ENGINE_CE:
		hdr = (const union amdgpu_firmware_header *)
							adev->gfx.ce_fw->data;
		break;

	case KGD_ENGINE_MEC1:
		hdr = (const union amdgpu_firmware_header *)
							adev->gfx.mec_fw->data;
		break;

	case KGD_ENGINE_MEC2:
		hdr = (const union amdgpu_firmware_header *)
							adev->gfx.mec2_fw->data;
		break;

	case KGD_ENGINE_RLC:
		hdr = (const union amdgpu_firmware_header *)
							adev->gfx.rlc_fw->data;
		break;

	case KGD_ENGINE_SDMA1:
		hdr = (const union amdgpu_firmware_header *)
							adev->sdma.instance[0].fw->data;
		break;

	case KGD_ENGINE_SDMA2:
		hdr = (const union amdgpu_firmware_header *)
							adev->sdma.instance[1].fw->data;
		break;

	default:
		return 0;
	}

	if (hdr == NULL)
		return 0;

	/* Only 12 bit in use*/
	return hdr->common.ucode_version;
}

static void set_num_of_requests(struct kgd_dev *kgd,
			uint8_t num_of_requests)
{
	pr_debug("in %s this is a stub\n", __func__);
}

static void set_vm_context_page_table_base(struct kgd_dev *kgd, uint32_t vmid,
		uint32_t page_table_base)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	/* TODO: Don't use hardcoded VMIDs */
	if (vmid < 8 || vmid > 15) {
		pr_err("amdkfd: trying to set page table base for wrong VMID\n");
		return;
	}
	WREG32(mmVM_CONTEXT8_PAGE_TABLE_BASE_ADDR + vmid - 8, page_table_base);
}
