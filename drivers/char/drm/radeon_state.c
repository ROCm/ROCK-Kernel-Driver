/* radeon_state.c -- State support for Radeon -*- linux-c -*-
 *
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 *    Kevin E. Martin <martin@valinux.com>
 */

#define __NO_VERSION__
#include "radeon.h"
#include "drmP.h"
#include "drm.h"
#include "radeon_drm.h"
#include "radeon_drv.h"
#include <linux/delay.h>


/* ================================================================
 * CP hardware state programming functions
 */

static inline void radeon_emit_clip_rect( drm_radeon_private_t *dev_priv,
					  drm_clip_rect_t *box )
{
	RING_LOCALS;

	DRM_DEBUG( "   box:  x1=%d y1=%d  x2=%d y2=%d\n",
		   box->x1, box->y1, box->x2, box->y2 );

	BEGIN_RING( 3 );
	OUT_RING( CP_PACKET3( RADEON_CNTL_SET_SCISSORS, 1 ));
	OUT_RING( (box->y1 << 16) | box->x1 );
	OUT_RING( ((box->y2 - 1) << 16) | (box->x2 - 1) );
	ADVANCE_RING();
}

/* Emit 1.1 state
 */
static void radeon_emit_state( drm_radeon_private_t *dev_priv,
			       drm_radeon_context_regs_t *ctx,
			       drm_radeon_texture_regs_t *tex,
			       unsigned int dirty )
{
	RING_LOCALS;
	DRM_DEBUG( "%s: dirty=0x%08x\n", __FUNCTION__, dirty );

	if ( dirty & RADEON_UPLOAD_CONTEXT ) {
		BEGIN_RING( 14 );
		OUT_RING( CP_PACKET0( RADEON_PP_MISC, 6 ) );
		OUT_RING( ctx->pp_misc );
		OUT_RING( ctx->pp_fog_color );
		OUT_RING( ctx->re_solid_color );
		OUT_RING( ctx->rb3d_blendcntl );
		OUT_RING( ctx->rb3d_depthoffset );
		OUT_RING( ctx->rb3d_depthpitch );
		OUT_RING( ctx->rb3d_zstencilcntl );
		OUT_RING( CP_PACKET0( RADEON_PP_CNTL, 2 ) );
		OUT_RING( ctx->pp_cntl );
		OUT_RING( ctx->rb3d_cntl );
		OUT_RING( ctx->rb3d_coloroffset );
		OUT_RING( CP_PACKET0( RADEON_RB3D_COLORPITCH, 0 ) );
		OUT_RING( ctx->rb3d_colorpitch );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_VERTFMT ) {
		BEGIN_RING( 2 );
		OUT_RING( CP_PACKET0( RADEON_SE_COORD_FMT, 0 ) );
		OUT_RING( ctx->se_coord_fmt );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_LINE ) {
		BEGIN_RING( 5 );
		OUT_RING( CP_PACKET0( RADEON_RE_LINE_PATTERN, 1 ) );
		OUT_RING( ctx->re_line_pattern );
		OUT_RING( ctx->re_line_state );
		OUT_RING( CP_PACKET0( RADEON_SE_LINE_WIDTH, 0 ) );
		OUT_RING( ctx->se_line_width );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_BUMPMAP ) {
		BEGIN_RING( 5 );
		OUT_RING( CP_PACKET0( RADEON_PP_LUM_MATRIX, 0 ) );
		OUT_RING( ctx->pp_lum_matrix );
		OUT_RING( CP_PACKET0( RADEON_PP_ROT_MATRIX_0, 1 ) );
		OUT_RING( ctx->pp_rot_matrix_0 );
		OUT_RING( ctx->pp_rot_matrix_1 );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_MASKS ) {
		BEGIN_RING( 4 );
		OUT_RING( CP_PACKET0( RADEON_RB3D_STENCILREFMASK, 2 ) );
		OUT_RING( ctx->rb3d_stencilrefmask );
		OUT_RING( ctx->rb3d_ropcntl );
		OUT_RING( ctx->rb3d_planemask );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_VIEWPORT ) {
		BEGIN_RING( 7 );
		OUT_RING( CP_PACKET0( RADEON_SE_VPORT_XSCALE, 5 ) );
		OUT_RING( ctx->se_vport_xscale );
		OUT_RING( ctx->se_vport_xoffset );
		OUT_RING( ctx->se_vport_yscale );
		OUT_RING( ctx->se_vport_yoffset );
		OUT_RING( ctx->se_vport_zscale );
		OUT_RING( ctx->se_vport_zoffset );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_SETUP ) {
		BEGIN_RING( 4 );
		OUT_RING( CP_PACKET0( RADEON_SE_CNTL, 0 ) );
		OUT_RING( ctx->se_cntl );
		OUT_RING( CP_PACKET0( RADEON_SE_CNTL_STATUS, 0 ) );
		OUT_RING( ctx->se_cntl_status );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_MISC ) {
		BEGIN_RING( 2 );
		OUT_RING( CP_PACKET0( RADEON_RE_MISC, 0 ) );
		OUT_RING( ctx->re_misc );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_TEX0 ) {
		BEGIN_RING( 9 );
		OUT_RING( CP_PACKET0( RADEON_PP_TXFILTER_0, 5 ) );
		OUT_RING( tex[0].pp_txfilter );
		OUT_RING( tex[0].pp_txformat );
		OUT_RING( tex[0].pp_txoffset );
		OUT_RING( tex[0].pp_txcblend );
		OUT_RING( tex[0].pp_txablend );
		OUT_RING( tex[0].pp_tfactor );
		OUT_RING( CP_PACKET0( RADEON_PP_BORDER_COLOR_0, 0 ) );
		OUT_RING( tex[0].pp_border_color );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_TEX1 ) {
		BEGIN_RING( 9 );
		OUT_RING( CP_PACKET0( RADEON_PP_TXFILTER_1, 5 ) );
		OUT_RING( tex[1].pp_txfilter );
		OUT_RING( tex[1].pp_txformat );
		OUT_RING( tex[1].pp_txoffset );
		OUT_RING( tex[1].pp_txcblend );
		OUT_RING( tex[1].pp_txablend );
		OUT_RING( tex[1].pp_tfactor );
		OUT_RING( CP_PACKET0( RADEON_PP_BORDER_COLOR_1, 0 ) );
		OUT_RING( tex[1].pp_border_color );
		ADVANCE_RING();
	}

	if ( dirty & RADEON_UPLOAD_TEX2 ) {
		BEGIN_RING( 9 );
		OUT_RING( CP_PACKET0( RADEON_PP_TXFILTER_2, 5 ) );
		OUT_RING( tex[2].pp_txfilter );
		OUT_RING( tex[2].pp_txformat );
		OUT_RING( tex[2].pp_txoffset );
		OUT_RING( tex[2].pp_txcblend );
		OUT_RING( tex[2].pp_txablend );
		OUT_RING( tex[2].pp_tfactor );
		OUT_RING( CP_PACKET0( RADEON_PP_BORDER_COLOR_2, 0 ) );
		OUT_RING( tex[2].pp_border_color );
		ADVANCE_RING();
	}
}

/* Emit 1.2 state
 */
static void radeon_emit_state2( drm_radeon_private_t *dev_priv,
				drm_radeon_state_t *state )
{
	RING_LOCALS;

	if (state->dirty & RADEON_UPLOAD_ZBIAS) {
		BEGIN_RING( 3 );
		OUT_RING( CP_PACKET0( RADEON_SE_ZBIAS_FACTOR, 1 ) );
		OUT_RING( state->context2.se_zbias_factor ); 
		OUT_RING( state->context2.se_zbias_constant ); 
		ADVANCE_RING();
	}

	radeon_emit_state( dev_priv, &state->context, 
			   state->tex, state->dirty );
}

