/*
    the api- and os-independet parts of the saa7146 device driver
    
    Copyright (C) 1998,1999 Michael Hunold <michael@mihu.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "saa7146_defs.h"

#define TRUNC(val,max) ((val) < (max) ? (val) : (max))

#ifdef __COMPILE_SAA7146__

struct saa7146_modes_constants
 modes_constants[] = {
	{ V_OFFSET_PAL,		V_FIELD_PAL,	V_ACTIVE_LINES_PAL,
	  H_OFFSET_PAL,		H_PIXELS_PAL,	H_PIXELS_PAL+1,
	  V_ACTIVE_LINES_PAL,	1024 },	/* PAL values */
	{ V_OFFSET_NTSC,	V_FIELD_NTSC,	V_ACTIVE_LINES_NTSC,
	  H_OFFSET_NTSC,	H_PIXELS_NTSC,  H_PIXELS_NTSC+1,
	  V_ACTIVE_LINES_NTSC,	1024 },	/* NTSC values */
	{ 0,0,0,0,0,0,0,0 }, /* secam values */
	{ 0,288,576, 
	  0,188*4,188*4+1,
	  288,188*4 } /* TS values */
};

/* -----------------------------------------------------------------------------------------
   helper functions for the calculation of the horizontal- and vertical scaling	registers,
   clip-format-register etc ...
   these functions take pointers to the (most-likely read-out original-values) and manipulate
   them according to the requested new scaling parameters.
   ----------------------------------------------------------------------------------------- */

/* hps_coeff used for CXY and CXUV; scale 1/1 -> scale 1/64 */
struct {
	u16 hps_coeff;
	u16 weight_sum;
} hps_h_coeff_tab [] = { 
	{0x00,   2}, {0x02,   4}, {0x00,   4}, {0x06,   8}, {0x02,   8},
	{0x08,   8}, {0x00,   8}, {0x1E,  16}, {0x0E,   8}, {0x26,   8},
	{0x06,   8}, {0x42,   8}, {0x02,   8}, {0x80,   8}, {0x00,   8},
	{0xFE,  16}, {0xFE,   8}, {0x7E,   8}, {0x7E,   8}, {0x3E,   8},
	{0x3E,   8}, {0x1E,   8}, {0x1E,   8}, {0x0E,   8}, {0x0E,   8},
	{0x06,   8}, {0x06,   8}, {0x02,   8}, {0x02,   8}, {0x00,   8},
	{0x00,   8}, {0xFE,  16}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8},
	{0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8},
	{0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8},
	{0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0xFE,   8}, {0x7E,   8},
	{0x7E,   8}, {0x3E,   8}, {0x3E,   8}, {0x1E,   8}, {0x1E,   8},
	{0x0E,   8}, {0x0E,   8}, {0x06,   8}, {0x06,   8}, {0x02,   8},
	{0x02,   8}, {0x00,   8}, {0x00,   8}, {0xFE,  16}
};

/* table of attenuation values for horizontal scaling */
u8 h_attenuation[] = { 1, 2, 4, 8, 2, 4, 8, 16, 0};

int calculate_h_scale_registers(struct saa7146* saa, u32 in_x, u32 out_x, int flip_lr, u32* hps_ctrl, u32* hps_v_gain, u32* hps_h_prescale, u32* hps_h_scale)
{
	/* horizontal prescaler */
	u32 dcgx = 0, xpsc = 0, xacm = 0, cxy = 0, cxuv = 0;
	/* horizontal scaler */
	u32 xim = 0, xp = 0, xsci =0;
	/* vertical scale & gain */
	u32 pfuv = 0;	
	/* helper variables */
	u32 h_atten = 0, i = 0;

	if ( 0 == out_x ) {
		printk("saa7146: ==> calculate_h_scale_registers: invalid value (=0).\n");
		return -EINVAL;
	}
	
	/* mask out vanity-bit */
	*hps_ctrl &= ~MASK_29;
		
	/* calculate prescale-(xspc)-value:	[n   .. 1/2) : 1
				    		[1/2 .. 1/3) : 2
				    		[1/3 .. 1/4) : 3
						... 			*/
	if (in_x > out_x) {
		xpsc = in_x / out_x;
	} else {
		/* zooming */
		xpsc = 1;						
	}
	
	/* if flip_lr-bit is set, number of pixels after horizontal prescaling must be < 384 */
	if ( 0 != flip_lr ) {
		/* set vanity bit */
		*hps_ctrl |= MASK_29;
	
		while (in_x / xpsc >= 384 )
			xpsc++;
	}
	/* if zooming is wanted, number of pixels after horizontal prescaling must be < 768 */
	else {
		while ( in_x / xpsc >= 768 )
			xpsc++;
	}
	
	/* maximum prescale is 64 (p.69) */
	if ( xpsc > 64 )
		xpsc = 64;

	/* keep xacm clear*/
	xacm = 0;
	
	/* set horizontal filter parameters (CXY = CXUV) */
	cxy = hps_h_coeff_tab[TRUNC(xpsc - 1, 63)].hps_coeff;
	cxuv = cxy;
	
	/* calculate and set horizontal fine scale (xsci) */
	
	/* bypass the horizontal scaler ? */
	if ( (in_x == out_x) && ( 1 == xpsc ) )
		xsci = 0x400;
	else	
		xsci = ( (1024 * in_x) / (out_x * xpsc) ) + xpsc;

	/* set start phase for horizontal fine scale (xp) to 0 */	
	xp = 0;
	
	/* set xim, if we bypass the horizontal scaler */
	if ( 0x400 == xsci )
		xim = 1;
	else
		xim = 0;
		
	/* if the prescaler is bypassed, enable horizontal accumulation mode (xacm)
	   and clear dcgx */
	if( 1 == xpsc ) {
		xacm = 1;
		dcgx = 0;
	} else {
		xacm = 0;
		/* get best match in the table of attenuations for horizontal scaling */
		h_atten = hps_h_coeff_tab[TRUNC(xpsc - 1, 63)].weight_sum;
	
		for (i = 0; h_attenuation[i] != 0; i++) {
			if (h_attenuation[i] >= h_atten)
				break;
		}
	
		dcgx = i;
	}

	/* the horizontal scaling increment controls the UV filter to reduce the bandwith to
	   improve the display quality, so set it ... */
	if ( xsci == 0x400)
		pfuv = 0x00;
	else if ( xsci < 0x600)
		pfuv = 0x01;
	else if ( xsci < 0x680)
		pfuv = 0x11;
	else if ( xsci < 0x700)
		pfuv = 0x22;
	else
		pfuv = 0x33;

	
	*hps_v_gain  &= MASK_W0|MASK_B2;
	*hps_v_gain  |= (pfuv << 24);	

	*hps_h_scale 	&= ~(MASK_W1 | 0xf000);
	*hps_h_scale	|= (xim << 31) | (xp << 24) | (xsci << 12);

	*hps_h_prescale	|= (dcgx << 27) | ((xpsc-1) << 18) | (xacm << 17) | (cxy << 8) | (cxuv << 0);

	return 0;
}

struct {
	u16 hps_coeff;
	u16 weight_sum;
} hps_v_coeff_tab [] = { 
	{0x0100,   2},  {0x0102,   4},  {0x0300,   4},  {0x0106,   8},
	{0x0502,   8},  {0x0708,   8},  {0x0F00,   8},  {0x011E,  16},
	{0x110E,  16},  {0x1926,  16},  {0x3906,  16},  {0x3D42,  16},
	{0x7D02,  16},  {0x7F80,  16},  {0xFF00,  16},  {0x01FE,  32},
	{0x01FE,  32},  {0x817E,  32},  {0x817E,  32},  {0xC13E,  32},
	{0xC13E,  32},  {0xE11E,  32},  {0xE11E,  32},  {0xF10E,  32},
	{0xF10E,  32},  {0xF906,  32},  {0xF906,  32},  {0xFD02,  32},
	{0xFD02,  32},  {0xFF00,  32},  {0xFF00,  32},  {0x01FE,  64},
	{0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},
	{0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},
	{0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},
	{0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},  {0x01FE,  64},
	{0x01FE,  64},  {0x817E,  64},  {0x817E,  64},  {0xC13E,  64},
	{0xC13E,  64},  {0xE11E,  64},  {0xE11E,  64},  {0xF10E,  64},
	{0xF10E,  64},  {0xF906,  64},  {0xF906,  64},  {0xFD02,  64},
	{0xFD02,  64},  {0xFF00,  64},  {0xFF00,  64},  {0x01FE, 128}
};

