// SPDX-License-Identifier: MIT
/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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
#include "amdgpu_gfx.h"
#include "v11_structs.h"
#include "mes_v11_0.h"
#include "mes_v11_0_userqueue.h"

#define AMDGPU_USERQ_PROC_CTX_SZ PAGE_SIZE
#define AMDGPU_USERQ_GANG_CTX_SZ PAGE_SIZE

static int
mes_v11_0_map_gtt_bo_to_gart(struct amdgpu_bo *bo)
{
	int ret;

	ret = amdgpu_bo_reserve(bo, true);
	if (ret) {
		DRM_ERROR("Failed to reserve bo. ret %d\n", ret);
		goto err_reserve_bo_failed;
	}

	ret = amdgpu_ttm_alloc_gart(&bo->tbo);
	if (ret) {
		DRM_ERROR("Failed to bind bo to GART. ret %d\n", ret);
		goto err_map_bo_gart_failed;
	}

	amdgpu_bo_unreserve(bo);
	bo = amdgpu_bo_ref(bo);

	return 0;

err_map_bo_gart_failed:
	amdgpu_bo_unreserve(bo);
err_reserve_bo_failed:
	return ret;
}

static int
mes_v11_0_create_wptr_mapping(struct amdgpu_userq_mgr *uq_mgr,
			      struct amdgpu_usermode_queue *queue,
			      uint64_t wptr)
{
	struct amdgpu_bo_va_mapping *wptr_mapping;
	struct amdgpu_vm *wptr_vm;
	struct amdgpu_userq_obj *wptr_obj = &queue->wptr_obj;
	int ret;

	wptr_vm = queue->vm;
	ret = amdgpu_bo_reserve(wptr_vm->root.bo, false);
	if (ret)
		return ret;

	wptr &= AMDGPU_GMC_HOLE_MASK;
	wptr_mapping = amdgpu_vm_bo_lookup_mapping(wptr_vm, wptr >> PAGE_SHIFT);
	amdgpu_bo_unreserve(wptr_vm->root.bo);
	if (!wptr_mapping) {
		DRM_ERROR("Failed to lookup wptr bo\n");
		return -EINVAL;
	}

	wptr_obj->obj = wptr_mapping->bo_va->base.bo;
	if (wptr_obj->obj->tbo.base.size > PAGE_SIZE) {
		DRM_ERROR("Requested GART mapping for wptr bo larger than one page\n");
		return -EINVAL;
	}

	ret = mes_v11_0_map_gtt_bo_to_gart(wptr_obj->obj);
	if (ret) {
		DRM_ERROR("Failed to map wptr bo to GART\n");
		return ret;
	}

	queue->wptr_obj.gpu_addr = amdgpu_bo_gpu_offset_no_check(wptr_obj->obj);
	return 0;
}

static int mes_v11_0_userq_map(struct amdgpu_userq_mgr *uq_mgr,
			       struct amdgpu_usermode_queue *queue,
			       struct amdgpu_mqd_prop *userq_props)
{
	struct amdgpu_device *adev = uq_mgr->adev;
	struct amdgpu_userq_obj *ctx = &queue->fw_obj;
	struct mes_add_queue_input queue_input;
	int r;

	memset(&queue_input, 0x0, sizeof(struct mes_add_queue_input));

	queue_input.process_va_start = 0;
	queue_input.process_va_end = (adev->vm_manager.max_pfn - 1) << AMDGPU_GPU_PAGE_SHIFT;

	/* set process quantum to 10 ms and gang quantum to 1 ms as default */
	queue_input.process_quantum = 100000;
	queue_input.gang_quantum = 10000;
	queue_input.paging = false;

	queue_input.process_context_addr = ctx->gpu_addr;
	queue_input.gang_context_addr = ctx->gpu_addr + AMDGPU_USERQ_PROC_CTX_SZ;
	queue_input.inprocess_gang_priority = AMDGPU_MES_PRIORITY_LEVEL_NORMAL;
	queue_input.gang_global_priority_level = AMDGPU_MES_PRIORITY_LEVEL_NORMAL;

