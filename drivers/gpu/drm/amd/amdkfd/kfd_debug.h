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
 */

#ifndef KFD_DEBUG_EVENTS_H_INCLUDED
#define KFD_DEBUG_EVENTS_H_INCLUDED

#include "kfd_priv.h"

uint32_t kfd_dbg_get_queue_status_word(struct queue *q, int flags);

int kfd_dbg_ev_query_debug_event(struct kfd_process *process,
			unsigned int *source_id,
			uint64_t exception_clear_mask,
			uint64_t *event_status);

void kfd_set_dbg_ev_from_interrupt(struct kfd_dev *dev,
				   unsigned int pasid,
				   uint32_t doorbell_id,
				   bool is_vmfault);
void kfd_dbg_ev_raise(int event_type, struct kfd_process *process,
			struct kfd_dev *dev,
			unsigned int source_id,
			bool use_worker);
int kfd_dbg_ev_enable(struct kfd_process *process);

int kfd_dbg_trap_disable(struct kfd_process *target,
			bool unwind,
			int unwind_count);
int kfd_dbg_trap_enable(struct kfd_process *target, uint32_t fd,
			uint32_t *ttmp_save);
int kfd_dbg_trap_set_wave_launch_override(struct kfd_process *target,
					uint32_t trap_override,
					uint32_t trap_mask_bits,
					uint32_t trap_mask_request,
					uint32_t *trap_mask_prev,
					uint32_t *trap_mask_supported);
int kfd_dbg_trap_set_wave_launch_mode(struct kfd_process *target,
					uint8_t wave_launch_mode);
int kfd_dbg_trap_clear_address_watch(struct kfd_process *target,
					uint32_t watch_id);
int kfd_dbg_trap_set_address_watch(struct kfd_process *target,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t *watch_id,
					uint32_t watch_mode);
int kfd_dbg_trap_set_precise_mem_ops(struct kfd_process *target,
		uint32_t enable);

static inline bool kfd_dbg_is_per_vmid_supported(struct kfd_dev *dev)
{
	return dev->device_info->asic_family == CHIP_ALDEBARAN;
}

void debug_event_write_work_handler(struct work_struct *work);
#endif