/* New (1.3) state mechanism.  3 commands (packet, scalar, vector) in
 * 1.3 cmdbuffers allow all previous state to be updated as well as
 * the tcl scalar and vector areas.  
 */
static struct { 
	int start; 
	int len; 
	const char *name;
} packet[RADEON_MAX_STATE_PACKETS] = {
	{ RADEON_PP_MISC,7,"RADEON_PP_MISC" },
	{ RADEON_PP_CNTL,3,"RADEON_PP_CNTL" },
	{ RADEON_RB3D_COLORPITCH,1,"RADEON_RB3D_COLORPITCH" },
	{ RADEON_RE_LINE_PATTERN,2,"RADEON_RE_LINE_PATTERN" },
	{ RADEON_SE_LINE_WIDTH,1,"RADEON_SE_LINE_WIDTH" },
	{ RADEON_PP_LUM_MATRIX,1,"RADEON_PP_LUM_MATRIX" },
	{ RADEON_PP_ROT_MATRIX_0,2,"RADEON_PP_ROT_MATRIX_0" },
	{ RADEON_RB3D_STENCILREFMASK,3,"RADEON_RB3D_STENCILREFMASK" },
	{ RADEON_SE_VPORT_XSCALE,6,"RADEON_SE_VPORT_XSCALE" },
	{ RADEON_SE_CNTL,2,"RADEON_SE_CNTL" },
	{ RADEON_SE_CNTL_STATUS,1,"RADEON_SE_CNTL_STATUS" },
	{ RADEON_RE_MISC,1,"RADEON_RE_MISC" },
	{ RADEON_PP_TXFILTER_0,6,"RADEON_PP_TXFILTER_0" },
	{ RADEON_PP_BORDER_COLOR_0,1,"RADEON_PP_BORDER_COLOR_0" },
	{ RADEON_PP_TXFILTER_1,6,"RADEON_PP_TXFILTER_1" },
	{ RADEON_PP_BORDER_COLOR_1,1,"RADEON_PP_BORDER_COLOR_1" },
	{ RADEON_PP_TXFILTER_2,6,"RADEON_PP_TXFILTER_2" },
	{ RADEON_PP_BORDER_COLOR_2,1,"RADEON_PP_BORDER_COLOR_2" },
	{ RADEON_SE_ZBIAS_FACTOR,2,"RADEON_SE_ZBIAS_FACTOR" },
	{ RADEON_SE_TCL_OUTPUT_VTX_FMT,11,"RADEON_SE_TCL_OUTPUT_VTX_FMT" },
	{ RADEON_SE_TCL_MATERIAL_EMMISSIVE_RED,17,"RADEON_SE_TCL_MATERIAL_EMMISSIVE_RED" },
};










#if RADEON_PERFORMANCE_BOXES
/* ================================================================
 * Performance monitoring functions
 */

static void radeon_clear_box( drm_radeon_private_t *dev_priv,
			      int x, int y, int w, int h,
			      int r, int g, int b )
{
	u32 pitch, offset;
	u32 color;
	RING_LOCALS;

	switch ( dev_priv->color_fmt ) {
	case RADEON_COLOR_FORMAT_RGB565:
		color = (((r & 0xf8) << 8) |
			 ((g & 0xfc) << 3) |
			 ((b & 0xf8) >> 3));
		break;
	case RADEON_COLOR_FORMAT_ARGB8888:
	default:
		color = (((0xff) << 24) | (r << 16) | (g <<  8) | b);
		break;
	}

	offset = dev_priv->back_offset;
	pitch = dev_priv->back_pitch >> 3;

	BEGIN_RING( 6 );

	OUT_RING( CP_PACKET3( RADEON_CNTL_PAINT_MULTI, 4 ) );
	OUT_RING( RADEON_GMC_DST_PITCH_OFFSET_CNTL |
		  RADEON_GMC_BRUSH_SOLID_COLOR |
		  (dev_priv->color_fmt << 8) |
		  RADEON_GMC_SRC_DATATYPE_COLOR |
		  RADEON_ROP3_P |
		  RADEON_GMC_CLR_CMP_CNTL_DIS );

	OUT_RING( (pitch << 22) | (offset >> 5) );
	OUT_RING( color );

	OUT_RING( (x << 16) | y );
	OUT_RING( (w << 16) | h );

	ADVANCE_RING();
}

static void radeon_cp_performance_boxes( drm_radeon_private_t *dev_priv )
{
	if ( atomic_read( &dev_priv->idle_count ) == 0 ) {
		radeon_clear_box( dev_priv, 64, 4, 8, 8, 0, 255, 0 );
	} else {
		atomic_set( &dev_priv->idle_count, 0 );
	}
}

#endif


/* ================================================================
 * CP command dispatch functions
 */

