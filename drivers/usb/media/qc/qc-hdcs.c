/* Start of file */

/* {{{ [fold] Comments */
/*
 * qc-usb, linux V4L driver for the Logitech QuickCam USB camera family
 *
 * qc-hdcs.c - HDCS Sensor Implementation
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

/* LSB bit of I2C or register address signifies write (0) or read (1) */

/* I2C Address */
#define HDCS_ADDR 	(0x55<<1)
 
/* {{{ [fold] I2C registers */
/* I2C Registers common for both HDCS-1000/1100 and HDCS-1020 */
#define HDCS_IDENT	(0x00<<1)	/* Identifications Register */
#define HDCS_STATUS	(0x01<<1)	/* Status Register */
#define HDCS_IMASK	(0x02<<1)	/* Interrupt Mask Register */
#define HDCS_PCTRL	(0x03<<1)	/* Pad Control Register */
#define HDCS_PDRV	(0x04<<1)	/* Pad Drive Control Register */
#define HDCS_ICTRL	(0x05<<1)	/* Interface Control Register */
#define HDCS_ITMG	(0x06<<1)	/* Interface Timing Register */
#define HDCS_BFRAC	(0x07<<1)	/* Baud Fraction Register */
#define HDCS_BRATE	(0x08<<1)	/* Baud Rate Register */
#define HDCS_ADCCTRL	(0x09<<1)	/* ADC Control Register */
#define HDCS_FWROW	(0x0A<<1)	/* First Window Row Register */
#define HDCS_FWCOL	(0x0B<<1)	/* First Window Column Register */
#define HDCS_LWROW	(0x0C<<1)	/* Last Window Row Register */
#define HDCS_LWCOL	(0x0D<<1)	/* Last Window Column Register */
#define HDCS_TCTRL	(0x0E<<1)	/* Timing Control Register */
#define HDCS_ERECPGA	(0x0F<<1)	/* PGA Gain Register: Even Row, Even Column */
#define HDCS_EROCPGA	(0x10<<1)	/* PGA Gain Register: Even Row, Odd Column */
#define HDCS_ORECPGA	(0x11<<1)	/* PGA Gain Register: Odd Row, Even Column */
#define HDCS_OROCPGA	(0x12<<1)	/* PGA Gain Register: Odd Row, Odd Column */
#define HDCS_ROWEXPL	(0x13<<1)	/* Row Exposure Low Register */
#define HDCS_ROWEXPH	(0x14<<1)	/* Row Exposure High Register */

/* I2C Registers only for HDCS-1000/1100 */
#define HDCS00_SROWEXPL	(0x15<<1)	/* Sub-Row Exposure Low Register */
#define HDCS00_SROWEXPH	(0x16<<1)	/* Sub-Row Exposure High Register */
#define HDCS00_CONFIG	(0x17<<1)	/* Configuration Register */
#define HDCS00_CONTROL	(0x18<<1)	/* Control Register */

/* I2C Registers only for HDCS-1020 */
#define HDCS20_SROWEXP	(0x15<<1)	/* Sub-Row Exposure Register	*/
#define HDCS20_ERROR	(0x16<<1)	/* Error Control Register 	*/
#define HDCS20_ITMG2	(0x17<<1)	/* Interface Timing 2 Register	*/
#define HDCS20_ICTRL2	(0x18<<1)	/* Interface Control 2 Register	*/
#define HDCS20_HBLANK	(0x19<<1)	/* Horizontal Blank Register	*/
#define HDCS20_VBLANK	(0x1A<<1)	/* Vertical Blank Register	*/
#define HDCS20_CONFIG	(0x1B<<1)	/* Configuration Register 	*/
#define HDCS20_CONTROL	(0x1C<<1)	/* Control Register		*/
/* }}} */

#define IS_870(qc)	(GET_PRODUCTID(qc)==0x870)
#define IS_1020(qc)	((qc)->sensor_data.sensor->flag != 0)
#define GET_CONTROL	(IS_1020(qc) ? HDCS20_CONTROL : HDCS00_CONTROL)
#define GET_CONFIG	(IS_1020(qc) ? HDCS20_CONFIG : HDCS00_CONFIG)

#define I2C_SET_CHECK(reg,val)	if ((r = qc_i2c_set(qc,(reg),(val)))<0) goto fail
#define STV_SET_CHECK(reg,val)	if ((r = qc_stv_set(qc,(reg),(val)))<0) goto fail
#define STV_SETW_CHECK(reg,val)	if ((r = qc_stv_setw(qc,(reg),(val)))<0) goto fail

/* Enables experimental compressed mode, works with HDCS-1000/0x840,
   mode derived from USB logs obtained from HDCS-1020/0x870
   (should give 640x480), not tested with HDCS-1020.
   On HDCS-1000, gives 30 fps but data is in unknown format,
   observed image width 163 bytes (how many pixels?).
   Frame length appears to vary, typically 3300-4550 bytes.
   (apparently quite simple, however).
    Use this with DUMPDATA mode. */
#define HDCS_COMPRESS 0

