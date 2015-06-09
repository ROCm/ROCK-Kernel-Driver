/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __AMDGPU_DM_H__
#define __AMDGPU_DM_H__

/*
#include "linux/switch.h"
*/

/*
 * This file contains the definition for amdgpu_display_manager
 * and its API for amdgpu driver's use.
 * This component provides all the display related functionality
 * and this is the only component that calls DAL API.
 * The API contained here intended for amdgpu driver use.
 * The API that is called directly from KMS framework is located
 * in amdgpu_dm_kms.h file
 */

#define AMDGPU_DM_MAX_DISPLAY_INDEX 31
/*
#include "include/amdgpu_dal_power_if.h"
#include "amdgpu_dm_irq.h"
*/

#include "irq_types.h"

/* Forward declarations */
struct amdgpu_device;
struct drm_device;
struct amdgpu_dm_irq_handler_data;

struct amdgpu_dm_prev_state {
	struct drm_framebuffer *fb;
	int32_t x;
	int32_t y;
	struct drm_display_mode mode;
};


struct amdgpu_display_manager {
	struct dal *dal;
	/* lock to be used when DAL is called from SYNC IRQ context */
	spinlock_t dal_lock;

	struct amdgpu_device *adev;	/*AMD base driver*/
	struct drm_device *ddev;	/*DRM base driver*/
	u16 display_indexes_num;

	u32 mode_query_option;

	struct amdgpu_dm_prev_state prev_state;

	/*
	 * 'irq_source_handler_table' holds a list of handlers
	 * per (DAL) IRQ source.
	 *
	 * Each IRQ source may need to be handled at different contexts.
	 * By 'context' we mean, for example:
	 * - The ISR context, which is the direct interrupt handler.
	 * - The 'deferred' context - this is the post-processing of the
	 *	interrupt, but at a lower priority.
	 *
	 * Note that handlers are called in the same order as they were
	 * registered (FIFO).
	 */
	struct list_head irq_handler_list_table[INTERRUPT_CONTEXT_NUMBER]
					  [DAL_IRQ_SOURCES_NUMBER];

	/* this spin lock synchronizes access to 'irq_handler_list_table' */
	spinlock_t irq_handler_list_table_lock;

	/* Timer-related data. */
	struct list_head timer_handler_list;
	struct workqueue_struct *timer_workqueue;
};


/* basic init/fini API */
int amdgpu_dm_init(struct amdgpu_device *adev);

void amdgpu_dm_fini(struct amdgpu_device *adev);

void amdgpu_dm_destroy(void);

/* initializes drm_device display related structures, based on the information
 * provided by DAL. The drm strcutures are: drm_crtc, drm_connector,
 * drm_encoder, drm_mode_config
 *
 * Returns 0 on success
 */
int amdgpu_dm_initialize_drm_device(
	struct amdgpu_display_manager *dm);

/* removes and deallocates the drm structures, created by the above function */
void amdgpu_dm_destroy_drm_device(
	struct amdgpu_display_manager *dm);

/* Locking/Mutex */
bool amdgpu_dm_acquire_dal_lock(struct amdgpu_display_manager *dm);

bool amdgpu_dm_release_dal_lock(struct amdgpu_display_manager *dm);

extern const struct amd_ip_funcs amdgpu_dm_funcs;

#endif /* __AMDGPU_DM_H__ */
