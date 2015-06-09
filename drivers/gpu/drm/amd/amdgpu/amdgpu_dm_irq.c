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

#include <drm/drmP.h>

#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_irq.h"


/******************************************************************************
 * Private declarations.
 *****************************************************************************/

struct handler_common_data {
	struct list_head list;
	interrupt_handler handler;
	void *handler_arg;

	/* DM which this handler belongs to */
	struct amdgpu_display_manager *dm;
};

struct amdgpu_dm_irq_handler_data {
	struct handler_common_data hcd;
	/* DAL irq source which registered for this interrupt. */
	enum dal_irq_source irq_source;
	/* In case this interrupt needs post-processing, 'work' will be queued*/
	struct work_struct work;
};

struct amdgpu_dm_timer_handler_data {
	struct handler_common_data hcd;
	struct delayed_work d_work;
};


#define DM_IRQ_TABLE_LOCK(adev, flags) \
	spin_lock_irqsave(&adev->dm.irq_handler_list_table_lock, flags)

#define DM_IRQ_TABLE_UNLOCK(adev, flags) \
	spin_unlock_irqrestore(&adev->dm.irq_handler_list_table_lock, flags)

/******************************************************************************
 * Private functions.
 *****************************************************************************/

static void init_handler_common_data(
	struct handler_common_data *hcd,
	void (*ih)(void *),
	void *args,
	struct amdgpu_display_manager *dm)
{
	hcd->handler = ih;
	hcd->handler_arg = args;
	hcd->dm = dm;
}

/**
 * dm_irq_work_func - Handle an IRQ outside of the interrupt handler proper.
 *
 * @work: work struct
 */
static void dm_irq_work_func(
	struct work_struct *work)
{
	struct amdgpu_dm_irq_handler_data *handler_data =
		container_of(work, struct amdgpu_dm_irq_handler_data, work);

	DRM_DEBUG_KMS("DM_IRQ: work_func: for dal_src=%d\n",
			handler_data->irq_source);

	/* Call a DAL subcomponent which registered for interrupt notification
	 * at INTERRUPT_LOW_IRQ_CONTEXT.
	 * (The most common use is HPD interrupt) */
	handler_data->hcd.handler(handler_data->hcd.handler_arg);
}

/**
 * Remove a handler and return a pointer to hander list from which the
 * handler was removed.
 */
static struct list_head *remove_irq_handler(
	struct amdgpu_device *adev,
	void *ih,
	const struct dal_interrupt_params *int_params)
{
	struct list_head *handler_list;
	struct list_head *entry, *tmp;
	struct amdgpu_dm_irq_handler_data *handler;
	unsigned long irq_table_flags;
	bool handler_removed = false;

	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	handler_list =
		&adev->dm.irq_handler_list_table[int_params->int_context]
						  [int_params->irq_source];
	list_for_each_safe(entry, tmp, handler_list) {

		handler = list_entry(entry, struct amdgpu_dm_irq_handler_data,
				hcd.list);

		if (ih == handler) {
			/* Found our handler. Remove it from the list. */
			list_del(&handler->hcd.list);
			handler_removed = true;
			break;
		}
	}

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

	if (handler_removed == false) {
		/* Not necessarily an error - caller may not
		 * know the context. */
		return NULL;
	}

	/* The handler was removed from the table,
	 * it means it is safe to flush all the 'work'
	 * (because no code can schedule a new one). */
	flush_work(&handler->work);

	kfree(handler);

	DRM_DEBUG_KMS(
	"DM_IRQ: removed irq handler: %p for: dal_src=%d, irq context=%d\n",
		ih, int_params->irq_source, int_params->int_context);

	return handler_list;
}

/* If 'handler_in == NULL' then remove ALL handlers. */
static void remove_timer_handler(
	struct amdgpu_device *adev,
	struct amdgpu_dm_timer_handler_data *handler_in)
{
	struct amdgpu_dm_timer_handler_data *handler_temp;
	struct list_head *handler_list;
	struct list_head *entry, *tmp;
	unsigned long irq_table_flags;
	bool handler_removed = false;

	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	handler_list = &adev->dm.timer_handler_list;