#if HDCS_COMPRESS
/* {{{ [fold] hdcs_compress_init(struct quickcam *qc, int flags) */
static int hdcs_compress_init(struct quickcam *qc, int flags)
{
	int r;

	if (flags & 1) {
	/************************************** Plugin camera **************************************/

	STV_SET_CHECK(0x1440, 0x00);							/* Turn on/off isochronous stream */
	// if ((r = qc_stv_getw(qc, 0xE00A)) != 0x0870)					/* ProductId */
	//	PDEBUG("Bad value 0x%02X in reg 0xE00A, should be 0x0870", r);

	STV_SET_CHECK(0x0423, 0x05);							/* Unknown (sometimes 4, sometimes 5) */
	// Warning: I2C address 0xBA is invalid
	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_stv_get(qc, 0x0424)) != 0x0a)					/* Successfully transmitted I2C commands */
	//	PDEBUG("Bad value 0x%02X in reg 0x0424, should be 0x0a", r);
	STV_SET_CHECK(0x0423, 0x04);							/* Unknown (sometimes 4, sometimes 5) */

	if ((r = qc_stv_get(qc, 0x1444)) != 0x10)					/* ? */
		PDEBUG("Bad value 0x%02X in reg 0x1444, should be 0x10", r);
	if ((r = qc_stv_get(qc, 0x1444)) != 0x10)					/* ? */
		PDEBUG("Bad value 0x%02X in reg 0x1444, should be 0x10", r);
	if ((r = qc_stv_get(qc, 0x1444)) != 0x10)					/* ? */
		PDEBUG("Bad value 0x%02X in reg 0x1444, should be 0x10", r);

	STV_SET_CHECK(0x0423, 0x05);							/* Unknown (sometimes 4, sometimes 5) */
	// Warning: I2C address 0x20 is invalid
	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_stv_get(qc, 0x0424)) != 0x0a)					/* Successfully transmitted I2C commands */
	//	PDEBUG("Bad value 0x%02X in reg 0x0424, should be 0x0a", r);
	// Warning: I2C address 0x20 is invalid
	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_stv_get(qc, 0x0424)) != 0x0a)					/* Successfully transmitted I2C commands */
	//	PDEBUG("Bad value 0x%02X in reg 0x0424, should be 0x0a", r);
	// Warning: I2C address 0x20 is invalid
	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_stv_get(qc, 0x0424)) != 0x0a)					/* Successfully transmitted I2C commands */
	//	PDEBUG("Bad value 0x%02X in reg 0x0424, should be 0x0a", r);
	STV_SET_CHECK(0x0423, 0x04);							/* Unknown (sometimes 4, sometimes 5) */
	STV_SET_CHECK(0x0423, 0x04);							/* Unknown (sometimes 4, sometimes 5) */

	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_stv_get(qc, 0x0424)) != 0x02)					/* Successfully transmitted I2C commands */
	//	PDEBUG("Bad value 0x%02X in reg 0x0424, should be 0x02", r);
	// if ((r = qc_stv_get(qc, 0x1410)) != 0x10)					/* I2C area, first reg value */
	//	PDEBUG("Bad value 0x%02X in reg 0x1410, should be 0x10", r);

	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_stv_get(qc, 0x0424)) != 0x02)					/* Successfully transmitted I2C commands */
	//	PDEBUG("Bad value 0x%02X in reg 0x0424, should be 0x02", r);
	// if ((r = qc_stv_get(qc, 0x1410)) != 0x10)					/* I2C area, first reg value */
	//	PDEBUG("Bad value 0x%02X in reg 0x1410, should be 0x10", r);

	STV_SET_CHECK(0x0423, 0x04);							/* Unknown (sometimes 4, sometimes 5) */
	if ((r = qc_stv_get(qc, 0x0424)) != 0x02)					/* Successfully transmitted I2C commands */
		PDEBUG("Bad value 0x%02X in reg 0x0424, should be 0x02", r);
	STV_SET_CHECK(0x0423, 0x04);							/* Unknown (sometimes 4, sometimes 5) */

	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_stv_get(qc, 0x0424)) != 0x02)					/* Successfully transmitted I2C commands */
	//	PDEBUG("Bad value 0x%02X in reg 0x0424, should be 0x02", r);
	// if ((r = qc_stv_get(qc, 0x1410)) != 0x10)					/* I2C area, first reg value */
	//	PDEBUG("Bad value 0x%02X in reg 0x1410, should be 0x10", r);

	// if ((r = qc_get_i2c(qc, qc->sensor_data.sensor, HDCS_IDENT))<0)		/* Identifications Register */
	//	PDEBUG("error reading sensor reg HDCS_IDENT");
	// if ((r = qc_stv_get(qc, 0x0424)) != 0x02)					/* Successfully transmitted I2C commands */
	//	PDEBUG("Bad value 0x%02X in reg 0x0424, should be 0x02", r);
	// if ((r = qc_stv_get(qc, 0x1410)) != 0x10)					/* I2C area, first reg value */
	//	PDEBUG("Bad value 0x%02X in reg 0x1410, should be 0x10", r);

	STV_SET_CHECK(0x1500, 0x1D);							/* ? */
	if ((r = qc_stv_get(qc, 0x1443)) != 0x00)					/* Scan rate? */
		PDEBUG("Bad value 0x%02X in reg 0x1443, should be 0x00", r);
	STV_SET_CHECK(0x1443, 0x01);							/* Scan rate? */
	if ((r = qc_stv_get(qc, 0x1443)) != 0x01)					/* Scan rate? */
		PDEBUG("Bad value 0x%02X in reg 0x1443, should be 0x01", r);
	STV_SET_CHECK(0x1443, 0x00);							/* Scan rate? */

	I2C_SET_CHECK(HDCS_PCTRL, 0x63);						/* Pad Control Register */
	I2C_SET_CHECK(HDCS_PDRV, 0x00);							/* Pad Drive Control Register */
	I2C_SET_CHECK(GET_CONFIG, 0x08);						/* Configuration Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	I2C_SET_CHECK(GET_CONTROL, 0x04);						/* Control Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	I2C_SET_CHECK(GET_CONTROL, 0x00);						/* Control Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1446, 0x00);							/* ? */
	I2C_SET_CHECK(HDCS_PCTRL, 0x63);						/* Pad Control Register */
	I2C_SET_CHECK(HDCS_PDRV, 0x00);							/* Pad Drive Control Register */
	I2C_SET_CHECK(GET_CONFIG, 0x08);						/* Configuration Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	if ((r = qc_stv_get(qc, 0x1445)) != 0x04)					/* Turn LED on/off? */
		PDEBUG("Bad value 0x%02X in reg 0x1445, should be 0x04", r);
	I2C_SET_CHECK(HDCS_ERECPGA, 0x3B);						/* PGA Gain Register: Even Row, Even Column */
	I2C_SET_CHECK(HDCS_EROCPGA, 0x3B);						/* PGA Gain Register: Even Row, Odd Column */
	I2C_SET_CHECK(HDCS_ORECPGA, 0x3B);						/* PGA Gain Register: Odd Row, Even Column */
	I2C_SET_CHECK(HDCS_OROCPGA, 0x3B);						/* PGA Gain Register: Odd Row, Odd Column */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1504, 0x07);							/* ? */
	STV_SET_CHECK(0x1503, 0x45);							/* ? */
	if ((r = qc_stv_get(qc, 0x1500)) != 0x1d)					/* ? */
		PDEBUG("Bad value 0x%02X in reg 0x1500, should be 0x1d", r);
	STV_SET_CHECK(0x1500, 0x1D);							/* ? */
	// if ((r = qc_stv_getw(qc, 0xE00A)) != 0x0870)					/* ProductId */
	//	PDEBUG("Bad value 0x%02X in reg 0xE00A, should be 0x0870", r);
	}
	
	if (flags & 2) {
	/************************************** Start grabbing **************************************/

	// if ((r = qc_stv_getw(qc, 0xE00A)) != 0x0870)					/* ProductId */
	//	PDEBUG("Bad value 0x%02X in reg 0xE00A, should be 0x0870", r);
	STV_SET_CHECK(0x0423, 0x04);							/* Unknown (sometimes 4, sometimes 5) */
	STV_SET_CHECK(0x1500, 0x1D);							/* ? */
	I2C_SET_CHECK(HDCS_PCTRL, 0x63);						/* Pad Control Register */
	I2C_SET_CHECK(HDCS_PDRV, 0x00);							/* Pad Drive Control Register */
	I2C_SET_CHECK(GET_CONFIG, 0x08);						/* Configuration Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x15C3, 0x02);							/* Y-Control, 1: 288 lines, 2: 144 lines */
	STV_SETW_CHECK(0x15C1, 0x027B);							/* Max. ISO packet size */
	I2C_SET_CHECK(HDCS_FWROW, 0x00);						/* First Window Row Register */
	I2C_SET_CHECK(HDCS_FWCOL, 0x0B);						/* First Window Column Register */
	I2C_SET_CHECK(HDCS_LWROW, 0x3D);						/* Last Window Row Register */
	I2C_SET_CHECK(HDCS_LWCOL, 0x5A);						/* Last Window Column Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1680, 0x00);							/* X-Control, 0xa: 352 columns, 6: 176 columns */
	I2C_SET_CHECK(HDCS_TCTRL, IS_1020(qc) ? 0xCB : 0x6B);				/* Timing Control Register */
	I2C_SET_CHECK(HDCS_ICTRL, 0x00);						/* Interface Control Register */
	I2C_SET_CHECK(HDCS_ITMG, 0x16);							/* Interface Timing Register */
	if (IS_1020(qc)) I2C_SET_CHECK(HDCS20_HBLANK, 0xD6);				/* Horizontal Blank Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1446, 0x00);							/* ? */
	if ((r = qc_stv_get(qc, 0x1446)) != 0x00)					/* ? */
		PDEBUG("Bad value 0x%02X in reg 0x1446, should be 0x00", r);
	STV_SET_CHECK(0x1446, 0x00);							/* ? */
	I2C_SET_CHECK(HDCS_ROWEXPL, 0x7B);						/* Row Exposure Low Register */
	I2C_SET_CHECK(HDCS_ROWEXPH, 0x00);						/* Row Exposure High Register */
	if (IS_1020(qc)) {
		I2C_SET_CHECK(HDCS20_SROWEXP, 0x01);					/* Sub-Row Exposure Register */
	} else {
		I2C_SET_CHECK(HDCS00_SROWEXPL, 0x01<<2);
		I2C_SET_CHECK(HDCS00_SROWEXPH, 0x00);
	}
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1501, 0xC2);							/* ? */
	STV_SET_CHECK(0x1502, 0xB0);							/* ? */
	I2C_SET_CHECK(HDCS_PCTRL, 0x63);						/* Pad Control Register */
	I2C_SET_CHECK(HDCS_PDRV, 0x00);							/* Pad Drive Control Register */
	I2C_SET_CHECK(GET_CONFIG, 0x08);						/* Configuration Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	/* Start isochronous streaming */
	I2C_SET_CHECK(GET_CONTROL, 0x04);						/* Control Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1440, 0x01);							/* Turn on/off isochronous stream */

	if ((r = qc_stv_get(qc, 0x1445)) != 0x04)					/* Turn LED on/off? */
		PDEBUG("Bad value 0x%02X in reg 0x1445, should be 0x04", r);
	if ((r = qc_stv_get(qc, 0x1445)) != 0x04)					/* Turn LED on/off? */
		PDEBUG("Bad value 0x%02X in reg 0x1445, should be 0x04", r);
	if ((r = qc_stv_get(qc, 0x1445)) != 0x04)					/* Turn LED on/off? */
		PDEBUG("Bad value 0x%02X in reg 0x1445, should be 0x04", r);

	/* Stop isochronous streaming */
	STV_SET_CHECK(0x1440, 0x00);							/* Turn on/off isochronous stream */
	I2C_SET_CHECK(GET_CONTROL, 0x00);						/* Control Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	STV_SET_CHECK(0x0423, 0x04);							/* Unknown (sometimes 4, sometimes 5) */
	STV_SET_CHECK(0x1500, 0x1D);							/* ? */
	I2C_SET_CHECK(HDCS_PCTRL, 0x63);						/* Pad Control Register */
	I2C_SET_CHECK(HDCS_PDRV, 0x00);							/* Pad Drive Control Register */
	I2C_SET_CHECK(GET_CONFIG, 0x08);						/* Configuration Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x15C3, 0x02);							/* Y-Control, 1: 288 lines, 2: 144 lines */
	STV_SETW_CHECK(0x15C1, 0x027B);							/* Max. ISO packet size */
	I2C_SET_CHECK(HDCS_FWROW, 0x00);						/* First Window Row Register */
	I2C_SET_CHECK(HDCS_FWCOL, 0x0B);						/* First Window Column Register */
	I2C_SET_CHECK(HDCS_LWROW, 0x3D);						/* Last Window Row Register */
	I2C_SET_CHECK(HDCS_LWCOL, 0x5A);						/* Last Window Column Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1680, 0x00);							/* X-Control, 0xa: 352 columns, 6: 176 columns */
	I2C_SET_CHECK(HDCS_TCTRL, IS_1020(qc) ? 0xCB : 0x6B);				/* Timing Control Register */
	I2C_SET_CHECK(HDCS_ICTRL, 0x00);						/* Interface Control Register */
	I2C_SET_CHECK(HDCS_ITMG, 0x16);							/* Interface Timing Register */
	if (IS_1020(qc)) I2C_SET_CHECK(HDCS20_HBLANK, 0xD6);				/* Horizontal Blank Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1446, 0x00);							/* ? */
	if ((r = qc_stv_get(qc, 0x1446)) != 0x00)					/* ? */
		PDEBUG("Bad value 0x%02X in reg 0x1446, should be 0x00", r);
	STV_SET_CHECK(0x1446, 0x00);							/* ? */
	I2C_SET_CHECK(HDCS_ROWEXPL, 0x7B);						/* Row Exposure Low Register */
	I2C_SET_CHECK(HDCS_ROWEXPH, 0x00);						/* Row Exposure High Register */
	if (IS_1020(qc)) {
		I2C_SET_CHECK(HDCS20_SROWEXP, 0x01);					/* Sub-Row Exposure Register */
	} else {
		I2C_SET_CHECK(HDCS00_SROWEXPL, 0x01<<2);
		I2C_SET_CHECK(HDCS00_SROWEXPH, 0x00);
	}
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1501, 0xC2);							/* ? */
	STV_SET_CHECK(0x1502, 0xB0);							/* ? */
	I2C_SET_CHECK(HDCS_PCTRL, 0x63);						/* Pad Control Register */
	I2C_SET_CHECK(HDCS_PDRV, 0x00);							/* Pad Drive Control Register */
	I2C_SET_CHECK(GET_CONFIG, 0x08);						/* Configuration Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	/* Start isochronous streaming */
	I2C_SET_CHECK(GET_CONTROL, 0x04);						/* Control Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	if ((r = qc_stv_get(qc, 0x1445)) != 0x04)					/* Turn LED on/off? */
		PDEBUG("Bad value 0x%02X in reg 0x1445, should be 0x04", r);
	STV_SET_CHECK(0x1440, 0x01);							/* Turn on/off isochronous stream */

	/* Stop isochronous streaming */
	STV_SET_CHECK(0x1440, 0x00);							/* Turn on/off isochronous stream */
	I2C_SET_CHECK(GET_CONTROL, 0x00);						/* Control Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	STV_SET_CHECK(0x0423, 0x04);							/* Unknown (sometimes 4, sometimes 5) */
	STV_SET_CHECK(0x1500, 0x1D);							/* ? */
	I2C_SET_CHECK(HDCS_PCTRL, 0x63);						/* Pad Control Register */
	I2C_SET_CHECK(HDCS_PDRV, 0x00);							/* Pad Drive Control Register */
	I2C_SET_CHECK(GET_CONFIG, 0x08);						/* Configuration Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x15C3, 0x02);							/* Y-Control, 1: 288 lines, 2: 144 lines */
	STV_SETW_CHECK(0x15C1, 0x0230);							/* Max. ISO packet size */
	I2C_SET_CHECK(HDCS_FWROW, 0x00);						/* First Window Row Register */
	I2C_SET_CHECK(HDCS_FWCOL, 0x07);						/* First Window Column Register */
	I2C_SET_CHECK(HDCS_LWROW, 0x49);						/* Last Window Row Register */
	I2C_SET_CHECK(HDCS_LWCOL, 0x5E);						/* Last Window Column Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1680, 0x00);							/* X-Control, 0xa: 352 columns, 6: 176 columns */
	I2C_SET_CHECK(HDCS_TCTRL, IS_1020(qc) ? 0xCE : 0x6E);				/* Timing Control Register */
	I2C_SET_CHECK(HDCS_ICTRL, 0x00);						/* Interface Control Register */
	I2C_SET_CHECK(HDCS_ITMG, 0x16);							/* Interface Timing Register */
	if (IS_1020(qc)) I2C_SET_CHECK(HDCS20_HBLANK, 0xCF);				/* Horizontal Blank Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1446, 0x00);							/* ? */
	if ((r = qc_stv_get(qc, 0x1446)) != 0x00)					/* ? */
		PDEBUG("Bad value 0x%02X in reg 0x1446, should be 0x00", r);
	STV_SET_CHECK(0x1446, 0x00);							/* ? */
	I2C_SET_CHECK(HDCS_ROWEXPL, 0x62);						/* Row Exposure Low Register */
	I2C_SET_CHECK(HDCS_ROWEXPH, 0x00);						/* Row Exposure High Register */
	if (IS_1020(qc)) {
		I2C_SET_CHECK(HDCS20_SROWEXP, 0x1A);					/* Sub-Row Exposure Register */
	} else {
		I2C_SET_CHECK(HDCS00_SROWEXPL, 0x1A<<2);
		I2C_SET_CHECK(HDCS00_SROWEXPH, 0x00);
	}
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1501, 0xB6);							/* ? */
	STV_SET_CHECK(0x1502, 0xA8);							/* ? */
	I2C_SET_CHECK(HDCS_PCTRL, 0x63);						/* Pad Control Register */
	I2C_SET_CHECK(HDCS_PDRV, 0x00);							/* Pad Drive Control Register */
	I2C_SET_CHECK(GET_CONFIG, 0x08);						/* Configuration Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	/* Start isochronous streaming */
	I2C_SET_CHECK(GET_CONTROL, 0x04);						/* Control Register */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	STV_SET_CHECK(0x1440, 0x01);							/* Turn on/off isochronous stream */

	if ((r = qc_stv_get(qc, 0x1445)) != 0x04)					/* Turn LED on/off? */
		PDEBUG("Bad value 0x%02X in reg 0x1445, should be 0x04", r);
	}

	return 0;
