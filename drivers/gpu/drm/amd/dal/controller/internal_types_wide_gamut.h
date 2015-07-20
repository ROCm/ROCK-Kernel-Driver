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

#ifndef __DAL_INTERNAL_TYPES_WIDE_GAMUT_H__
#define __DAL_INTERNAL_TYPES_WIDE_GAMUT_H__

enum wide_gamut_color_mode {
	/* 00 - BITS2:0 Bypass */
	WIDE_GAMUT_COLOR_MODE_GRAPHICS_BYPASS,
	/* 01 - hard coded coefficient TV RGB */
	WIDE_GAMUT_COLOR_MODE_GRAPHICS_PREDEFINED,
	/* 02 - hard coded coefficient YCbCr601 */
	/* 03 - hard coded coefficient YCbCr709 */
	/* 04 - programmable OUTPUT CSC coefficient */
	WIDE_GAMUT_COLOR_MODE_GRAPHICS_OUTPUT_CSC,
	/* 05 - programmable shared registers COMM_MATRIXB_TRANS coefficient */
	WIDE_GAMUT_COLOR_MODE_GRAPHICS_MATRIX_B,
	/* 00 - BITS6:4 Bypass */
	WIDE_GAMUT_COLOR_MODE_OVERLAY_BYPASS,
	/* 01 - hard coded coefficient TV RGB */
	WIDE_GAMUT_COLOR_MODE_OVERLAY_PREDEFINED,
	/* 02 - hard coded coefficient YCbCr601 */
	/* 03 - hard coded coefficient YCbCr709 */
	/* 04 - programmable OUTPUT CSC coefficient */
	WIDE_GAMUT_COLOR_MODE_OVERLAY_OUTPUT_CSC,
	/* 05 - programmable shared registers COMM_MATRIXB_TRANS coefficient */
	WIDE_GAMUT_COLOR_MODE_OVERLAY_MATRIX_B
};

enum wide_gamut_degamma_mode {
	/*  00  - BITS1:0 Bypass */
	WIDE_GAMUT_DEGAMMA_MODE_GRAPHICS_BYPASS,
	/*  0x1 - PWL gamma ROM A */
	WIDE_GAMUT_DEGAMMA_MODE_GRAPHICS_PWL_ROM_A,
	/*  0x2 - PWL gamma ROM B */
	WIDE_GAMUT_DEGAMMA_MODE_GRAPHICS_PWL_ROM_B,
	/*  00  - BITS5:4 Bypass */
	WIDE_GAMUT_DEGAMMA_MODE_OVL_BYPASS,
	/*  0x1 - PWL gamma ROM A */
	WIDE_GAMUT_DEGAMMA_MODE_OVL_PWL_ROM_A,
	/*  0x2 - PWL gamma ROM B */
	WIDE_GAMUT_DEGAMMA_MODE_OVL_PWL_ROM_B,
};

enum wide_gamut_regamma_mode {
	/*  0x0  - BITS2:0 Bypass */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_BYPASS,
	/*  0x1  - Fixed curve sRGB 2.4 */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_SRGB24,
	/*  0x2  - Fixed curve xvYCC 2.22 */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_XYYCC22,
	/*  0x3  - Programmable control A */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_A,
	/*  0x4  - Programmable control B */
	WIDE_GAMUT_REGAMMA_MODE_GRAPHICS_MATRIX_B,
	/*  0x0  - BITS6:4 Bypass */
	WIDE_GAMUT_REGAMMA_MODE_OVL_BYPASS,
	/*  0x1  - Fixed curve sRGB 2.4 */
	WIDE_GAMUT_REGAMMA_MODE_OVL_SRGB24,
	/*  0x2  - Fixed curve xvYCC 2.22 */
	WIDE_GAMUT_REGAMMA_MODE_OVL_XYYCC22,
	/*  0x3  - Programmable control A */
	WIDE_GAMUT_REGAMMA_MODE_OVL_MATRIX_A,
	/*  0x4  - Programmable control B */
	WIDE_GAMUT_REGAMMA_MODE_OVL_MATRIX_B
};

#endif /* __DAL_INTERNAL_TYPES_WIDE_GAMUT_H__ */