/* table of attenuation values for vertical scaling */
u16 v_attenuation[] = { 2, 4, 8, 16, 32, 64, 128, 256, 0};

int calculate_v_scale_registers(struct saa7146* saa, u32 in_y, u32 out_y, u32* hps_v_scale, u32* hps_v_gain)
{
	u32 yacm = 0, ysci = 0, yacl = 0, ypo = 0, ype = 0;	/* vertical scaling */
	u32 dcgy = 0, cya_cyb = 0;				/* vertical scale & gain */
				
	u32 v_atten = 0, i = 0;					/* helper variables */

	/* error, if vertical zooming */
	if ( in_y < out_y ) {
		printk("saa7146: ==> calculate_v_scale_registers: we cannot do vertical zooming.\n");
		return -EINVAL;
	}

	/* linear phase interpolation may be used if scaling is between 1 and 1/2
	   or scaling is between 1/2 and 1/4 (if interlace is set; see below) */
	if( ((2*out_y) >= in_y) || (((4*out_y) >= in_y) && saa->interlace != 0)) {

		/* convention: if scaling is between 1/2 and 1/4 we only use
		   the even lines, the odd lines get discarded (see function move_to)
		   if interlace is set */
		if( saa->interlace != 0 && (out_y*4) >= in_y && (out_y*2) <= in_y) 
			out_y *= 2;

		yacm = 0;
		yacl = 0;
		cya_cyb = 0x00ff;
		
		/* calculate scaling increment */
		if ( in_y > out_y )
			ysci = ((1024 * in_y) / (out_y + 1)) - 1024;
		else
			ysci = 0;

		dcgy = 0;

		/* calculate ype and ypo */
                if (saa->interlace !=0) { 
                    
                    /* Special case for interlaced input */

                    /* See Philips SAA7146A Product Spec (page 75):       */
                    /* "For interlaced input, ype and ypo is defiend as   */
                    /* YPeven= 3/2 x YPodd (line 1 = odd)"                */
                    /*                                                    */
                    /* It looks like the spec is wrong!                   */
                    /* The  ad hoc values below works fine for a target   */
                    /* window height of 480 (vertical scale = 1/1) NTSC.  */
                    /* PLI: December 27, 2000.                            */
                    ypo=64;
                    ype=0; 
                } else {
                    ype = ysci / 16;
                    ypo = ype + (ysci / 64);
                }
	}
	else {
		yacm = 1;	

		/* calculate scaling increment */
		ysci = (((10 * 1024 * (in_y - out_y - 1)) / in_y) + 9) / 10;

		/* calculate ype and ypo */
		ypo = ype = ((ysci + 15) / 16);

		/* the sequence length interval (yacl) has to be set according
		   to the prescale value, e.g.	[n   .. 1/2) : 0
		   				[1/2 .. 1/3) : 1
						[1/3 .. 1/4) : 2
						... */
		if ( ysci < 512) {
			yacl = 0;
		}
		else {
			yacl = ( ysci / (1024 - ysci) );
		}

		/* get filter coefficients for cya, cyb from table hps_v_coeff_tab */	
		cya_cyb = hps_v_coeff_tab[TRUNC(yacl, 63)].hps_coeff;

		/* get best match in the table of attenuations for vertical scaling */
		v_atten = hps_v_coeff_tab[TRUNC(yacl, 63)].weight_sum;

		for (i = 0; v_attenuation[i] != 0; i++) {
			if (v_attenuation[i] >= v_atten)
				break;
		}
	
		dcgy = i;
	}

	/* ypo and ype swapped in spec ? */
	*hps_v_scale	|= (yacm << 31) | (ysci << 21) | (yacl << 15) | (ypo << 8 ) | (ype << 1);

	*hps_v_gain	&= ~(MASK_W0|MASK_B2);
	*hps_v_gain	|= (dcgy << 16) | (cya_cyb << 0);

	return 0;
}

void calculate_hxo_hyo_and_sources(struct saa7146* saa, int port_sel, int sync_sel, u32* hps_h_scale, u32* hps_ctrl)
{
	u32 hyo = 0, hxo = 0;
	
	hyo = modes_constants[saa->mode].v_offset;
	hxo = modes_constants[saa->mode].h_offset;
				
	*hps_h_scale	&= ~(MASK_B0 | 0xf00);
	*hps_ctrl	&= ~(MASK_W0 | MASK_B2 | MASK_30 | MASK_31 | MASK_28);

	*hps_h_scale	|= (hxo <<  0);
	*hps_ctrl	|= (hyo << 12);

	*hps_ctrl	|= ( port_sel == 0 ? 0x0 : MASK_30);
	*hps_ctrl	|= ( sync_sel == 0 ? 0x0 : MASK_28);
}

void calculate_output_format_register(struct saa7146* saa, u16 palette, u32* clip_format)
{
	/* clear out the necessary bits */
	*clip_format &= 0x0000ffff;	
	/* set these bits new */
	*clip_format |=  (( ((palette&0xf00)>>8) << 30) | ((palette&0x00f) << 24) | (((palette&0x0f0)>>4) << 16));
}

void calculate_bcs_ctrl_register(struct saa7146 *saa, u32 brightness, u32 contrast, u32 colour, u32 *bcs_ctrl)
{
	*bcs_ctrl = ((brightness << 24) | (contrast << 16) | (colour <<  0));
}


int calculate_video_dma1_grab(struct saa7146* saa, int frame, struct saa7146_video_dma* vdma1) 
{
	int depth = 0;
	
	switch(saa->grab_format[frame]) {
		case YUV422_COMPOSED:
		case RGB15_COMPOSED:
		case RGB16_COMPOSED:
			depth = 2;
			break;
		case RGB24_COMPOSED:
			depth = 3;
			break;
		default:
			depth = 4;	
		break;
	}

	vdma1->pitch		= saa->grab_width[frame]*depth*2;
	vdma1->base_even	= 0;
	vdma1->base_odd		= vdma1->base_even + (vdma1->pitch/2);
	vdma1->prot_addr	= (saa->grab_width[frame]*saa->grab_height[frame]*depth)-1;
	vdma1->num_line_byte	= ((modes_constants[saa->mode].v_field<<16) + modes_constants[saa->mode].h_pixels);
	vdma1->base_page	= virt_to_bus(saa->page_table[frame]) | ME1;

	/* convention: if scaling is between 1/2 and 1/4 we only use
	   the even lines, the odd lines get discarded (see vertical scaling) */
	if( saa->interlace != 0 && saa->grab_height[frame]*4 >= modes_constants[saa->mode].v_calc && saa->grab_height[frame]*2 <= modes_constants[saa->mode].v_calc) {
		vdma1->base_odd = vdma1->prot_addr;
		vdma1->pitch /= 2;
	}

	return 0;
}

/* ---------------------------------------------*/
/* position of overlay-window			*/
/* ---------------------------------------------*/
		
/* calculate the new memory offsets for a desired position */
int move_to(struct saa7146* saa, int w_x, int w_y, int w_height, int b_width, int b_depth, int b_bpl, u32 base, int td_flip)
{	
	struct	saa7146_video_dma	vdma1;

	if( w_y < 0 || w_height <= 0 || b_depth <= 0 || b_bpl <= 0 || base == 0 ) {
		printk("saa7146: ==> calculate_video_dma1_overlay: illegal values: y: %d  h: %d  d: %d  b: %d  base: %d\n",w_y ,w_height,b_depth,b_bpl,base);
		return -EINVAL;
	}	

	/* calculate memory offsets for picture, look if we shall top-down-flip */
	vdma1.pitch	= 2*b_bpl;
	if ( 0 == td_flip ) {
		vdma1.prot_addr = (u32)base + ((w_height+w_y+1)*b_width*(b_depth/4));		
		vdma1.base_even = (u32)base + (w_y * (vdma1.pitch/2)) + (w_x * (b_depth / 8)); 
		vdma1.base_odd  = vdma1.base_even + (vdma1.pitch / 2);
	}
	else {
		vdma1.prot_addr = (u32)base + (w_y * (vdma1.pitch/2));
		vdma1.base_even = (u32)base + ((w_y+w_height) * (vdma1.pitch/2)) + (w_x * (b_depth / 8)); 
		vdma1.base_odd  = vdma1.base_even + (vdma1.pitch / 2);
		vdma1.pitch    *= -1;
	}	

	/* convention: if scaling is between 1/2 and 1/4 we only use
	   the even lines, the odd lines get discarded (see vertical scaling) */
	if( saa->interlace != 0 && w_height*4 >= modes_constants[saa->mode].v_calc && w_height*2 <= modes_constants[saa->mode].v_calc) {
		vdma1.base_odd = vdma1.prot_addr;
		vdma1.pitch /= 2;
	}
		
	vdma1.base_page = 0;
	vdma1.num_line_byte = (modes_constants[saa->mode].v_field<<16)+modes_constants[saa->mode].h_pixels;

	saa7146_write(saa->mem, BASE_EVEN1,     vdma1.base_even);
	saa7146_write(saa->mem, BASE_ODD1,      vdma1.base_odd);
	saa7146_write(saa->mem, PROT_ADDR1,     vdma1.prot_addr);
	saa7146_write(saa->mem, BASE_PAGE1,     vdma1.base_page);
	saa7146_write(saa->mem, PITCH1,		vdma1.pitch);
	saa7146_write(saa->mem, NUM_LINE_BYTE1,	vdma1.num_line_byte);

	/* update the video dma 1 registers */
      	saa7146_write(saa->mem, MC2, (MASK_02 | MASK_18));
	
	return 0;

}

