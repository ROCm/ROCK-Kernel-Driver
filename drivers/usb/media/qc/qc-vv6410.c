/* Start of file */

/* {{{ [fold] Comments */
/*
 * qce-ga, linux V4L driver for the QuickCam Express and Dexxa QuickCam
 *
 * vv6410.c - VV6410 Sensor Implementation
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

#ifndef QCEGA_MODE
#define QCEGA_MODE 0		/* If the driver doesn't work for you, try changing this to "1" */
#endif

/* LSB bit of I2C address signifies write (0) or read (1) */

/* I2C Address */
#define VV6410_ADDR		(0x10<<1)

/* {{{ [fold] I2C Registers */
/* Status registers */
#define VV6410_DEVICEH		0x00	/* Chip identification number including revision indicator */
#define VV6410_DEVICEL		0x01
#define VV6410_STATUS0		0x02	/* User can determine whether timed I2C data has been consumed by interrogating flag states */
#define VV6410_LINECOUNTH	0x03	/* Current line counter value */
#define VV6410_LINECOUNTL	0x04
#define VV6410_XENDH		0x05	/* End x coordinate of image size */
#define VV6410_XENDL		0x06
#define VV6410_YENDH		0x07	/* End y coordinate of image size */
#define VV6410_YENDL		0x08
#define VV6410_DARKAVGH		0x09	/* This is the average pixel value returned from the dark line offset cancellation algorithm */
#define VV6410_DARKAVGL		0x0A
#define VV6410_BLACKAVGH	0x0B	/* This is the average pixel value returned from the black line offset cancellation algorithm  */
#define VV6410_BLACKAVGL	0x0C
#define VV6410_STATUS1		0x0D	/* Flags to indicate whether the x or y image coordinates have been clipped */

/* Setup registers */
#define VV6410_SETUP0		0x10	/* Low-power/sleep modes & video timing */
#define VV6410_SETUP1		0x11	/* Various parameters */
#define VV6410_SYNCVALUE	0x12	/* Contains pixel counter reset value used by external sync */
#define VV6410_FGMODES		0x14	/* Frame grabbing modes (FST, LST and QCK) */
#define VV6410_PINMAPPING	0x15	/* FST and QCK mapping modes. */
#define VV6410_DATAFORMAT	0x16	/* Data resolution */
#define VV6410_OPFORMAT		0x17	/* Output coding formats */
#define VV6410_MODESELECT	0x18	/* Various mode select bits */

/* Exposure registers */
#define VV6410_FINEH		0x20	/* Fine exposure. */
#define VV6410_FINEL		0x21
#define VV6410_COARSEH		0x22	/* Coarse exposure */
#define VV6410_COARSEL		0x23
#define VV6410_ANALOGGAIN	0x24	/* Analog gain setting */
#define VV6410_CLKDIV		0x25	/* Clock division */
#define VV6410_DARKOFFSETH	0x2C	/* Dark line offset cancellation value */
#define VV6410_DARKOFFSETL	0x2D
#define VV6410_DARKOFFSETSETUP	0x2E	/* Dark line offset cancellation enable */

/* Colour registers (none on this camera!) */

/* Video timing registers */
#define VV6410_LINELENGTHH	0x52	/* Line Length (Pixel Clocks) */
#define VV6410_LINELENGTHL	0x53
#define VV6410_XOFFSETH		0x57	/* X-co-ordinate of top left corner of region of interest (x-offset) */
#define VV6410_XOFFSETL		0x58
#define VV6410_YOFFSETH		0x59	/* Y-co-ordinate of top left corner of region of interest (y-offset) */
#define VV6410_YOFFSETL		0x5A
#define VV6410_FIELDLENGTHH	0x61	/* Field length (Lines) */
#define VV6410_FIELDLENGTHL	0x62

/* Text overlay registers (none on this camera!) */

/* I2C autoload registers (none on this camera!) */