	queue_input.process_id = queue->vm->pasid;
	queue_input.queue_type = queue->queue_type;
	queue_input.mqd_addr = queue->mqd.gpu_addr;
	queue_input.wptr_addr = userq_props->wptr_gpu_addr;
	queue_input.queue_size = userq_props->queue_size >> 2;
	queue_input.doorbell_offset = userq_props->doorbell_index;
	queue_input.page_table_base_addr = amdgpu_gmc_pd_addr(queue->vm->root.bo);
	queue_input.wptr_mc_addr = queue->wptr_obj.gpu_addr;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->add_hw_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r) {
		DRM_ERROR("Failed to map queue in HW, err (%d)\n", r);
		return r;
	}

	DRM_DEBUG_DRIVER("Queue (doorbell:%d) mapped successfully\n", userq_props->doorbell_index);
	return 0;
}

static void mes_v11_0_userq_unmap(struct amdgpu_userq_mgr *uq_mgr,
				  struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_device *adev = uq_mgr->adev;
	struct mes_remove_queue_input queue_input;
	struct amdgpu_userq_obj *ctx = &queue->fw_obj;
	int r;

	memset(&queue_input, 0x0, sizeof(struct mes_remove_queue_input));
	queue_input.doorbell_offset = queue->doorbell_index;
	queue_input.gang_context_addr = ctx->gpu_addr + AMDGPU_USERQ_PROC_CTX_SZ;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->remove_hw_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		DRM_ERROR("Failed to unmap queue in HW, err (%d)\n", r);
}

static int mes_v11_0_userq_create_ctx_space(struct amdgpu_userq_mgr *uq_mgr,
					    struct amdgpu_usermode_queue *queue,
					    struct drm_amdgpu_userq_in *mqd_user)
{
	struct amdgpu_userq_obj *ctx = &queue->fw_obj;
	int r, size;

	/*
	 * The FW expects at least one page space allocated for
	 * process ctx and gang ctx each. Create an object
	 * for the same.
	 */
	size = AMDGPU_USERQ_PROC_CTX_SZ + AMDGPU_USERQ_GANG_CTX_SZ;
	r = amdgpu_userqueue_create_object(uq_mgr, ctx, size);
	if (r) {
		DRM_ERROR("Failed to allocate ctx space bo for userqueue, err:%d\n", r);
		return r;
	}

	/* Shadow, GDS and CSA objects come directly from userspace */
	if (mqd_user->ip_type == AMDGPU_HW_IP_GFX) {
		struct v11_gfx_mqd *mqd = queue->mqd.cpu_ptr;
		struct drm_amdgpu_userq_mqd_gfx11 *mqd_gfx_v11;

		if (mqd_user->mqd_size != sizeof(*mqd_gfx_v11) || !mqd_user->mqd) {
			DRM_ERROR("Invalid GFX MQD\n");
			return -EINVAL;
		}

		mqd_gfx_v11 = memdup_user(u64_to_user_ptr(mqd_user->mqd), mqd_user->mqd_size);
		if (IS_ERR(mqd_gfx_v11)) {
			DRM_ERROR("Failed to read user MQD\n");
			amdgpu_userqueue_destroy_object(uq_mgr, ctx);
			return -ENOMEM;
		}

		mqd->shadow_base_lo = mqd_gfx_v11->shadow_va & 0xFFFFFFFC;
		mqd->shadow_base_hi = upper_32_bits(mqd_gfx_v11->shadow_va);

		mqd->gds_bkup_base_lo = mqd_gfx_v11->gds_va & 0xFFFFFFFC;
		mqd->gds_bkup_base_hi = upper_32_bits(mqd_gfx_v11->gds_va);

		mqd->fw_work_area_base_lo = mqd_gfx_v11->csa_va & 0xFFFFFFFC;
		mqd->fw_work_area_base_hi = upper_32_bits(mqd_gfx_v11->csa_va);
		kfree(mqd_gfx_v11);
	} else if (mqd_user->ip_type == AMDGPU_HW_IP_DMA) {
		struct v11_sdma_mqd *mqd = queue->mqd.cpu_ptr;
		struct drm_amdgpu_userq_mqd_sdma_gfx11 *mqd_sdma_v11;

		if (mqd_user->mqd_size != sizeof(*mqd_sdma_v11) || !mqd_user->mqd) {
			DRM_ERROR("Invalid SDMA MQD\n");
			return -EINVAL;
		}

		mqd_sdma_v11 = memdup_user(u64_to_user_ptr(mqd_user->mqd), mqd_user->mqd_size);
		if (IS_ERR(mqd_sdma_v11)) {
			DRM_ERROR("Failed to read sdma user MQD\n");
			amdgpu_userqueue_destroy_object(uq_mgr, ctx);
			return -ENOMEM;
		}

		mqd->sdmax_rlcx_csa_addr_lo = mqd_sdma_v11->csa_va & 0xFFFFFFFC;
		mqd->sdmax_rlcx_csa_addr_hi = upper_32_bits(mqd_sdma_v11->csa_va);
	}