/* ---------------------------------------------*/
/* size of window (overlay)			*/
/* ---------------------------------------------*/

int set_window(struct saa7146* saa, int width, int height, int flip_lr, int port_sel, int sync_sel)
{
	u32 hps_v_scale = 0, hps_v_gain  = 0, hps_ctrl = 0, hps_h_prescale = 0, hps_h_scale = 0;

	/* set vertical scale according to selected mode: 0 = PAL, 1 = NTSC */
	hps_v_scale = 0; /* all bits get set by the function-call */
	hps_v_gain  = 0; /* fixme: saa7146_read(saa->mem, HPS_V_GAIN);*/ 
	calculate_v_scale_registers(saa, modes_constants[saa->mode].v_calc, height, &hps_v_scale, &hps_v_gain);

	/* set horizontal scale according to selected mode: 0 = PAL, 1 = NTSC */
	hps_ctrl 	= 0;
	hps_h_prescale	= 0; /* all bits get set in the function */
	hps_h_scale	= 0;
	calculate_h_scale_registers(saa, modes_constants[saa->mode].h_calc, width, 0, &hps_ctrl, &hps_v_gain, &hps_h_prescale, &hps_h_scale);

	/* set hyo and hxo */
	calculate_hxo_hyo_and_sources(saa, port_sel, sync_sel, &hps_h_scale, &hps_ctrl);
	
	/* write out new register contents */
	saa7146_write(saa->mem, HPS_V_SCALE,	hps_v_scale);
	saa7146_write(saa->mem, HPS_V_GAIN,	hps_v_gain);
	saa7146_write(saa->mem, HPS_CTRL,	hps_ctrl);
	saa7146_write(saa->mem, HPS_H_PRESCALE,hps_h_prescale);
	saa7146_write(saa->mem, HPS_H_SCALE,	hps_h_scale);

	/* upload shadow-ram registers */
      	saa7146_write( saa->mem, MC2, (MASK_05 | MASK_06 | MASK_21 | MASK_22) );

/*
	printk("w:%d,h:%d\n",width,height);
*/
	return 0;

}

void set_output_format(struct saa7146* saa, u16 palette)
{
	u32	clip_format = saa7146_read(saa->mem, CLIP_FORMAT_CTRL);
	
	dprintk("saa7146: ==> set_output_format: pal:0x%03x\n",palette);
	
	/* call helper function */
	calculate_output_format_register(saa,palette,&clip_format);
	dprintk("saa7146: ==> set_output_format: 0x%08x\n",clip_format);

	/* update the hps registers */
	saa7146_write(saa->mem, CLIP_FORMAT_CTRL, clip_format);
      	saa7146_write(saa->mem, MC2, (MASK_05 | MASK_21));
}

void set_picture_prop(struct saa7146 *saa, u32 brightness, u32 contrast, u32 colour)
{
	u32	bcs_ctrl = 0;
	
	calculate_bcs_ctrl_register(saa, brightness, contrast, colour, &bcs_ctrl);
	saa7146_write(saa->mem, BCS_CTRL, bcs_ctrl);
	
	/* update the bcs register */
      	saa7146_write(saa->mem, MC2, (MASK_06 | MASK_22));
}

/* ---------------------------------------------*/
/* overlay enable/disable			*/
/* ---------------------------------------------*/

/* enable(1) / disable(0) video */
void video_setmode(struct saa7146* saa, int v)
{
	hprintk("saa7146: ==> video_setmode; m:%d\n",v);
	
	/* disable ? */
	if(v==0) {
		/* disable video dma1 */
      		saa7146_write(saa->mem, MC1, MASK_22);
	} else {/* or enable ? */
		/* fixme: enable video */
	        saa7146_write(saa->mem, MC1, (MASK_06 | MASK_22));
	}
}		

/* -----------------------------------------------------
   common grabbing-functions. if you have some simple
   saa7146-based frame-grabber you can most likely call
   these. they do all the revision-dependend stuff and
   do rps/irq-based grabbing for you.
   -----------------------------------------------------*/

/* this function initializes the rps for the next grab for any "old"
   saa7146s (= revision 0). it assumes that the rps is *not* running
   when it gets called. */
int init_rps0_rev0(struct saa7146* saa, int frame, int irq_call)
{
	struct	saa7146_video_dma	vdma1;
	u32 hps_v_scale = 0, hps_v_gain  = 0, hps_ctrl = 0, hps_h_prescale = 0, hps_h_scale = 0;
	u32 clip_format = 0; /* this can be 0, since we don't do clipping */
	u32 bcs_ctrl = 0;
	
	int count = 0;

/* these static values are used to remember the last "programming" of the rps.
   if the height, width and format of the grab has not changed (which is very likely
   when some streaming capture is done) the reprogramming overhead can be minimized */
static	int last_height = 0;
static	int last_width = 0;
static	int last_format = 0;
static	int last_port = 0;
static	int last_frame = -1;

	/* write the address of the rps-program */
	saa7146_write(saa->mem, RPS_ADDR0, virt_to_bus(&saa->rps0[ 0]));

	/* let's check if we can re-use something of the last grabbing process */
	if (	   saa->grab_height[frame] != last_height
		|| saa->grab_width[frame] != last_width
		|| saa->grab_port[frame] != last_port
		|| saa->grab_format[frame] != last_format ) {

		/* nope, we have to start from the beginning */
		calculate_video_dma1_grab(saa, frame, &vdma1);
		calculate_v_scale_registers(saa, modes_constants[saa->mode].v_calc, saa->grab_height[frame], &hps_v_scale, &hps_v_gain);
		calculate_h_scale_registers(saa, modes_constants[saa->mode].h_calc, saa->grab_width[frame], 0, &hps_ctrl, &hps_v_gain, &hps_h_prescale, &hps_h_scale);
		calculate_hxo_hyo_and_sources(saa, saa->grab_port[frame], saa->grab_port[frame], &hps_h_scale, &hps_ctrl);
		calculate_output_format_register(saa,saa->grab_format[frame],&clip_format);
		calculate_bcs_ctrl_register(saa, 0x80, 0x40, 0x40, &bcs_ctrl);

		count = 0;

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG_MASK | (MC1/4));		/* turn off video-dma1 and dma2 (clipping)*/
		saa->rps0[ count++ ] = cpu_to_le32(MASK_06 | MASK_22 | MASK_05 | MASK_21);	/* => mask */
		saa->rps0[ count++ ] = cpu_to_le32(MASK_22 | MASK_21);			/* => values */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | ( saa->grab_port[frame] == 0 ? MASK_12 : MASK_14));	/* wait for o_fid_a/b */
		saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | ( saa->grab_port[frame] == 0 ? MASK_11 : MASK_13));	/* wait for e_fid_a/b */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG | (6 << 8) | HPS_CTRL/4);	/* upload hps-registers for next grab */
		saa->rps0[ count++ ] = cpu_to_le32(hps_ctrl);
		saa->rps0[ count++ ] = cpu_to_le32(hps_v_scale);
		saa->rps0[ count++ ] = cpu_to_le32(hps_v_gain);
		saa->rps0[ count++ ] = cpu_to_le32(hps_h_prescale);
		saa->rps0[ count++ ] = cpu_to_le32(hps_h_scale);
		saa->rps0[ count++ ] = cpu_to_le32(bcs_ctrl);

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG | (1 << 8) | CLIP_FORMAT_CTRL/4);/* upload hps-registers for next grab */
		saa->rps0[ count++ ] = cpu_to_le32(clip_format);

		saa->rps0[ count++ ] = cpu_to_le32(CMD_UPLOAD | MASK_05 | MASK_06);		/* upload hps1/2 */

		/* upload video-dma1 registers for next grab */
		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG | (6 << 8) | BASE_ODD1/4);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.base_odd);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.base_even);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.prot_addr);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.pitch);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.base_page);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.num_line_byte);

		saa->rps0[ count++ ] = cpu_to_le32(CMD_UPLOAD | MASK_02);		/* upload video-dma1 */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG_MASK | (MC1/4));		/* turn on video-dma1 */
		saa->rps0[ count++ ] = cpu_to_le32(MASK_06 | MASK_22);	    		/* => mask */
		saa->rps0[ count++ ] = cpu_to_le32(MASK_06 | MASK_22);			/* => values */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG | (1 << 8) | (MC2/4)); 	/* Write MC2 */
		saa->rps0[ count++ ] = cpu_to_le32((1 << (27+frame)) | (1 << (11+frame)));

		saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | ( saa->grab_port[frame] == 0 ? MASK_12 : MASK_14));	/* wait for o_fid_a/b */
		saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | ( saa->grab_port[frame] == 0 ? MASK_11 : MASK_13));	/* wait for e_fid_a/b */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG_MASK | (MC1/4));     		/* turn off video-dma1 */
		saa->rps0[ count++ ] = cpu_to_le32(MASK_06 | MASK_22);	    		/* => mask */
		saa->rps0[ count++ ] = cpu_to_le32(MASK_22);					/* => values */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_INTERRUPT);				/* generate interrupt */
		saa->rps0[ count++ ] = cpu_to_le32(CMD_STOP);				/* stop processing */	
	} else {
	
		/* the height, width, ... have not changed. check if the user wants to grab to
		   another *buffer* */
		if( frame != last_frame ) {

			/* ok, we want to grab to another buffer, but with the same programming.
			   it is sufficient to adjust the video_dma1-registers and the rps-signal stuff. */
			saa->rps0[ 20 ] = cpu_to_le32(virt_to_bus(saa->page_table[frame]) | ME1);
			saa->rps0[ 27 ] = cpu_to_le32((1 << (27+frame)) | (1 << (11+frame)));
	
		}
	 }
	 
	/* if we are called from within the irq-handler, the hps is at the beginning of a
	   new frame. the rps does not need to wait the new frame, and so we tweak the
	   starting address a little bit and so we can directly start grabbing again.
	   note: for large video-sizes and slow computers this can cause jaggy pictures
	   because the whole process is not in sync. perhaps one should be able to
	   disable this. (please remember that this whole stuff only belongs to 
	   "old" saa7146s (= revision 0), newer saa7146s don´t have any hardware-bugs
	   and capture works fine. (see below) */   
	if( 1 == irq_call ) {
		saa7146_write(saa->mem, RPS_ADDR0, virt_to_bus(&saa->rps0[15]));
	}	
	
	/* turn on rps */
	saa7146_write(saa->mem, MC1, (MASK_12 | MASK_28));	

	/* store the values for the last grab */
	last_height = saa->grab_height[frame];
	last_width = saa->grab_width[frame];
	last_format = saa->grab_format[frame];
	last_port = saa->grab_port[frame];
	last_frame = frame;

	return 0;
}

