/* Start of file */

/* {{{ [fold] Comments */
/*
 * qce-ga, linux V4L driver for the QuickCam Express and Dexxa QuickCam
 *
 * pb0100.c - PB0100 Sensor Implementation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
/* }}} */

#ifdef NOKERNEL
#include "quickcam.h"
#else
#include <linux/quickcam.h>
#endif

/* I2C Address */
#define PB_ADDR 		0xBA
 
/* {{{ [fold] I2C Registers */
#define PB_IDENT		0x00	/* R0   Chip Version */
#define PB_RSTART		0x01	/* R1   Row Window Start */
#define PB_CSTART		0x02	/* R2   Column Window Start */
#define PB_RWSIZE		0x03	/* R3   Row Window Size */
#define PB_CWSIZE		0x04	/* R4   Column  Window Size */
#define PB_CFILLIN		0x05	/* R5   Column Fill-In */
#define PB_VBL			0x06	/* R6   Vertical Blank Count */
#define PB_CONTROL		0x07	/* R7   Control Mode */
#define PB_FINTTIME		0x08	/* R8   Integration Time/Frame Unit Count */
#define PB_RINTTIME		0x09	/* R9   Integration Time/Row Unit Count */
#define PB_ROWSPEED		0x0A	/* R10  Row Speed Control */
#define PB_ABORTFRAME		0x0B	/* R11  Abort Frame */
/* #define PB_R12		0x0C	   R12  Reserved */
#define PB_RESET		0x0D	/* R13  Reset */
#define PB_EXPGAIN		0x0E	/* R14  Exposure Gain Command */
#define PB_R15			0x0F	/* R15  Expose0 */
#define PB_R16			0x10	/* R16  Expose1 */
#define PB_R17			0x11	/* R17  Expose2 */
#define PB_R18			0x12	/* R18  Low0_DAC */
#define PB_R19			0x13	/* R19  Low1_DAC */
#define PB_R20			0x14	/* R20  Low2_DAC */
#define PB_R21			0x15	/* R21  Threshold11 */
#define PB_R22			0x16	/* R22  Threshold0x */
#define PB_UPDATEINT		0x17	/* R23  Update Interval */
#define PB_R24			0x18	/* R24  High_DAC */
#define PB_R25			0x19	/* R25  Trans0H */
#define PB_R26			0x1A	/* R26  Trans1L */
#define PB_R27			0x1B	/* R27  Trans1H */
#define PB_R28			0x1C	/* R28  Trans2L */
/* #define PB_R29		0x1D	   R29  Reserved */
/* #define PB_R30		0x1E	   R30  Reserved */
#define PB_R31			0x1F	/* R31  Wait to Read */
#define PB_PREADCTRL		0x20	/* R32  Pixel Read Control Mode */
#define PB_R33			0x21	/* R33  IREF_VLN */
#define PB_R34			0x22	/* R34  IREF_VLP */
#define PB_R35			0x23	/* R35  IREF_VLN_INTEG */
#define PB_R36			0x24	/* R36  IREF_MASTER */
#define PB_R37			0x25	/* R37  IDACP */
#define PB_R38			0x26	/* R38  IDACN */
#define PB_R39			0x27	/* R39  DAC_Control_Reg */
#define PB_R40			0x28	/* R40  VCL */
#define PB_R41			0x29	/* R41  IREF_VLN_ADCIN */
/* #define PB_R42		0x2A	   R42  Reserved */
#define PB_G1GAIN		0x2B	/* R43  Green 1 Gain */
#define PB_BGAIN		0x2C	/* R44  Blue Gain */
#define PB_RGAIN		0x2D	/* R45  Red Gain */
#define PB_G2GAIN		0x2E	/* R46  Green 2 Gain */
#define PB_R47			0x2F	/* R47  Dark Row Address */
#define PB_R48			0x30	/* R48  Dark Row Options */
/* #define PB_R49		0x31	   R49  Reserved */
#define PB_R50			0x32	/* R50  Image Test Data */
#define PB_ADCMAXGAIN		0x33	/* R51  Maximum Gain */
#define PB_ADCMINGAIN		0x34	/* R52  Minimum Gain */
#define PB_ADCGLOBALGAIN	0x35	/* R53  Global Gain */
#define PB_R54			0x36	/* R54  Maximum Frame */
#define PB_R55			0x37	/* R55  Minimum Frame */
/* #define PB_R56		0x38	   R56  Reserved */
#define PB_VOFFSET		0x39	/* R57  VOFFSET */
#define PB_R58			0x3A	/* R58  Snap-Shot Sequence Trigger */
#define PB_ADCGAINH		0x3B	/* R59  VREF_HI */
#define PB_ADCGAINL		0x3C	/* R60  VREF_LO */
/* #define PB_R61		0x3D	   R61  Reserved */
/* #define PB_R62		0x3E	   R62  Reserved */
/* #define PB_R63		0x3F	   R63  Reserved */
#define PB_R64			0x40	/* R64  Red/Blue Gain */
#define PB_R65			0x41	/* R65  Green 2/Green 1 Gain */
#define PB_R66			0x42	/* R66  VREF_HI/LO */
#define PB_R67			0x43	/* R67  Integration Time/Row Unit Count */
#define PB_R240			0xF0	/* R240 ADC Test */
#define PB_R241			0xF1    /* R241 Chip Enable */
/* #define PB_R242		0xF2	   R242 Reserved */
/* }}} */