	list_for_each_safe(entry, tmp, handler_list) {
		/* Note that list_for_each_safe() guarantees that
		 * handler_temp is NOT null. */
		handler_temp = list_entry(entry,
				struct amdgpu_dm_timer_handler_data, hcd.list);

		if (handler_in == NULL || handler_in == handler_temp) {
			list_del(&handler_temp->hcd.list);
			DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

			DRM_DEBUG_KMS("DM_IRQ: removing timer handler: %p\n",
					handler_temp);

			if (handler_in == NULL) {
				/* Since it is still in the queue, it must
				 * be cancelled. */
				cancel_delayed_work_sync(&handler_temp->d_work);
			}

			kfree(handler_temp);
			handler_removed = true;

			DM_IRQ_TABLE_LOCK(adev, irq_table_flags);
		}

		if (handler_in == NULL) {
			/* Remove ALL handlers. */
			continue;
		}

		if (handler_in == handler_temp) {
			/* Remove a SPECIFIC handler.
			 * Found our handler - we can stop here. */
			break;
		}
	}

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

	if (handler_in != NULL && handler_removed == false) {
		DRM_ERROR("DM_IRQ: handler: %p is not in the list!\n",
				handler_in);
	}
}

/**
 * dm_timer_work_func - Handle a timer.
 *
 * @work: work struct
 */
static void dm_timer_work_func(
	struct work_struct *work)
{
	struct amdgpu_dm_timer_handler_data *handler_data =
		container_of(work, struct amdgpu_dm_timer_handler_data,
				d_work.work);

	DRM_DEBUG_KMS("DM_IRQ: work_func: handler_data=%p\n", handler_data);

	/* Call a DAL subcomponent which registered for timer notification. */
	handler_data->hcd.handler(handler_data->hcd.handler_arg);

	/* We support only "single shot" timers. That means we must delete
	 * the handler after it was called. */
	remove_timer_handler(handler_data->hcd.dm->adev, handler_data);
}

/******************************************************************************
 * Public functions.
 *
 * Note: caller is responsible for input validation.
 *****************************************************************************/

void *amdgpu_dm_irq_register_interrupt(
	struct amdgpu_device *adev,
	struct dal_interrupt_params *int_params,
	void (*ih)(void *),
	void *handler_args)
{
	struct list_head *handler_list;
	struct amdgpu_dm_irq_handler_data *handler_data;
	unsigned long irq_table_flags;

	handler_data = kzalloc(sizeof(*handler_data), GFP_KERNEL);
	if (!handler_data) {
		DRM_ERROR("DM_IRQ: failed to allocate irq handler!\n");
		return DAL_INVALID_IRQ_HANDLER_IDX;
	}

	memset(handler_data, 0, sizeof(*handler_data));

	init_handler_common_data(&handler_data->hcd, ih, handler_args,
			&adev->dm);

	handler_data->irq_source = int_params->irq_source;

	INIT_WORK(&handler_data->work, dm_irq_work_func);

	/* Lock the list, add the handler. */
	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	handler_list = &adev->dm.irq_handler_list_table[int_params->int_context]
						  [int_params->irq_source];

	list_add_tail(&handler_data->hcd.list, handler_list);

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

	/* This pointer will be stored by code which requested interrupt
	 * registration.
	 * The same pointer will be needed in order to unregister the
	 * interrupt. */

	DRM_DEBUG_KMS(
	"DM_IRQ: added irq handler: %p for: dal_src=%d, irq context=%d\n",
		handler_data, int_params->irq_source, int_params->int_context);

	return handler_data;
}

void amdgpu_dm_irq_unregister_interrupt(
	struct amdgpu_device *adev,
	enum dal_irq_source irq_source,
	void *ih)
{
	struct list_head *handler_list;
	struct dal_interrupt_params int_params;
	int i;

	memset(&int_params, 0, sizeof(int_params));

	int_params.irq_source = irq_source;

	for (i = 0; i < INTERRUPT_CONTEXT_NUMBER; i++) {

		int_params.int_context = i;

		handler_list = remove_irq_handler(adev, ih, &int_params);

		if (handler_list != NULL)
			break;
	}

	if (handler_list == NULL) {
		/* If we got here, it means we searched all irq contexts
		 * for this irq source, but the handler was not found. */
		DRM_ERROR(
		"DM_IRQ: failed to find irq handler:%p for irq_source:%d!\n",
			ih, irq_source);
	}
}