	return 0;
}

static int mes_v11_0_userq_mqd_create(struct amdgpu_userq_mgr *uq_mgr,
				      struct drm_amdgpu_userq_in *args_in,
				      struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_device *adev = uq_mgr->adev;
	struct amdgpu_mqd *mqd_hw_default = &adev->mqds[queue->queue_type];
	struct drm_amdgpu_userq_in *mqd_user = args_in;
	struct amdgpu_mqd_prop *userq_props;
	int r;

	/* Structure to initialize MQD for userqueue using generic MQD init function */
	userq_props = kzalloc(sizeof(struct amdgpu_mqd_prop), GFP_KERNEL);
	if (!userq_props) {
		DRM_ERROR("Failed to allocate memory for userq_props\n");
		return -ENOMEM;
	}

	if (!mqd_user->wptr_va || !mqd_user->rptr_va ||
	    !mqd_user->queue_va || mqd_user->queue_size == 0) {
		DRM_ERROR("Invalid MQD parameters for userqueue\n");
		r = -EINVAL;
		goto free_props;
	}

	r = amdgpu_userqueue_create_object(uq_mgr, &queue->mqd, mqd_hw_default->mqd_size);
	if (r) {
		DRM_ERROR("Failed to create MQD object for userqueue\n");
		goto free_props;
	}

	/* Initialize the MQD BO with user given values */
	userq_props->wptr_gpu_addr = mqd_user->wptr_va;
	userq_props->rptr_gpu_addr = mqd_user->rptr_va;
	userq_props->queue_size = mqd_user->queue_size;
	userq_props->hqd_base_gpu_addr = mqd_user->queue_va;
	userq_props->mqd_gpu_addr = queue->mqd.gpu_addr;
	userq_props->use_doorbell = true;
	userq_props->doorbell_index = queue->doorbell_index;

	queue->userq_prop = userq_props;

	r = mqd_hw_default->init_mqd(adev, (void *)queue->mqd.cpu_ptr, userq_props);
	if (r) {
		DRM_ERROR("Failed to initialize MQD for userqueue\n");
		goto free_mqd;
	}

	/* Create BO for FW operations */
	r = mes_v11_0_userq_create_ctx_space(uq_mgr, queue, mqd_user);
	if (r) {
		DRM_ERROR("Failed to allocate BO for userqueue (%d)", r);
		goto free_mqd;
	}

	/* FW expects WPTR BOs to be mapped into GART */
	r = mes_v11_0_create_wptr_mapping(uq_mgr, queue, userq_props->wptr_gpu_addr);
	if (r) {
		DRM_ERROR("Failed to create WPTR mapping\n");
		goto free_ctx;
	}

	/* Map userqueue into FW using MES */
	r = mes_v11_0_userq_map(uq_mgr, queue, userq_props);
	if (r) {
		DRM_ERROR("Failed to init MQD\n");
		goto free_ctx;
	}

	return 0;

free_ctx:
	amdgpu_userqueue_destroy_object(uq_mgr, &queue->fw_obj);

free_mqd:
	amdgpu_userqueue_destroy_object(uq_mgr, &queue->mqd);

free_props:
	kfree(userq_props);

	return r;
}

static void
mes_v11_0_userq_mqd_destroy(struct amdgpu_userq_mgr *uq_mgr,
			    struct amdgpu_usermode_queue *queue)
{
	mes_v11_0_userq_unmap(uq_mgr, queue);
	amdgpu_bo_unref(&queue->wptr_obj.obj);
	amdgpu_userqueue_destroy_object(uq_mgr, &queue->fw_obj);
	kfree(queue->userq_prop);
	amdgpu_userqueue_destroy_object(uq_mgr, &queue->mqd);
}

const struct amdgpu_userq_funcs userq_mes_v11_0_funcs = {
	.mqd_create = mes_v11_0_userq_mqd_create,
	.mqd_destroy = mes_v11_0_userq_mqd_destroy,
};