#define I2C_SETW_CHECK(reg,val)	if ((r = qc_i2c_setw(qc,(reg),(val)))<0) goto fail
#define STV_SET_CHECK(reg,val)	if ((r = qc_stv_set(qc,(reg),(val)))<0) goto fail
#define STV_SETW_CHECK(reg,val)	if ((r = qc_stv_setw(qc,(reg),(val)))<0) goto fail

/*
 * The spec file for the PB-0100 suggests the following for best quality
 * images after the sensor has been reset :
 *
 * PB_ADCGAINL      = R60 = 0x03 (3 dec)      : sets low reference of ADC to produce good black level
 * PB_PREADCTRL     = R32 = 0x1400 (5120 dec) : Enables global gain changes through R53
 * PB_ADCMINGAIN    = R52 = 0x10 (16 dec)     : Sets the minimum gain for auto-exposure
 * PB_ADCGLOBALGAIN = R53 = 0x10 (16 dec)     : Sets the global gain
 * PB_EXPGAIN       = R14 = 0x11 (17 dec)     : Sets the auto-exposure value
 * PB_UPDATEINT     = R23 = 0x02 (2 dec)      : Sets the speed on auto-exposure routine
 * PB_CFILLIN       = R5  = 0x0E (14 dec)     : Sets the frame rate
 */