int init_rps0_rev1(struct saa7146* saa, int frame) {

static	int old_width[SAA7146_MAX_BUF];		/* pixel width of grabs */
static	int old_height[SAA7146_MAX_BUF];	/* pixel height of grabs */
static	int old_format[SAA7146_MAX_BUF];	/* video format of grabs */
static	int old_port[SAA7146_MAX_BUF];		/* video port for grab */
	
static	int buf_stat[SAA7146_MAX_BUF];

	struct	saa7146_video_dma	vdma1;
	u32 hps_v_scale = 0, hps_v_gain  = 0, hps_ctrl = 0, hps_h_prescale = 0, hps_h_scale = 0;
	u32 clip_format = 0; /* this can be 0, since we don't do clipping */
	u32 bcs_ctrl = 0;

	int i = 0, count = 0;

	/* check if something has changed since the last grab for this buffer */
	if ( 	   saa->grab_height[frame]	== old_height[frame]
		&& saa->grab_width[frame]	== old_width[frame]
		&& saa->grab_port[frame] 	== old_port[frame]
		&& saa->grab_format[frame]	== old_format[frame] ) {

		/* nope, nothing to be done here */
		return 0;
	}

	/* re-program the rps0 completely */

	/* indicate that the user has requested re-programming of the 'frame'-buffer */
	buf_stat[frame] = 1;

	/* turn off rps */
	saa7146_write(saa->mem, MC1, MASK_28);	


	/* write beginning of rps-program */
	count = 0;
	saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | MASK_12);				/* wait for o_fid_a */
	saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | MASK_11);				/* wait for e_fid_a */
	for(i = 0; i < saa->buffers; i++) {		
		saa->rps0[ count++ ] = cpu_to_le32(CMD_JUMP  | (1 << (21+i)));		/* check signal x, jump if set */
		saa->rps0[ count++ ] = cpu_to_le32(virt_to_bus(&saa->rps0[40*(i+1)]));
	}
	saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | MASK_12);				/* wait for o_fid_a */
	saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | MASK_11);				/* wait for e_fid_a */
	saa->rps0[ count++ ] = cpu_to_le32(CMD_JUMP);					/* jump to the beginning */
	saa->rps0[ count++ ] = cpu_to_le32(virt_to_bus(&saa->rps0[2]));
		
	for(i = 0; i < saa->buffers; i++) {

		/* we only re-program the i-th buffer if the user had set some values for it earlier.
		   otherwise the calculation-functions may fail. */
		if( buf_stat[i] == 0)
			continue;

		count = 40*(i+1);

		calculate_video_dma1_grab(saa, i, &vdma1);
		calculate_v_scale_registers(saa, modes_constants[saa->mode].v_calc, saa->grab_height[i], &hps_v_scale, &hps_v_gain);
		calculate_h_scale_registers(saa, modes_constants[saa->mode].h_calc, saa->grab_width[i], 0, &hps_ctrl, &hps_v_gain, &hps_h_prescale, &hps_h_scale);
		calculate_hxo_hyo_and_sources(saa, saa->grab_port[i], saa->grab_port[i], &hps_h_scale, &hps_ctrl);
		calculate_output_format_register(saa,saa->grab_format[i],&clip_format);
		calculate_bcs_ctrl_register(saa, 0x80, 0x40, 0x40, &bcs_ctrl);
	
		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG | (6 << 8) | HPS_CTRL/4);	/* upload hps-registers for next grab */
		saa->rps0[ count++ ] = cpu_to_le32(hps_ctrl);
		saa->rps0[ count++ ] = cpu_to_le32(hps_v_scale);
		saa->rps0[ count++ ] = cpu_to_le32(hps_v_gain);
		saa->rps0[ count++ ] = cpu_to_le32(hps_h_prescale);
		saa->rps0[ count++ ] = cpu_to_le32(hps_h_scale);
		saa->rps0[ count++ ] = cpu_to_le32(bcs_ctrl);

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG | (1 << 8) | CLIP_FORMAT_CTRL/4);/* upload hps-registers for next grab */
		saa->rps0[ count++ ] = cpu_to_le32(clip_format);	

		saa->rps0[ count++ ] = cpu_to_le32(CMD_UPLOAD | MASK_05 | MASK_06);		/* upload hps1/2 */
	
		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG | (6 << 8) | BASE_ODD1/4);	/* upload video-dma1 registers for next grab */
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.base_odd);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.base_even);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.prot_addr);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.pitch);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.base_page);
		saa->rps0[ count++ ] = cpu_to_le32(vdma1.num_line_byte);

		saa->rps0[ count++ ] = cpu_to_le32(CMD_UPLOAD | MASK_02);		/* upload video-dma1 */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG_MASK | (MC1/4));		/* turn on video-dma1 */
		saa->rps0[ count++ ] = cpu_to_le32(MASK_06 | MASK_22);	    		/* => mask */
		saa->rps0[ count++ ] = cpu_to_le32(MASK_06 | MASK_22);			/* => values */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | ( saa->grab_port[i] == 0 ? MASK_12 : MASK_14));	/* wait for o_fid_a/b */
		saa->rps0[ count++ ] = cpu_to_le32(CMD_PAUSE | ( saa->grab_port[i] == 0 ? MASK_11 : MASK_13));	/* wait for e_fid_a/b */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG_MASK | (MC1/4));		/* turn off video-dma1 and dma2 (clipping)*/
		saa->rps0[ count++ ] = cpu_to_le32(MASK_06 | MASK_22 | MASK_05 | MASK_21);	/* => mask */
		saa->rps0[ count++ ] = cpu_to_le32(MASK_22 | MASK_21);			/* => values */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_WR_REG | (1 << 8) | (MC2/4)); 	/* Write MC2 */
		saa->rps0[ count++ ] = cpu_to_le32((1 << (27+i)));
		saa->rps0[ count++ ] = cpu_to_le32(CMD_INTERRUPT);				/* generate interrupt */

		saa->rps0[ count++ ] = cpu_to_le32(CMD_JUMP);	/* jump to the beginning */
		saa->rps0[ count++ ] = cpu_to_le32(virt_to_bus(&saa->rps0[2]));

		old_height[frame] = saa->grab_height[frame];
		old_width[frame]  = saa->grab_width[frame];
		old_port[frame]	  = saa->grab_port[frame];
		old_format[frame] = saa->grab_format[frame];
	}

	/* write the address of the rps-program */
	saa7146_write(saa->mem, RPS_ADDR0, virt_to_bus(&saa->rps0[ 0]));
	/* turn on rps again */
	saa7146_write(saa->mem, MC1, (MASK_12 | MASK_28));	

	return 0;
}

