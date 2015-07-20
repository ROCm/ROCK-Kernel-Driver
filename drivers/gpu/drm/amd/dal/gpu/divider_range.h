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

#ifndef __DAL_DIVIDER_RANGE_H__
#define __DAL_DIVIDER_RANGE_H__

/** ***************************************************************************
 *
 * DividerRange -
 *     Used by Display Engine Clock DCE41 Class
 *     Provides conversion from mmDENTIST_DISPCLK_CNTL.DENTIST_DISPCLK_WDIVIDER
 *     To actual divider.
 *
 *     Note: Resulting divider from CalcDivider is scaled up by
 *     dividerRangeScaleFactor
 *
 *     Display clock frequency is selected by programming the divider id (DID)
 *     DENTIST_DISPCLK_CNTL.DENTIST_DISPCLK_WDIVIDER.  The conversion from DID
 *     setting in the register to the actual divider happens in hardware.
 *
 *          Display Engine Clock (DISPCLK) = VCO Clock/ Divider
 *
 *     This will divide down the VCO frequency to the desired clock.
 *     The range for the dividers is from 2 to 63, but there are different step
 *     sizes in that range as shown by the tables below:
 *
 *          Did[6:0]           Divider     Remarks
 *          0000000b           1.00         N/A
 *          0000001b000111b  N/A	         N/A
 *          0001000b111111b  2.005.75  Step size = 0.25
 *          1000000b011111b  16.001.50 Step size = 0.50
 *          1100000b111111b  32.003.00 Step size = 1.00
 *
 *
 *             DID Divider     DID Divider      DID Divider
 *             -----------     -----------      -----------
 *             08  2.00        40  16.00        60  32.00
 *             09  2.25        41  16.50        61  33.00
 *             0a  2.50        42  17.00        62  34.00
 *             0b  2.75        43  17.50        63  35.00
 *             etc...          etc...           etc...
 *             3c  15.00       5c  30.00        7c  60.00
 *             3d  15.25       5d  30.50        7d  61.00
 *             3e  15.50       5e  31.00        7e  62.00
 *             3f  15.75       5f  31.50        7f  63.00
 *             -----------     -----------      -----------
 *  step size =     0.25            0.50             1.00
 *
 *
 *         __________                                   _________
 *        |          |  VCO            _____  SCLK     |         |
 *        |  Main    |---------+----->| DFS |--------->|  GFX    |  (PPLib)
 *        |  PLL     | 1.6 ~   |       -----           |         |
 *        |          | 3.2 GHz |                        ---------
 *         ----------          |                        _________
 *                             |       _____  DCLK     |         |  (UVD)
 *                             +----->| DFS |--------->|  UVD    |
 *                             |       -----           |         |
 *                             |                        ---------
 *                             |                        _________
 *                             |       _____  DISPCLK  |         |
 *                             +----->| DFS |--------->|         |  (DAL&VBIOS)
 *                             |       -----  400MHz*  |         |
 *                             |                       |   DCE   |
 *                             |       _____  DPREFCLK |         |
 *                             +----->| DFS |--------->|         |
 *                             |       -----  400MHz   |         |
 *                             |                        ---------
 *
 *                                           *DISPCLK may be lower for
 *                                            additional power savings.
 *
 *
 *
 *      Definitions: DID - Divider ID
 *                   DFS - Digital Frequency Synthesizer
 *
 *
 ******************************************************************************/
enum divider_error_types {
	INVALID_DID = 0,
	INVALID_DIVIDER = 1
};

struct divider_range {
	uint32_t div_range_start;
	/* The end of this range of dividers.*/
	uint32_t div_range_end;
	/* The distance between each divider in this range.*/
	uint32_t div_range_step;
	/* The divider id for the lowest divider.*/
	uint32_t did_min;
	/* The divider id for the highest divider.*/
	uint32_t did_max;
};

bool dal_divider_range_construct(
	struct divider_range *div_range,
	uint32_t range_start,
	uint32_t range_step,
	uint32_t did_min,
	uint32_t did_max);

uint32_t dal_divider_range_get_divider(
	struct divider_range *div_range,
	uint32_t ranges_num,
	uint32_t did);
uint32_t dal_divider_range_get_did(
	struct divider_range *div_range,
	uint32_t ranges_num,
	uint32_t divider);


#endif /* __DAL_DIVIDER_RANGE_H__ */