/**
 * amdgpu_dm_irq_schedule_work - schedule all work items registered for
 * 				the "irq_source".
 */
void amdgpu_dm_irq_schedule_work(
	struct amdgpu_device *adev,
	enum dal_irq_source irq_source)
{
	struct list_head *handler_list;
	struct amdgpu_dm_irq_handler_data *handler;
	struct list_head *entry, *tmp;
	unsigned long irq_table_flags;

	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	/* Since the caller is interested in 'work_struct' then
	 * the irq will be post-processed at "INTERRUPT_LOW_IRQ_CONTEXT". */
	handler_list =
		&adev->dm.irq_handler_list_table[INTERRUPT_LOW_IRQ_CONTEXT]
						     [irq_source];

	list_for_each_safe(entry, tmp, handler_list) {

		handler = list_entry(entry, struct amdgpu_dm_irq_handler_data,
				hcd.list);

		DRM_DEBUG_KMS("DM_IRQ: schedule_work: for dal_src=%d\n",
				handler->irq_source);

		schedule_work(&handler->work);
	}

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);
}

int amdgpu_dm_irq_init(
	struct amdgpu_device *adev)
{
	int ctx;
	int src;
	struct list_head *lh;

	DRM_DEBUG_KMS("DM_IRQ\n");

	spin_lock_init(&adev->dm.irq_handler_list_table_lock);

	for (ctx = 0; ctx < INTERRUPT_CONTEXT_NUMBER; ctx++) {
		for (src = 0; src < DAL_IRQ_SOURCES_NUMBER; src++) {

			lh = &adev->dm.irq_handler_list_table[ctx][src];
			INIT_LIST_HEAD(lh);
		}
	}

	INIT_LIST_HEAD(&adev->dm.timer_handler_list);

	/* allocate and initialize the workqueue for DM timer */
	adev->dm.timer_workqueue = create_singlethread_workqueue(
			"dm_timer_queue");
	if (adev->dm.timer_workqueue == NULL) {
		DRM_ERROR("DM_IRQ: unable to create timer queue!\n");
		return -1;
	}

	return 0;
}

void amdgpu_dm_irq_register_timer(
	struct amdgpu_device *adev,
	struct dal_timer_interrupt_params *int_params,
	interrupt_handler ih,
	void *args)
{
	unsigned long jf_delay;
	struct list_head *handler_list;
	struct amdgpu_dm_timer_handler_data *handler_data;
	unsigned long irq_table_flags;

	handler_data = kzalloc(sizeof(*handler_data), GFP_KERNEL);
	if (!handler_data) {
		DRM_ERROR("DM_IRQ: failed to allocate timer handler!\n");
		return;
	}

	memset(handler_data, 0, sizeof(*handler_data));

	init_handler_common_data(&handler_data->hcd, ih, args, &adev->dm);

	INIT_DELAYED_WORK(&handler_data->d_work, dm_timer_work_func);

	/* Lock the list, add the handler. */
	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	handler_list = &adev->dm.timer_handler_list;

	list_add_tail(&handler_data->hcd.list, handler_list);

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

	jf_delay = usecs_to_jiffies(int_params->micro_sec_interval);

	queue_delayed_work(adev->dm.timer_workqueue, &handler_data->d_work,
			jf_delay);

	DRM_DEBUG_KMS("DM_IRQ: added handler:%p with micro_sec_interval=%llu\n",
			handler_data, int_params->micro_sec_interval);
	return;
}

/* DM IRQ and timer resource release */
void amdgpu_dm_irq_fini(
	struct amdgpu_device *adev)
{
	DRM_DEBUG_KMS("DM_IRQ: releasing resources.\n");

	/* Cancel ALL timers and release handlers (if any). */
	remove_timer_handler(adev, NULL);
	/* Release the queue itself. */
	destroy_workqueue(adev->dm.timer_workqueue);
}
