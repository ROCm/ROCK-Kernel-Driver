/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dm_services.h"

#include "include/irq_service_interface.h"
#include "include/logger_interface.h"

#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
#include "dce110/irq_service_dce110.h"
#endif

#if defined(CONFIG_DRM_AMD_DAL_DCE8_0)
	/*
	 * TODO: implement DCE8.x IRQ service
	 */
#include "dce110/irq_service_dce110.h"
#endif

#include "irq_service.h"

bool dal_irq_service_construct(
	struct irq_service *irq_service,
	struct irq_service_init_data *init_data)
{
	if (!init_data || !init_data->ctx)
		return false;

	irq_service->ctx = init_data->ctx;
	return true;
}

struct irq_service *dal_irq_service_create(
	enum dce_version version,
	struct irq_service_init_data *init_data)
{
	switch (version) {
#if defined(CONFIG_DRM_AMD_DAL_DCE8_0)
	case DCE_VERSION_8_0:
		return dal_irq_service_dce110_create(init_data);
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE10_0)
	case DCE_VERSION_10_0:
		return dal_irq_service_dce110_create(init_data);
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_2)
	case DCE_VERSION_11_2:
		return dal_irq_service_dce110_create(init_data);
#endif
#if defined(CONFIG_DRM_AMD_DAL_DCE11_0)
	case DCE_VERSION_11_0:
		return dal_irq_service_dce110_create(init_data);
#endif
	default:
		return NULL;
	}
}

void dal_irq_service_destroy(struct irq_service **irq_service)
{
	if (!irq_service || !*irq_service) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dm_free(*irq_service);

	*irq_service = NULL;
}

const struct irq_source_info *find_irq_source_info(
	struct irq_service *irq_service,
	enum dc_irq_source source)
{
	if (source > DAL_IRQ_SOURCES_NUMBER || source < DC_IRQ_SOURCE_INVALID)
		return NULL;

	return &irq_service->info[source];
}

void dal_irq_service_set_generic(
	struct irq_service *irq_service,
	const struct irq_source_info *info,
	bool enable)
{
	uint32_t addr = info->enable_reg;
	uint32_t value = dm_read_reg(irq_service->ctx, addr);

	value = (value & ~info->enable_mask) |
		(info->enable_value[enable ? 0 : 1] & info->enable_mask);
	dm_write_reg(irq_service->ctx, addr, value);
}

bool dal_irq_service_set(
	struct irq_service *irq_service,
	enum dc_irq_source source,
	bool enable)
{
	const struct irq_source_info *info =
		find_irq_source_info(irq_service, source);

	if (!info) {
		dal_logger_write(
			irq_service->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_IRQ_SERVICE,
			"%s: cannot find irq info table entry for %d\n",
			__func__,
			source);
		return false;
	}

	dal_irq_service_ack(irq_service, source);

	if (info->funcs->set)
		return info->funcs->set(irq_service, info, enable);

	dal_irq_service_set_generic(irq_service, info, enable);

	return true;
}

void dal_irq_service_ack_generic(
	struct irq_service *irq_service,
	const struct irq_source_info *info)
{
	uint32_t addr = info->ack_reg;
	uint32_t value = dm_read_reg(irq_service->ctx, addr);

	value = (value & ~info->ack_mask) |
		(info->ack_value & info->ack_mask);
	dm_write_reg(irq_service->ctx, addr, value);
}

bool dal_irq_service_ack(
	struct irq_service *irq_service,
	enum dc_irq_source source)
{
	const struct irq_source_info *info =
		find_irq_source_info(irq_service, source);

	if (!info) {
		dal_logger_write(
			irq_service->ctx->logger,
			LOG_MAJOR_ERROR,
			LOG_MINOR_COMPONENT_IRQ_SERVICE,
			"%s: cannot find irq info table entry for %d\n",
			__func__,
			source);
		return false;
	}

	if (info->funcs->ack)
		return info->funcs->ack(irq_service, info);

	dal_irq_service_ack_generic(irq_service, info);

	return true;
}

enum dc_irq_source dal_irq_service_to_irq_source(
		struct irq_service *irq_service,
		uint32_t src_id,
		uint32_t ext_id)
{
	return irq_service->funcs->to_dal_irq_source(
		irq_service,
		src_id,
		ext_id);
}
