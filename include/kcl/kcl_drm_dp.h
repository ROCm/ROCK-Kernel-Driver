/*
 * Copyright © 2008 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
#ifndef _KCL_DRM_DP_H
#define _KCL_DRM_DP_H

#ifndef DP_SINK_VIDEO_FALLBACK_FORMATS
#define DP_SINK_VIDEO_FALLBACK_FORMATS			0x020
#endif
#ifndef DP_FEC_CAPABILITY_1
#define DP_FEC_CAPABILITY_1				0x091
#endif

#ifndef DP_DSC_CONFIGURATION
#define DP_DSC_CONFIGURATION				0x161
#endif
#ifndef DP_PHY_SQUARE_PATTERN
#define DP_PHY_SQUARE_PATTERN				0x249
#endif

#ifndef DP_DSC_MAX_SLICE_COUNT_AND_AGGREGATION_0
#define DP_DSC_MAX_SLICE_COUNT_AND_AGGREGATION_0	0x2270
#endif
#ifndef DP_DSC_DECODER_0_MAXIMUM_SLICE_COUNT_MASK
#define DP_DSC_DECODER_0_MAXIMUM_SLICE_COUNT_MASK	(1 << 0)
#endif
#ifndef DP_DSC_DECODER_0_AGGREGATION_SUPPORT_MASK
#define DP_DSC_DECODER_0_AGGREGATION_SUPPORT_MASK	(0b111 << 1)
#endif
#ifndef DP_DSC_DECODER_0_AGGREGATION_SUPPORT_SHIFT
#define DP_DSC_DECODER_0_AGGREGATION_SUPPORT_SHIFT	1
#endif
#ifndef DP_DSC_DECODER_COUNT_MASK
#define DP_DSC_DECODER_COUNT_MASK			(0b111 << 5)
#endif
#ifndef DP_DSC_DECODER_COUNT_SHIFT
#define DP_DSC_DECODER_COUNT_SHIFT			5
#endif
#ifndef DP_MAIN_LINK_CHANNEL_CODING_SET
#define DP_MAIN_LINK_CHANNEL_CODING_SET			0x108
#endif
#ifndef DP_MAIN_LINK_CHANNEL_CODING_PHY_REPEATER
#define DP_MAIN_LINK_CHANNEL_CODING_PHY_REPEATER	0xF0006
#endif
#ifndef DP_INTRA_HOP_AUX_REPLY_INDICATION
#define DP_INTRA_HOP_AUX_REPLY_INDICATION		(1 << 3)
#endif

#endif