fail:	return r;
}
/* }}} */
#endif
/* {{{ [fold] hdcs_init: Initialise parameters (from Georg Acher's user module for hdcs sensor) */
static int hdcs_init(struct quickcam *qc)
{
	struct qc_sensor_data *sd = &qc->sensor_data;
	unsigned char control = GET_CONTROL;
	unsigned char config = GET_CONFIG;
	int r,tctrl,astrt,psmp;

	if (sd->compress) return -EINVAL;
	sd->maxwidth  = IS_1020(qc) ? 352 : 360;	/* CIF */
	sd->maxheight = IS_1020(qc) ? 292 : 296;
	if (sd->subsample) {
		sd->maxwidth  /= 2;		/* QCIF */
		sd->maxheight /= 2;
	}
	if ((r = qc_i2c_break(qc))<0) goto fail;	/* The following setting must go into same I2C packet */
#if HDCS_COMPRESS
r = hdcs_compress_init(qc, 3);
qc_i2c_wait(qc);
qc_i2c_break(qc);
qc_stv_set(qc, 0x1440, 0x00);		/* Turn on/off isochronous stream */
qc_i2c_set(qc, GET_CONTROL, BIT(1));	/* Stop and enter sleep mode */
qc_i2c_wait(qc);
if (r) PDEBUG("hdcs_compress_init(1) = %i", r);
return 0;
#endif
	STV_SET_CHECK(STV_REG23, 0);

	/* Set the STV0602AA in STV0600 emulation mode */
	if (IS_870(qc)) STV_SET_CHECK(0x1446, 1);

	/* Reset the image sensor (keeping it to 1 is a problem) */
	I2C_SET_CHECK(control, 1);
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	I2C_SET_CHECK(control, 0);
	if ((r = qc_i2c_wait(qc))<0) goto fail;
	
	I2C_SET_CHECK(HDCS_STATUS, BIT(6)|BIT(5)|BIT(4)|BIT(3)|BIT(2)|BIT(1));	/* Clear status (writing 1 will clear the corresponding status bit) */
	
	I2C_SET_CHECK(HDCS_IMASK, 0x00);	/* Disable all interrupts */
	if ((r = qc_i2c_wait(qc))<0) goto fail;

	STV_SET_CHECK(STV_REG00, 0x1D);
	STV_SET_CHECK(STV_REG04, 0x07);
	STV_SET_CHECK(STV_REG03, 0x95);

	STV_SET_CHECK(STV_REG23, 0);

	STV_SET_CHECK(STV_SCAN_RATE, 0x20);	/* Larger -> slower */

	STV_SETW_CHECK(STV_ISO_SIZE, 847);	/* ISO-Size, 0x34F = 847 .. 0x284 = 644 */

	/* Set mode */
	STV_SET_CHECK(STV_Y_CTRL, 0x01);	/* 0x02: half, 0x01: full */
	STV_SET_CHECK(STV_X_CTRL, 0x0A);	/* 0x06: half, 0x0A: full */
 
	/* These are not good final values, which will be set in set_size */
	/* However, it looks like it's best to set some values at this point nevertheless */
	I2C_SET_CHECK(HDCS_FWROW, 0);		/* Start at row 0 */
	I2C_SET_CHECK(HDCS_FWCOL, 0);		/* Start at column 0 */
	I2C_SET_CHECK(HDCS_LWROW, 0x47);	/* End at row 288 */
	I2C_SET_CHECK(HDCS_LWCOL, 0x57);	/* End at column 352 */

	/* 0x07 - 0x50 */
	astrt = 3;	/* 0..3, doesn't seem to have any effect... hmm.. smaller is slower with subsampling */
	if (!IS_1020(qc)) {
		/* HDCS-1000 (tctrl was 0x09, but caused some HDCS-1000 not to work) */
		/* Frame rate on HDCS-1000 0x46D:0x840 depending on PSMP:
		 *  4 = doesn't work at all
		 *  5 = 7.8 fps,
		 *  6 = 6.9 fps,
		 *  8 = 6.3 fps,
		 * 10 = 5.5 fps,
		 * 15 = 4.4 fps,
		 * 31 = 2.8 fps */
		/* Frame rate on HDCS-1000 0x46D:0x870 depending on PSMP:
		 * 15 = doesn't work at all
		 * 18 = doesn't work at all
		 * 19 = 7.3 fps
		 * 20 = 7.4 fps
		 * 21 = 7.4 fps
		 * 22 = 7.4 fps
		 * 24 = 6.3 fps
		 * 30 = 5.4 fps */
		psmp = IS_870(qc) ? 20 : 5;	/* 4..31 (was 30, changed to 20) */
		tctrl = (astrt<<5) | psmp;
	} else {
		/* HDCS-1020 (tctrl was 0x7E, but causes slow frame rate on HDCS-1020) */
		/* Changed to 6 which should give 8.1 fps */
		psmp = 6;			/* 4..31 (was 9, changed to 6 to improve fps */
		tctrl = (astrt<<6) | psmp;
	}
	I2C_SET_CHECK(HDCS_TCTRL, tctrl);	/* Set PGA sample duration (was 0x7E for IS_870, but caused slow framerate with HDCS-1020) */

	I2C_SET_CHECK(control, 0);		/* FIXME:should not be anymore necessary (already done) */

	I2C_SET_CHECK(HDCS_ROWEXPL, 0);
	I2C_SET_CHECK(HDCS_ROWEXPH, 0);
	if (IS_1020(qc)) {
		I2C_SET_CHECK(HDCS20_SROWEXP, 0);
		I2C_SET_CHECK(HDCS20_ERROR, BIT(0)|BIT(2));	/* Clear error conditions by writing 1 */
	} else {
		I2C_SET_CHECK(HDCS00_SROWEXPL, 0);
		I2C_SET_CHECK(HDCS00_SROWEXPH, 0);
	}
	if ((r = qc_i2c_wait(qc))<0) goto fail;
 
	STV_SET_CHECK(STV_REG01, 0xB5);
	STV_SET_CHECK(STV_REG02, 0xA8);

	I2C_SET_CHECK(HDCS_PCTRL, BIT(6)|BIT(5)|BIT(1)|BIT(0));
	I2C_SET_CHECK(HDCS_PDRV,  0x00);
	I2C_SET_CHECK(HDCS_ICTRL, (sd->subsample ? BIT(7) : 0) | BIT(5));
	I2C_SET_CHECK(HDCS_ITMG,  BIT(4)|BIT(1));

	/* CONFIG: Bit 3: continous frame capture, bit 2: stop when frame complete */
	I2C_SET_CHECK(config, (sd->subsample ? BIT(5) : 0) | BIT(3));
	I2C_SET_CHECK(HDCS_ADCCTRL, 10);	/* ADC output resolution to 10 bits */
	if ((r = qc_i2c_wait(qc))<0) goto fail;
fail:	return r;
}
/* }}} */
/* {{{ [fold] hdcs_start: Start grabbing */
static int hdcs_start(struct quickcam *qc)
{
	int r;
#if HDCS_COMPRESS
r = hdcs_compress_init(qc, 2);
qc_i2c_wait(qc);
if (r) PDEBUG("hdcs_compress_init(1) = %i", r);
return 0;
#endif
	if ((r = qc_i2c_break(qc))<0) goto fail;
	I2C_SET_CHECK(GET_CONTROL, BIT(2));	/* Run enable */
	if ((r = qc_i2c_break(qc))<0) goto fail;
fail:	return r;
}
/* }}} */
/* {{{ [fold] hdcs_stop: Stop grabbing */
static int hdcs_stop(struct quickcam *qc)
{
	int r;
	if ((r = qc_i2c_break(qc))<0) goto fail;
	I2C_SET_CHECK(GET_CONTROL, BIT(1));	/* Stop and enter sleep mode */
	r = qc_i2c_wait(qc);
fail:	return r;
}
/* }}} */
/* {{{ [fold] hdcs_set_exposure: Set exposure time, val=0..65535 */
static int hdcs_set_exposure(struct quickcam *qc, unsigned int val)
{
	struct qc_sensor_data *sd = &qc->sensor_data;
	unsigned char control = GET_CONTROL;
	unsigned int rowexp;		/* rowexp,srowexp = 15 bits (0..32767) */
	unsigned int srowexp;		/* sub-row exposure (smaller is brighter) */
	unsigned int max_srowexp;	/* Maximum srowexp value + 1 */
	int r;

	/* Absolute black at srowexp=2672,width=360; 2616, width=352; 1896, width=256 for hdcs1000 */

	if (val==sd->exposure) return 0;
	sd->exposure = val;
	val *= 16;		/* 16 seems to be the smallest change that actually affects brightness */
	max_srowexp = sd->width*15/2 - 104 + 1;
	srowexp = max_srowexp - (val % max_srowexp) - 1;
	rowexp  = val / max_srowexp;
	if (qcdebug&QC_DEBUGCAMERA) PDEBUG("width=%i height=%i rowexp=%i srowexp=%i",sd->width,sd->height,rowexp,srowexp);
	if ((r = qc_i2c_break(qc))<0) goto fail;		/* The following setting must go into same I2C packet */
	I2C_SET_CHECK(control, 0);				/* Stop grabbing */
	I2C_SET_CHECK(HDCS_ROWEXPL, rowexp & 0xFF);		/* Number of rows to expose */
	I2C_SET_CHECK(HDCS_ROWEXPH, rowexp >> 8);
	if (IS_1020(qc)) {
		srowexp = 0;	//FIXME:need formula to compute srowexp for HDCS1020!
		srowexp >>= 2;					/* Bits 0..1 are hardwired to 0 */
		I2C_SET_CHECK(HDCS20_SROWEXP, srowexp & 0xFF);	/* Number of pixels to expose */
	} else {
		I2C_SET_CHECK(HDCS00_SROWEXPL, srowexp & 0xFF);	/* Number of pixels to expose */
		I2C_SET_CHECK(HDCS00_SROWEXPH, srowexp >> 8);
	}
	if (IS_1020(qc)) {
		I2C_SET_CHECK(HDCS20_ERROR, BIT(0));		/* Reset exposure error flag */
	} else {
		I2C_SET_CHECK(HDCS_STATUS, BIT(4));		/* Reset exposure error flag */
	}
	I2C_SET_CHECK(control, BIT(2));				/* Restart grabbing */
	if ((r = qc_i2c_break(qc))<0) goto fail;
#if 0
	/* Warning: the code below will cause about 0.1 second delay and may cause lost frames */
	if (PARANOID) {
		/* Check if the new exposure setting is valid */
		if ((r = qc_i2c_wait(qc))<0) goto fail;
		if (IS_1020(qc)) {
			if ((r = qc_get_i2c(qc,qc->sensor_data.sensor, HDCS20_ERROR))<0) goto fail;
			if (r & BIT(0)) PDEBUG("exposure error (1020)");
		} else {
			if ((r = qc_get_i2c(qc,qc->sensor_data.sensor, HDCS_STATUS))<0) goto fail;
			if (r & BIT(4)) PDEBUG("exposure error (1000)");
		}
	}
	if ((r = qc_i2c_wait(qc))<0) goto fail;
#endif
	qc_frame_flush(qc);
fail:	return (r<0) ? r : 0;
}
/* }}} */
/* {{{ [fold] hdcs_set_gains: Set gains */
static int hdcs_set_gains(struct quickcam *qc, unsigned int hue, unsigned int sat, unsigned int val)
{
	static const unsigned int min_gain = 8;
	struct qc_sensor_data *sd = &qc->sensor_data;
	unsigned int rgain, bgain, ggain;
	int r;
	qc_hsv2rgb(hue, sat, val, &rgain, &bgain, &ggain);
	rgain >>= 8;					/* After this the values are 0..255 */
	ggain >>= 8;
	bgain >>= 8;
	rgain = MAX(rgain, min_gain);			/* Do not allow very small values, they cause bad (low-contrast) image */
	ggain = MAX(ggain, min_gain);
	bgain = MAX(bgain, min_gain);
	if (rgain==sd->rgain && ggain==sd->ggain && bgain==sd->bgain) return 0;
	sd->rgain = rgain;			
	sd->ggain = ggain;
	sd->bgain = bgain;
	if (rgain > 127) rgain = rgain/2 | BIT(7);	/* Bit 7 doubles the programmed values */
	if (ggain > 127) ggain = ggain/2 | BIT(7);	/* Double programmed value if necessary */
	if (bgain > 127) bgain = bgain/2 | BIT(7);
	if ((r = qc_i2c_break(qc))<0) goto fail;
	I2C_SET_CHECK(HDCS_ERECPGA, ggain);
	I2C_SET_CHECK(HDCS_EROCPGA, rgain);
	I2C_SET_CHECK(HDCS_ORECPGA, bgain);
	I2C_SET_CHECK(HDCS_OROCPGA, ggain);
fail:	return r;
}
/* }}} */
/* {{{ [fold] hdcs_set_levels() */
static int hdcs_set_levels(struct quickcam *qc, unsigned int exp, unsigned int gain, unsigned int hue, unsigned int sat)
{
	int r = 0;
//#if !HDCS_COMPRESS
	if ((r = hdcs_set_exposure(qc, gain))<0) goto fail;
//#endif
	hdcs_set_gains(qc, hue, sat, exp);
fail:	return r;
}
/* }}} */
/* {{{ [fold] hdcs_set_size: Sets the size of the capture window */
/*
 *  Sets the size (scaling) of the capture window.
 *  If subsample could return the image size we use subsample.
 */