static void radeon_cp_dispatch_clear( drm_device_t *dev,
				      drm_radeon_clear_t *clear,
				      drm_radeon_clear_rect_t *depth_boxes )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_depth_clear_t *depth_clear = &dev_priv->depth_clear;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	unsigned int flags = clear->flags;
	u32 rb3d_cntl = 0, rb3d_stencilrefmask= 0;
	int i;
	RING_LOCALS;
	DRM_DEBUG( __FUNCTION__": flags = 0x%x\n", flags );

	if ( dev_priv->page_flipping && dev_priv->current_page == 1 ) {
		unsigned int tmp = flags;

		flags &= ~(RADEON_FRONT | RADEON_BACK);
		if ( tmp & RADEON_FRONT ) flags |= RADEON_BACK;
		if ( tmp & RADEON_BACK )  flags |= RADEON_FRONT;
	}

	/* We have to clear the depth and/or stencil buffers by
	 * rendering a quad into just those buffers.  Thus, we have to
	 * make sure the 3D engine is configured correctly.
	 */
	if ( flags & (RADEON_DEPTH | RADEON_STENCIL) ) {
		rb3d_cntl = depth_clear->rb3d_cntl;

		if ( flags & RADEON_DEPTH ) {
			rb3d_cntl |=  RADEON_Z_ENABLE;
		} else {
			rb3d_cntl &= ~RADEON_Z_ENABLE;
		}

		if ( flags & RADEON_STENCIL ) {
			rb3d_cntl |=  RADEON_STENCIL_ENABLE;
			rb3d_stencilrefmask = clear->depth_mask; /* misnamed field */
		} else {
			rb3d_cntl &= ~RADEON_STENCIL_ENABLE;
			rb3d_stencilrefmask = 0x00000000;
		}
	}

	for ( i = 0 ; i < nbox ; i++ ) {
		int x = pbox[i].x1;
		int y = pbox[i].y1;
		int w = pbox[i].x2 - x;
		int h = pbox[i].y2 - y;

		DRM_DEBUG( "dispatch clear %d,%d-%d,%d flags 0x%x\n",
			   x, y, w, h, flags );

		if ( flags & (RADEON_FRONT | RADEON_BACK) ) {
			BEGIN_RING( 4 );

			/* Ensure the 3D stream is idle before doing a
			 * 2D fill to clear the front or back buffer.
			 */
			RADEON_WAIT_UNTIL_3D_IDLE();

			OUT_RING( CP_PACKET0( RADEON_DP_WRITE_MASK, 0 ) );
			OUT_RING( clear->color_mask );

			ADVANCE_RING();

			/* Make sure we restore the 3D state next time.
			 */
			dev_priv->sarea_priv->ctx_owner = 0;
		}

		if ( flags & RADEON_FRONT ) {
			BEGIN_RING( 6 );

			OUT_RING( CP_PACKET3( RADEON_CNTL_PAINT_MULTI, 4 ) );
			OUT_RING( RADEON_GMC_DST_PITCH_OFFSET_CNTL |
				  RADEON_GMC_BRUSH_SOLID_COLOR |
				  (dev_priv->color_fmt << 8) |
				  RADEON_GMC_SRC_DATATYPE_COLOR |
				  RADEON_ROP3_P |
				  RADEON_GMC_CLR_CMP_CNTL_DIS );

			OUT_RING( dev_priv->front_pitch_offset );
			OUT_RING( clear->clear_color );

			OUT_RING( (x << 16) | y );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}

		if ( flags & RADEON_BACK ) {
			BEGIN_RING( 6 );

			OUT_RING( CP_PACKET3( RADEON_CNTL_PAINT_MULTI, 4 ) );
			OUT_RING( RADEON_GMC_DST_PITCH_OFFSET_CNTL |
				  RADEON_GMC_BRUSH_SOLID_COLOR |
				  (dev_priv->color_fmt << 8) |
				  RADEON_GMC_SRC_DATATYPE_COLOR |
				  RADEON_ROP3_P |
				  RADEON_GMC_CLR_CMP_CNTL_DIS );

			OUT_RING( dev_priv->back_pitch_offset );
			OUT_RING( clear->clear_color );

			OUT_RING( (x << 16) | y );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}

		if ( flags & (RADEON_DEPTH | RADEON_STENCIL) ) {

			radeon_emit_clip_rect( dev_priv,
					       &sarea_priv->boxes[i] );

			BEGIN_RING( 28 );

			RADEON_WAIT_UNTIL_2D_IDLE();

			OUT_RING( CP_PACKET0( RADEON_PP_CNTL, 1 ) );
			OUT_RING( 0x00000000 );
			OUT_RING( rb3d_cntl );

			OUT_RING_REG( RADEON_RB3D_ZSTENCILCNTL,
				      depth_clear->rb3d_zstencilcntl );
			OUT_RING_REG( RADEON_RB3D_STENCILREFMASK,
				      rb3d_stencilrefmask );
			OUT_RING_REG( RADEON_RB3D_PLANEMASK,
				      0x00000000 );
			OUT_RING_REG( RADEON_SE_CNTL,
				      depth_clear->se_cntl );

			/* Radeon 7500 doesn't like vertices without
			 * color.
			 */
			OUT_RING( CP_PACKET3( RADEON_3D_DRAW_IMMD, 13 ) );
			OUT_RING( RADEON_VTX_Z_PRESENT |
				  RADEON_VTX_PKCOLOR_PRESENT);
			OUT_RING( (RADEON_PRIM_TYPE_RECT_LIST |
				   RADEON_PRIM_WALK_RING |
				   RADEON_MAOS_ENABLE |
				   RADEON_VTX_FMT_RADEON_MODE |
				   (3 << RADEON_NUM_VERTICES_SHIFT)) );

			OUT_RING( depth_boxes[i].ui[CLEAR_X1] );
			OUT_RING( depth_boxes[i].ui[CLEAR_Y1] );
			OUT_RING( depth_boxes[i].ui[CLEAR_DEPTH] );
			OUT_RING( 0x0 );

			OUT_RING( depth_boxes[i].ui[CLEAR_X1] );
			OUT_RING( depth_boxes[i].ui[CLEAR_Y2] );
			OUT_RING( depth_boxes[i].ui[CLEAR_DEPTH] );
			OUT_RING( 0x0 );

			OUT_RING( depth_boxes[i].ui[CLEAR_X2] );
			OUT_RING( depth_boxes[i].ui[CLEAR_Y2] );
			OUT_RING( depth_boxes[i].ui[CLEAR_DEPTH] );
			OUT_RING( 0x0 );

			ADVANCE_RING();

			/* Make sure we restore the 3D state next time.
			 */
			dev_priv->sarea_priv->ctx_owner = 0;
		}
	}

	/* Increment the clear counter.  The client-side 3D driver must
	 * wait on this value before performing the clear ioctl.  We
	 * need this because the card's so damned fast...
	 */
	dev_priv->sarea_priv->last_clear++;

	BEGIN_RING( 4 );

	RADEON_CLEAR_AGE( dev_priv->sarea_priv->last_clear );
	RADEON_WAIT_UNTIL_IDLE();

	ADVANCE_RING();
}

static void radeon_cp_dispatch_swap( drm_device_t *dev )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	int i;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

#if RADEON_PERFORMANCE_BOXES
	/* Do some trivial performance monitoring...
	 */
	radeon_cp_performance_boxes( dev_priv );
#endif

	/* Wait for the 3D stream to idle before dispatching the bitblt.
	 * This will prevent data corruption between the two streams.
	 */
	BEGIN_RING( 2 );

	RADEON_WAIT_UNTIL_3D_IDLE();

	ADVANCE_RING();

	for ( i = 0 ; i < nbox ; i++ ) {
		int x = pbox[i].x1;
		int y = pbox[i].y1;
		int w = pbox[i].x2 - x;
		int h = pbox[i].y2 - y;

		DRM_DEBUG( "dispatch swap %d,%d-%d,%d\n",
			   x, y, w, h );

		BEGIN_RING( 7 );

		OUT_RING( CP_PACKET3( RADEON_CNTL_BITBLT_MULTI, 5 ) );
		OUT_RING( RADEON_GMC_SRC_PITCH_OFFSET_CNTL |
			  RADEON_GMC_DST_PITCH_OFFSET_CNTL |
			  RADEON_GMC_BRUSH_NONE |
			  (dev_priv->color_fmt << 8) |
			  RADEON_GMC_SRC_DATATYPE_COLOR |
			  RADEON_ROP3_S |
			  RADEON_DP_SRC_SOURCE_MEMORY |
			  RADEON_GMC_CLR_CMP_CNTL_DIS |
			  RADEON_GMC_WR_MSK_DIS );
		
		/* Make this work even if front & back are flipped:
		 */
		if (dev_priv->current_page == 0) {
			OUT_RING( dev_priv->back_pitch_offset );
			OUT_RING( dev_priv->front_pitch_offset );
		} 
		else {
			OUT_RING( dev_priv->front_pitch_offset );
			OUT_RING( dev_priv->back_pitch_offset );
		}

		OUT_RING( (x << 16) | y );
		OUT_RING( (x << 16) | y );
		OUT_RING( (w << 16) | h );

		ADVANCE_RING();
	}

	/* Increment the frame counter.  The client-side 3D driver must
	 * throttle the framerate by waiting for this value before
	 * performing the swapbuffer ioctl.
	 */
	dev_priv->sarea_priv->last_frame++;

	BEGIN_RING( 4 );

	RADEON_FRAME_AGE( dev_priv->sarea_priv->last_frame );
	RADEON_WAIT_UNTIL_2D_IDLE();

	ADVANCE_RING();
}

static void radeon_cp_dispatch_flip( drm_device_t *dev )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;
	DRM_DEBUG( "%s: page=%d\n", __FUNCTION__, dev_priv->current_page );

#if RADEON_PERFORMANCE_BOXES
	/* Do some trivial performance monitoring...
	 */
	radeon_cp_performance_boxes( dev_priv );
#endif

	BEGIN_RING( 4 );

	RADEON_WAIT_UNTIL_3D_IDLE();
/*
	RADEON_WAIT_UNTIL_PAGE_FLIPPED();
*/
	OUT_RING( CP_PACKET0( RADEON_CRTC_OFFSET, 0 ) );

	if ( dev_priv->current_page == 0 ) {
		OUT_RING( dev_priv->back_offset );
		dev_priv->current_page = 1;
	} else {
		OUT_RING( dev_priv->front_offset );
		dev_priv->current_page = 0;
	}

	ADVANCE_RING();

	/* Increment the frame counter.  The client-side 3D driver must
	 * throttle the framerate by waiting for this value before
	 * performing the swapbuffer ioctl.
	 */
	dev_priv->sarea_priv->last_frame++;
	dev_priv->sarea_priv->pfCurrentPage = dev_priv->current_page;

	BEGIN_RING( 2 );

	RADEON_FRAME_AGE( dev_priv->sarea_priv->last_frame );

	ADVANCE_RING();
}