/* this funtion is called whenever a new grab is requested. if possible (that
   means: if the rps is not running) it re-programs the rps, otherwise it relys on
   the irq-handler to do that */
int set_up_grabbing(struct saa7146* saa, int frame)
{
	u32 mc1 = 0;

	if( 0 == saa->revision ) {

		/* check if the rps is currently in use */
		mc1   = saa7146_read(saa->mem, MC1);

		/* the rps is not running ... */
		if( 0 == ( mc1 & MASK_12) ) {

			/* we can completly re-program the rps now */
			dprintk("saa7146_v4l.o: ==> set_up_grabbing: start new rps.\n");
			init_rps0_rev0(saa,frame,0);
		} else {
	
			/* the rps is running. in this case, the irq-handler is responsible for
			   re-programming the rps and nothing can be done right now */
			dprintk("saa7146_v4l.o: ==> set_up_grabbing: no new rps started.\n");
		}
	} else {
			/* check if something has changed, reprogram if necessary */
			init_rps0_rev1(saa,frame);
			/* set rps-signal-bit to start grabbing */
			saa7146_write(saa->mem, MC2, (1 << (27+frame)) |  (1 << (11+frame)));
	}
	
	return 0;	
}


void saa7146_std_grab_irq_callback_rps0(struct saa7146* saa, u32 isr, void* data)
{
	u32 mc2 = 0;
	int i = 0;
		
	hprintk("saa7146_v4l.o: ==> saa7146_v4l_irq_callback_rps0\n");

	/* check for revision: old revision */
	if( 0 == saa->revision ) {

		/* look what buffer has been grabbed, set the ´done´-flag and clear the signal */
		mc2   = saa7146_read(saa->mem, MC2);
		for( i = 0; i < saa->buffers; i++ ) {

			if ((0 != (mc2 & (1 << (11+i)))) && (GBUFFER_GRABBING == saa->frame_stat[i])) {
				saa->frame_stat[i] = GBUFFER_DONE;
				saa7146_write(saa->mem, MC2, (1<<(27+i)));
			}
		}

		/* look if there is another buffer we can grab to */
		for( i = 0; i < saa->buffers; i++ ) {
			if ( GBUFFER_GRABBING == saa->frame_stat[i] )
					break;
		}

		/* yes, then set up the rps again */
		if( saa->buffers != i) {
			init_rps0_rev0(saa,i,1);
		}
 	} else {
		/* new revisions */

		/* look what buffer has been grabbed, set the ´done´-flag */
		mc2   = saa7146_read(saa->mem, MC2);
		for( i = 0; i < saa->buffers; i++ ) {

			if ((0 == (mc2 & (1 << (11+i)))) && (GBUFFER_GRABBING == saa->frame_stat[i])) {
				saa->frame_stat[i] = GBUFFER_DONE;
			}
		}
				
	}
	/* notify any pending process */
	wake_up_interruptible(&saa->rps0_wq);
	return;
}

/* ---------------------------------------------*/
/* mask-clipping				*/
/* ---------------------------------------------*/
int calculate_clipping_registers_mask(struct saa7146* saa, u32 width, u32 height, struct saa7146_video_dma* vdma2, u32* clip_format, u32* arbtr_ctrl)
{ 
	u32 clip_addr = 0, clip_pitch = 0;

	dprintk("saa7146: ==> calculate_clipping_registers_mask\n");

	/* adjust arbitration control register */
	*arbtr_ctrl &= 0xffff00ff;
	*arbtr_ctrl |= 0x00001000;	

	clip_addr	= virt_to_bus(saa->clipping);
	clip_pitch = ((width+31)/32)*4;

	vdma2->base_even	= clip_addr;	
	vdma2->base_page	= 0x04;	/* enable read - operation */
	vdma2->prot_addr	= clip_addr + (clip_pitch*height);

	/* convention: if scaling is between 1/2 and 1/4 we only use
	   the even lines, the odd lines get discarded (see vertical scaling) */
	if( saa->interlace != 0 && height*4 >= modes_constants[saa->mode].v_calc && height*2 <= modes_constants[saa->mode].v_calc) {
		vdma2->base_odd		= vdma2->prot_addr;
		vdma2->pitch		= clip_pitch;
		vdma2->num_line_byte	= (((height)-1) << 16) | (clip_pitch-1);
	} else {
		vdma2->base_odd		= clip_addr+clip_pitch;
		vdma2->pitch		= clip_pitch*2;
		vdma2->num_line_byte	= (((height/2)-1) << 16) | (clip_pitch-1);
	}

	*clip_format &= 0xfffffff7;

	return 0;
}

/* helper functions for emulate rect-clipping via mask-clipping.
   note: these are extremely inefficient, but for clipping with less than 16
   windows rect-clipping should be done anyway...
*/

/* clear one pixel of the clipping memory at position (x,y) */
void set_pixel(s32 x, s32 y, s32 window_width, u32* mem) {

	u32 mem_per_row = 0;
	u32 adr		= 0;
	u32 shift 	= 0;
	u32 bit 	= 0;

	mem_per_row 	= (window_width + 31 )/ 32 ;
	adr 		= y * mem_per_row + (x / 32);
	shift 		= 31 - (x % 32);
	bit 		= (1 << shift);
	
	mem[adr] |= bit;	
}

/* clear a box out of the clipping memory, beginning at (x,y) with "width" and "height" */
void set_box(s32 x, s32 y, s32 width, s32 height, s32 window_width, s32 window_height, u32* mem)
{
	s32 ty = 0;
	s32 tx = 0;

	/* the video_clip-struct may contain negative values to indicate that a window
	   doesn't lay completly over the video window. Thus, we correct the values */
	
	if( width < 0) {
		x += width; width = -width;
	}
	if( height < 0) {
		y += height; height = -height;
	}

	if( x < 0) {
		width += x; x = 0;
	}
	if( y < 0) {
		height += y; y = 0;
	}

	if( width <= 0 || height <= 0) {
 		printk("saa7146: ==> set_box: sanity error!\n");
		return;
	}			

	if(x + width > window_width)
		width -= (x + width) - window_width;
	if(y + height > window_height)
		height -= (y + height) - window_height;

	/* Now, set a '1' in the memory, where no video picture should appear */
	for(ty = y; ty < y+height; ty++) {		
		for(tx = x; tx < x+width; tx++) {
			set_pixel(tx, ty, window_width, mem);
		}
	}
}

int emulate_rect_clipping(struct saa7146 *saa, u16 clipcount, int x[], int y[], int w[], int h[], u32 w_width, u32 w_height)
{
	int i = 0;
	
	/* clear out clipping mem */
	memset(saa->clipping, 0x0, CLIPPING_MEM_SIZE*sizeof(u32));

	/* go through list of clipping-windows, clear out rectangular-regions in the clipping memory */
	for(i = 0; i < clipcount; i++) {
		set_box(x[i], y[i], w[i], h[i], w_width, w_height, saa->clipping);
	}		

	return 0;
}

/* ---------------------------------------------*/
/* rectangle-clipping				*/
/* ---------------------------------------------*/

#define MIN(x,y)	( ((x) < (y)) ? (x) : (y) )
#define MAX(x,y)	( ((x) > (y)) ? (x) : (y) )