static int hdcs_set_size(struct quickcam *qc, unsigned int width, unsigned int height)
{
	/* The datasheet doesn't seem to say this, but HDCS-1000
	 * has visible windows size of 360x296 pixels, the first upper-left
	 * visible pixel is at 8,8.
	 * From Andrey's test image: looks like HDCS-1020 upper-left
	 * visible pixel is at 24,8 (y maybe even smaller?) and lower-right
	 * visible pixel at 375,299 (x maybe even larger?)
	 */
	unsigned int originx   = IS_1020(qc) ? 24 : 8;		/* First visible pixel */
	unsigned int maxwidth  = IS_1020(qc) ? 352 : 360;	/* Visible sensor size */
	unsigned int originy   = 8;
	unsigned int maxheight = IS_1020(qc) ? 292 : 296;

	unsigned char control = GET_CONTROL;
	struct qc_sensor_data *sd = &qc->sensor_data;
	int r;
	unsigned int x, y;

#if HDCS_COMPRESS
	return 0;
#endif
	if (sd->subsample) {
		width *= 2;
		height *= 2;
		width  = (width + 3)/4*4;		/* Width must be multiple of 4 */
		height = (height + 3)/4*4;		/* Height must be multiple of 4 */
		sd->width = width / 2;
		sd->height = height / 2;		/* The image sent will be subsampled by 2 */
	} else {
		sd->width  = width  = (width + 3)/4*4;	/* Width must be multiple of 4 */
		sd->height = height = (height + 3)/4*4;	/* Height must be multiple of 4 */
	}
	x = (maxwidth - width)/2;			/* Center image by computing upper-left corner */
	y = (maxheight - height)/2;
	width /= 4;
	height /= 4;
	x = (x + originx)/4;				/* Must be multiple of 4 (low bits wired to 0) */
	y = (y + originy)/4;

	if ((r = qc_i2c_break(qc))<0) goto fail;
	I2C_SET_CHECK(control, 0);			/* Stop grabbing */
	I2C_SET_CHECK(HDCS_FWROW, y);
	I2C_SET_CHECK(HDCS_FWCOL, x);
	I2C_SET_CHECK(HDCS_LWROW, y+height-1);
	I2C_SET_CHECK(HDCS_LWCOL, x+width-1);
	I2C_SET_CHECK(control, BIT(2));			/* Restart grabbing */
	
	/* The exposure timings need to be recomputed when size is changed */
	x = sd->exposure;
	sd->exposure = -1;
	if ((r = hdcs_set_exposure(qc, x))<0) goto fail;
fail:	return r;
}
/* }}} */