static int bad_prim_vertex_nr( int primitive, int nr )
{
	switch (primitive & RADEON_PRIM_TYPE_MASK) {
	case RADEON_PRIM_TYPE_NONE:
	case RADEON_PRIM_TYPE_POINT:
		return nr < 1;
	case RADEON_PRIM_TYPE_LINE:
		return (nr & 1) || nr == 0;
	case RADEON_PRIM_TYPE_LINE_STRIP:
		return nr < 2;
	case RADEON_PRIM_TYPE_TRI_LIST:
	case RADEON_PRIM_TYPE_3VRT_POINT_LIST:
	case RADEON_PRIM_TYPE_3VRT_LINE_LIST:
	case RADEON_PRIM_TYPE_RECT_LIST:
		return nr % 3 || nr == 0;
	case RADEON_PRIM_TYPE_TRI_FAN:
	case RADEON_PRIM_TYPE_TRI_STRIP:
		return nr < 3;
	default:
		return 1;
	}	
}



typedef struct {
	unsigned int start;
	unsigned int finish;
	unsigned int prim;
	unsigned int numverts;
	unsigned int offset;   
        unsigned int vc_format;
} drm_radeon_tcl_prim_t;

static void radeon_cp_dispatch_vertex( drm_device_t *dev,
				       drm_buf_t *buf,
				       drm_radeon_tcl_prim_t *prim,
				       drm_clip_rect_t *boxes,
				       int nbox )

{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_clip_rect_t box;
	int offset = dev_priv->agp_buffers_offset + buf->offset + prim->start;
	int numverts = (int)prim->numverts;
	int i = 0;
	RING_LOCALS;

	DRM_DEBUG("%s: hwprim 0x%x vfmt 0x%x %d..%d %d verts\n",
		  __FUNCTION__,
		  prim->prim,
		  prim->vc_format,
		  prim->start,
		  prim->finish,
		  prim->numverts);

	if (bad_prim_vertex_nr( prim->prim, prim->numverts )) {
		DRM_ERROR( "bad prim %x numverts %d\n", 
			   prim->prim, prim->numverts );
		return;
	}

	do {
		/* Emit the next cliprect */
		if ( i < nbox ) {
			if (__copy_from_user( &box, &boxes[i], sizeof(box) ))
				return;

			radeon_emit_clip_rect( dev_priv, &box );
		}

		/* Emit the vertex buffer rendering commands */
		BEGIN_RING( 5 );

		OUT_RING( CP_PACKET3( RADEON_3D_RNDR_GEN_INDX_PRIM, 3 ) );
		OUT_RING( offset );
		OUT_RING( numverts );
		OUT_RING( prim->vc_format );
		OUT_RING( prim->prim | RADEON_PRIM_WALK_LIST |
			  RADEON_COLOR_ORDER_RGBA |
			  RADEON_VTX_FMT_RADEON_MODE |
			  (numverts << RADEON_NUM_VERTICES_SHIFT) );

		ADVANCE_RING();

		i++;
	} while ( i < nbox );
}



static void radeon_cp_discard_buffer( drm_device_t *dev, drm_buf_t *buf )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv = buf->dev_private;
	RING_LOCALS;

	buf_priv->age = ++dev_priv->sarea_priv->last_dispatch;

	/* Emit the vertex buffer age */
	BEGIN_RING( 2 );
	RADEON_DISPATCH_AGE( buf_priv->age );
	ADVANCE_RING();

	buf->pending = 1;
	buf->used = 0;
}

static void radeon_cp_dispatch_indirect( drm_device_t *dev,
					 drm_buf_t *buf,
					 int start, int end )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;
	DRM_DEBUG( "indirect: buf=%d s=0x%x e=0x%x\n",
		   buf->idx, start, end );

	if ( start != end ) {
		int offset = (dev_priv->agp_buffers_offset
			      + buf->offset + start);
		int dwords = (end - start + 3) / sizeof(u32);

		/* Indirect buffer data must be an even number of
		 * dwords, so if we've been given an odd number we must
		 * pad the data with a Type-2 CP packet.
		 */
		if ( dwords & 1 ) {
			u32 *data = (u32 *)
				((char *)dev_priv->buffers->handle
				 + buf->offset + start);
			data[dwords++] = RADEON_CP_PACKET2;
		}

		/* Fire off the indirect buffer */
		BEGIN_RING( 3 );

		OUT_RING( CP_PACKET0( RADEON_CP_IB_BASE, 1 ) );
		OUT_RING( offset );
		OUT_RING( dwords );

		ADVANCE_RING();
	}
}


static void radeon_cp_dispatch_indices( drm_device_t *dev,
					drm_buf_t *elt_buf,
					drm_radeon_tcl_prim_t *prim, 
					drm_clip_rect_t *boxes,
					int nbox )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_clip_rect_t box;
	int offset = dev_priv->agp_buffers_offset + prim->offset;
	u32 *data;
	int dwords;
	int i = 0;
	int start = prim->start + RADEON_INDEX_PRIM_OFFSET;
	int count = (prim->finish - start) / sizeof(u16);

	DRM_DEBUG("%s: hwprim 0x%x vfmt 0x%x %d..%d offset: %x nr %d\n",
		  __FUNCTION__,
		  prim->prim,
		  prim->vc_format,
		  prim->start,
		  prim->finish,
		  prim->offset,
		  prim->numverts);

	if (bad_prim_vertex_nr( prim->prim, count )) {
		DRM_ERROR( "bad prim %x count %d\n", 
			   prim->prim, count );
		return;
	}


	if ( start >= prim->finish ||
	     (prim->start & 0x7) ) {
		DRM_ERROR( "buffer prim %d\n", prim->prim );
		return;
	}

	dwords = (prim->finish - prim->start + 3) / sizeof(u32);

	data = (u32 *)((char *)dev_priv->buffers->handle +
		       elt_buf->offset + prim->start);

	data[0] = CP_PACKET3( RADEON_3D_RNDR_GEN_INDX_PRIM, dwords-2 );
	data[1] = offset;
	data[2] = prim->numverts;
	data[3] = prim->vc_format;
	data[4] = (prim->prim |
		   RADEON_PRIM_WALK_IND |
		   RADEON_COLOR_ORDER_RGBA |
		   RADEON_VTX_FMT_RADEON_MODE |
		   (count << RADEON_NUM_VERTICES_SHIFT) );

	do {
		if ( i < nbox ) {
			if (__copy_from_user( &box, &boxes[i], sizeof(box) ))
				return;
			
			radeon_emit_clip_rect( dev_priv, &box );
		}

		radeon_cp_dispatch_indirect( dev, elt_buf,
					     prim->start,
					     prim->finish );

		i++;
	} while ( i < nbox );

}

#define RADEON_MAX_TEXTURE_SIZE (RADEON_BUFFER_SIZE - 8 * sizeof(u32))

