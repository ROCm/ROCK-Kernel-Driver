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

#include "dal_services.h"

#include "include/display_path_interface.h"
#include "display_path.h"

struct link_service *dal_display_path_get_link_query_interface(
	const struct display_path *path, uint32_t idx)
{

	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		return path->stream_contexts[idx].link_query_interface;
	return NULL;
}
void dal_display_path_set_link_query_interface(struct display_path *path,
	uint32_t idx, struct link_service *link)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		path->stream_contexts[idx].link_query_interface = link;
}
struct link_service *dal_display_path_get_link_config_interface(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		return path->stream_contexts[idx].link_config_interface;
	return NULL;
}
struct link_service *dal_display_path_get_link_service_interface(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		return path->stream_contexts[idx].link_query_interface;
	return NULL;
}

struct encoder *dal_display_path_get_upstream_encoder(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links
		&& path->stream_contexts[idx].state.LINK)
		return path->stream_contexts[idx].encoder;
	return NULL;
}
struct encoder *dal_display_path_get_upstream_object(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		return path->stream_contexts[idx].encoder;
	return NULL;
}
struct encoder *dal_display_path_get_downstream_encoder(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx + 1 < path->number_of_links
		&& path->stream_contexts[idx].state.LINK)
		return path->stream_contexts[idx + 1].encoder;
	return NULL;
}
struct encoder *dal_display_path_get_downstream_object(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx + 1 < path->number_of_links)
		return path->stream_contexts[idx + 1].encoder;
	return NULL;

}

struct audio *dal_display_path_get_audio(const struct display_path *path,
	uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links
		&& path->stream_contexts[idx].state.AUDIO)
		return path->stream_contexts[idx].audio;
	return NULL;
}
void dal_display_path_set_audio(struct display_path *path, uint32_t idx,
	struct audio *a)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		path->stream_contexts[idx].audio = a;
}
struct audio *dal_display_path_get_audio_object(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		return path->stream_contexts[idx].audio;
	return NULL;
}
void dal_display_path_set_audio_active_state(struct display_path *path,
	uint32_t idx, bool state)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		path->stream_contexts[idx].state.AUDIO = state;
}

enum engine_id dal_display_path_get_stream_engine(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		return path->stream_contexts[idx].engine;
	return ENGINE_ID_UNKNOWN;
}
void dal_display_path_set_stream_engine(struct display_path *path, uint32_t idx,
	enum engine_id id)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		path->stream_contexts[idx].engine = id;
}

bool dal_display_path_is_link_active(const struct display_path *path,
	uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		return path->stream_contexts[idx].state.LINK;
	return false;

}
void dal_display_path_set_link_active_state(struct display_path *path,
	uint32_t idx, bool state)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		path->stream_contexts[idx].state.LINK = state;
}

enum signal_type dal_display_path_get_config_signal(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		return path->stream_contexts[idx].output_config_signal;
	return SIGNAL_TYPE_NONE;

}
enum signal_type dal_display_path_get_query_signal(
	const struct display_path *path, uint32_t idx)
{
	idx = (idx == SINK_LINK_INDEX) ? path->number_of_links - 1 : idx;
	if (idx < path->number_of_links)
		return path->stream_contexts[idx].output_query_signal;
	return SIGNAL_TYPE_NONE;
}