/* System registers */
#define VV6410_BLACKOFFSETH	0x70	/* Black offset cancellation default value */
#define VV6410_BLACKOFFSETL	0x71
#define VV6410_BLACKOFFSETSETUP	0x72	/* Black offset cancellation setup */
#define VV6410_CR0		0x75	/* Analog Control Register 0 */
#define VV6410_CR1		0x76	/* Analog Control Register 1 */
#define VV6410_AS0		0x77	/* ADC Setup Register */
#define VV6410_AT0		0x78	/* Analog Test Register */
#define VV6410_AT1		0x79	/* Audio Amplifier Setup Register */
/* }}} */

#define I2C_SET_CHECK(reg,val)	if ((r = qc_i2c_set(qc,(reg),(val)))<0) goto fail
#define STV_SET_CHECK(reg,val)	if ((r = qc_stv_set(qc,(reg),(val)))<0) goto fail
#define STV_SETW_CHECK(reg,val)	if ((r = qc_stv_setw(qc,(reg),(val)))<0) goto fail
#define IS_850(qc)		(GET_PRODUCTID(qc)==0x850)	/* Is it QuickCam Web/Legocam? */

#if QCEGA_MODE
#warning "Using old compatible code (QCEGA_MODE=1)"
#warning "If this works but otherwise it doesn't work, let me know!"
static int mode = 0;
#define VV6410_CONTROL 		0x10	// Setup0
#define VV6410_GAIN  		0x24
/* {{{ [fold] vv6410_set_window() */
static int vv6410_set_window(struct quickcam *qc, int x, int y,int width, int height)
{
	int r = 0;

	// x offset
        x = MAX(1,x);
	I2C_SET_CHECK(0x57,x >> 8);
	I2C_SET_CHECK(0x58,x & 0xff);

	// y offset
        y = MAX(1,y);
	I2C_SET_CHECK(0x59,y >> 8);
	I2C_SET_CHECK(0x5a,y & 0xff);

        // Set the real
        if (qc->sensor_data.subsample) {
            qc->sensor_data.width=180;
            qc->sensor_data.height=148;
        } else {
            qc->sensor_data.width=356;
            qc->sensor_data.height=292;
        }

	// line length
        if (qc->sensor_data.subsample) {
            if (IS_850(qc))
                width=250;
            else
                width=360; /* 180 * 2 (CLK-DIV is 2) */
        }
        else {
          if (IS_850(qc))
                width=416;
          else
                width=712; /* 356 * 2 */
	}

	I2C_SET_CHECK(0x52, width >> 8);
	I2C_SET_CHECK(0x53, width & 0xff);

	// field length (num lines)
        if (qc->sensor_data.subsample)
          height=160; /* nearest of 148 = 10 * 16 */
        else
          height=320; // 304; /* nearest of 292 = 19 * 16 */ 

	I2C_SET_CHECK(0x61,height >> 8);
	I2C_SET_CHECK(0x62,height & 0xff);

        // usb_quickcam_i2c_add(&i2cbuff,0x25,0x02);
	if ((r = qc_i2c_wait(qc))<0) goto fail;

        return 0;
fail:
	return -ENAMETOOLONG;	//some silly code just for testing
}
/* }}} */
#endif