static int radeon_cp_dispatch_texture( drm_device_t *dev,
				       drm_radeon_texture_t *tex,
				       drm_radeon_tex_image_t *image )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_buf_t *buf;
	u32 format;
	u32 *buffer;
	u8 *data;
	int size, dwords, tex_width, blit_width;
	u32 y, height;
	int ret = 0, i;
	RING_LOCALS;

	/* FIXME: Be smarter about this...
	 */
	buf = radeon_freelist_get( dev );
	if ( !buf ) return -EAGAIN;

	DRM_DEBUG( "tex: ofs=0x%x p=%d f=%d x=%hd y=%hd w=%hd h=%hd\n",
		   tex->offset >> 10, tex->pitch, tex->format,
		   image->x, image->y, image->width, image->height );

	/* The compiler won't optimize away a division by a variable,
	 * even if the only legal values are powers of two.  Thus, we'll
	 * use a shift instead.
	 */
	switch ( tex->format ) {
	case RADEON_TXFORMAT_ARGB8888:
	case RADEON_TXFORMAT_RGBA8888:
		format = RADEON_COLOR_FORMAT_ARGB8888;
		tex_width = tex->width * 4;
		blit_width = image->width * 4;
		break;
	case RADEON_TXFORMAT_AI88:
	case RADEON_TXFORMAT_ARGB1555:
	case RADEON_TXFORMAT_RGB565:
	case RADEON_TXFORMAT_ARGB4444:
		format = RADEON_COLOR_FORMAT_RGB565;
		tex_width = tex->width * 2;
		blit_width = image->width * 2;
		break;
	case RADEON_TXFORMAT_I8:
	case RADEON_TXFORMAT_RGB332:
		format = RADEON_COLOR_FORMAT_CI8;
		tex_width = tex->width * 1;
		blit_width = image->width * 1;
		break;
	default:
		DRM_ERROR( "invalid texture format %d\n", tex->format );
		return -EINVAL;
	}

	DRM_DEBUG( "   tex=%dx%d  blit=%d\n",
		   tex_width, tex->height, blit_width );

	/* Flush the pixel cache.  This ensures no pixel data gets mixed
	 * up with the texture data from the host data blit, otherwise
	 * part of the texture image may be corrupted.
	 */
	BEGIN_RING( 4 );

	RADEON_FLUSH_CACHE();
	RADEON_WAIT_UNTIL_IDLE();

	ADVANCE_RING();

#ifdef __BIG_ENDIAN
	/* The Mesa texture functions provide the data in little endian as the
	 * chip wants it, but we need to compensate for the fact that the CP
	 * ring gets byte-swapped
	 */
	BEGIN_RING( 2 );
	OUT_RING_REG( RADEON_RBBM_GUICNTL, RADEON_HOST_DATA_SWAP_32BIT );
	ADVANCE_RING();
#endif

	/* Make a copy of the parameters in case we have to update them
	 * for a multi-pass texture blit.
	 */
	y = image->y;
	height = image->height;
	data = (u8 *)image->data;

	size = height * blit_width;

	if ( size > RADEON_MAX_TEXTURE_SIZE ) {
		/* Texture image is too large, do a multipass upload */
		ret = -EAGAIN;

		/* Adjust the blit size to fit the indirect buffer */
		height = RADEON_MAX_TEXTURE_SIZE / blit_width;
		size = height * blit_width;

		/* Update the input parameters for next time */
		image->y += height;
		image->height -= height;
		image->data = (char *)image->data + size;

		if ( copy_to_user( tex->image, image, sizeof(*image) ) ) {
			DRM_ERROR( "EFAULT on tex->image\n" );
			return -EFAULT;
		}
	} else if ( size < 4 && size > 0 ) {
		size = 4;
	}

	dwords = size / 4;

	/* Dispatch the indirect buffer.
	 */
	buffer = (u32 *)((char *)dev_priv->buffers->handle + buf->offset);

	buffer[0] = CP_PACKET3( RADEON_CNTL_HOSTDATA_BLT, dwords + 6 );
	buffer[1] = (RADEON_GMC_DST_PITCH_OFFSET_CNTL |
		     RADEON_GMC_BRUSH_NONE |
		     (format << 8) |
		     RADEON_GMC_SRC_DATATYPE_COLOR |
		     RADEON_ROP3_S |
		     RADEON_DP_SRC_SOURCE_HOST_DATA |
		     RADEON_GMC_CLR_CMP_CNTL_DIS |
		     RADEON_GMC_WR_MSK_DIS);

	buffer[2] = (tex->pitch << 22) | (tex->offset >> 10);
	buffer[3] = 0xffffffff;
	buffer[4] = 0xffffffff;
	buffer[5] = (y << 16) | image->x;
	buffer[6] = (height << 16) | image->width;
	buffer[7] = dwords;

	buffer += 8;

	if ( tex_width >= 32 ) {
		/* Texture image width is larger than the minimum, so we
		 * can upload it directly.
		 */
		if ( copy_from_user( buffer, data, dwords * sizeof(u32) ) ) {
			DRM_ERROR( "EFAULT on data, %d dwords\n", dwords );
			return -EFAULT;
		}
	} else {
		/* Texture image width is less than the minimum, so we
		 * need to pad out each image scanline to the minimum
		 * width.
		 */
		for ( i = 0 ; i < tex->height ; i++ ) {
			if ( copy_from_user( buffer, data, tex_width ) ) {
				DRM_ERROR( "EFAULT on pad, %d bytes\n",
					   tex_width );
				return -EFAULT;
			}
			buffer += 8;
			data += tex_width;
		}
	}

	buf->pid = current->pid;
	buf->used = (dwords + 8) * sizeof(u32);

	radeon_cp_dispatch_indirect( dev, buf, 0, buf->used );
	radeon_cp_discard_buffer( dev, buf );

	/* Flush the pixel cache after the blit completes.  This ensures
	 * the texture data is written out to memory before rendering
	 * continues.
	 */
	BEGIN_RING( 4 );

	RADEON_FLUSH_CACHE();
	RADEON_WAIT_UNTIL_2D_IDLE();

	ADVANCE_RING();

	return ret;
}

static void radeon_cp_dispatch_stipple( drm_device_t *dev, u32 *stipple )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int i;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	BEGIN_RING( 35 );

	OUT_RING( CP_PACKET0( RADEON_RE_STIPPLE_ADDR, 0 ) );
	OUT_RING( 0x00000000 );

	OUT_RING( CP_PACKET0_TABLE( RADEON_RE_STIPPLE_DATA, 31 ) );
	for ( i = 0 ; i < 32 ; i++ ) {
		OUT_RING( stipple[i] );
	}

	ADVANCE_RING();
}


/* ================================================================
 * IOCTL functions
 */

int radeon_cp_clear( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_clear_t clear;
	drm_radeon_clear_rect_t depth_boxes[RADEON_NR_SAREA_CLIPRECTS];
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev );

	if ( copy_from_user( &clear, (drm_radeon_clear_t *)arg,
			     sizeof(clear) ) )
		return -EFAULT;

	RING_SPACE_TEST_WITH_RETURN( dev_priv );

	if ( sarea_priv->nbox > RADEON_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = RADEON_NR_SAREA_CLIPRECTS;

	if ( copy_from_user( &depth_boxes, clear.depth_boxes,
			     sarea_priv->nbox * sizeof(depth_boxes[0]) ) )
		return -EFAULT;

	radeon_cp_dispatch_clear( dev, &clear, depth_boxes );

	COMMIT_RING();
	return 0;
}



/* Not sure why this isn't set all the time:
 */ 
static int radeon_do_init_pageflip( drm_device_t *dev )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	dev_priv->crtc_offset =      RADEON_READ( RADEON_CRTC_OFFSET );
	dev_priv->crtc_offset_cntl = RADEON_READ( RADEON_CRTC_OFFSET_CNTL );

	RADEON_WRITE( RADEON_CRTC_OFFSET, dev_priv->front_offset );
	RADEON_WRITE( RADEON_CRTC_OFFSET_CNTL,
		      dev_priv->crtc_offset_cntl |
		      RADEON_CRTC_OFFSET_FLIP_CNTL );

	dev_priv->page_flipping = 1;
	dev_priv->current_page = 0;
	dev_priv->sarea_priv->pfCurrentPage = dev_priv->current_page;

	return 0;
}

