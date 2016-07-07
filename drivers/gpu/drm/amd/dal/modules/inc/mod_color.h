/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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


#ifndef MOD_COLOR_H_
#define MOD_COLOR_H_

#include "dm_services.h"

struct mod_color {
	int dummy;
};

struct color_space_coordinates {
	unsigned int redX;
	unsigned int redY;
	unsigned int greenX;
	unsigned int greenY;
	unsigned int blueX;
	unsigned int blueY;
	unsigned int whiteX;
	unsigned int whiteY;
};

struct gamut_space_coordinates {
	unsigned int redX;
	unsigned int redY;
	unsigned int greenX;
	unsigned int greenY;
	unsigned int blueX;
	unsigned int blueY;
};

struct gamut_space_entry {
	unsigned int index;
	unsigned int redX;
	unsigned int redY;
	unsigned int greenX;
	unsigned int greenY;
	unsigned int blueX;
	unsigned int blueY;

	int a0;
	int a1;
	int a2;
	int a3;
	int gamma;
};

struct white_point_coodinates {
	unsigned int whiteX;
	unsigned int whiteY;
};

struct white_point_coodinates_entry {
	unsigned int index;
	unsigned int whiteX;
	unsigned int whiteY;
};

struct mod_color *mod_color_create(struct dc *dc);
void mod_color_destroy(struct mod_color *mod_color);

bool mod_color_adjust_temperature(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int temperature);

bool mod_color_set_user_enable(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		bool user_enable);

bool mod_color_adjust_source_gamut(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct gamut_space_coordinates inputGamutCoord,
		struct white_point_coodinates inputWhitePointCoord);

bool mod_color_adjust_destination_gamut(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct gamut_space_coordinates inputGamutCoord,
		struct white_point_coodinates inputWhitePointCoord);

#endif /* MOD_COLOR_H_ */