/* {{{ [fold] vv6410_set_size: Set window size */
static int vv6410_set_size(struct quickcam *qc, unsigned int width, unsigned int height)
{
	struct qc_sensor_data *sd = &qc->sensor_data;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("vv6410_set_size(qc=%p,width=%i,height=%i)",qc,width,height);
	/* VV6410 appears to always give fixed 356*292 pixels */
	sd->width = sd->maxwidth;
	sd->height = sd->maxheight;
	return 0;
}
/* }}} */
/* {{{ [fold] vv6410_start: Start grabbing */
static int vv6410_start(struct quickcam *qc)
{
	struct qc_sensor_data *sd = &qc->sensor_data;
	int r;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("vv6410_start(qc=%p)",qc);
	if (PARANOID && !qc)  { PDEBUG("qc==NULL"); return -EINVAL; }
	I2C_SET_CHECK(VV6410_SETUP0, sd->subsample ? (BIT(7)|BIT(6)) : 0x00);
	if (IS_850(qc)) qc_stv_set(qc, 0x1445, 1);		/* Turn on LED */
	r = qc_i2c_wait(qc);
fail:	return r;
}
/* }}} */
/* {{{ [fold] vv6410_stop: Stop grabbing */
static int vv6410_stop(struct quickcam *qc)
{
	static const int low_power_mode = 0; //1;
	static const int sleep_mode     = 1;
	struct qc_sensor_data *sd = &qc->sensor_data;
	unsigned char cmd;
	int r;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("vv6410_stop(qc=%p)",qc);
	if (IS_850(qc)) qc_stv_set(qc, 0x1445, 0);		/* Turn off LED */
	cmd = (sleep_mode << 1) | low_power_mode;
	if (sd->subsample) cmd |= BIT(7)|BIT(6);		/* sub-sampled QCIF mode */
	I2C_SET_CHECK(VV6410_SETUP0, cmd);
	r = qc_i2c_wait(qc);
fail:	return r;
}
/* }}} */
#if COMPRESS
struct stv_init {
	const u8 *data;	/* If NULL, only single value to write, stored in len */
	u16 start;
	u8 len;
};
#endif
/* {{{ [fold] vv6410_init: Initialise parameters for vv6410 sensor. */
/* Just try to send the same commands as Windoze QuickCam soft */
static int vv6410_init(struct quickcam *qc)
{
	struct qc_sensor_data *sd = &qc->sensor_data;
	int r;

#if COMPRESS
	if (IS_850(qc)) {
/* {{{ [fold] Initialization with compression support */

/* {{{ [fold] [fold] stv_init[] */
	static const u8 x0540[] = {			/* 0x0540 - 0x0551 */
		0x97,0x0B,0x4C,0xFC,0x36,0x00,0x75,0x00,0x59,0x02,0x32,0x01,0x56,0xFD,0xEE,0xFF,
		0xB8,0x05 };
	static const u8 x0560[] = {			/* 0x0560 - 0x0563 */
		0x40,0xFF,0xBF,0xBF };
	static const u8 x1500[] = {			/* 0x1500 - 0x150F */
		0x0B,0xA7,0xB7,0x00,0x00,0x00,0x14,0x14,0x14,0x14,0x2B,0x02,0x2B,0x02,0x2B,0x02 };
	static const u8 x1520[] = {			/* 0x1520 - 0x152A */
		0x05,0x14,0x0F,0x0F,0x98,0x98,0x98,0x98,0x2D,0x00,0x01 };
	static const u8 x1530[] = {			/* 0x1530 - 0x153B */
		0x08,0x02,0x00,0x00,0x02,0x00,0x02,0x00,0x60,0x01,0x20,0x01 };
	static const u8 x1552[] = {			/* 0x1552 - 0x1558 */
		0x72,0x90,0x00,0xB0,0xF0,0x77,0x72 };
	static const u8 x1564[] = {			/* 0x1564 - 0x1567 */
		0x00,0xFF,0x0C,0x00 };
	static const u8 x1580[] = {			/* 0x1580 - 0x158F */
		0x02,0x40,0x01,0xF0,0x00,0xD1,0x01,0xAC,0x01,0x07,0x00,0x00,0x00,0x00,0x00,0x00 };
	static const u8 x1590[] = {			/* 0x1590 - 0x15A5 */
		0xA8,0x05,0x64,0x07,0x0F,0x03,0xD8,0x07,0xA6,0x06,0x71,0x04,0x8F,0x01,0xFF,0xFB,
		0xEC,0xE6,0xE0,0xD9,0xC4,0xB8 };
	static const u8 x15C1[] = {			/* 0x15C1 - 0x15C2 */
		0x4B, 0x02 };			/* Output word 0x024B=587 (ISO size) */
	static const struct stv_init stv_init[] = {
		{ NULL,  0x1620, 0x80 },	/* This reg is written twice. Some kind of reset? */
		{ NULL,  0x1620, 0x00 },
		{ x0540, 0x0540, SIZE(x0540) },
		{ x0560, 0x0560, SIZE(x0560) },
		{ NULL,  0x1423, 0x04 },
		{ NULL,  0x1440, 0x00 },
		{ NULL,  0x1443, 0x00 },
		{ NULL,  0x1445, 0x01 },
		{ x1500, 0x1500, SIZE(x1500) },
		{ x1520, 0x1520, SIZE(x1520) },
		{ x1530, 0x1530, SIZE(x1530) },
		{ x1552, 0x1552, SIZE(x1552) },
		{ x1564, 0x1564, SIZE(x1564) },
		{ x1580, 0x1580, SIZE(x1580) },
		{ x1590, 0x1590, SIZE(x1590) },
		{ x15C1, 0x15C1, SIZE(x15C1) },
		{ NULL,  0x15C3, 0x00 },
		{ NULL,  0x15C9, 0x01 },
		{ NULL,  0x1704, 0x00 },
	};
/* }}} */
/* {{{ [fold] vv_init[][2] */
	static const u8 vv_init[][2] = {
		/* Setup registers */
		{ VV6410_SETUP0,	BIT(2) },	/* Soft reset */
		{ VV6410_SETUP0,	BIT(1)|BIT(0) },	/* 25 fps PAL (30 fps NTSC doesn't work!), sleep mode */
		{ VV6410_SETUP1,	BIT(6) },	/* Use unsuffled read-out mode */
		{ VV6410_FGMODES,	BIT(6)|BIT(4)|BIT(2)|BIT(0) },	/* All modes to 1 */
		{ VV6410_PINMAPPING,	0x00 },
		{ VV6410_DATAFORMAT,	BIT(7)|BIT(0) },	/* Pre-clock generator divide off */
		{ VV6410_OPFORMAT,	BIT(3)|BIT(4) },
		/* Exposure registers */
		{ VV6410_FINEH,		320 >> 8 },		/* Initial exposure */
		{ VV6410_FINEL,		320 & 0xFF },
		{ VV6410_COARSEH,	192 >> 8 },
		{ VV6410_COARSEL,	192 & 0xFF },
		{ VV6410_ANALOGGAIN,	0xF0 | 11 },		/* Gain to 11 */
		{ VV6410_CLKDIV,	0x01 },			/* Pixel clock divisor 2 */
		/* Video timing registers */
		{ VV6410_LINELENGTHH,	(416-1) >> 8 },		/* Set line length (columns) to 417 */
		{ VV6410_LINELENGTHL,	(416-1) & 0xFF },
		{ VV6410_FIELDLENGTHH,	(320-1) >> 8 },		/* Set field length (rows) to 320 */
		{ VV6410_FIELDLENGTHL,	(320-1) & 0xFF },
		/* System registers */
		{ VV6410_AS0,		BIT(6)|BIT(4)|BIT(3)|BIT(2)|BIT(1) },	/* Enable voltage doubler */
		{ VV6410_AT0,		0x00 },
		{ VV6410_AT1,		BIT(4)|BIT(0) },	/* Power up audio, differential */
	};
/* }}} */

	unsigned int cols = 416;
	unsigned int rows = 320;
	unsigned int x = 1;
	unsigned int y = 1;
	int i,j;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("vv6410_init(qc=%p)",qc);
	if (PARANOID && !qc) { PDEBUG("qc==NULL"); return -EINVAL; }

	sd->width  = 320;	/* Default to compressed mode */
	sd->height = 240;

	for (i=0; i<SIZE(stv_init); i++) {
		if (stv_init[i].data==NULL) {
			STV_SET_CHECK(stv_init[i].start, stv_init[i].len);
		} else {
			for (j=0; j<stv_init[i].len; j++) {
				STV_SET_CHECK(stv_init[i].start+j, stv_init[i].data[j]);
			}
		}
	}
	if (!IS_850(qc)) {
		STV_SET_CHECK(0x1443, sd->subsample ? 0x10 : 0x20);	/* Scan rate */
		STV_SET_CHECK(0x1446,1);
		STV_SETW_CHECK(0x15C1,600);			/* ISO size, 0x380|orig:600 */
		STV_SET_CHECK(0x1680,0x14);			/* X ctrl */
	}

	for (i=0; i<SIZE(vv_init); i++) {
		I2C_SET_CHECK(vv_init[i][0], vv_init[i][1]);
		if (i<2) if ((r = qc_i2c_wait(qc))<0) goto fail;
	}

	if (!sd->compress) {
		/* Disable compression */
		STV_SET_CHECK(0x1443, sd->subsample ? 0x00 : 0x10);	/* Scan rate: Larger -> slower */
		STV_SETW_CHECK(0x15C1, 1023);				/* ISO-Size */
		STV_SET_CHECK(0x15C3, 1);				/* Y control */
		sd->width  = 356;
		sd->height = 292;
		if (qc->settings.subsample) {
			//FIXME:subsampling (still) doesn't work yet
			cols=250;
			rows=160;
			sd->width  = 180;
			sd->height = 148;
			I2C_SET_CHECK(VV6410_SETUP0, BIT(7)|BIT(6)|BIT(1)|BIT(0));	/* Subsampled timing mode */
		}
	}
	I2C_SET_CHECK(VV6410_XOFFSETH,     x >> 8);
	I2C_SET_CHECK(VV6410_XOFFSETL,     x & 0xFF);
	I2C_SET_CHECK(VV6410_YOFFSETH,     y >> 8);
	I2C_SET_CHECK(VV6410_YOFFSETL,     y & 0xFF);
	I2C_SET_CHECK(VV6410_LINELENGTHH,  (cols-1) >> 8);
	I2C_SET_CHECK(VV6410_LINELENGTHL,  (cols-1) & 0xFF);
	I2C_SET_CHECK(VV6410_FIELDLENGTHH, (rows-1) >> 8);
	I2C_SET_CHECK(VV6410_FIELDLENGTHL, (rows-1) & 0xFF);
	sd->maxwidth  = sd->width;
	sd->maxheight = sd->height;
	return 0;
/* }}} */
	} else {
#endif
/* {{{ [fold] Initialization without compression support */
	if (sd->compress) return -EINVAL;
	if (sd->subsample) {
		sd->maxwidth  = 180;
		sd->maxheight = 148;
	} else {
		sd->maxwidth  = 356;
		sd->maxheight = 292;
	}


#if QCEGA_MODE
{
int line_length = mode?250:416;//415;
 
        if (mode) {
           sd->subsample=1; // quater.
           sd->width      = 180;
           sd->height     = 148;
        } else {
           sd->subsample=0;
           sd->width      = 356;
           sd->height     = 292;
        }

        STV_SET_CHECK(STV_REG23, 5); // was 5.


	if (!IS_850(qc)) {
	     /* logitech quickcam web has 0x850 as idProduct */
	     STV_SET_CHECK(0x1446, 1);
        }


	STV_SET_CHECK(STV_SCAN_RATE, 0x00);


	STV_SET_CHECK(0x1423, 0x04);


	STV_SET_CHECK(STV_REG00, 0x1b); // 0x0b



	I2C_SET_CHECK(VV6410_CONTROL,0x04); // reset to defaults
	if ((r = qc_i2c_wait(qc))<0) goto fail;


        
	
	// CIF or QCIF and sleep.
	if (IS_850(qc)) {
		I2C_SET_CHECK(VV6410_CONTROL,(mode?0xa2:0x02));
	} else {
	        I2C_SET_CHECK(VV6410_CONTROL,(mode?0xc2:0x02));
	}

	if ((r = qc_i2c_wait(qc))<0) goto fail;



	I2C_SET_CHECK(VV6410_GAIN,0xfb);
	if ((r = qc_i2c_wait(qc))<0) goto fail;




	
	STV_SET_CHECK(STV_REG04, 0x07);

	
	STV_SET_CHECK(STV_REG03, 0x45);


        /* set window size */
        if ((r=vv6410_set_window(qc,0,0,48,64)) < 0) {
                PRINTK(KERN_ERR, "vv6410_set_window failed");
                goto fail;
        }


	/* EXPERIMENTAL */
        /*
	 * line length default is 415 so it's the value we use to 
	 * calculate values for  registers 0x20-0x21
	 * Ref. DS Pag. 67
         */   
	I2C_SET_CHECK(0x20,mode? ((line_length-23)>>8):((line_length-51)>>8));

	I2C_SET_CHECK(0x21,mode?((line_length-23)&0xff):((line_length-51)&0xff));

	I2C_SET_CHECK(0x22,mode?0x00:0x01);
         //usb_quickcam_i2c_add(&i2cbuff,0x23,mode?0x9e:0x3e);
	I2C_SET_CHECK(0x23,mode?158:318&0xff);
	I2C_SET_CHECK(0x24,0xfa);
         // clock divisor.
	I2C_SET_CHECK(0x25,0x01);

	if ((r = qc_i2c_wait(qc))<0) goto fail;


	/*
	if (isaweb(dev))
	  {
	    //EXPERIMENTAL: dark/black pixel cancellation
	    usb_quickcam_i2c_add(&i2cbuff,0x3e,0x01);
	    usb_quickcam_i2c_add(&i2cbuff,0x72,0x01);
	    if (usb_quickcam_i2c_send(dev,&i2cbuff,VV6410_ADDR) < 0) {
      	      printk(KERN_ERR "usb_control_msg dark/black pixel failed");
	      goto error;
	    }
	  }
	 */
	STV_SET_CHECK(STV_REG01, 0xb7);

	STV_SET_CHECK(STV_REG02, 0xa7);



	// setup
	I2C_SET_CHECK(0x11,0x18); // 0x18 or Jochen 0x40
	I2C_SET_CHECK(0x14,0x55); // was 0x55
	I2C_SET_CHECK(0x15,0x10); // 0x10 or Jochen:0x00
	I2C_SET_CHECK(0x16,0x81); // Pre clock dividor.
	I2C_SET_CHECK(0x17,0x18); // they are reserved.
	I2C_SET_CHECK(0x18,0x00);
	I2C_SET_CHECK(0x77,0x5e);
	I2C_SET_CHECK(0x78,0x04);// 0x04 or Jochen:0x00
	if (IS_850(qc)) {
	  I2C_SET_CHECK(0x79,0x11);//audio init
	}
	if ((r = qc_i2c_wait(qc))<0) goto fail;


	
	
	STV_SETW_CHECK(STV_ISO_SIZE, IS_850(qc)?1023:600);	  // 0x380|orig:600




	STV_SET_CHECK(STV_Y_CTRL, 1);


	STV_SET_CHECK(STV_SCAN_RATE, mode?0x00:0x10);



	if (!IS_850(qc)) {
	    /* logitech quickam web has 0x0850 as idProduct */
		STV_SET_CHECK(STV_X_CTRL, 0x14);
	}
}

#else
	STV_SET_CHECK(0x0423,0x05);			/* Unknown register, 0x04 or 0x05 */
	STV_SET_CHECK(0x1423,0x04);			/* Unknown register, 0x04 or 0x05 */
	STV_SET_CHECK(0x1443,0x00);			/* Scan rate */
	STV_SET_CHECK(0x1500,0x1B);			/* 0x0B */
	STV_SET_CHECK(0x1501,0xB7);
	STV_SET_CHECK(0x1502,0xA7);
	STV_SET_CHECK(0x1503,0x45);
	STV_SET_CHECK(0x1504,0x07);
	STV_SET_CHECK(0x15C3,1);			/* Y ctrl */
	if (IS_850(qc)) {
		STV_SET_CHECK(0x1443, sd->subsample ? 0x20 : 0x10);	/* Scan rate */
		STV_SETW_CHECK(0x15C1,1023);				/* ISO size, 0x380|orig:600 */
	} else {
		STV_SET_CHECK(0x1443, sd->subsample ? 0x10 : 0x20);	/* Scan rate */
		STV_SET_CHECK(0x1446,1);
		STV_SETW_CHECK(0x15C1,600);				/* ISO size, 0x380|orig:600 */
		STV_SET_CHECK(0x1680,0x14);				/* X ctrl */
	}

	I2C_SET_CHECK(0x10,0x04);			/*  Control register: reset to defaults */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	I2C_SET_CHECK(0x10,sd->subsample ? 0xC2 : 0x02);/*  Control register: CIF or QCIF and sleep */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	I2C_SET_CHECK(0x11,0x18);			/* 0x18 or Jochen 0x40 */
	I2C_SET_CHECK(0x14,0x55);
	I2C_SET_CHECK(0x15,0x10);			/* 0x10 or Jochen:0x00 */
	I2C_SET_CHECK(0x16,0x81);			/* Pre clock dividor. */
	I2C_SET_CHECK(0x17,0x18);			/* they are reserved. */
	I2C_SET_CHECK(0x18,0x00);
	I2C_SET_CHECK(0x24,0xFB);			/* Set gain value */
	I2C_SET_CHECK(0x25,0x01);			/* Clock divisor value */
	I2C_SET_CHECK(0x77,0x5E);
	I2C_SET_CHECK(0x78,0x04);			/* 0x04 or Jochen:0x00 */
	if (IS_850(qc)) {
		I2C_SET_CHECK(0x3E,0x01);		/* EXPERIMENTAL: dark/black pixel cancellation */
		I2C_SET_CHECK(0x72,0x01);
	}

	if ((r = qc_i2c_wait(qc))<0) goto fail;
#endif

	return 0;
/* }}} */
#if COMPRESS
	}
#endif

fail:	return r;
}
/* }}} */
/* {{{ [fold] vv6410_set_exposure() */
static int vv6410_set_exposure(struct quickcam *qc, unsigned int val)
{
	struct qc_sensor_data *sd = &qc->sensor_data;
	static const unsigned int linelength = 415;	/* For CIF */
	unsigned int fine;
	unsigned int coarse;
	int r;
	
	val = (val*val >> 14) + val/4;
	if (sd->exposure==val) return 0;
	sd->exposure = val;
	fine = val % linelength;
	coarse = val / linelength;
	if (coarse>=512) coarse = 512;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("vv6410_set_exposure %d (%i,%i)",val,coarse,fine);
	I2C_SET_CHECK(VV6410_FINEH,   fine >> 8);
	I2C_SET_CHECK(VV6410_FINEL,   fine & 0xFF);
	I2C_SET_CHECK(VV6410_COARSEH, coarse >> 8);
	I2C_SET_CHECK(VV6410_COARSEL, coarse & 0xFF);
fail:	return r;
}
/* }}} */
/* {{{ [fold] vv6410_set_gains() */
static int vv6410_set_gains(struct quickcam *qc, unsigned int hue, unsigned int sat, unsigned int val)
{
	static const int maxgain = 13;			/* Absolute maximum is 14, recommended is 12 */
	struct qc_sensor_data *sd = &qc->sensor_data;
	unsigned int gain;
	int r;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("vv6410_set_gains %d %d %d", hue, sat, val);
	gain = val / 256;
	gain >>= 4;
	if (gain > maxgain) gain = maxgain;
	if (sd->rgain==gain) return 0;
	sd->rgain = gain;
	r = qc_i2c_set(qc, VV6410_ANALOGGAIN, 0xF0 | gain);
	return r;
}
/* }}} */
/* {{{ [fold] vv6410_set_levels() */
static int vv6410_set_levels(struct quickcam *qc, unsigned int exp, unsigned int gain, unsigned int hue, unsigned int sat)
{
	int r;
	if ((r = vv6410_set_exposure(qc, exp))<0) goto fail;
	vv6410_set_gains(qc, hue, sat, gain);
fail:	return r;
}
/* }}} */

/* {{{ [fold] struct qc_sensor qc_sensor_vv6410 */
const struct qc_sensor qc_sensor_vv6410 = {
	name:		"VV6410",
	manufacturer:	"ST Microelectronics",
	init:		vv6410_init,
	start:		vv6410_start,
	stop:		vv6410_stop,
	set_size:	vv6410_set_size,
	set_levels:	vv6410_set_levels,
	/* Exposure and gain control information */
	autoexposure:	FALSE,
	adapt_gainlow:	40000,
	adapt_gainhigh:	65535,
	/* Information needed to access the sensor via I2C */
	reg23: 		5,
	i2c_addr: 	VV6410_ADDR,
	/* Identification information used for auto-detection */
	id_reg:		VV6410_DEVICEH,
	id:		0x19,
	length_id:	1,
};
/* }}} */

/* End of file */