int radeon_do_cleanup_pageflip( drm_device_t *dev )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	RADEON_WRITE( RADEON_CRTC_OFFSET,      dev_priv->crtc_offset );
	RADEON_WRITE( RADEON_CRTC_OFFSET_CNTL, dev_priv->crtc_offset_cntl );

	dev_priv->page_flipping = 0;
	dev_priv->current_page = 0;
	dev_priv->sarea_priv->pfCurrentPage = dev_priv->current_page;

	return 0;
}

/* Swapping and flipping are different operations, need different ioctls.
 * They can & should be intermixed to support multiple 3d windows.  
 */
int radeon_cp_flip( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev );

	RING_SPACE_TEST_WITH_RETURN( dev_priv );

	if (!dev_priv->page_flipping) 
		radeon_do_init_pageflip( dev );
		
	radeon_cp_dispatch_flip( dev );

	COMMIT_RING();
	return 0;
}

int radeon_cp_swap( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev );

	RING_SPACE_TEST_WITH_RETURN( dev_priv );

	if ( sarea_priv->nbox > RADEON_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = RADEON_NR_SAREA_CLIPRECTS;

	radeon_cp_dispatch_swap( dev );
	dev_priv->sarea_priv->ctx_owner = 0;

	COMMIT_RING();
	return 0;
}

int radeon_cp_vertex( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_radeon_vertex_t vertex;
	drm_radeon_tcl_prim_t prim;

	LOCK_TEST_WITH_RETURN( dev );

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &vertex, (drm_radeon_vertex_t *)arg,
			     sizeof(vertex) ) )
		return -EFAULT;

	DRM_DEBUG( "%s: pid=%d index=%d count=%d discard=%d\n",
		   __FUNCTION__, current->pid,
		   vertex.idx, vertex.count, vertex.discard );

	if ( vertex.idx < 0 || vertex.idx >= dma->buf_count ) {
		DRM_ERROR( "buffer index %d (of %d max)\n",
			   vertex.idx, dma->buf_count - 1 );
		return -EINVAL;
	}
	if ( vertex.prim < 0 ||
	     vertex.prim > RADEON_PRIM_TYPE_3VRT_LINE_LIST ) {
		DRM_ERROR( "buffer prim %d\n", vertex.prim );
		return -EINVAL;
	}

	RING_SPACE_TEST_WITH_RETURN( dev_priv );
	VB_AGE_TEST_WITH_RETURN( dev_priv );

	buf = dma->buflist[vertex.idx];

	if ( buf->pid != current->pid ) {
		DRM_ERROR( "process %d using buffer owned by %d\n",
			   current->pid, buf->pid );
		return -EINVAL;
	}
	if ( buf->pending ) {
		DRM_ERROR( "sending pending buffer %d\n", vertex.idx );
		return -EINVAL;
	}

	/* Build up a prim_t record:
	 */
	if (vertex.count) {
		buf->used = vertex.count; /* not used? */

		if ( sarea_priv->dirty & ~RADEON_UPLOAD_CLIPRECTS ) {
			radeon_emit_state( dev_priv,
					   &sarea_priv->context_state,
					   sarea_priv->tex_state,
					   sarea_priv->dirty );
			
			sarea_priv->dirty &= ~(RADEON_UPLOAD_TEX0IMAGES |
					       RADEON_UPLOAD_TEX1IMAGES |
					       RADEON_UPLOAD_TEX2IMAGES |
					       RADEON_REQUIRE_QUIESCENCE);
		}

		prim.start = 0;
		prim.finish = vertex.count; /* unused */
		prim.prim = vertex.prim;
		prim.numverts = vertex.count;
		prim.vc_format = dev_priv->sarea_priv->vc_format;
		
		radeon_cp_dispatch_vertex( dev, buf, &prim,
					   dev_priv->sarea_priv->boxes,
					   dev_priv->sarea_priv->nbox );
	}

	if (vertex.discard) {
		radeon_cp_discard_buffer( dev, buf );
	}

	COMMIT_RING();
	return 0;
}

int radeon_cp_indices( struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_radeon_indices_t elts;
	drm_radeon_tcl_prim_t prim;
	int count;

	LOCK_TEST_WITH_RETURN( dev );

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &elts, (drm_radeon_indices_t *)arg,
			     sizeof(elts) ) )
		return -EFAULT;

	DRM_DEBUG( "%s: pid=%d index=%d start=%d end=%d discard=%d\n",
		   __FUNCTION__, current->pid,
		   elts.idx, elts.start, elts.end, elts.discard );

	if ( elts.idx < 0 || elts.idx >= dma->buf_count ) {
		DRM_ERROR( "buffer index %d (of %d max)\n",
			   elts.idx, dma->buf_count - 1 );
		return -EINVAL;
	}
	if ( elts.prim < 0 ||
	     elts.prim > RADEON_PRIM_TYPE_3VRT_LINE_LIST ) {
		DRM_ERROR( "buffer prim %d\n", elts.prim );
		return -EINVAL;
	}

	RING_SPACE_TEST_WITH_RETURN( dev_priv );
	VB_AGE_TEST_WITH_RETURN( dev_priv );

	buf = dma->buflist[elts.idx];

	if ( buf->pid != current->pid ) {
		DRM_ERROR( "process %d using buffer owned by %d\n",
			   current->pid, buf->pid );
		return -EINVAL;
	}
	if ( buf->pending ) {
		DRM_ERROR( "sending pending buffer %d\n", elts.idx );
		return -EINVAL;
	}

	count = (elts.end - elts.start) / sizeof(u16);
	elts.start -= RADEON_INDEX_PRIM_OFFSET;

	if ( elts.start & 0x7 ) {
		DRM_ERROR( "misaligned buffer 0x%x\n", elts.start );
		return -EINVAL;
	}
	if ( elts.start < buf->used ) {
		DRM_ERROR( "no header 0x%x - 0x%x\n", elts.start, buf->used );
		return -EINVAL;
	}

	buf->used = elts.end;

	if ( sarea_priv->dirty & ~RADEON_UPLOAD_CLIPRECTS ) {
		radeon_emit_state( dev_priv,
				   &sarea_priv->context_state,
				   sarea_priv->tex_state,
				   sarea_priv->dirty );

		sarea_priv->dirty &= ~(RADEON_UPLOAD_TEX0IMAGES |
				       RADEON_UPLOAD_TEX1IMAGES |
				       RADEON_UPLOAD_TEX2IMAGES |
				       RADEON_REQUIRE_QUIESCENCE);
	}


	/* Build up a prim_t record:
	 */
	prim.start = elts.start;
	prim.finish = elts.end; 
	prim.prim = elts.prim;
	prim.offset = 0;	/* offset from start of dma buffers */
	prim.numverts = RADEON_MAX_VB_VERTS; /* duh */
	prim.vc_format = dev_priv->sarea_priv->vc_format;
	
	radeon_cp_dispatch_indices( dev, buf, &prim,
				   dev_priv->sarea_priv->boxes,
				   dev_priv->sarea_priv->nbox );
	if (elts.discard) {
		radeon_cp_discard_buffer( dev, buf );
	}

	COMMIT_RING();
	return 0;
}

