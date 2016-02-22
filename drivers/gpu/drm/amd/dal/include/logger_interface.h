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

#ifndef __DAL_LOGGER_INTERFACE_H__
#define __DAL_LOGGER_INTERFACE_H__

#include "logger_types.h"

struct dal_logger;
struct dc_context;
union logger_flags;

/*
 * TODO: This logger functionality needs to be implemented and reworked.
 */

/*
 *
 * DAL logger functionality
 *
 */

struct dal_logger *dal_logger_create(struct dc_context *ctx);

uint32_t dal_logger_destroy(struct dal_logger **logger);

uint32_t dal_logger_get_mask(
	struct dal_logger *logger,
	enum log_major lvl_major, enum log_minor lvl_minor);

uint32_t dal_logger_set_mask(
		struct dal_logger *logger,
		enum log_major lvl_major, enum log_minor lvl_minor);

uint32_t dal_logger_get_masks(
	struct dal_logger *logger,
	enum log_major lvl_major);

void dal_logger_set_masks(
	struct dal_logger *logger,
	enum log_major lvl_major, uint32_t log_mask);

uint32_t dal_logger_unset_mask(
		struct dal_logger *logger,
		enum log_major lvl_major, enum log_minor lvl_minor);

bool dal_logger_should_log(
		struct dal_logger *logger,
		enum log_major major,
		enum log_minor minor);

uint32_t dal_logger_get_flags(
		struct dal_logger *logger);

void dal_logger_set_flags(
		struct dal_logger *logger,
		union logger_flags flags);

void dal_logger_write(
		struct dal_logger *logger,
		enum log_major major,
		enum log_minor minor,
		const char *msg,
		...);

void dal_logger_append(
		struct log_entry *entry,
		const char *msg,
		...);

uint32_t dal_logger_read(
		struct dal_logger *logger,
		uint32_t output_buffer_size,
		char *output_buffer,
		uint32_t *bytes_read,
		bool single_line);

void dal_logger_open(
		struct dal_logger *logger,
		struct log_entry *entry,
		enum log_major major,
		enum log_minor minor);

void dal_logger_close(struct log_entry *entry);

uint32_t dal_logger_get_buffer_size(struct dal_logger *logger);

uint32_t dal_logger_set_buffer_size(
		struct dal_logger *logger,
		uint32_t new_size);

const struct log_major_info *dal_logger_enum_log_major_info(
		struct dal_logger *logger,
		unsigned int enum_index);

const struct log_minor_info *dal_logger_enum_log_minor_info(
		struct dal_logger *logger,
		enum log_major major,
		unsigned int enum_index);

/* Any function which is empty or have incomplete implementation should be
 * marked by this macro.
 * Note that the message will be printed exactly once for every function
 * it is used in order to avoid repeating of the same message. */
#define DAL_LOGGER_NOT_IMPL(log_minor, fmt, ...) \
{ \
	static bool print_not_impl = true; \
\
	if (print_not_impl == true) { \
		print_not_impl = false; \
		dal_logger_write(ctx->logger, LOG_MAJOR_WARNING, \
		log_minor, "DAL_NOT_IMPL: " fmt, ##__VA_ARGS__); \
	} \
}

/******************************************************************************
 * Convenience macros to save on typing.
 *****************************************************************************/

#define DC_ERROR(...) \
	dal_logger_write(dc_ctx->logger, \
		LOG_MAJOR_ERROR, LOG_MINOR_COMPONENT_DC, \
		__VA_ARGS__);

#define DC_SYNC_INFO(...) \
	dal_logger_write(dc_ctx->logger, \
		LOG_MAJOR_SYNC, LOG_MINOR_SYNC_TIMING, \
		__VA_ARGS__);

#endif /* __DAL_LOGGER_INTERFACE_H__ */
