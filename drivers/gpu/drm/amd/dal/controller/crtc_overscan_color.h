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

#ifndef __DAL_CRTC_OVERSCAN_COLOR_H__
#define __DAL_CRTC_OVERSCAN_COLOR_H__

/* overscan in blank for YUV color space. For RGB, it is zero for black. */
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4CV 0x1f4
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4CV 0x40
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4CV 0x1f4

#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4TV 0x200
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4TV 0x40
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4TV 0x200

/* overscan in blank for YUV color space when in SuperAA crossfire mode */
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_CB_YUV_4SUPERAA 0x1a2
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_Y_YUV_4SUPERAA 0x20
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_CR_YUV_4SUPERAA 0x1a2

/* OVERSCAN COLOR FOR RGB LIMITED RANGE
 * (16~253) 16*4 (Multiple over 256 code leve) =64 (0x40) */
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_B_RGB_LIMITED_RANGE 0x40
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_G_RGB_LIMITED_RANGE 0x40
#define CRTC_OVERSCAN_COLOR_BLACK_COLOR_R_RGB_LIMITED_RANGE 0X40

#endif /* __DAL_CRTC_OVERSCAN_COLOR_H__ */