int radeon_cp_texture( struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_texture_t tex;
	drm_radeon_tex_image_t image;
	int ret;

	LOCK_TEST_WITH_RETURN( dev );

	if ( copy_from_user( &tex, (drm_radeon_texture_t *)arg, sizeof(tex) ) )
		return -EFAULT;

	if ( tex.image == NULL ) {
		DRM_ERROR( "null texture image!\n" );
		return -EINVAL;
	}

	if ( copy_from_user( &image,
			     (drm_radeon_tex_image_t *)tex.image,
			     sizeof(image) ) )
		return -EFAULT;

	RING_SPACE_TEST_WITH_RETURN( dev_priv );
	VB_AGE_TEST_WITH_RETURN( dev_priv );

	ret = radeon_cp_dispatch_texture( dev, &tex, &image );

	COMMIT_RING();
	return ret;
}

int radeon_cp_stipple( struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_stipple_t stipple;
	u32 mask[32];

	LOCK_TEST_WITH_RETURN( dev );

	if ( copy_from_user( &stipple, (drm_radeon_stipple_t *)arg,
			     sizeof(stipple) ) )
		return -EFAULT;

	if ( copy_from_user( &mask, stipple.mask, 32 * sizeof(u32) ) )
		return -EFAULT;

	RING_SPACE_TEST_WITH_RETURN( dev_priv );

	radeon_cp_dispatch_stipple( dev, mask );

	COMMIT_RING();
	return 0;
}

int radeon_cp_indirect( struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_radeon_indirect_t indirect;
	RING_LOCALS;

	LOCK_TEST_WITH_RETURN( dev );

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &indirect, (drm_radeon_indirect_t *)arg,
			     sizeof(indirect) ) )
		return -EFAULT;

	DRM_DEBUG( "indirect: idx=%d s=%d e=%d d=%d\n",
		   indirect.idx, indirect.start,
		   indirect.end, indirect.discard );

	if ( indirect.idx < 0 || indirect.idx >= dma->buf_count ) {
		DRM_ERROR( "buffer index %d (of %d max)\n",
			   indirect.idx, dma->buf_count - 1 );
		return -EINVAL;
	}

	buf = dma->buflist[indirect.idx];

	if ( buf->pid != current->pid ) {
		DRM_ERROR( "process %d using buffer owned by %d\n",
			   current->pid, buf->pid );
		return -EINVAL;
	}
	if ( buf->pending ) {
		DRM_ERROR( "sending pending buffer %d\n", indirect.idx );
		return -EINVAL;
	}

	if ( indirect.start < buf->used ) {
		DRM_ERROR( "reusing indirect: start=0x%x actual=0x%x\n",
			   indirect.start, buf->used );
		return -EINVAL;
	}

	RING_SPACE_TEST_WITH_RETURN( dev_priv );
	VB_AGE_TEST_WITH_RETURN( dev_priv );

	buf->used = indirect.end;

	/* Wait for the 3D stream to idle before the indirect buffer
	 * containing 2D acceleration commands is processed.
	 */
	BEGIN_RING( 2 );

	RADEON_WAIT_UNTIL_3D_IDLE();

	ADVANCE_RING();

	/* Dispatch the indirect buffer full of commands from the
	 * X server.  This is insecure and is thus only available to
	 * privileged clients.
	 */
	radeon_cp_dispatch_indirect( dev, buf, indirect.start, indirect.end );
	if (indirect.discard) {
		radeon_cp_discard_buffer( dev, buf );
	}


	COMMIT_RING();
	return 0;
}

int radeon_cp_vertex2( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_radeon_vertex2_t vertex;
	int i;
	unsigned char laststate;

	LOCK_TEST_WITH_RETURN( dev );

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &vertex, (drm_radeon_vertex_t *)arg,
			     sizeof(vertex) ) )
		return -EFAULT;

	DRM_DEBUG( __FUNCTION__": pid=%d index=%d discard=%d\n",
		   current->pid, vertex.idx, vertex.discard );

	if ( vertex.idx < 0 || vertex.idx >= dma->buf_count ) {
		DRM_ERROR( "buffer index %d (of %d max)\n",
			   vertex.idx, dma->buf_count - 1 );
		return -EINVAL;
	}

	RING_SPACE_TEST_WITH_RETURN( dev_priv );
	VB_AGE_TEST_WITH_RETURN( dev_priv );

	buf = dma->buflist[vertex.idx];

	if ( buf->pid != current->pid ) {
		DRM_ERROR( "process %d using buffer owned by %d\n",
			   current->pid, buf->pid );
		return -EINVAL;
	}

	if ( buf->pending ) {
		DRM_ERROR( "sending pending buffer %d\n", vertex.idx );
		return -EINVAL;
	}
	
	if (sarea_priv->nbox > RADEON_NR_SAREA_CLIPRECTS)
		return -EINVAL;

	for (laststate = 0xff, i = 0 ; i < vertex.nr_prims ; i++) {
		drm_radeon_prim_t prim;
		drm_radeon_tcl_prim_t tclprim;
		
		if ( copy_from_user( &prim, &vertex.prim[i], sizeof(prim) ) )
			return -EFAULT;
		
		if ( prim.stateidx != laststate ) {
			drm_radeon_state_t state;			       
				
			if ( copy_from_user( &state, 
					     &vertex.state[prim.stateidx], 
					     sizeof(state) ) )
				return -EFAULT;

			radeon_emit_state2( dev_priv, &state );

			laststate = prim.stateidx;
		}

		tclprim.start = prim.start;
		tclprim.finish = prim.finish;
		tclprim.prim = prim.prim;
		tclprim.vc_format = prim.vc_format;

		if ( prim.prim & RADEON_PRIM_WALK_IND ) {
			tclprim.offset = prim.numverts * 64;
			tclprim.numverts = RADEON_MAX_VB_VERTS; /* duh */

			radeon_cp_dispatch_indices( dev, buf, &tclprim,
						    sarea_priv->boxes,
						    sarea_priv->nbox);
		} else {
			tclprim.numverts = prim.numverts;
			tclprim.offset = 0; /* not used */

			radeon_cp_dispatch_vertex( dev, buf, &tclprim,
						   sarea_priv->boxes,
						   sarea_priv->nbox);
		}
		
		if (sarea_priv->nbox == 1)
			sarea_priv->nbox = 0;
	}

	if ( vertex.discard ) {
		radeon_cp_discard_buffer( dev, buf );
	}

	COMMIT_RING();
	return 0;
}


static int radeon_emit_packets( 
	drm_radeon_private_t *dev_priv,
	drm_radeon_cmd_header_t header,
	drm_radeon_cmd_buffer_t *cmdbuf )
{
	int id = (int)header.packet.packet_id;
	int sz = packet[id].len;
	int reg = packet[id].start;
	int *data = (int *)cmdbuf->buf;
	RING_LOCALS;
   
	if (sz * sizeof(int) > cmdbuf->bufsz) 
		return -EINVAL;

	BEGIN_RING(sz+1);
	OUT_RING( CP_PACKET0( reg, (sz-1) ) );
	OUT_RING_USER_TABLE( data, sz );
	ADVANCE_RING();

	cmdbuf->buf += sz * sizeof(int);
	cmdbuf->bufsz -= sz * sizeof(int);
	return 0;
}

static inline int radeon_emit_scalars( 
	drm_radeon_private_t *dev_priv,
	drm_radeon_cmd_header_t header,
	drm_radeon_cmd_buffer_t *cmdbuf )
{
	int sz = header.scalars.count;
	int *data = (int *)cmdbuf->buf;
	int start = header.scalars.offset;
	int stride = header.scalars.stride;
	RING_LOCALS;

	BEGIN_RING( 3+sz );
	OUT_RING( CP_PACKET0( RADEON_SE_TCL_SCALAR_INDX_REG, 0 ) );
	OUT_RING( start | (stride << RADEON_SCAL_INDX_DWORD_STRIDE_SHIFT));
	OUT_RING( CP_PACKET0_TABLE( RADEON_SE_TCL_SCALAR_DATA_REG, sz-1 ) );
	OUT_RING_USER_TABLE( data, sz );
	ADVANCE_RING();
	cmdbuf->buf += sz * sizeof(int);
	cmdbuf->bufsz -= sz * sizeof(int);
	return 0;
}