/* simple double-sort algorithm with duplicate elimination */
int sort_and_eliminate(u32* values, int* count)
{
	int low = 0, high = 0, top = 0, temp = 0;
	int cur = 0, next = 0;
	
	/* sanity checks */
	if( (0 > *count) || (NULL == values) ) {
		printk("saa7146: ==> sort_and_eliminate: internal error #1\n");
		return -EINVAL;
	}
	
	/* bubble sort the first ´count´ items of the array ´values´ */
	for( top = *count; top > 0; top--) {
		for( low = 0, high = 1; high < top; low++, high++) {
			if( values[low] > values[high] ) {
				temp = values[low];
				values[low] = values[high];
				values[high] = temp;
			}
		}
	}

	/* remove duplicate items */
	for( cur = 0, next = 1; next < *count; next++) {
		if( values[cur] != values[next])
			values[++cur] = values[next];
	}
	
	*count = cur + 1;
	 
	return 0;
}

int calculate_clipping_registers_rect(struct saa7146 *saa, int clipcount, int x[], int y[], int w[], int h[], u32 width, u32 height, struct saa7146_video_dma* vdma2, u32* clip_format, u32* arbtr_ctrl)
{
	u32 line_list[32];			
	u32 pixel_list[32];
	u32 numdwords = 0;

	int i = 0, j = 0;
	int l = 0, r = 0, t = 0, b = 0;
	int cnt_line = 0, cnt_pixel = 0;

	dprintk("saa7146: ==> calculate_clipping_registers_clip\n");
							
	/* clear out memory */
	memset(&line_list[0],  0x00, sizeof(u32)*32);
	memset(&pixel_list[0], 0x00, sizeof(u32)*32);
	memset(saa->clipping,  0x00, sizeof(u32)*CLIPPING_MEM_SIZE);

	/* fill the line and pixel-lists */
	for(i = 0; i < clipcount; i++) {

		/* calculate values for l(eft), r(ight), t(op), b(ottom) */
		l = x[i];
		r = x[i]+w[i];
		t = y[i];
		b = y[i]+h[i];

		/* insert left/right coordinates */
		pixel_list[ 2*i   ] = MIN(l, width);
		pixel_list[(2*i)+1] = MIN(r, width);
		/* insert top/bottom coordinates */
		line_list[ 2*i   ] = MIN(t, height);
		line_list[(2*i)+1] = MIN(b, height);
	}

	/* sort and eliminate lists */
	cnt_line = cnt_pixel = 2*clipcount;
	sort_and_eliminate( &pixel_list[0], &cnt_pixel );
	sort_and_eliminate( &line_list[0], &cnt_line );

	/* calculate the number of used u32s */
	numdwords = MAX( (cnt_line+1), (cnt_pixel+1))*2; 
	numdwords = MAX(4, numdwords);
	numdwords = MIN(64, numdwords);

	/* fill up cliptable */
	for(i = 0; i < cnt_pixel; i++) {
		saa->clipping[2*i] |= (pixel_list[i] << 16);
	}
	for(i = 0; i < cnt_line; i++) {
		saa->clipping[(2*i)+1] |= (line_list[i] << 16);
	}

	/* fill up cliptable with the display infos */
	for(j = 0; j < clipcount; j++) {

		for(i = 0; i < cnt_pixel; i++) {

			if( x[j] < 0)
				x[j] = 0;

			if( pixel_list[i] < (x[j] + w[j])) {
			
				if ( pixel_list[i] >= x[j] ) {
					saa->clipping[2*i] |= (1 << j);			
				}
			}
		}
		for(i = 0; i < cnt_line; i++) {

			if( y[j] < 0)
				y[j] = 0;

			if( line_list[i] < (y[j] + h[j]) ) {

				if( line_list[i] >= y[j] ) {
					saa->clipping[(2*i)+1] |= (1 << j);			
				}
			}
		}
	}

	/* adjust arbitration control register */
	*arbtr_ctrl &= 0xffff00ff;
	*arbtr_ctrl |= 0x00001c00;	
	
	vdma2->base_even	= virt_to_bus(saa->clipping);
	vdma2->base_odd		= virt_to_bus(saa->clipping);
	vdma2->prot_addr	= virt_to_bus(saa->clipping)+((sizeof(u32))*(numdwords));
	vdma2->base_page	= 0x04;
	vdma2->pitch		= 0x00;
	vdma2->num_line_byte	= (0 << 16 | (sizeof(u32))*(numdwords-1) );

	/* set clipping-mode. please note again, that for sizes below 1/2, we only use the
	   even-field. because of this, we have to specify ´recinterl´ correctly (specs, p. 97)*/
	*clip_format &= 0xfffffff7;

	if( saa->interlace != 0 && height*4 >= modes_constants[saa->mode].v_calc && height*2 <= modes_constants[saa->mode].v_calc) {
		*clip_format |= 0x00000000;
	} else {
		*clip_format |= 0x00000008;
	}
	return 0;
}


/* ---------------------------------------------*/
/* main function for clipping			*/
/* ---------------------------------------------*/
/* arguments:
	type 	= see ´saa7146.h´
	width 	= width of the video-window
	height 	= height of the video-window
	*mask 	= pointer to mask memory	(only needed for mask-clipping)
	*clips	= pointer to clip-window-list   (only needed for rect-clipping)
	clipcount = # of clip-windows		(only needed for rect-clipping)
*/
int clip_windows(struct saa7146* saa, u32 type, u32 width, u32 height, u32* mask, u16 clipcount, int x[], int y[], int w[], int h[]) 
{
	struct	saa7146_video_dma	vdma2;

	u32 clip_format	= saa7146_read(saa->mem, CLIP_FORMAT_CTRL);
	u32 arbtr_ctrl	= saa7146_read(saa->mem, PCI_BT_V1);

	hprintk("saa7146: ==> clip_windows\n");
	
	/* some sanity checks first */
	if ( width <= 0 || height <= 0 ) {
		printk("saa7146: ==> clip_windows: sanity error #1!\n");
		return -EINVAL;
	}

	/* check if anything to do here, disable clipping if == 0 */
	if( clipcount == 0 ) {
	
		/* mask out relevant bits (=lower word)*/
		clip_format &= MASK_W1;

		/* upload clipping-registers*/
		saa7146_write(saa->mem, CLIP_FORMAT_CTRL,clip_format);
 		saa7146_write(saa->mem, MC2, (MASK_05 | MASK_21));
 
 		/* disable video dma2 */
		saa7146_write(saa->mem, MC1, (MASK_21));

		return 0;
	}

	switch(type) {
		
		case SAA7146_CLIPPING_MASK_INVERTED:
		case SAA7146_CLIPPING_MASK:
		{
		  printk("mask\n");
			/* sanity check */
			if( NULL == mask ) {
				printk("saa7146: ==> clip_windows: sanity error #1!\n");
				return -EINVAL;
			}
			
			/* copy the clipping mask to structure */
			memmove(saa->clipping, mask, CLIPPING_MEM_SIZE*sizeof(u32));
			/* set clipping registers */
			calculate_clipping_registers_mask(saa,width,height,&vdma2,&clip_format,&arbtr_ctrl);

			break;
		}

		case SAA7146_CLIPPING_RECT_INVERTED:
		case SAA7146_CLIPPING_RECT:
		{
			/* see if we have anything to do */
			if ( 0 == clipcount ) {
				return 0;	
			}

			/* sanity check */
			if( NULL == x || NULL == y || NULL == w || NULL == h ) {
				printk("saa7146: ==> clip_windows: sanity error #2!\n");
				return -EINVAL;
			}

			/* rectangle clipping can only handle 16 overlay windows; if we
			   have more, we have do emulate the whole thing with mask-clipping */
			if (1) { //clipcount >  > 16 ) {
			  //printk("emulate\n");
				emulate_rect_clipping(saa, clipcount, x,y,w,h, width, height);
				calculate_clipping_registers_mask(saa,width,height,&vdma2,&clip_format,&arbtr_ctrl);
				if( SAA7146_CLIPPING_RECT == type )
					type = SAA7146_CLIPPING_MASK;
				else
					type = SAA7146_CLIPPING_MASK_INVERTED;
				
			}
			else {
				calculate_clipping_registers_rect(saa,clipcount,x,y,w,h,width,height,&vdma2,&clip_format,&arbtr_ctrl);
			}
			
			break;
		}

		default:
		{
			printk("saa7146: ==> clip_windows: internal error #1!\n");
			return -EINVAL;
		}
	
	}

	/* set clipping format */
	clip_format &= 0xffff0008;
	clip_format |= (type << 4);

	saa7146_write(saa->mem, BASE_EVEN2,     vdma2.base_even);
	saa7146_write(saa->mem, BASE_ODD2,      vdma2.base_odd);
	saa7146_write(saa->mem, PROT_ADDR2,     vdma2.prot_addr);
	saa7146_write(saa->mem, BASE_PAGE2,     vdma2.base_page);
	saa7146_write(saa->mem, PITCH2,		vdma2.pitch);
	saa7146_write(saa->mem, NUM_LINE_BYTE2,	vdma2.num_line_byte);

	saa7146_write(saa->mem, CLIP_FORMAT_CTRL,clip_format);
	saa7146_write(saa->mem, PCI_BT_V1, arbtr_ctrl);	

	/* upload clip_control-register, clipping-registers, enable video dma2 */
	saa7146_write(saa->mem, MC2, (MASK_05 | MASK_21 | MASK_03 | MASK_19));
	saa7146_write(saa->mem, MC1, (MASK_05 | MASK_21));
/*
       	printk("ARBTR_CTRL:     0x%08x\n",saa7146_read(saa->mem, PCI_BT_V1));
       	printk("CLIP_FORMAT:    0x%08x\n",saa7146_read(saa->mem, CLIP_FORMAT_CTRL));
        printk("BASE_ODD1:      0x%08x\n",saa7146_read(saa->mem, BASE_ODD1));
        printk("BASE_EVEN1:     0x%08x\n",saa7146_read(saa->mem, BASE_EVEN1));
        printk("PROT_ADDR1:     0x%08x\n",saa7146_read(saa->mem, PROT_ADDR1));
        printk("PITCH1:         0x%08x\n",saa7146_read(saa->mem, PITCH1));
        printk("BASE_PAGE1:     0x%08x\n",saa7146_read(saa->mem, BASE_PAGE1));
        printk("NUM_LINE_BYTE1: 0x%08x\n",saa7146_read(saa->mem, NUM_LINE_BYTE1));
        printk("BASE_ODD2:      0x%08x\n",saa7146_read(saa->mem, BASE_ODD2));
        printk("BASE_EVEN2:     0x%08x\n",saa7146_read(saa->mem, BASE_EVEN2));
        printk("PROT_ADDR2:     0x%08x\n",saa7146_read(saa->mem, PROT_ADDR2));
        printk("PITCH2:         0x%08x\n",saa7146_read(saa->mem, PITCH2));
        printk("BASE_PAGE2:     0x%08x\n",saa7146_read(saa->mem, BASE_PAGE2));
        printk("NUM_LINE_BYTE2: 0x%08x\n",saa7146_read(saa->mem, NUM_LINE_BYTE2));
*/
	return 0;

}		
#endif