/* {{{ [fold] struct qc_sensor qc_sensor_hdcs1000 */
const struct qc_sensor qc_sensor_hdcs1000 = {
	name:		"HDCS-1000/1100",
	manufacturer:	"Hewlett Packard",
	init:		hdcs_init,
	start:		hdcs_start,
	stop:		hdcs_stop,
	set_size:	hdcs_set_size,
	set_levels:	hdcs_set_levels,
	/* Exposure and gain control information */
	autoexposure:	FALSE,
	adapt_gainlow:	20,
	adapt_gainhigh:	20000,
	/* Information needed to access the sensor via I2C */
	reg23: 		0,
	i2c_addr: 	HDCS_ADDR,
	/* Identification information used for auto-detection */
	id_reg:		HDCS_IDENT | 1,
	id:		0x08,
	length_id:	1,
	flag:		0,
};
/* }}} */
/* {{{ [fold] struct qc_sensor qc_sensor_hdcs1020 */
const struct qc_sensor qc_sensor_hdcs1020 = {
	name:		"HDCS-1020",
	manufacturer:	"Agilent Technologies",
	init:		hdcs_init,
	start:		hdcs_start,
	stop:		hdcs_stop,
	set_size:	hdcs_set_size,
	set_levels:	hdcs_set_levels,
	/* Exposure and gain control information */
	autoexposure:	FALSE,
	adapt_gainlow:	20,
	adapt_gainhigh:	20000,
	/* Information needed to access the sensor via I2C */
	reg23: 		0,
	i2c_addr: 	HDCS_ADDR,
	/* Identification information used for auto-detection */
	id_reg:		HDCS_IDENT | 1,
	id:		0x10,
	length_id:	1,
	flag:		1,
};
/* }}} */

/* End of file */