static inline int radeon_emit_vectors( 
	drm_radeon_private_t *dev_priv,
	drm_radeon_cmd_header_t header,
	drm_radeon_cmd_buffer_t *cmdbuf )
{
	int sz = header.vectors.count;
	int *data = (int *)cmdbuf->buf;
	int start = header.vectors.offset;
	int stride = header.vectors.stride;
	RING_LOCALS;

	BEGIN_RING( 3+sz );
	OUT_RING( CP_PACKET0( RADEON_SE_TCL_VECTOR_INDX_REG, 0 ) );
	OUT_RING( start | (stride << RADEON_VEC_INDX_OCTWORD_STRIDE_SHIFT));
	OUT_RING( CP_PACKET0_TABLE( RADEON_SE_TCL_VECTOR_DATA_REG, (sz-1) ) );
	OUT_RING_USER_TABLE( data, sz );
	ADVANCE_RING();

	cmdbuf->buf += sz * sizeof(int);
	cmdbuf->bufsz -= sz * sizeof(int);
	return 0;
}


static int radeon_emit_packet3( drm_device_t *dev,
				drm_radeon_cmd_buffer_t *cmdbuf )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int cmdsz, tmp;
	int *cmd = (int *)cmdbuf->buf;
	RING_LOCALS;


	DRM_DEBUG("%s\n", __FUNCTION__);

	if (__get_user( tmp, &cmd[0]))
		return -EFAULT;

	cmdsz = 2 + ((tmp & RADEON_CP_PACKET_COUNT_MASK) >> 16);

	if ((tmp & 0xc0000000) != RADEON_CP_PACKET3 ||
	    cmdsz * 4 > cmdbuf->bufsz)
		return -EINVAL;

	BEGIN_RING( cmdsz );
	OUT_RING_USER_TABLE( cmd, cmdsz );
	ADVANCE_RING();

	cmdbuf->buf += cmdsz * 4;
	cmdbuf->bufsz -= cmdsz * 4;
	return 0;
}


static int radeon_emit_packet3_cliprect( drm_device_t *dev,
					 drm_radeon_cmd_buffer_t *cmdbuf )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_clip_rect_t box;
	int cmdsz, tmp;
	int *cmd = (int *)cmdbuf->buf;
	drm_clip_rect_t *boxes = cmdbuf->boxes;
	int i = 0;
	RING_LOCALS;

	DRM_DEBUG("%s\n", __FUNCTION__);

	if (__get_user( tmp, &cmd[0]))
		return -EFAULT;

	cmdsz = 2 + ((tmp & RADEON_CP_PACKET_COUNT_MASK) >> 16);

	if ((tmp & 0xc0000000) != RADEON_CP_PACKET3 ||
	    cmdsz * 4 > cmdbuf->bufsz)
		return -EINVAL;

	do {
		if ( i < cmdbuf->nbox ) {
			if (__copy_from_user( &box, &boxes[i], sizeof(box) ))
				return -EFAULT;
			radeon_emit_clip_rect( dev_priv, &box );
		}
		
		BEGIN_RING( cmdsz );
		OUT_RING_USER_TABLE( cmd, cmdsz );
		ADVANCE_RING();

	} while ( ++i < cmdbuf->nbox );

 	if (cmdbuf->nbox == 1)
		cmdbuf->nbox = 0;

	cmdbuf->buf += cmdsz * 4;
	cmdbuf->bufsz -= cmdsz * 4;
	return 0;
}



int radeon_cp_cmdbuf( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf = 0;
	int idx;
	drm_radeon_cmd_buffer_t cmdbuf;
	drm_radeon_cmd_header_t header;

	LOCK_TEST_WITH_RETURN( dev );

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &cmdbuf, (drm_radeon_cmd_buffer_t *)arg,
			     sizeof(cmdbuf) ) ) {
		DRM_ERROR("copy_from_user\n");
		return -EFAULT;
	}

	DRM_DEBUG( __FUNCTION__": pid=%d\n", current->pid );
	RING_SPACE_TEST_WITH_RETURN( dev_priv );
	VB_AGE_TEST_WITH_RETURN( dev_priv );


	if (verify_area( VERIFY_READ, cmdbuf.buf, cmdbuf.bufsz ))
		return -EFAULT;

	if (cmdbuf.nbox &&
	    verify_area( VERIFY_READ, cmdbuf.boxes, 
			 cmdbuf.nbox * sizeof(drm_clip_rect_t)))
		return -EFAULT;

	while ( cmdbuf.bufsz >= sizeof(header) ) {
		
		if (__get_user( header.i, (int *)cmdbuf.buf )) {
			DRM_ERROR("__get_user %p\n", cmdbuf.buf);
			return -EFAULT;
		}

		cmdbuf.buf += sizeof(header);
		cmdbuf.bufsz -= sizeof(header);

		switch (header.header.cmd_type) {
		case RADEON_CMD_PACKET: 
			if (radeon_emit_packets( dev_priv, header, &cmdbuf )) {
				DRM_ERROR("radeon_emit_packets failed\n");
				return -EINVAL;
			}
			break;

		case RADEON_CMD_SCALARS:
			if (radeon_emit_scalars( dev_priv, header, &cmdbuf )) {
				DRM_ERROR("radeon_emit_scalars failed\n");
				return -EINVAL;
			}
			break;

		case RADEON_CMD_VECTORS:
			if (radeon_emit_vectors( dev_priv, header, &cmdbuf )) {
				DRM_ERROR("radeon_emit_vectors failed\n");
				return -EINVAL;
			}
			break;

		case RADEON_CMD_DMA_DISCARD:
			idx = header.dma.buf_idx;
			if ( idx < 0 || idx >= dma->buf_count ) {
				DRM_ERROR( "buffer index %d (of %d max)\n",
					   idx, dma->buf_count - 1 );
				return -EINVAL;
			}

			buf = dma->buflist[idx];
			if ( buf->pid != current->pid || buf->pending ) {
				DRM_ERROR( "bad buffer\n" );
				return -EINVAL;
			}

			radeon_cp_discard_buffer( dev, buf );
			break;

		case RADEON_CMD_PACKET3:
			if (radeon_emit_packet3( dev, &cmdbuf )) {
				DRM_ERROR("radeon_emit_packet3 failed\n");
				return -EINVAL;
			}
			break;

		case RADEON_CMD_PACKET3_CLIP:
			if (radeon_emit_packet3_cliprect( dev, &cmdbuf )) {
				DRM_ERROR("radeon_emit_packet3_clip failed\n");
				return -EINVAL;
			}
			break;

		default:
			DRM_ERROR("bad cmd_type %d at %p\n", 
				  header.header.cmd_type,
				  cmdbuf.buf - sizeof(header));
			return -EINVAL;
		}
	}


	COMMIT_RING();
	return 0;
}



int radeon_cp_getparam( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_getparam_t param;
	int value;

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &param, (drm_radeon_getparam_t *)arg,
			     sizeof(param) ) ) {
		DRM_ERROR("copy_from_user\n");
		return -EFAULT;
	}

	DRM_DEBUG( __FUNCTION__": pid=%d\n", current->pid );

	switch( param.param ) {
	case RADEON_PARAM_AGP_BUFFER_OFFSET:
		value = dev_priv->agp_buffers_offset;
		break;
	default:
		return -EINVAL;
	}

	if ( copy_to_user( param.value, &value, sizeof(int) ) ) {
		DRM_ERROR( "copy_to_user\n" );
		return -EFAULT;
	}
	
	return 0;
}