#ifdef __COMPILE_SAA7146_I2C__

/* ---------------------------------------------*/
/* i2c-helper functions				*/
/* ---------------------------------------------*/

/* this functions gets the status from the saa7146 at address 'addr'
   and returns it */
u32 i2c_status_check(struct saa7146* saa) 
{
	u32 iicsta = 0;
	
	iicsta = saa7146_read(saa->mem, I2C_STATUS );
	hprintk("saa7146: ==> i2c_status_check:0x%08x\n",iicsta);

	return iicsta;
}

/* this function should be called after an i2c-command has been written. 
   if we are debugging, it checks, if the busy flags rises and falls correctly
   and reports a timeout (-1) or the error-bits set like in described in the specs,
   p.123, table 110 */
int i2c_busy_rise_and_fall(struct saa7146* saa, int timeout)
{
	int i = 0;
	u32 status = 0;
		
	hprintk("saa7146: ==> i2c_busy_rise_and_fall\n");

	/* wait until busy-flag rises */
	for (i = 5; i > 0; i--) {

		hprintk("saa7146: i2c_busy_rise_and_fall; rise wait %d\n",i);

		status = i2c_status_check(saa);

		/* check busy flag */
		if ( 0 != (status & SAA7146_I2C_BUSY))
			break;

		/* see if anything can be done while we're waiting */
		cond_resched ();
		mdelay(1);
	}

	/* we don't check the i-value, since it does not matter
	   if we missed the rise of the busy flag or the fall or
	   whatever. we just have to wait some undefined time
	   after an i2c-command has been written out */	
	
	/* wait until busy-flag is inactive or error is reported */
	for (i = timeout; i > 0; i--) {
	
		hprintk("saa7146: i2c_busy_rise_and_fall; fall wait %d\n",i);

		status = i2c_status_check(saa);

		/* check busy flag */
		if ( 0 == (status & SAA7146_I2C_BUSY))
			break;

		/* check error flag */
		if ( 0 != (status & SAA7146_I2C_ERR))
			break;

		/* see if anything can be done while we're waiting */
		cond_resched ();

		mdelay(1);
	}
	
	/* did a timeout occur ? */
	if ( 0 == i ) {
		hprintk("saa7146: i2c_busy_rise_and_fall: timeout #2\n");
		return -1;
	}
	
	/* report every error pending */
	switch( status & 0xfc ) {
		
		case SAA7146_I2C_SPERR:
			hprintk("saa7146: i2c_busy_rise_and_fall: error due to invalid start/stop condition\n");
			break;

		case SAA7146_I2C_APERR:
			hprintk("saa7146: i2c_busy_rise_and_fall: error in address phase\n");
			break;

		case SAA7146_I2C_DTERR:
			hprintk("saa7146: i2c_busy_rise_and_fall: error in data transmission\n");
			break;

		case SAA7146_I2C_DRERR:
			hprintk("saa7146: i2c_busy_rise_and_fall: error when receiving data\n");
			break;

		case SAA7146_I2C_AL:
			hprintk("saa7146: i2c_busy_rise_and_fall: error because arbitration lost\n");
			break;
	}
				
	return status;	

}

/* this functions resets the saa7146 at address 'addr'
   and returns 0 if everything was fine, otherwise -1 */
int i2c_reset(struct saa7146* saa)
{
	u32 status = 0;

	hprintk("saa7146: ==> i2c_reset\n");

	status = i2c_status_check(saa);

	/* clear data-byte for sure */
    	saa7146_write(saa->mem, I2C_TRANSFER, 0x00);
	
	/* check if any operation is still in progress */
	if ( 0 != ( status & SAA7146_I2C_BUSY) ) {

		/* Yes, kill ongoing operation */
		hprintk("saa7146: i2c_reset: busy_state detected\n"); 

		/* set ABORT-OPERATION-bit */
		saa7146_write(saa->mem, I2C_STATUS, ( SAA7146_I2C_BBR | MASK_07));
		saa7146_write(saa->mem, MC2, (MASK_00 | MASK_16));
		mdelay( SAA7146_I2C_DELAY );

		/* clear all error-bits pending; this is needed because p.123, note 1 */
		saa7146_write(saa->mem, I2C_STATUS, SAA7146_I2C_BBR );
		saa7146_write(saa->mem, MC2, (MASK_00 | MASK_16));
		mdelay( SAA7146_I2C_DELAY );
 	}

	/* check if any other error is still present */
	if ( SAA7146_I2C_BBR != (status = i2c_status_check(saa)) ) {

		/* yes, try to kick it */
		hprintk("saa7146: i2c_reset: error_state detected, status:0x%08x\n",status);

		/* clear all error-bits pending */
		saa7146_write(saa->mem, I2C_STATUS, SAA7146_I2C_BBR );
		saa7146_write(saa->mem, MC2, (MASK_00 | MASK_16));
		mdelay( SAA7146_I2C_DELAY );
 		/* the data sheet says it might be necessary to clear the status
		   twice after an abort */
		saa7146_write(saa->mem, I2C_STATUS, SAA7146_I2C_BBR );
		saa7146_write(saa->mem, MC2, (MASK_00 | MASK_16));
	}

	/* if any error is still present, a fatal error has occured ... */
	if ( SAA7146_I2C_BBR != (status = i2c_status_check(saa)) ) {
		hprintk("saa7146: i2c_reset: fatal error, status:0x%08x\n",status);
		return -1;
	}

	return 0;
}

/* this functions writes out the data-bytes at 'data' to the saa7146
   at address 'addr' regarding the 'timeout' and 'retries' values;
   it returns 0 if ok, -1 if the transfer failed, -2 if the transfer
   failed badly (e.g. address error) */