/* {{{ [fold] pb0100_init: Initialise parameters of PB100 sensor */
static int pb0100_init(struct quickcam *qc)
{
	static const Bool natural = TRUE;	/* Disable flicker control for natural lighting? */
	struct qc_sensor_data *sd = &qc->sensor_data;
	int r;

	if (sd->compress) return -EINVAL;
	sd->maxwidth  = 360;
	sd->maxheight = 288;		/* Sensor has 296 rows but top 8 are opaque */
	if (sd->subsample) {
		sd->maxwidth  /= 2;
		sd->maxheight /= 2;
	}
	sd->exposure = 0;

	STV_SET_CHECK(STV_REG00, 1);
	STV_SET_CHECK(STV_SCAN_RATE, 0);

	/* Reset sensor */
	I2C_SETW_CHECK(PB_RESET, 1);
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	I2C_SETW_CHECK(PB_RESET, 0);
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	/* Disable chip */
	I2C_SETW_CHECK(PB_CONTROL, BIT(5)|BIT(3));
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	/* Gain stuff...*/
	I2C_SETW_CHECK(PB_PREADCTRL, BIT(12)|BIT(10)|BIT(6));
	I2C_SETW_CHECK(PB_ADCGLOBALGAIN, 12);
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	/* Set up auto-exposure */
	I2C_SETW_CHECK(PB_R28, 12);			/* ADC VREF_HI new setting for a transition from the Expose1 to the Expose2 setting */
	I2C_SETW_CHECK(PB_ADCMAXGAIN, 180);		/* gain max for autoexposure */
	I2C_SETW_CHECK(PB_ADCMINGAIN, 12);		/* gain min for autoexposure  */
	I2C_SETW_CHECK(PB_R54, 3);			/* Maximum frame integration time (programmed into R8) allowed for auto-exposure routine */
	I2C_SETW_CHECK(PB_R55, 0);			/* Minimum frame integration time (programmed into R8) allowed for auto-exposure routine */
	I2C_SETW_CHECK(PB_UPDATEINT, 1);
	I2C_SETW_CHECK(PB_R15, 800);			/* R15  Expose0 (maximum that auto-exposure may use) */
	I2C_SETW_CHECK(PB_R17, 10);			/* R17  Expose2 (minimum that auto-exposure may use) */

	if (qc->settings.adaptive) {
		I2C_SETW_CHECK(PB_EXPGAIN, (natural?BIT(6):0)|BIT(4)|BIT(0));
	} else {
		I2C_SETW_CHECK(PB_EXPGAIN, 0);
	}
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	I2C_SETW_CHECK(PB_VOFFSET, 0);			/* 0x14 */
	I2C_SETW_CHECK(PB_ADCGAINH, 11);		/* 0x0D */
	I2C_SETW_CHECK(PB_ADCGAINL, 0);			/* Set black level (important!) */

	/* ??? */
	STV_SET_CHECK(STV_REG04, 0x07);
	STV_SET_CHECK(STV_REG03, 0x45);
	STV_SET_CHECK(STV_REG00, 0x11);

	/* Set mode */
	STV_SET_CHECK(STV_Y_CTRL, sd->subsample ? 2 : 1);	/* 0x02: half, 0x01: full FIXME: this doesn't work! */
	STV_SET_CHECK(STV_X_CTRL, sd->subsample ? 6 : 0x0A);	/* 0x06: Half, 0x0A: Full */

	/* ISO-Size (0x27b: 635... why? - HDCS uses 847) */
	STV_SETW_CHECK(STV_ISO_SIZE, 847);

	/* Setup sensor window */
	I2C_SETW_CHECK(PB_RSTART, 0);
	I2C_SETW_CHECK(PB_CSTART, 0);
	I2C_SETW_CHECK(PB_RWSIZE, 240-1);			/* 0xF7: 240 */
	I2C_SETW_CHECK(PB_CWSIZE, 320-1);			/* 0x13F: 320 */
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	/* Scan rate? */
	STV_SET_CHECK(STV_SCAN_RATE, sd->subsample ? 0x10 : 0x20);	/* larger -> slower */

	/* Scan/timing for the sensor */
	I2C_SETW_CHECK(PB_ROWSPEED, BIT(4)|BIT(3)|BIT(1));
	I2C_SETW_CHECK(PB_CFILLIN, 14);
	I2C_SETW_CHECK(PB_VBL, 0);
	I2C_SETW_CHECK(PB_FINTTIME, 0);
	I2C_SETW_CHECK(PB_RINTTIME, 123);
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	STV_SET_CHECK(STV_REG01, 0xC2);
	STV_SET_CHECK(STV_REG02, 0xB0);

fail:	return r;
}
/* }}} */
/* {{{ [fold] pb0100_set_exposure() */
static int pb0100_set_exposure(struct quickcam *qc, unsigned int val)
{
	int r;
	struct qc_sensor_data *sd = &qc->sensor_data;
	val >>= 7;
	if (val==sd->exposure) return 0;
	sd->exposure = val;
	I2C_SETW_CHECK(PB_RINTTIME, val);	/* R9 */
fail:	return r;
}
/* }}} */
/* {{{ [fold] pb0100_set_gains() */
static int pb0100_set_gains(struct quickcam *qc, u16 hue, u16 sat, u16 val)
{
	struct qc_sensor_data *sd = &qc->sensor_data;
	unsigned int rgain, bgain, ggain;
	int r;
	qc_hsv2rgb(hue, sat, val, &rgain, &bgain, &ggain);
	rgain >>= 8;		/* After this the values are 0..255 */
	ggain >>= 8;
	bgain >>= 8;
	if (rgain==sd->rgain && ggain==sd->ggain && bgain==sd->bgain) return 0;
	sd->rgain = rgain;
	sd->ggain = ggain;
	sd->bgain = bgain;
	I2C_SETW_CHECK(PB_RGAIN,  rgain);	/* R43 */
	I2C_SETW_CHECK(PB_G1GAIN, ggain);	/* R44 */
	I2C_SETW_CHECK(PB_G2GAIN, ggain);	/* R45 */
	I2C_SETW_CHECK(PB_BGAIN,  bgain);	/* R46 */
fail:	return r;
}
/* }}} */
/* {{{ [fold] pb0100_set_levels() */
static int pb0100_set_levels(struct quickcam *qc, unsigned int exp, unsigned int gain, unsigned int hue, unsigned int sat)
{
	int r;
	/* When automatic exposure control in Photobit is used, the exposure/gain
	 * registers shouldn't be touched. The sensor may update them only rarely
	 * and if they're changed they may be incorrect until the sensor updates
	 * the registers next time.
	 * FIXME: shouldn't qc-driver.c ensure this function isnt called when adaptive is used?
	 */
	if (qc->settings.adaptive) return 0;
	if ((r = pb0100_set_exposure(qc, exp))<0) goto fail;
	pb0100_set_gains(qc, hue, sat, gain);
fail:	return r;
}
/* }}} */
/* {{{ [fold] pb0100_set_target: Set target brightness for sensor autoexposure, val=0..65535 */
static int pb0100_set_target(struct quickcam *qc, unsigned int val)
{
	struct qc_sensor_data *sd = &qc->sensor_data;
	unsigned int totalpixels, brightpixels, darkpixels;
	int r;

	val >>= 8;			/* val = 0..255 (0-50% of bright pixels) */
	if (val==sd->exposure) return 0;
	sd->exposure = val;

	/* Number of pixels counted by the sensor when subsampling the pixels.
	 * Slightly larger than the real value to avoid oscillation */
	totalpixels = sd->width * sd->height;
	totalpixels = totalpixels/(8*8) + totalpixels/(64*64);

	brightpixels = (totalpixels * val) >> 8;
	darkpixels   = totalpixels - brightpixels;
	I2C_SETW_CHECK(PB_R21, brightpixels);	/* R21 */
	I2C_SETW_CHECK(PB_R22, darkpixels);	/* R22 */
fail:	return r;
}
/* }}} */
/* {{{ [fold] pb0100_set_size: Set window size */
/* Window location and size are controlled by R1, R2, R3 and R4.
 * The default size is CIF (352x288) with to right at (4,12)
 * and bottom left at (355, 299)
 *
 * We try to ensure that the captured area is in the center of
 * the camera purely because that's nicer.  It would be better
 * if the PB0100 sensor supported capture scaling!
 *
 * We do it in on step otherwise size change may take more
 * than one frame (like xawtv who tests 64x48 and uses 352x288)
 * 3072 = 64x48, 16896 = 352x48, 101376 = 352x288.
 */