int i2c_write_out(struct saa7146* saa, u32* data, int timeout)
{
	int status = 0;
		
	hprintk("saa7146: ==> writeout: 0x%08x (before) (to:%d)\n",*data,timeout);
	
	/* write out i2c-command */
	saa7146_write(saa->mem, I2C_TRANSFER, *data);
   	saa7146_write(saa->mem, I2C_STATUS, SAA7146_I2C_BBR);
	saa7146_write(saa->mem, MC2, (MASK_00 | MASK_16));

	/* after writing out an i2c-command we have to wait for a while;
	   because we do not know, how long we have to wait, we simply look
	   what the busy-flag is doing, before doing something else */
	 
	/* reason: while fiddling around with the i2c-routines, I noticed
	   that after writing out an i2c-command, one may not read out the
	   status immediately after that. you *must* wait some time, before
	   even the busy-flag gets set */
	   
	status = i2c_busy_rise_and_fall(saa,timeout);

	if ( -1 == status ) {
		hprintk("saa7146: i2c_write_out; timeout\n");
		return -ETIMEDOUT;
	}

	/* we only handle address-errors here */
	if ( 0 != (status & SAA7146_I2C_APERR)) {
		hprintk("saa7146: i2c_write_out; error in address phase\n");
		return -EREMOTEIO;
	}

	/* check for some other mysterious error; we don't handle this here */
	if ( 0 != ( status & 0xff)) {
		hprintk("saa7146: i2c_write_out: some error has occured\n");
        	return -EIO;
  	}
	
	/* read back data, just in case we were reading ... */
	*data = saa7146_read(saa->mem, I2C_TRANSFER);

	hprintk("saa7146: writeout: 0x%08x (after)\n",*data);

	return 0;
}

int clean_up(struct i2c_msg m[], int num, u32 *op)
{
	u16 i, j;
	u16 op_count = 0;

	/* loop through all messages */
	for(i = 0; i < num; i++) {
		op_count++;
		/* loop throgh all bytes of message i */
		for(j = 0; j < m[i].len; j++) {
			/* write back all bytes that could have been read */
			m[i].buf[j] = (op[op_count/3] >> ((3-(op_count%3))*8));
			op_count++;
		}
	}
	
	return 0;
}

int prepare(struct i2c_msg m[], int num, u32 *op)
{
	u16 h1, h2;
	u16 i, j, addr;
	u16 mem = 0, op_count = 0;

//for (i=0; i<num; i++) { printk ("\n%02x (%s): ", m[i].addr, m[i].flags & I2C_M_RD ? "R" : "W"); for (j=0; j<m[i].len; j++) { m[i].buf[j] &= 0xff; printk (" %02x ", (u8) m[i].buf[j]); } } printk ("\n");
	/* determine size of needed memory */
	for(i = 0; i < num; i++)
		mem += m[i].len + 1;

	/* we need one u32 for three bytes to be send plus
	   one byte to address the device */
	mem = 1 + ((mem-1) / 3);

	if ( mem > I2C_MEM_SIZE ) {
		hprintk("saa7146: prepare: i2c-message to big\n");
		return -1;
	}

	/* be careful: clear out the i2c-mem first */
	memset(op,0,sizeof(u32)*mem);

	for(i = 0; i < num; i++) {
		/* insert the address of the i2c-slave.
		 * note: we get 7-bit-i2c-addresses,
		 * so we have to perform a translation
		 */
		addr = (m[i].addr << 1) | ((m[i].flags & I2C_M_RD) ? 1 : 0);
		h1 = op_count/3; h2 = op_count%3;
		op[h1] |= ((u8)addr << ((3-h2)*8));
		op[h1] |= (SAA7146_I2C_START << ((3-h2)*2));
		op_count++;
		/* loop through all bytes of message i */
		for(j = 0; j < m[i].len; j++) {
			/* insert the data bytes */
			h1 = op_count/3; h2 = op_count%3;
			op[h1] |= ((u8)m[i].buf[j] << ((3-h2)*8));
			op[h1] |= (SAA7146_I2C_CONT << ((3-h2)*2));
			op_count++;
		}
	}

	/* have a look at the last byte inserted:
	 * if it was: ...CONT change it to ...STOP
	 */
	h1 = (op_count-1)/3; h2 = (op_count-1)%3;
	if ( SAA7146_I2C_CONT == (0x3 & ((op[h1]) >> ((3-h2)*2))) ) {
		op[h1] &= ~(0x2 << ((3-h2)*2));
		op[h1] |= (SAA7146_I2C_STOP << ((3-h2)*2));
	}

	return mem;
}
#endif


#ifdef __COMPILE_SAA7146_DEBI__

/* functions for accessing the debi-port. note: we currently don't support 
 * page-table-transfers.
 */

#define MY_DEBI_TIMEOUT_MS 5

int	debi_transfer(struct saa7146* saa, struct saa7146_debi_transfer* dt)
{
	u32	debi_config = 0, debi_command = 0, debi_page = 0, debi_ad = 0;
	u32	timeout = MY_DEBI_TIMEOUT_MS;

	/* sanity checks */
	if(dt->direction > 1 || dt->timeout > 15 || dt->swap > 3 || dt->slave16 > 2 || dt->intel > 1 || dt->increment > 1 || dt->tien > 1 )
		return -EINVAL;

	debi_page	= 0;
	/* keep bits 31,30,28 clear */
	debi_config	= (dt->timeout << 22) | (dt->swap << 20) | (dt->slave16 << 19) | (dt->increment << 18) | (dt->intel << 17) | (dt->tien << 16);
	debi_command	= (dt->num_bytes << 17) | (dt->direction << 16) | (dt->address << 0);
	debi_ad		= dt->mem;

	saa7146_write(saa->mem, DEBI_PAGE,	debi_page);	
	saa7146_write(saa->mem, DEBI_CONFIG,	debi_config);	
	saa7146_write(saa->mem, DEBI_COMMAND,	debi_command);	
	saa7146_write(saa->mem, DEBI_AD,	debi_ad);	

	/* upload debi-registers */
	saa7146_write(saa->mem, MC2, (MASK_01|MASK_17));

	/* wait for DEBI upload to complete */
	while (! (saa7146_read(saa->mem, MC2) & 0x2));

	while( --timeout ) {
		/* check, if DEBI still active */
		u32 psr = saa7146_read(saa->mem, PSR);
		if (0 !=  (psr & SPCI_DEBI_S)) {
			/* check, if error occured */
/*			if ( 0 != (saa7146_read(saa->mem, SSR) & (MASK_23|MASK_22))) { */
			if ( 0 != (saa7146_read(saa->mem, SSR) & (MASK_22))) {
				/* clear error status and indicate error */
				saa7146_write(saa->mem, ISR, SPCI_DEBI_E);
				return -1;
			}
		 }
		else {
			/* Clear status bit */
			saa7146_write(saa->mem, ISR, SPCI_DEBI_S);
			break;
		}
		/* I don´t know how we should actually wait for the debi to have finished.
		   we simply wait 1ms here and then check in a loop for max. MY_DEBI_TIMEOUT_MS */		
		mdelay(1);
	}

	/* check for timeout */
	if( 0 == timeout ) {
		return -1;
	}

	/* read back data if we did immediate read-transfer */
	if(dt->num_bytes <= 4 && dt->direction == 1) {
		dt->mem = saa7146_read(saa->mem, DEBI_AD);
		switch(dt->num_bytes) {
			case 1:
			 dt->mem &= 0x000000ff;
			break;
			case 2:
			 dt->mem &= 0x0000ffff;
			break;
			case 3:
			 dt->mem &= 0x00ffffff;
			break;
		}		
	}
	
	return 0;
}
#endif

#ifdef __COMPILE_SAA7146_STUFF__
/* ---------------------------------------------*/
/* helper-function: set gpio-pins		*/
/* ---------------------------------------------*/
void	gpio_set(struct saa7146* saa, u8 pin, u8 data)
{
	u32 value = 0;

	/* sanity check */
	if(pin > 3)
		return;

	/* read old register contents */
	value = saa7146_read(saa->mem, GPIO_CTRL );
	
	value &= ~(0xff << (8*pin));
	value |= (data << (8*pin));

	saa7146_write(saa->mem, GPIO_CTRL, value);
}

void select_input(struct saa7146* saa, int p)
{
	u32 hps_ctrl = 0;

	/* sanity check */
	if( p < 0 || p > 1 )
		return;

	/* read old state */
	hps_ctrl = saa7146_read(saa->mem, HPS_CTRL);

	/* mask out relevant bits */
	hps_ctrl &= ~( MASK_31 | MASK_30 | MASK_28 );

	/* set bits for input b */
	if( 1 == p ) {
		hps_ctrl |= ( (1 << 30) | (1 << 28) );
	}

	/* write back & upload register */
	saa7146_write(saa->mem, HPS_CTRL, hps_ctrl);
	saa7146_write(saa->mem, MC2, (MASK_05 | MASK_21));
} 

#endif