static int pb0100_set_size(struct quickcam *qc, unsigned int w, unsigned int h)
{
	static const unsigned int originx   = 0;	/* First visible pixel */
	static const unsigned int originy   = 8;
	static const unsigned int maxwidth  = 360;	/* Visible sensor size */
	static const unsigned int maxheight = 288;
	struct qc_sensor_data *sd = &qc->sensor_data;
	int x, y;
	int r;

	sd->width = w;
	sd->height = h;
	if (sd->subsample) {
		w *= 2;
		h *= 2;
	}
	x = (maxwidth - w)/2;			/* Center image by computing upper-left corner */
	y = (maxheight - h)/2;
	x = (x + originx) & ~1;			/* Must be even to align to the Bayer pattern */
	y = (y + originy) & ~1;
	I2C_SETW_CHECK(PB_RSTART, y);		/* PB_RSTART = 12 + y */
	I2C_SETW_CHECK(PB_CSTART, x);		/* PB_CSTART = 4 + x */
	I2C_SETW_CHECK(PB_RWSIZE, h - 1);	/* PB_RWSIZE = h - 1 */
	I2C_SETW_CHECK(PB_CWSIZE, w - 1);	/* PB_CWSIZE = w - 1 */

	if (qc->settings.adaptive) {
		/* The automatic exposure counts need to be recomputed when size is changed */
		x = sd->exposure << 8;
		sd->exposure = -1;
		if ((r = pb0100_set_target(qc, x))<0) goto fail;
	}

	r = qc_i2c_wait(qc);
fail:	return r;
}
/* }}} */
/* {{{ [fold] pb0100_start: Start grabbing */
static int pb0100_start(struct quickcam *qc)
{
	int r;
	I2C_SETW_CHECK(PB_CONTROL, BIT(5)|BIT(3)|BIT(1));
	r = qc_i2c_wait(qc);
fail:	return r;
}
/* }}} */
/* {{{ [fold] pb0100_stop: Stop grabbing */
static int pb0100_stop(struct quickcam *qc)
{
	int r;
	I2C_SETW_CHECK(PB_ABORTFRAME, 1);
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	I2C_SETW_CHECK(PB_CONTROL, BIT(5)|BIT(3));	/* Set bit 1 to zero */
	r = qc_i2c_wait(qc);
fail:	return r;
}
/* }}} */

/* {{{ [fold] struct qc_sensor qc_sensor_pb0100 */
const struct qc_sensor qc_sensor_pb0100 = {
	name:		"PB-0100/0101",
	manufacturer:	"Photobit",
	init:		pb0100_init,
	start:		pb0100_start,
	stop:		pb0100_stop,
	set_size:	pb0100_set_size,
	set_levels:	pb0100_set_levels,
	set_target:	pb0100_set_target,
	/* Exposure and gain control information */
	autoexposure:	TRUE,
	/* Information needed to access the sensor via I2C */
	reg23: 		1,
	i2c_addr: 	PB_ADDR,
	/* Identification information used for auto-detection */
	id_reg:		PB_IDENT,
	id:		0x64,
	length_id:	2,
};
/* }}} */

/* End of file */
