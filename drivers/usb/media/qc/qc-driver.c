/* Start of file */

/* {{{ [fold] Comments  */

/*
 * qc-usb, Logitech QuickCam video driver with V4L support
 * Derived from qce-ga, linux V4L driver for the QuickCam Express and Dexxa QuickCam
 *
 * qc-driver.c - main driver part
 *
 * Copyright (C) 2001  Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 * Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (C) 2002,2003  Tuukka Toivonen
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

/* Cam variations of Logitech QuickCam:
   P/N 861037:      Sensor HDCS1000        ASIC STV0600
   P/N 861050-0010: Sensor HDCS1000        ASIC STV0600
   P/N 861050-0020: Sensor Photobit PB100  ASIC STV0600-1 ("QuickCam Express")
   P/N 861055:      Sensor ST VV6410       ASIC STV0610 ("LEGO cam")
   P/N 861075-0040: Sensor HDCS1000        ASIC
   P/N 961179-0700: Sensor ST VV6410       ASIC STV0602 (Dexxa WebCam USB)
   P/N 861040-0000: Sensor ST VV6410       ASIC STV0610 ("QuickCam Web")

   For any questions ask 
   	qce-ga-devel@lists.sourceforge.net	- about code
   	qce-ga-discussion@lists.sourceforge.net	- about usage
*/
/* }}} */
/* {{{ [fold] Includes  */
#ifdef NOKERNEL
#include "quickcam.h"
#else
#include <linux/quickcam.h>
#endif
#include <linux/module.h>

#include "qc-memory.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#include <linux/slab.h>
#else
#include <linux/malloc.h>
#endif
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <linux/capability.h>
#include <linux/poll.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/moduleparam.h>
#endif
/* }}} */
/* {{{ [fold] Module parameters  */
MODULE_PARM_DESC(qcdebug, "Sets the debug output (bitfield)");
MODULE_PARM(qcdebug, "i");
int qcdebug = DEBUGLEVEL;

MODULE_PARM_DESC(keepsettings, "Keep picture settings across one open to another (0-1)");
MODULE_PARM(keepsettings, "i");
static int keepsettings = 0;

MODULE_PARM_DESC(settle, "Maximum number of frames to wait picture brightness to settle (0-255)");
MODULE_PARM(settle, "i");
static int settle = 0;

/* Subsampling is used to allow higher scan rate with smaller images. */
MODULE_PARM_DESC(subsample, "Sets subsampling (0-1)");
MODULE_PARM(subsample, "i");
static int subsample = 0;	/* normal or sub-sample (sub-sample to increase the speed) */

MODULE_PARM_DESC(compress, "Enable compressed mode (0-1)");
MODULE_PARM(compress, "i");
static int compress = 0;	/* Enable compressed mode if available (higher framerate) */

MODULE_PARM_DESC(frameskip, "How frequently capture frames (0-10)");
MODULE_PARM(frameskip, "i");
static int frameskip = 0;

MODULE_PARM_DESC(quality, "Sets the picture quality (0-5)");
MODULE_PARM(quality, "i");
static int quality = 5;		/* 5 = generalized adjustable Pei-Tam method */

MODULE_PARM_DESC(adaptive, "Automatic adaptive brightness control (0-1)");
MODULE_PARM(adaptive, "i");
static int adaptive = 1;

MODULE_PARM_DESC(equalize, "Equalize image (0-1)");
MODULE_PARM(equalize, "i");
static int equalize = 0;	/* Disabled by default */

MODULE_PARM_DESC(userlut, "Apply user-specified lookup-table (0-1)");
MODULE_PARM(userlut, "i");
static int userlut = 0;		/* Disabled by default */

MODULE_PARM_DESC(retryerrors, "Retry if image capture fails, otherwise return error code (0-1)");
MODULE_PARM(retryerrors, "i");
static int retryerrors = 1;	/* Enabled by default */

/* Bug in Xvideo(?): if the width is not divisible by 8 and Xvideo is used, the frame is shown wrongly */
MODULE_PARM_DESC(compatible, "Enable workaround for bugs in application programs (bitfield)");
MODULE_PARM(compatible, "i");
static int compatible = 0;	/* Disabled by default */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,5)
MODULE_PARM_DESC(video_nr, "Set videodevice number (/dev/videoX)");
MODULE_PARM(video_nr,"i");
/* video_nr option allows to specify a certain /dev/videoX device */
/* (like /dev/video0 or /dev/video1 ...)                          */
/* for autodetect first available use video_nr=-1 (defaultvalue)  */
static int video_nr = -1;
#endif
/* }}} */
/* {{{ [fold] Miscellaneous data  */
#ifndef MODULE_LICENSE		/* Appeared in 2.4.10 */
#ifdef MODULE
#define MODULE_LICENSE(license) \
static const char __module_license[] __attribute__((section(".modinfo"))) = \
	"license=" license
#else
#define MODULE_LICENSE(license)
#endif
#endif

MODULE_SUPPORTED_DEVICE("video");
MODULE_DESCRIPTION("Logitech QuickCam USB driver");
MODULE_AUTHOR("See README");
MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;

static const int min_framewidth  = 32;	/* Minimum image size we allow delivering to user application */
static const int min_frameheight = 32;

static const char qc_proc_name[] = "video/quickcam";
#define qc_name (&qc_proc_name[6])

static struct usb_device_id qc_device_table[] = {
	{ USB_DEVICE(0x046D, 0x0840) },		/* QuickCam Express */
	{ USB_DEVICE(0x046D, 0x0850) },		/* LEGO cam / QuickCam Web */
	{ USB_DEVICE(0x046D, 0x0870) },		/* Dexxa WebCam USB */
	{ }
};
MODULE_DEVICE_TABLE(usb, qc_device_table);

extern const struct qc_sensor qc_sensor_pb0100;
extern const struct qc_sensor qc_sensor_hdcs1000;
extern const struct qc_sensor qc_sensor_hdcs1020;
extern const struct qc_sensor qc_sensor_vv6410;

static const struct qc_sensor *sensors[] = {
	&qc_sensor_hdcs1000,
	&qc_sensor_hdcs1020,
	&qc_sensor_pb0100,
	&qc_sensor_vv6410,
};

static LIST_HEAD(quickcam_list);		/* Linked list containing all QuickCams */
static DECLARE_MUTEX(quickcam_list_lock);	/* Always lock first quickcam_list_lock, then qc->lock */

/* Default values for user-specified lookup-table; may be overwritten by user */
static unsigned char userlut_contents[QC_LUT_SIZE] = {
	/* Red */
	0, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
	18, 18, 18, 18, 18, 18, 18, 25, 30, 35, 38, 42,
	44, 47, 50, 53, 54, 57, 59, 61, 63, 65, 67, 69,
	71, 71, 73, 75, 77, 78, 80, 81, 82, 84, 85, 87,
	88, 89, 90, 91, 93, 94, 95, 97, 98, 98, 99, 101,
	102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113,
	114, 115, 116, 116, 117, 118, 119, 120, 121, 122, 123, 124,
	125, 125, 126, 127, 128, 129, 129, 130, 131, 132, 133, 134,
	134, 135, 135, 136, 137, 138, 139, 140, 140, 141, 142, 143,
	143, 143, 144, 145, 146, 147, 147, 148, 149, 150, 150, 151,
	152, 152, 152, 153, 154, 154, 155, 156, 157, 157, 158, 159,
	159, 160, 161, 161, 161, 162, 163, 163, 164, 165, 165, 166,
	167, 167, 168, 168, 169, 170, 170, 170, 171, 171, 172, 173,
	173, 174, 174, 175, 176, 176, 177, 178, 178, 179, 179, 179,
	180, 180, 181, 181, 182, 183, 183, 184, 184, 185, 185, 186,
	187, 187, 188, 188, 188, 188, 189, 190, 190, 191, 191, 192,
	192, 193, 193, 194, 195, 195, 196, 196, 197, 197, 197, 197,
	198, 198, 199, 199, 200, 201, 201, 202, 202, 203, 203, 204,
	204, 205, 205, 206, 206, 206, 206, 207, 207, 208, 208, 209,
	209, 210, 210, 211, 211, 212, 212, 213, 213, 214, 214, 215,
	215, 215, 215, 216, 216, 217, 217, 218, 218, 218, 219, 219,
	220, 220, 221, 221,

	/* Green */
	0, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
	21, 21, 21, 21, 21, 21, 21, 28, 34, 39, 43, 47,
	50, 53, 56, 59, 61, 64, 66, 68, 71, 73, 75, 77,
	79, 80, 82, 84, 86, 87, 89, 91, 92, 94, 95, 97,
	98, 100, 101, 102, 104, 105, 106, 108, 109, 110, 111, 113,
	114, 115, 116, 117, 118, 120, 121, 122, 123, 124, 125, 126,
	127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138,
	139, 140, 141, 142, 143, 144, 144, 145, 146, 147, 148, 149,
	150, 151, 151, 152, 153, 154, 155, 156, 156, 157, 158, 159,
	160, 160, 161, 162, 163, 164, 164, 165, 166, 167, 167, 168,
	169, 170, 170, 171, 172, 172, 173, 174, 175, 175, 176, 177,
	177, 178, 179, 179, 180, 181, 182, 182, 183, 184, 184, 185,
	186, 186, 187, 187, 188, 189, 189, 190, 191, 191, 192, 193,
	193, 194, 194, 195, 196, 196, 197, 198, 198, 199, 199, 200,
	201, 201, 202, 202, 203, 204, 204, 205, 205, 206, 206, 207,
	208, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214,
	214, 215, 215, 216, 217, 217, 218, 218, 219, 219, 220, 220,
	221, 221, 222, 222, 223, 224, 224, 225, 225, 226, 226, 227,
	227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232, 233,
	233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 239,
	239, 240, 240, 241, 241, 242, 242, 243, 243, 243, 244, 244,
	245, 245, 246, 246,

	/* Blue */
	0, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
	23, 23, 23, 23, 23, 23, 23, 30, 37, 42, 47, 51,
	55, 58, 61, 64, 67, 70, 72, 74, 78, 80, 82, 84,
	86, 88, 90, 92, 94, 95, 97, 100, 101, 103, 104, 106,
	107, 110, 111, 112, 114, 115, 116, 118, 119, 121, 122, 124,
	125, 126, 127, 128, 129, 132, 133, 134, 135, 136, 137, 138,
	139, 140, 141, 143, 144, 145, 146, 147, 148, 149, 150, 151,
	152, 154, 155, 156, 157, 158, 158, 159, 160, 161, 162, 163,
	165, 166, 166, 167, 168, 169, 170, 171, 171, 172, 173, 174,
	176, 176, 177, 178, 179, 180, 180, 181, 182, 183, 183, 184,
	185, 187, 187, 188, 189, 189, 190, 191, 192, 192, 193, 194,
	194, 195, 196, 196, 198, 199, 200, 200, 201, 202, 202, 203,
	204, 204, 205, 205, 206, 207, 207, 209, 210, 210, 211, 212,
	212, 213, 213, 214, 215, 215, 216, 217, 217, 218, 218, 220,
	221, 221, 222, 222, 223, 224, 224, 225, 225, 226, 226, 227,
	228, 228, 229, 229, 231, 231, 232, 233, 233, 234, 234, 235,
	235, 236, 236, 237, 238, 238, 239, 239, 240, 240, 242, 242,
	243, 243, 244, 244, 245, 246, 246, 247, 247, 248, 248, 249,
	249, 250, 250, 251, 251, 253, 253, 254, 254, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255
};

static void qc_usb_exit(struct quickcam *qc);
static int qc_capt_init(struct quickcam *qc);
static void qc_capt_exit(struct quickcam *qc);
static int qc_capt_get(struct quickcam *qc, unsigned char **frame);
static int qc_isoc_init(struct quickcam *qc);
static void qc_isoc_exit(struct quickcam *qc);
/* }}} */

/* {{{ [fold] **** Miscellaneous functions ************************************** */

/* {{{ [fold] qc_usleep(long usec) */
void qc_usleep(unsigned long usec)
{
	wait_queue_head_t wq;
	init_waitqueue_head(&wq);
	interruptible_sleep_on_timeout(&wq, usec*HZ/1000000);
}
/* }}} */
/* {{{ [fold] qc_unlink_urb_sync(struct urb *urb) */
/* Unlink URB synchronously (usb_unlink_urb may not be synchronous).
 * Note: at this moment the URB completion handler must not resubmit the same URB.
 */
static void qc_unlink_urb_sync(struct urb *urb) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
	usb_kill_urb(urb);
#else
	int r;
	while ((r=usb_unlink_urb(urb)) == -EBUSY) {
		/* The URB is not anymore linked (status!=-EINPROGRESS) but 
		 * usb_unlink_urb() was asynchronous and URB's completion handler still will run */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout( (HZ/100)==0 ? 1 : HZ/100);
	}
	/* if (r!=-EBUSY),
	 * usb_unlink_urb() called synchronously the completion handler and
	 * there's no need to wait or anything else */
	if (r) PDEBUG("qc_unlink_urb_sync(%p): r=%i", urb, r);
#endif
}
/* }}} */
/* {{{ [fold] int qc_get_i2c(struct quickcam *qc, const struct qc_sensor *sensor, int reg) */
/* Read a sensor byte or word wide register value via STV0600 I2C bus
 * qc_i2c_init() must be called first!
 */
int qc_get_i2c(struct quickcam *qc, const struct qc_sensor *sensor, int reg)
{
	struct usb_device *dev = qc->dev;
	int ret;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGCAMERA) PDEBUG("qc_get_i2c(qc=%p,sensor=%p,reg=0x%04X)",qc,sensor,reg);
	TEST_BUGR(dev==NULL);
	if (sizeof(qc->dmabuf)<35) BUG();

	/* We need here extra write to the STV register before reading the I2C register */
	/* Also wait until there are no pending control URB requests */
	if ((ret = qc_stv_set(qc, STV_REG23, sensor->reg23))<0) goto fail;

	memset(qc->dmabuf, 0, 35);
	qc->dmabuf[0]    = reg;
	qc->dmabuf[0x20] = sensor->i2c_addr;
	qc->dmabuf[0x21] = 0;			/* 0+1 = 1 value, one byte or word wide register */
	qc->dmabuf[0x22] = 3;			/* Read I2C register */
	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		0x04,
		0x40,
		0x1400, 0,			/* Write I2C register address, 35 bytes */
		qc->dmabuf, 0x23, 3*HZ);
	if (ret < 0) goto fail;
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		0x04,
		0xC0,
		0x1410, 0, 			/* Read register contents from I2C, 1 or 2 bytes */
		qc->dmabuf, sensor->length_id, 3*HZ);
	if (ret < 0) goto fail;
	ret = qc->dmabuf[0];
	if (sensor->length_id>1) ret |= qc->dmabuf[1]<<8;	/* Assume LSB is always first from data received via USB */
	if (qcdebug&QC_DEBUGCAMERA) PDEBUG("qc_get_i2c(reg=0x%04X) = %04X", reg, ret);
	return ret;

fail:	PDEBUG("qc_get_i2c failed, code=%i",ret);
	return ret;
}
/* }}} */
/* {{{ [fold] int qc_stv_set(struct quickcam *qc, unsigned short reg, unsigned char val) */
/*
 * Set one byte register in the STV-chip. qc_i2c_init() must be called first!
 */
int qc_stv_set(struct quickcam *qc, unsigned short reg, unsigned char val)
{
	int ret;
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGCAMERA) PDEBUG("qc_stv_set(qc=%p,reg=0x%04X,val=%u)",qc,(int)reg,(int)val);
	TEST_BUGR(qc==NULL);
	if (sizeof(qc->dmabuf)<1) BUG();
	qc_i2c_wait(qc);		/* Wait until no pending commands from qc_i2c_* */
	qc->dmabuf[0] = val;
	ret = usb_control_msg(qc->dev, usb_sndctrlpipe(qc->dev, 0),
		0x04,			/* Request */
		0x40,			/* RequestType */
		reg, 0,			/* Value, Index */
		qc->dmabuf, 1, 3*HZ);
	if ((qcdebug&QC_DEBUGERRORS || qcdebug&QC_DEBUGLOGIC) && ret<0) PDEBUG("Failed qc_stv_set()=%i", ret);
	if (ret<0) return ret;
	return 0;
}
/* }}} */
/* {{{ [fold] int qc_stv_get(struct quickcam *qc, unsigned short reg) */
/*
 * Read one byte register in the STV-chip. qc_i2c_init() must be called first!
 * Return the unsigned register value or negative error code on error.
 */
int qc_stv_get(struct quickcam *qc, unsigned short reg)
{
	int ret;
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGCAMERA) PDEBUG("qc_stv_get(qc=%p,reg=0x%04X)",qc,(int)reg);
	TEST_BUGR(qc==NULL);
	if (sizeof(qc->dmabuf)<1) BUG();
	qc_i2c_wait(qc);		/* Wait until no pending commands from qc_i2c_* */
	ret = usb_control_msg(qc->dev, usb_rcvctrlpipe(qc->dev, 0),
		0x04,			/* Request */
		0xC0,			/* RequestType */
		reg, 0,			/* Value, Index */
		qc->dmabuf, 1, 3*HZ);
	if ((qcdebug&QC_DEBUGERRORS || qcdebug&QC_DEBUGLOGIC) && ret<0) PDEBUG("Failed qc_stv_get()=%i", ret);
	if (ret<0) return ret;
	if (qcdebug&QC_DEBUGCAMERA) PDEBUG("qc_stv_get(reg=0x%04X)=%02X", reg, qc->dmabuf[0]);
	return qc->dmabuf[0];
}
/* }}} */
/* {{{ [fold] int qc_stv_setw(struct quickcam *qc, unsigned short reg, unsigned short val) */
/*
 * Set two byte register in the STV-chip. qc_i2c_init() must be called first!
 * "w" means either "word" or "wide", depending on your religion ;)
 */
int qc_stv_setw(struct quickcam *qc, unsigned short reg, unsigned short val)
{
	int ret;
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGCAMERA) PDEBUG("qc_stv_setw(qc=%p,reg=0x%04X,val=%i)",qc,(int)reg,(int)val);
	TEST_BUGR(qc==NULL);
	if (sizeof(qc->dmabuf)<2) BUG();
	qc_i2c_wait(qc);
	qc->dmabuf[0] = val & 0xFF;
	qc->dmabuf[1] = (val >> 8) & 0xFF;
	ret = usb_control_msg(qc->dev, usb_sndctrlpipe(qc->dev, 0),
		0x04,
		0x40,
		reg, 0,
		qc->dmabuf, 2, 3*HZ);
	if ((qcdebug&QC_DEBUGERRORS || qcdebug&QC_DEBUGLOGIC) && ret<0) PDEBUG("Failed qc_stv_setw()=%i", ret);
	if (ret<0) return ret;
	return 0;
}
/* }}} */
/* {{{ [fold] void qc_hsv2rgb(s16 hue, u16 sat, u16 val, int *red, int *green, int *blue) */
/* Convert HSI (hue, saturation, intensity) to RGB (red, green, blue).
 * All input and output values are 0..65535.
 * Hue is actually signed, so it is -32768..32767, but this is equivalent
 * since it is the angle around full circle (0=Red, 21845=Green, 43690=Blue).
 * Based on libgimp, converted to 16.16 fixed point by tuukkat.
 */
void qc_hsv2rgb(s16 hue, u16 sat, u16 val, int *red, int *green, int *blue)
{
	unsigned int segment, valsat;
	signed int   h = (u16)hue;
	unsigned int s = (sat<32768) ? 0 : (sat-32768)*2;	/* 32768 or less = no saturation */
	unsigned int v = val;					/* value = intensity */
	unsigned int p;

#if 1	/* Make common case more efficient */
	if (s == 0) {
		*red   = v;
		*green = v;
		*blue  = v;
		return;
	}
#endif
	segment = (h + 10923) & 0xFFFF;		
	segment = segment*3 >> 16;		/* 0..2: 0=R, 1=G, 2=B */
	hue -= segment * 21845;			/* -10923..10923 */
	h = hue;
	h *= 3;
	valsat = v*s >> 16;			/* 0..65534 */
	p = v - valsat;
	if (h>=0) {
		unsigned int t = v - (valsat * (32769 - h) >> 15);
		switch (segment) {
		default:
			PDEBUG("hsi2rgb: this can never happen!");
		case 0:	/* R-> */
			*red   = v;
			*green = t;
			*blue  = p;
			break;
		case 1:	/* G-> */
			*red   = p;
			*green = v;
			*blue  = t;
			break;
		case 2:	/* B-> */
			*red   = t;
			*green = p;
			*blue  = v;
			break;
		}
	} else {
		unsigned int q = v - (valsat * (32769 + h) >> 15);
		switch (segment) {
		default:
			PDEBUG("hsi2rgb: this can never happen!");
		case 0:	/* ->R */
			*red   = v;
			*green = p;
			*blue  = q;
			break;
		case 1:	/* ->G */
			*red   = q;
			*green = v;
			*blue  = p;
			break;
		case 2:	/* ->B */
			*red   = p;
			*green = q;
			*blue  = v;
			break;
		}
	}
	//PDEBUG("hue=%i sat=%i val=%i  segment=%i h=%i  r=%i g=%i b=%i",hue,sat,val, segment,h, *red,*green,*blue);
}

/* }}} */
/* {{{ [fold] int qc_lock(struct quickcam *qc) */
/* Takes a lock on quickcam_list_lock and verifies that the given qc is available */
/* Returns 0 on success in which case the lock must be freed later or negative error code */
static int qc_lock(struct quickcam *qc)
{
	struct quickcam *q;

	if (down_interruptible(&quickcam_list_lock)) return -ERESTARTSYS;

	/* Search for the device in the list of plugged quickcams (this prevents a race condition) */
	list_for_each_entry(q, &quickcam_list, list) {
		if (q == qc) break;			/* Found it? */
	}
	if (q != qc) {
		PDEBUG("can not find the device to open");
		up(&quickcam_list_lock);
		return -ENODEV;
	}
	return 0;
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_i2c:    I2C URB messaging routines (qc_i2c_*) ************* */

/* We have here a quite typical producer-consumer scheme:
 * URB interrupt handler routine consumes i2c data, while
 * kernel mode processes create more of it.
 * Ref: Linux Device Drivers, Alessandro Rubini et al, 2nd edition, pg. 279
 * "Using Circular Buffers"
 */

static const int qc_i2c_maxbufsize = 0x23;

/* {{{ [fold] (private) qc_i2c_nextpacket(struct quickcam *qc) */
/* Fill URB and submit it, if there are more data to send 
 * Consume data from "commands" array. May be called from interrupt context.
 * Return standard error code.
 */
static int qc_i2c_nextpacket(struct quickcam *qc)
{
	struct qc_i2c_data *id = &qc->i2c_data;
	struct urb *urb = id->urb;
	u8 *tb = urb->transfer_buffer, flags;
	struct usb_ctrlrequest *cr = (struct usb_ctrlrequest *)urb->setup_packet;
	unsigned int newtail, length, regnum, i, j;
	signed int r;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_i2c_nextpacket(quickcam=%p), tail=%i, head=%i, interrupt=%i",qc,id->tail,id->head,(int)in_interrupt());
	IDEBUG_TEST(*id);

	if (!qc->connected) {
		/* Device was disconnected, cancel all pending packets and return */
		id->tail = id->head = id->newhead = 0;
		id->packets = 0;
		return -ENODEV;
	}

	newtail = id->tail;					/* First data to fetch */
	if (id->packets<=1 && newtail==id->head) {	/* packets==0 or 1: no extra URB need to be scheduled */
		if (qcdebug&QC_DEBUGCONTROLURBS) PDEBUG("No more control URBs to send");
		r = 0;
		goto nourbs;
	}
	if (id->packets<=1) {
		/* Collect data from circular buffer to URB transfer buffer */
		/* Now id->tail!=id->head: there's at least one packet to send */
		TEST_BUGR(newtail==id->head);
		id->packets = 1;
		if (GET_PRODUCTID(qc)==0x0850) id->packets = 2;
		regnum = 0x0400;
		length = qc_i2c_maxbufsize;

		i = 0;							/* Transfer buffer position */
		if (!(id->commands[newtail].flags & I2C_FLAG_WORD)) {
			/* Normal byte-wide register write */
			if (qcdebug&QC_DEBUGCONTROLURBS) PDEBUG("Setting byte-wide registers");
			do {
				tb[i]      = id->commands[newtail].regnum;
				tb[i+0x10] = id->commands[newtail].loval;
				flags      = id->commands[newtail].flags;
				i++;
				newtail    = (newtail + 1) % I2C_MAXCOMMANDS;	/* Next data to fetch */
				if (flags & I2C_FLAG_BREAK) break;		/* Start new packet */
				if (newtail == id->head) break;		/* No more data? */
				if (i > 0x0F) break;			/* Transfer buffer full? */
				if (id->commands[newtail].flags & I2C_FLAG_WORD) break;
			} while (TRUE);
/*if (flags&I2C_FLAG_BREAK) PDEBUG("breaking!!!!!!!!!!");
{
int mm;
for(mm=0;mm<i;mm++) printk("%02X=%02X ",tb[mm],tb[mm+0x10]);
printk("\n");
}*/
			for (j=i; j<0x10; j++) tb[j+0x10] = 0;	/* Zero out unused register values just to be sure */
		} else {
			/* Two-byte-wide register write (used in Photobit) */
			if (qcdebug&QC_DEBUGCONTROLURBS) PDEBUG("Setting word-wide registers");
			do {
				tb[i]        = id->commands[newtail].regnum;
				tb[i*2+0x10] = id->commands[newtail].loval;
				tb[i*2+0x11] = id->commands[newtail].hival;
				flags        = id->commands[newtail].flags;
				i++;
				newtail = (newtail + 1) % I2C_MAXCOMMANDS;	/* Next data to fetch */
				if (flags & I2C_FLAG_BREAK) break;		/* Start new packet */
				if (newtail == id->head) break;		/* No more data? */
				if (i > 0x07) break;			/* Transfer buffer full? */
				if (!(id->commands[newtail].flags & I2C_FLAG_WORD)) break;
			} while (TRUE);
			for (j=i*2; j<0x10; j++) tb[j+0x10] = 0;	/* Zero out unused register values just to be sure */
		}
		for (j=i; j<0x10; j++) tb[j] = 0;	/* Zero out unused register addresses just to be sure */
		tb[0x20] = qc->sensor_data.sensor->i2c_addr;
		tb[0x21] = i-1;			/* Number of commands to send - 1 */
		tb[0x22] = 1;			/* Write cmd, 03 would be read. */
		id->tail = newtail;
		if (qcdebug&QC_DEBUGCONTROLURBS) PDEBUG("sending i2c packet, cmds=%i, reg0=%02X, val0=%02X",tb[0x21]+1,tb[0],tb[0x10]);
	} else {
		/* id->packets==2: send extra packet for QuickCam Web */
		if (qcdebug&QC_DEBUGCONTROLURBS) PDEBUG("sending finalization packet");
		id->packets = 1;
		regnum = 0x1704;
		length = 1;
		tb[0] = 1;
	}
	urb->dev    = qc->dev;		/* 2.4.x zeroes urb->dev after submission */
	urb->pipe   = usb_sndctrlpipe(qc->dev, 0);
	urb->transfer_buffer_length = length;
	cr->wValue  = cpu_to_le16(regnum);
	cr->wLength = cpu_to_le16(length);
	r = usb_submit_urb(urb,GFP_ATOMIC);
	CHECK_ERROR(r<0, nourbs, "Failed qc_i2c_nextpacket()=%i", r);
	return 0;

nourbs:	id->packets = 0;	/* No more URBs are scheduled */
	wake_up(&id->wq);	//FIXME: race condition: now packets=0, so id could be freed and wake_up do oops
	return r;
}
/* }}} */
/* {{{ [fold] (private) qc_i2c_handler(struct urb *urb) */
/* This is URB completion handler and is called in interrupt context.
 * For each submitted URB, this function is guaranteed to be called exactly once.
 * This function may not be called reentrantly for the same qc (should be ok, IRQs don't nest).
 * It will resubmit the same URB, if
 * - The previous URB completed without error
 * - Camera is still connected (qc->connected == TRUE)
 * - There is still commands to be sent in commands buffer or pid=0x850 and finalization packet is not yet sent.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void qc_i2c_handler(struct urb *urb, struct pt_regs *ptregs)
#else
static void qc_i2c_handler(struct urb *urb)
#endif
{
	struct quickcam *qc = urb->context;

	if (qcdebug&QC_DEBUGINTERRUPTS) PDEBUG("[INTR] qc_i2c_handler(urb=%p)",urb);
	TEST_BUG(urb==NULL);
	TEST_BUG(qc==NULL);
	IDEBUG_TEST(qc->i2c_data);
	if (urb->status<0) {
		switch (urb->status) {
		default:
			/* Seen here: ECONNABORTED    103     Software caused connection abort */
			PRINTK(KERN_ERR,"Unhandled control URB error %i",urb->status);
		case -EPROTO:		/* Bitstuff error or unknown USB error */
		case -EILSEQ:		/* CRC mismatch */
		case -ETIMEDOUT:	/* Transfer timed out */
		case -EREMOTEIO:	/* Short packet detected */
		case -EPIPE:		/* Babble detect or endpoint stalled */
			/* We could try resubmitting the URB here */
		case -ENOENT:		/* URB was unlinked */
		case -ENODEV:		/* Device was removed */
		case -ECONNRESET:	/* Asynchronous unlink, should not happen */
			PRINTK(KERN_ERR,"Control URB error %i",urb->status);
			qc->i2c_data.packets = 0;	/* Don't schedule more URBs */
			wake_up(&qc->i2c_data.wq);
			return;
		}
	}
	qc_i2c_nextpacket(qc);
}
/* }}} */
/* {{{ [fold] qc_i2c_flush(struct quickcam *qc) */
/* Allow all register settings set earlier to be scheduled and sent to camera */
static int qc_i2c_flush(struct quickcam *qc)
{
	struct qc_i2c_data *id = &qc->i2c_data;
	int r = 0;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_i2c_flush(quickcam=%p,regs=%i)",qc,
			(id->newhead+I2C_MAXCOMMANDS-id->head)%I2C_MAXCOMMANDS);
	IDEBUG_TEST(*id);
	id->head = id->newhead;
	if (id->packets==0)	/* Schedule URB if there aren't any in progress */
		r = qc_i2c_nextpacket(qc);
	return r;
}
/* }}} */
/* {{{ [fold] qc_i2c_wait(struct quickcam *qc) */
/* Wait until all previosly set registers are set or abort all transmissions
 * and return error code.
 * After this function returns, there will not be uncompleted I2C URBs.
 */
int qc_i2c_wait(struct quickcam *qc)
{
	struct qc_i2c_data *id = &qc->i2c_data;
	int r;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_i2c_wait(quickcam=%p)",qc);
	TEST_BUGR(in_interrupt());
	TEST_BUGR(qc==NULL);
	IDEBUG_TEST(*id);

	if (!qc->connected) goto cancel;
	r = qc_i2c_flush(qc);
	if (r>=0) r = wait_event_interruptible(id->wq, id->packets==0);
	if (r<0) goto cancel;
	return 0;

cancel:	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("Canceling pending URB %p, packets=%i", id->urb, id->packets);
	PDEBUG("i2c_cancel: qc=%p, id=%p",qc,id);
	PDEBUG("i2c_cancel: id->urb=%p", id->urb);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	if (qc->connected) {
		PDEBUG("i2c_cancel: id->urb->dev=%p", id->urb->dev);
		if (id->urb->dev!=NULL) {
			PDEBUG("i2c_cancel: id->urb->dev->bus=%p", id->urb->dev->bus);
			if (id->urb->dev->bus!=NULL) {
				PDEBUG("i2c_cancel: id->urb->dev->bus->op=%p", id->urb->dev->bus->op);
				//PDEBUG("id->urb->dev->bus->op->unlink=%p", id->urb->dev->bus->op->unlink);
			}
		}
	}
#endif
	/* Cancel URB if it is in progress or in completion handler */
	if (id->packets > 0) qc_unlink_urb_sync(id->urb);
	TEST_BUGR_MSG(id->packets!=0, "i2c_wait: packets=%i", id->packets);
	return 0;
}
/* }}} */
/* {{{ [fold] (private) qc_i2c_set0(struct quickcam *qc, unsigned char regnum, unsigned char loval, unsigned char hival, int flags) */
/* Called from qc_i2c_set and qc_i2c_setw, should not be called elsewhere */
static int qc_i2c_set0(struct quickcam *qc, unsigned char regnum, unsigned char loval, unsigned char hival, int flags)
{
	struct qc_i2c_data *id = &qc->i2c_data;
	unsigned int newhead;
	signed int r;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_i2c_set0(quickcam=%p,reg=%02X,val=%02X%02X)",qc,regnum,hival,loval);
	TEST_BUGR(qc==NULL);
	IDEBUG_TEST(*id);
	newhead = id->newhead;
	id->commands[newhead].loval  = loval;
	id->commands[newhead].hival  = hival;
	id->commands[newhead].regnum = regnum;
	id->commands[newhead].flags  = flags;
	newhead = (newhead + 1) % I2C_MAXCOMMANDS;
	if (newhead == id->tail) {		/* If buffer is full, wait until it's empty */
		if (qcdebug&QC_DEBUGCONTROLURBS) PDEBUG("i2c buffer is full, waiting");
		r = qc_i2c_wait(qc);
		if (r<0) return r;
	}
	TEST_BUGR(newhead==id->tail);	/* no i2c buffer space but nothing to send!!! */
	id->newhead = newhead;
	return 0;
}
/* }}} */
/* {{{ [fold] qc_i2c_set(struct quickcam *qc, unsigned char reg, unsigned char val) */
/* Set an I2C register to desired value */
/* (queue setting to be sent later when qc_i2c_flush() is called) */
inline int qc_i2c_set(struct quickcam *qc, unsigned char reg, unsigned char val)
{
	return qc_i2c_set0(qc, reg, val, 0, 0);
}
/* }}} */
/* {{{ [fold] qc_i2c_setw(struct quickcam *qc, unsigned char reg, unsigned short val) */
/* Set a two-byte (word length) I2C register to desired value (queue setting to be sent later) */
/* (queue setting to be sent later when qc_i2c_flush() is called) */
inline int qc_i2c_setw(struct quickcam *qc, unsigned char reg, unsigned short val)
{
	return qc_i2c_set0(qc, reg, val & 0xFF, (val >> 8) & 0xFF, I2C_FLAG_WORD);
}
/* }}} */
/* {{{ [fold] qc_i2c_break(struct quickcam *qc)  */
/* The next register written will be sent in another packet */
int qc_i2c_break(struct quickcam *qc)
{
	struct qc_i2c_data *id = &qc->i2c_data;
	unsigned int prevhead;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_i2c_break(quickcam=%p)",qc);
	TEST_BUGR(qc==NULL);
	IDEBUG_TEST(*id);
	/* We access an entry that may be already submitted and even finished */
	/* But it should not harm */
	prevhead = (id->newhead + I2C_MAXCOMMANDS - 1) % I2C_MAXCOMMANDS;
	id->commands[prevhead].flags |= I2C_FLAG_BREAK;
	barrier();
	return qc_i2c_flush(qc);
}
/* }}} */
/* {{{ [fold] qc_i2c_init(struct quickcam *qc) */
/* Initialize structures and hardware for I2C communication */
static int qc_i2c_init(struct quickcam *qc)
{
	struct qc_i2c_data *id = &qc->i2c_data;
	struct urb *urb;
	struct usb_ctrlrequest *cr;
	int r = -ENOMEM;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_i2c_init(quickcam=%p)",qc);
	TEST_BUGR(qc==NULL);

	id->tail = id->head = id->newhead = 0;	/* Next position to be filled and sent is 0 */
	id->packets = 0;
	init_waitqueue_head(&id->wq);

	/* Allocate an URB and associated buffers and fill them */
	urb = id->urb = usb_alloc_urb(0,GFP_KERNEL);
	if (!urb) goto fail1;
	cr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	urb->setup_packet = (unsigned char *)cr;
	if (!cr) goto fail2;
	urb->transfer_buffer = kmalloc(qc_i2c_maxbufsize*sizeof(u8), GFP_KERNEL);	/* Allocate maximum ever needed */
	if (!urb->transfer_buffer) goto fail3;
	spin_lock_init(&urb->lock);
	urb->complete = qc_i2c_handler;
	urb->context  = qc;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
	urb->timeout  = 3*HZ;			/* 1 s */
#endif
	cr->bRequestType = 0x40;
	cr->bRequest     = 0x04;
	cr->wIndex       = 0;
	IDEBUG_INIT(*id);
	return 0;

fail3:	kfree(cr);
fail2:	usb_free_urb(urb);
	POISON(id->urb);
fail1:	return r;
}
/* }}} */
/* {{{ [fold] qc_i2c_exit(struct quickcam *qc) */
/* Close messaging, free up memory, stop messaging */
static void qc_i2c_exit(struct quickcam *qc)
{
	struct qc_i2c_data *id = &qc->i2c_data;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_i2c_exit(qc=%p)",qc);
	TEST_BUG(qc==NULL);
	qc_i2c_wait(qc);
	kfree(id->urb->setup_packet);
	kfree(id->urb->transfer_buffer);
	POISON(id->urb->setup_packet);
	POISON(id->urb->transfer_buffer);
	usb_free_urb(id->urb);
	IDEBUG_EXIT(*id);
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_proc:   /proc interface *********************************** */
#if HAVE_PROCFS

static struct proc_dir_entry *qc_proc_entry = NULL;	/* Initialization should not be necessary, but just in case... */

/* {{{ [fold] qc_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data) */
static inline const char *qc_proc_yesno(Bool b)
{
	return b ? "Yes" : "No";
}

static int qc_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct quickcam *qc = data;
	char *out = page;
	int len;

	if (qc_lock(qc) < 0) return 0;

	out += sprintf(out, "\tGeneral driver status\n");
	out += sprintf(out, "Driver version   : %s\n", VERSION);
	out += sprintf(out, "Kernel version   : %s\n", UTS_RELEASE);
	if (qc->dev!=NULL) {
	out += sprintf(out, "Device Id        : %04X:%04X\n", (int)GET_VENDORID(qc), (int)GET_PRODUCTID(qc));
	out += sprintf(out, "USB bus number   : %i\n", qc->dev->bus->busnum);
	}
	out += sprintf(out, "Users            : %i\n", qc->users);
	out += sprintf(out, "Connected        : %s\n", qc_proc_yesno(qc->connected));

	out += sprintf(out, "\n\tPicture settings set by user\n");
	out += sprintf(out, "Brightness       : %d\n", (int)qc->vpic.brightness);
	out += sprintf(out, "Hue              : %d\n", (int)qc->vpic.hue);
	out += sprintf(out, "Color            : %d\n", (int)qc->vpic.colour);
	out += sprintf(out, "Contrast         : %d\n", (int)qc->vpic.contrast);
	out += sprintf(out, "Whiteness        : %d\n", (int)qc->vpic.whiteness);
	if (qc->users > 0) {
	out += sprintf(out, "Depth            : %d\n", (int)qc->vpic.depth);
	out += sprintf(out, "Palette          : %s\n", qc_fmt_getname(qc->vpic.palette));
	}

	if (qc->users > 0) {
	out += sprintf(out, "\n\tOutput window\n");
	out += sprintf(out, "Width            : %d\n", (int)qc->vwin.width);
	out += sprintf(out, "Height           : %d\n", (int)qc->vwin.height);
	}

	out += sprintf(out, "\n\tSensor\n");
	out += sprintf(out, "Type             : %s\n", qc->sensor_data.sensor->name);
	out += sprintf(out, "Manufacturer     : %s\n", qc->sensor_data.sensor->manufacturer);
	if (qc->users > 0) {
	out += sprintf(out, "Maximum width    : %d\n", qc->sensor_data.maxwidth);
	out += sprintf(out, "Maximum height   : %d\n", qc->sensor_data.maxheight);
	out += sprintf(out, "Current width    : %d\n", qc->sensor_data.width);
	out += sprintf(out, "Current height   : %d\n", qc->sensor_data.height);
	}

	out += sprintf(out, "\n\tI2C command stream\n");
	out += sprintf(out, "Scheduled packets: %d\n", qc->i2c_data.packets);
	out += sprintf(out, "Packets on queue : %d\n", (I2C_MAXCOMMANDS + qc->i2c_data.head - qc->i2c_data.tail) % I2C_MAXCOMMANDS);

	if (qc->users > 0) {
	out += sprintf(out, "\n\tIsochronous data stream\n");
	out += sprintf(out, "Stream enabled   : %s\n", qc_proc_yesno(qc->isoc_data.streaming));
	out += sprintf(out, "Transfer errors  : %d\n", qc->isoc_data.errorcount);

	out += sprintf(out, "\n\tFrame buffering\n");
	out += sprintf(out, "Frames on queue  : %d\n", (FRAME_BUFFERS + qc->frame_data.head - qc->frame_data.tail) % FRAME_BUFFERS);
	out += sprintf(out, "Capturing        : %s\n", qc_proc_yesno(qc->stream_data.capturing));
	out += sprintf(out, "Waiting processes: %d\n", qc->frame_data.waiting);
	}

	out += sprintf(out, "\n\tAutomatic exposure control\n");
	out += sprintf(out, "Picture intensity: %d\n", qc->adapt_data.oldmidvalue);
	out += sprintf(out, "Exposure setting : %d\n", qc->adapt_data.exposure);
	out += sprintf(out, "Gain setting     : %d\n", qc->adapt_data.gain);
	out += sprintf(out, "Delta value      : %d\n", qc->adapt_data.olddelta);
	out += sprintf(out, "Control algorithm: ");
	switch (qc->adapt_data.controlalg) {
		case EXPCONTROL_SATURATED: out += sprintf(out, "Saturated\n"); break;
		case EXPCONTROL_NEWTON:    out += sprintf(out, "Newton\n"); break;
		case EXPCONTROL_FLOAT:     out += sprintf(out, "Float\n"); break;
		default: out += sprintf(out, "?\n"); break;
	}

	out += sprintf(out, "\n\tDefault settings\n");
	out += sprintf(out, "Debug            : 0x%02X\n", qcdebug);
	out += sprintf(out, "Keep settings    : %s\n", qc_proc_yesno(qc->settings.keepsettings));
	out += sprintf(out, "Settle max frames: %i\n", qc->settings.settle);
	out += sprintf(out, "Subsampling      : %s\n", qc_proc_yesno(qc->settings.subsample));
	out += sprintf(out, "Compress         : %s\n", qc_proc_yesno(qc->settings.compress));
	out += sprintf(out, "Frame skipping   : %i\n", qc->settings.frameskip);
	out += sprintf(out, "Image quality    : %i\n", qc->settings.quality);
	out += sprintf(out, "Adaptive         : %s\n", qc_proc_yesno(qc->settings.adaptive));
	out += sprintf(out, "Equalize         : %s\n", qc_proc_yesno(qc->settings.equalize));
	out += sprintf(out, "User lookup-table: %s\n", qc_proc_yesno(qc->settings.userlut));
	out += sprintf(out, "Retryerrors      : %s\n", qc_proc_yesno(qc->settings.retryerrors));
	out += sprintf(out, "Compatible 16x   : %s\n", qc_proc_yesno(qc->settings.compat_16x));
	out += sprintf(out, "Compatible DblBuf: %s\n", qc_proc_yesno(qc->settings.compat_dblbuf));
	out += sprintf(out, "Compatible ToRgb : %s\n", qc_proc_yesno(qc->settings.compat_torgb));

	up(&quickcam_list_lock);

	len = out - page;
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0) return 0;
	} else
		len = count;
	*start = page + off;
	return len;
}
/* }}} */
/* {{{ [fold] qc_proc_write(struct file *file, const char *buffer, unsigned long count, void *data) */
static int qc_proc_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	/* we don't support this....yet? Might replace qcset some day */
	return -EINVAL;
}
/* }}} */
/* {{{ [fold] qc_proc_create(struct quickcam *qc) */
/* Called for each camera plugged in, create file containing information of the camera */
static int qc_proc_create(struct quickcam *qc)
{
	char name[9];
	struct proc_dir_entry *entry;
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_proc_create(quickcam=%p)",qc);
	TEST_BUGR(!qc);
	qc->proc_entry = NULL;
	if (qc_proc_entry==NULL) return -ENOTDIR;
	sprintf(name, "video%d", qc->vdev.minor);
	entry = create_proc_entry(name, S_IFREG | S_IRUGO | S_IWUSR, qc_proc_entry);
	if (!entry) {
		PRINTK(KERN_WARNING,"Could not register procfs file entry");
		return -ENXIO;
	}
	entry->owner = THIS_MODULE;
	entry->data = qc;
	entry->read_proc = qc_proc_read;
	entry->write_proc = qc_proc_write;
	qc->proc_entry = entry;
	return 0;
}
/* }}} */
/* {{{ [fold] qc_proc_destroy(struct quickcam *qc) */
/* qc_proc_destroy may be called after qc_proc_create for given quickcam even if it failed */
static void qc_proc_destroy(struct quickcam *qc)
{
	char name[9];
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_proc_destroy(quickcam=%p)",qc);
	TEST_BUG(!qc);
	if (!qc->proc_entry) return;
	TEST_BUG(!qc_proc_entry);
	sprintf(name, "video%d", qc->vdev.minor);
	remove_proc_entry(name, qc_proc_entry);
	POISON(qc->proc_entry);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_proc_destroy() done");
}
/* }}} */
/* {{{ [fold] qc_proc_init(void) */
/* Called when the driver is initially loaded, creates "/proc/video/quickcam" subdirectory */
static int qc_proc_init(void)
{
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_proc_init()");
	proc_mkdir("video", NULL);			/* Might fail, if the directory already exists, but we don't care */
	qc_proc_entry = create_proc_entry(qc_proc_name, S_IFDIR, NULL);
	if (!qc_proc_entry) {
		PRINTK(KERN_WARNING,"Could not register procfs dir entry");
		return -ENXIO;
	}
	qc_proc_entry->owner = THIS_MODULE;
	return 0;
}
/* }}} */
/* {{{ [fold] qc_proc_exit(void) */
/* Can be called after qc_proc_init() even if it has failed, in which case this does nothing */
static void qc_proc_exit(void)
{
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_proc_exit()");
	if (!qc_proc_entry) return;
	remove_proc_entry(qc_proc_name, NULL);
	POISON(qc_proc_entry);
}
/* }}} */

#else
static inline int qc_proc_create(struct quickcam *qc) { return 0; }
static inline void qc_proc_destroy(struct quickcam *qc) { }
static inline int qc_proc_init(void) { return 0; }
static inline void qc_proc_exit(void) { }
#endif /* HAVE_PROCFS */
/* }}} */
/* {{{ [fold] **** qc_adapt:  Automatic exposure control ************************ */

#define MEASURE_ADAPT_DELAY 0		/* Measure adaptation delay, only for test purposes */

/* {{{ [fold] qc_adapt_init(struct quickcam *qc) */
/* Initialize automatic exposure control structure. */
static int qc_adapt_init(struct quickcam *qc)
{
	struct qc_adapt_data *ctrl = &qc->adapt_data;
	ctrl->gain         = 32768;
	ctrl->olddelta     = 4*256;			/* best guess */
	ctrl->exposure     = 32768;
	ctrl->oldexposure  = ctrl->exposure + 1;	/* Slightly different for _issettled() */
	ctrl->midvaluesum  = ctrl->oldmidvalue = 0;
	ctrl->framecounter = 0;
	ctrl->controlalg   = EXPCONTROL_SATURATED;
	IDEBUG_INIT(*ctrl);
	return 0;
}
/* }}} */
/* {{{ [fold] qc_adapt_exit(struct quickcam *qc) */

static inline void qc_adapt_exit(struct quickcam *qc)
{
#ifdef DEBUG
	struct qc_adapt_data *ctrl = &qc->adapt_data;
	if (qcdebug&QC_DEBUGINIT) PDEBUG("qc_adapt_exit(ctrl=%p)",ctrl);
	IDEBUG_EXIT(*ctrl);
#endif
}

/* }}} */
/* {{{ [fold] qc_adapt_reset(struct quickcam *qc) */
/* Must be called each time just before starting video adaptation */
static inline void qc_adapt_reset(struct quickcam *qc)
{
	IDEBUG_TEST(qc->adapt_data);
	if (!qc->settings.keepsettings) {
		IDEBUG_EXIT(qc->adapt_data);
		qc_adapt_init(qc);
	}
}
/* }}} */
/* {{{ [fold] qc_adapt_hassettled(struct quickcam *qc) */
/* Return TRUE if the image brightness has settled */
static inline Bool qc_adapt_hassettled(struct quickcam *qc)
{
	struct qc_adapt_data *ctrl = &qc->adapt_data;
	IDEBUG_TEST(*ctrl);
	if (ctrl->framecounter != 0) return FALSE;
//PDEBUG("control=%i  oldexp=%i  exp=%i",ctrl->controlalg,ctrl->oldexposure,ctrl->exposure);
	return ctrl->controlalg==EXPCONTROL_FLOAT || ctrl->oldexposure==ctrl->exposure;
}
/* }}} */
/* {{{ [fold] qc_adapt(struct quickcam *qc, int midvalue, int target, int *ret_exposure, int *ret_gain) */

/* Set image exposure and gain so that computed midvalue approaches the target value.
 * midvalue = average pixel intensity on image 0..255
 * target   = user settable preferable intensity 0..255
 * *ret_exposure = the exposure value to use for the camera, 0..65535
 * *ret_gain     = the gain to use for the camera, 0..65535.
 */
static void qc_adapt(struct quickcam *qc, int midvalue, int target, int *ret_exposure, int *ret_gain)
{
#if !MEASURE_ADAPT_DELAY
	struct qc_adapt_data *ctrl = &qc->adapt_data;
	/* Here are some constant for controlling the adaptation algorithm. You may play with them. */
	static const int saturation_min = 32;			/* (0-127) If midvalue is out of this range, image is */
	static const int saturation_max = 256 - 8;		/* (128-255) considered saturated and no Newton is used */

	static const int adaptation_min = 5;			/* (0-128) For small variations, do not change exposure */

	static const int delta_min      = 256/2;		/* (2-16*256) Minimum and maximum value for delta */
	static const int delta_max      = 256*256;		/* (4*256-1024*256) */
	
	static const int dmidvalue_min  = 400;			/* (1-128) Minimum differences, under which delta estimation (FIXME:disabled by changing values very big) */
	static const int dexposure_min  = 400;			/* (1-32000) will not be done due to inaccuracies */
	
	static const int delta_speed    = 256;			/* (0-256) How fast or slowly delta can change */
	static const int small_adapt    = 4;			/* (0-1024) When very near optimal, exposure change size */
	static const int underestimate  = 16;			/* (0-250) Underestimation, may prevent oscillation */
	static const int bestguess      = 256/2;		/* (2-1024*256) If delta can not be computed, guess this */
	static const int midvalueaccum  = 2;			/* (1-100) How many frames to use for midvalue averaging */
	static const int framedelay     = 5;			/* (0-8) How many frames there are before a new exposure setting in effect */
								/* With QuickCam Web: if set at frame #0, it will be in effect at frame #4; skip 3 frames #1,#2,#3 */
								/* -> should be 3 with QuickCam Web, but it oscillates, FIXME:why? Setting to 4 fixes this */
	static const int gainstep       = 256;			/* (0-32768) Amount to change gain at one step */
	static const int gainneeded     = 10;			/* (0-255) How eagerly to change brightness with gain */
	/* End of tunable constants */

	int newexposure, delta=0;
	int dexposure=0, dmidvalue=0;
	int deviation=0;			/* Deviation of actual brightness from target brightness */
	int smoothdelta=0;			/* Final, smoothed, value of delta */

	TEST_BUG(ctrl==NULL || ret_gain==NULL || ret_exposure==NULL);
	IDEBUG_TEST(*ctrl);

	if (ctrl->framecounter >= framedelay)
		ctrl->midvaluesum += midvalue;
	ctrl->framecounter++;
	if (ctrl->framecounter < framedelay+midvalueaccum) {
		*ret_exposure = ctrl->exposure;
		*ret_gain     = ctrl->gain;
		return;
	}

	midvalue = ctrl->midvaluesum / midvalueaccum;
	ctrl->framecounter = 0;
	ctrl->midvaluesum  = 0;

	if (ctrl->exposure >= qc->sensor_data.sensor->adapt_gainhigh && 
	    ctrl->oldexposure >= qc->sensor_data.sensor->adapt_gainhigh &&
	    target - ctrl->oldmidvalue > gainneeded &&
	    target - midvalue > gainneeded)
	{
		/* Exposure is at maximum, but image is still too dark. Increase gain.*/
		ctrl->gain = ctrl->gain + ctrl->gain/2 + gainstep;
		if (ctrl->gain > 65535) ctrl->gain = 65535;
		if (qcdebug&QC_DEBUGADAPTATION) PDEBUG("increasing gain to %i", ctrl->gain);
	} else 
	if (ctrl->exposure <= qc->sensor_data.sensor->adapt_gainlow &&
	    ctrl->oldexposure <= qc->sensor_data.sensor->adapt_gainlow &&
	    target - ctrl->oldmidvalue <= gainneeded &&
	    target - midvalue <= gainneeded)
	{
		/* Decrease gain if unnecessarily high */
		ctrl->gain = ctrl->gain - ctrl->gain/2 - gainstep;
		if (ctrl->gain < 0) ctrl->gain = 0;
		if (qcdebug&QC_DEBUGADAPTATION) PDEBUG("decreasing gain to %i", ctrl->gain);
	}
	
	if (ctrl->oldmidvalue<saturation_min || midvalue<saturation_min) {
		/* Image was undersaturated, Newton method would give inaccurate results */
		if (qcdebug&QC_DEBUGADAPTATION) PDEBUG("Increasing exposure");
		ctrl->controlalg = EXPCONTROL_SATURATED;
		newexposure = ctrl->exposure * 2;
	} else
	if (ctrl->oldmidvalue>=saturation_max || midvalue>=saturation_max) {
		/* Image is oversaturated, Newton method would give inaccurate results */
		if (qcdebug&QC_DEBUGADAPTATION) PDEBUG("Decreasing exposure");
		ctrl->controlalg = EXPCONTROL_SATURATED;
		newexposure = ctrl->exposure / 2;
	} else {
		deviation = target - midvalue;
		if (ABS(deviation) < adaptation_min) {
			/* For small variations, adapt linearly */
			if (qcdebug&QC_DEBUGADAPTATION) PDEBUG("Small deviation %i",deviation);
			ctrl->controlalg = EXPCONTROL_FLOAT;
			newexposure = small_adapt * SGN(deviation) + ctrl->exposure;
		} else {
			/* Try using Newton method for estimating correct exposure value */
			ctrl->controlalg = EXPCONTROL_NEWTON;
			dmidvalue = midvalue       - ctrl->oldmidvalue;
			dexposure = ctrl->exposure - ctrl->oldexposure;
			if (ABS(dmidvalue) <  dmidvalue_min || 
			    ABS(dexposure) <  dexposure_min ||
			    SGN(dmidvalue) != SGN(dexposure))
			{
				/* Can not estimate delta with Newton method, just guess */
				if (ctrl->olddelta < 2) {
					if (qcdebug&QC_DEBUGADAPTATION) PDEBUG("Best guessing");
					smoothdelta = bestguess;
				} else {
					Bool cross = SGN(midvalue-target) != SGN(ctrl->oldmidvalue-target);
					smoothdelta = cross ? (ctrl->olddelta / 2) : (ctrl->olddelta * 3 / 2);
					if (qcdebug&QC_DEBUGADAPTATION) PDEBUG("Change more exposure, smoothdelta=%i",smoothdelta);
				}
			} else {
				/* Everything is well, use here actual Newton method */
				delta       = (256 - underestimate) * dexposure / dmidvalue;
				smoothdelta = (delta_speed*delta + (256-delta_speed)*ctrl->olddelta) / 256;
				if (qcdebug&QC_DEBUGADAPTATION) PDEBUG("Using Newton, delta=%i",delta);
			}
		}
		/* Compute new exposure based on guessed/computed delta */
		smoothdelta = CLIP(smoothdelta, delta_min,delta_max);
		dexposure = deviation * smoothdelta / 256;
		/* Newton works linearly, but exposure/brightness are not linearly related */
		/* The following test fixes the worst deficiencies due to that (I hope) */
		if (-dexposure > ctrl->exposure/2)
			dexposure = -ctrl->exposure/2;
		newexposure = dexposure + ctrl->exposure;
		ctrl->olddelta = smoothdelta;
	}

	newexposure       = CLIP(newexposure, 2,65535);

	if (qcdebug&QC_DEBUGADAPTATION) 
		PDEBUG("midval=%i dev=%i dmidv=%i dexp=%i smdelta=%i olddelta=%i newexp=%i gain=%i",
		midvalue,deviation,dmidvalue,dexposure,smoothdelta,ctrl->olddelta,newexposure,ctrl->gain);

	ctrl->oldexposure = ctrl->exposure;
	ctrl->exposure    = newexposure;
	ctrl->oldmidvalue = midvalue;
	*ret_exposure     = newexposure;
	*ret_gain         = ctrl->gain;
#else
	/* This code is for measuring the delay between an exposure settings and until
	 * it becomes in effect. Only useful for developing the adaptation algorithm. */
	/* Some delays: when a setting is changed at frame number #0,
	 * it becomes in effect in frame xx for	exposure	gain
	 * QuickCam Web/0850/normal mode	4		4
	 * QuickCam Web/0850/compressed mode	5		5
	 * QuickCam Express/840			2		1-5
	 *
	 */
	static int exp = 0;
	static int gain = 0;
	static const int changedel = 20;
	static int state = 0;
	static int framenum = 0;
	PRINTK(KERN_CRIT,"Measuring: framenum=%i, midvalue=%i",framenum,midvalue);
	if ((framenum%changedel)==0) {
		switch (state) {
		default:
		case 0:
			PRINTK(KERN_CRIT,"Measuring: set to black");
			exp = 0;
			gain = 0;
			break;
		case 1:
			PRINTK(KERN_CRIT,"Measuring: changing exposure");
			exp = 65535;
			break;
		case 2:
			PRINTK(KERN_CRIT,"Measuring: changing gain");
			gain = 32535;
			break;
		}
		state = ((state+1) % 3);
	}
	*ret_exposure = exp;
	*ret_gain = gain;
	framenum++;
#endif
}

/* }}} */

/* }}} */
/* {{{ [fold] **** qc_frame:  Frame capturing functions ************************* */

/* From /usr/src/linux/Documentation/smp.tex:
 * + Kernel mode process (e.g. system calls):
 *   - No other kernel mode processes may run simultaneously/pre-empt
 *     (kernel mode processes are atomic with respect to each other)
 *     (Does not hold for 2.6.x)
 *   - Exception is voluntary sleeping, in which case re-entry is allowed
 *     (Does not hold for 2.6.x)
 *   - Interrupts may pre-empt (but return to same process)
 *     (interrupts can be disabled if necessary)
 * + Interrupt mode execution
 *   - Kernel mode process may not pre-empt/execute simultaneously
 *   - Other interrupts may pre-empt, however same interrupt is not nested
 */

/* We have here a quite typical producer-consumer scheme:
 * Interrupt routine produces more frame data, while
 * kernel mode processes consume it
 * Read: Linux Device Drivers, Alessandro Rubini et al, 2nd edition, pg. 279
 * "Using Circular Buffers"
 */

/* Initialization and cleanup routines, called from kernel mode processes */
/* {{{ [fold] qc_frame_init(struct quickcam *qc) */
static int qc_frame_init(struct quickcam *qc)
{
	struct qc_frame_data *fd = &qc->frame_data;
	int n;

	if (qcdebug&QC_DEBUGFRAME || qcdebug&QC_DEBUGINIT) PDEBUG("qc_frame_init(qc=%p)",qc);
	TEST_BUGR(qc==NULL || fd==NULL);
	TEST_BUGR(in_interrupt());
	fd->rawdatabuf = vmalloc(FRAME_DATASIZE * FRAME_BUFFERS);
	if (!fd->rawdatabuf) return -ENOMEM;
	memset(fd->rawdatabuf, 0, FRAME_DATASIZE * FRAME_BUFFERS);	/* Never let user access random kernel data */
	fd->head       = 0;		/* First buffer to fill */
	fd->tail       = 0;		/* First buffer to get */
	spin_lock_init(&fd->tail_lock);
	fd->tail_in_use= FALSE;
	init_waitqueue_head(&fd->wq);
	fd->waiting    = 0;
	fd->exiting    = FALSE;
	for (n=0; n<FRAME_BUFFERS; n++) fd->buffers[n].rawdatalen = 0;
	fd->lost_frames = 0;
	IDEBUG_INIT(*fd);
	return 0;
}
/* }}} */
/* {{{ [fold] qc_frame_exit(struct quickcam *qc) */
/* This function must be called with qc->lock acquired 
 * (it may release it temporarily and sleep) */
static void qc_frame_exit(struct quickcam *qc)
{
	struct qc_frame_data *fd = &qc->frame_data;
#if PARANOID
	unsigned long startjiffy = jiffies;
#endif
	if (qcdebug&QC_DEBUGFRAME || qcdebug&QC_DEBUGINIT) PDEBUG("qc_frame_exit(qc=%p,tail=%i,head=%i)",qc,fd->tail,fd->head);
	TEST_BUG(in_interrupt());
	TEST_BUG(qc==NULL || fd==NULL);
	fd->exiting = TRUE;
	fd->maxrawdatalen = 0;		/* Hopefully stops all ongoing captures, might need locking though */
	wake_up(&fd->wq);
	if (qcdebug&QC_DEBUGFRAME) PDEBUG("waiting=%i",fd->waiting);
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_frame_exit() : %i", qc, sem_getcount(&qc->lock));
	up(&qc->lock);			/* The lock was down when entering this function */
	while (fd->waiting > 0) {
		schedule();
#if PARANOID
		if (jiffies-startjiffy > 60*HZ) {
			PRINTK(KERN_CRIT,"Wait queue never completing!! (waiting=%i)",fd->waiting);
			break;
		}
#endif
	}
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down(%p) in qc_frame_exit() : %i", qc, sem_getcount(&qc->lock));
	down(&qc->lock);
	vfree(fd->rawdatabuf);
	POISON(fd->rawdatabuf);
	IDEBUG_EXIT(*fd);
}
/* }}} */

/* Consumer routines, called from kernel mode processes */
/* {{{ [fold] qc_frame_get(struct quickcam *qc, unsigned char **buf) */
/* Wait until next frame is ready and return the frame length
 * and set buf to point to the frame. If error happens,
 * return standard Linux negative error number.
 * qc_frame_free() must be called after the frame is not needed anymore.
 * qc->lock must be acquired when entering this routine
 * (it may release it temporarily and sleep).
 */
static int qc_frame_get(struct quickcam *qc, unsigned char **buf)
{
	struct qc_frame_data *fd = &qc->frame_data;
	int ret;

	TEST_BUGR(qc==NULL || fd==NULL || fd->tail_in_use);
	TEST_BUGR(in_interrupt());
	IDEBUG_TEST(*fd);

	/* Wait until the next frame is available */
	if (qcdebug&QC_DEBUGFRAME) PDEBUG("qc_frame_get/consume(qc=%p,tail=%i,head=%i)",qc,fd->tail,fd->head);
	fd->waiting++;
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_frame_get() : %i", qc, sem_getcount(&qc->lock));
	up(&qc->lock);					/* Release lock while waiting */

	ret = wait_event_interruptible(fd->wq, fd->head!=fd->tail || fd->exiting);	//FIXME:What if we get -ERESTARTSYS?
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down(%p) in qc_frame_get() : %i", qc, sem_getcount(&qc->lock));
	down(&qc->lock);
	if (!ret) {
		if (!fd->exiting) {
			unsigned int t;
			spin_lock(&fd->tail_lock);
			fd->tail_in_use = TRUE;
			t = fd->tail;
			spin_unlock(&fd->tail_lock);
			if (qcdebug&QC_DEBUGFRAME) PDEBUG("qc_frame_get/consume(qc=%p,tail=%i,head=%i,tail->rawdatalen=%i), got frame",qc,t,fd->head,fd->buffers[t].rawdatalen);
			*buf = fd->rawdatabuf + t*FRAME_DATASIZE;
			ret  = fd->buffers[t].rawdatalen;
		} else {
			ret = -ENODATA;
		}
	}
	fd->waiting--;
	fd->lost_frames = 0;
	if (ret<0 && (qcdebug&(QC_DEBUGERRORS|QC_DEBUGFRAME))) PDEBUG("failed qc_frame_get()=%i",ret);
	return ret;
}
/* }}} */
/* {{{ [fold] qc_frame_free(struct quickcam *qc) */
/* Free up the last frame returned from qc_frame_get() (it must be called first) */
static inline void qc_frame_free(struct quickcam *qc)
{
	struct qc_frame_data *fd = &qc->frame_data;
	TEST_BUG(qc==NULL || fd==NULL);
	TEST_BUG(in_interrupt());
	TEST_BUG(fd->head==fd->tail);			/* The current fd->tail is not available to be freed! */
	IDEBUG_TEST(*fd);
	if (qcdebug&QC_DEBUGFRAME) PDEBUG("qc_frame_free/consume(qc=%p,tail=%i,head=%i)",qc,fd->tail,fd->head);
	/* Free up previous frame and advance to next */
	spin_lock(&fd->tail_lock);
	fd->tail_in_use = FALSE;
	fd->tail = (fd->tail + 1) % FRAME_BUFFERS;
	spin_unlock(&fd->tail_lock);
}
/* }}} */
/* {{{ [fold] qc_frame_test(struct quickcam *qc) */
/* Return TRUE if next frame is immediately available, FALSE otherwise. */
static inline Bool qc_frame_test(struct quickcam *qc)
{
	struct qc_frame_data *fd = &qc->frame_data;
	IDEBUG_TEST(*fd);
	if (qcdebug&QC_DEBUGFRAME) PDEBUG("qc_frame_test/consume(qc=%p,tail=%i,head=%i)",qc,fd->tail,fd->head);
	return fd->head != fd->tail;
}
/* }}} */

/* Producer routines, called from interrupt context */
/* {{{ [fold] qc_frame_begin(struct quickcam *qc) */
/* Begin capturing next frame from camera. If buffer is full, the frame will be lost */
static void qc_frame_begin(struct quickcam *qc)
{
	struct qc_frame_data *fd = &qc->frame_data;
	int framesize, h;
	TEST_BUG(qc==NULL || fd==NULL);
	IDEBUG_TEST(*fd);
	if (qcdebug&QC_DEBUGFRAME) PDEBUG("qc_frame_begin/produce(qc=%p,tail=%i,head=%i)",qc,fd->tail,fd->head);
	if (fd->exiting) return;
	TEST_BUG(fd->rawdatabuf==NULL);
	h = fd->head;
	fd->buffers[h].rawdatalen = 0;

	/* Use sensor information to get the framesize (i.e. how much we expect to receive bytes per image) */
	/* FIXME: should compute data size differently in compressed mode */
	framesize = qc->sensor_data.width * qc->sensor_data.height;
	fd->maxrawdatalen = MIN(framesize, FRAME_DATASIZE);
}
/* }}} */
/* {{{ [fold] qc_frame_add(struct quickcam *qc, unsigned char *data, int datalen) */
/* Store more data for a frame, return nonzero if too much data or other error */
static int qc_frame_add(struct quickcam *qc, unsigned char *data, int datalen)
{
	struct qc_frame_data *fd = &qc->frame_data;
	int h = fd->head;
	int bytes;

	TEST_BUGR(qc==NULL || fd==NULL);
	IDEBUG_TEST(*fd);
	TEST_BUGR(fd->rawdatabuf==NULL);
	if (fd->maxrawdatalen <= fd->buffers[h].rawdatalen) {
		if (qcdebug&QC_DEBUGERRORS) PDEBUG("buffer disabled, maxrawdatalen=%i rawdatalen=%i datalen=%i",fd->maxrawdatalen,fd->buffers[h].rawdatalen, datalen);
		return -EBUSY;
	}
	bytes = MIN(datalen, fd->maxrawdatalen - fd->buffers[h].rawdatalen);
	memcpy(fd->rawdatabuf + h*FRAME_DATASIZE + fd->buffers[h].rawdatalen, data, bytes);
	fd->buffers[h].rawdatalen += bytes;
	if (bytes < datalen) {
		if (qcdebug&QC_DEBUGERRORS) PRINTK(KERN_ERR,"out of buffer space by %i, maxrawdatalen=%i rawdatalen=%i datalen=%i", datalen-bytes,fd->maxrawdatalen,fd->buffers[h].rawdatalen, datalen);
		return -ENOSPC;
	}
	return 0;
}
/* }}} */
/* {{{ [fold] qc_frame_end(struct quickcam *qc) */
/* Finished capturing most recent frame from camera */
/* (may be premature end, in which case some data is missing) */
static void qc_frame_end(struct quickcam *qc)
{
	static const int minrawdatalen = 32*32;	/* If frame length is less than this many bytes, discard it */
	struct qc_frame_data *fd = &qc->frame_data;
	unsigned int t, h;
	Bool lost_frame;
	TEST_BUG(qc==NULL || fd==NULL);
	h = fd->head;
	if (qcdebug&QC_DEBUGFRAME) PDEBUG("qc_frame_end/produce(qc=%p,tail=%i,head=%i), got %i bytes",qc,fd->tail,h,fd->buffers[h].rawdatalen);
	IDEBUG_TEST(*fd);
	fd->maxrawdatalen = 0;			/* Stop frame data capturing */
#if DUMPDATA
	PDEBUG("frame_end: got %i bytes", fd->buffers[h].rawdatalen);
#endif
	if (fd->buffers[h].rawdatalen < minrawdatalen) {
		/* No enough data in buffer, don't advance index */
		if (qcdebug&QC_DEBUGERRORS) PDEBUG("discarding frame with only %u bytes", fd->buffers[h].rawdatalen);
		return;
	}
	h = (h + 1) % FRAME_BUFFERS;		/* Select next frame buffer to fill */

	lost_frame = FALSE;
	spin_lock(&fd->tail_lock);
	t = fd->tail;
	if (t == h) {
		lost_frame = TRUE;
		/* FIXME: the below should work fine for two buffers, but not so well for more. It should be possible
		 * to drop oldest frame even when the current tail is in use. */
		if (fd->tail_in_use) {
			/* Can not drop the oldest frame, it is in use. Drop the newest frame */
			h = (h + FRAME_BUFFERS - 1) % FRAME_BUFFERS;		/* Decrease head by one back to the original */
			if (qcdebug&QC_DEBUGFRAME) PDEBUG("dropping newest frame");
		} else {
			/* Drop the oldest frame */
			fd->tail = (t + 1) % FRAME_BUFFERS;			/* Drop the oldest frame away */
			if (qcdebug&QC_DEBUGFRAME) PDEBUG("dropping oldest frame");
		}
	}
	spin_unlock(&fd->tail_lock);
	if (lost_frame) {
		if (qcdebug&QC_DEBUGCOMMON || qcdebug&QC_DEBUGFRAME) PRINTK(KERN_NOTICE,"frame lost");
		fd->lost_frames++;
		if (fd->lost_frames > 10) {
			/* Here we should call qc_isoc_stop() to stop isochronous
			 * streaming since the application is clearly not reading frames at all.
			 * However, we are now in interrupt context but qc_isoc_stop() has
			 * to be in process context... so we can't do that.
			 * FIXME: add tasklet/bottomhalf/whatever needed to do it.
			 */
			if (qcdebug&QC_DEBUGFRAME) PDEBUG("too many lost frames: %i", fd->lost_frames);
		}
	}
	fd->head = h;
	wake_up(&fd->wq);
}
/* }}} */
/* {{{ [fold] qc_frame_flush(struct quickcam *qc)  */
/* Reject the current data already captured into buffer and end frame */
void qc_frame_flush(struct quickcam *qc)
{
	struct qc_frame_data *fd = &qc->frame_data;
	unsigned int h = fd->head;
	TEST_BUG(qc==NULL || fd==NULL);
	IDEBUG_TEST(*fd);
	if (qcdebug&QC_DEBUGFRAME) PDEBUG("qc_frame_flush/produce(qc=%p,tail=%i,head=%i), flush %i bytes",qc,fd->tail,h,fd->buffers[h].rawdatalen);
	fd->buffers[h].rawdatalen = 0;		/* Empty buffer */
	fd->maxrawdatalen = 0;			/* Stop frame data capturing */
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_stream: USB datastream processing functions *************** */

/* {{{ [fold] qc_stream_init(struct quickcam *qc) */
/* Initialize datastream processing */
static int qc_stream_init(struct quickcam *qc)
{
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_stream_init(quickcam=%p)",qc);
	qc->stream_data.capturing = FALSE;
	qc->stream_data.frameskip = qc->settings.frameskip;
	IDEBUG_INIT(qc->stream_data);
	return 0;
}
/* }}} */
/* {{{ [fold] qc_stream_exit(struct quickcam *qc) */
/* Stop datastream processing, after this qc_stream_add should not be called */
static void qc_stream_exit(struct quickcam *qc)
{
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_stream_exit(quickcam=%p)",qc);
	if (qc->stream_data.capturing)
		qc_frame_end(qc);
	IDEBUG_EXIT(qc->stream_data);
}
/* }}} */
/* {{{ [fold] qc_stream_error(struct quickcam *qc) */
/* This is called when there are data lost due to errors in the stream */
static void qc_stream_error(struct quickcam *qc)
{
	/* Skip rest of data for this frame */
	if (qcdebug&QC_DEBUGERRORS) PDEBUG("qc_stream_error(qc=%p)", qc);
	if (qc->stream_data.capturing)
		qc_frame_end(qc);
	IDEBUG_EXIT(qc->stream_data);
	qc_stream_init(qc);
}
/* }}} */
/* {{{ [fold] qc_stream_add(struct quickcam *qc, unsigned char *data, int datalen) */
/*
 * Analyse an USB packet of the data stream and store it appropriately.
 * Each packet contains an integral number of chunks. Each chunk has
 * 2-bytes identification, followed by 2-bytes that describe the chunk
 * length. Known/guessed chunk identifications are:
 * 8001/8005/C001/C005 - Begin new frame
 * 8002/8006/C002/C006 - End frame
 * 0200/4200           - Contains actual image data, bayer or compressed
 * 0005                - 11 bytes of unknown data
 * 0100                - 2 bytes of unknown data
 * The 0005 and 0100 chunks seem to appear only in compressed stream.
 * Return the amount of image data received or negative value on error.
 */
static int qc_stream_add(struct quickcam *qc, unsigned char *data, int datalen)
{
	struct qc_stream_data *sd = &qc->stream_data;
	int id, len, error, totaldata = 0;
	
	IDEBUG_TEST(*sd);
	while (datalen) {
		if (datalen < 4) {
			if (qcdebug&QC_DEBUGBITSTREAM) PRINTK(KERN_ERR,"missing chunk header");
			break;
		}
		id  = (data[0]<<8) | data[1];
		len = (data[2]<<8) | data[3];
		data    += 4;
		datalen -= 4;
		if (datalen < len) {
			if (qcdebug&QC_DEBUGBITSTREAM) PRINTK(KERN_ERR,"missing chunk contents");
			break;
		}
		switch (id) {
		case 0x8001:
		case 0x8005:
		case 0xC001:
		case 0xC005:
			/* Begin new frame, len should be zero */
			if (PARANOID && len!=0) PDEBUG("New frame: len!=0");
			if (sd->capturing) {
				if (qcdebug&QC_DEBUGBITSTREAM) PDEBUG("Missing frame end mark in stream");
				qc_frame_end(qc);
			}
			sd->capturing = TRUE;
			if (--sd->frameskip < 0) sd->frameskip = qc->settings.frameskip;
			if (sd->frameskip==0) qc_frame_begin(qc);
			break;
		case 0x8002:
		case 0x8006:
		case 0xC002:
		case 0xC006:
			/* End frame, len should be zero */
			if (PARANOID && len!=0) PDEBUG("End frame: len!=0");
			if (sd->capturing) {
				if (sd->frameskip==0) qc_frame_end(qc);
			} else {
				if (qcdebug&QC_DEBUGBITSTREAM) PDEBUG("Missing frame begin mark in stream");
			}
			sd->capturing = FALSE;
			break;
		case 0x0200:
		case 0x4200:
			/* Image data */
			if (!sd->capturing && (qcdebug&QC_DEBUGBITSTREAM)) PDEBUG("Chunk of data outside frames!");
			if (sd->capturing && sd->frameskip==0) {
				error = qc_frame_add(qc, data, len);
			} else {
				error = 0;
			}
			if (error) {
				/* If qc_frame_add returns error, there is more data than the frame may have,
				 * in which case we assume stream is corrupted and skip rest packet */
				if (qcdebug&QC_DEBUGERRORS) PDEBUG("qc_frame_add error %i",error);
			} else {
				totaldata += len;
			}
			break;
		case 0x0005:
			/* Unknown chunk with 11 bytes of data, occurs just before end of each frame in compressed mode */
			if (len==11) break;
		case 0x0100:
			/* Unknown chunk with 2 bytes of data, occurs 2-3 times per USB interrupt */
			if (len==2) break;
		default:
			/* Unknown chunk */
			#ifdef DEBUG
			if (qcdebug&QC_DEBUGBITSTREAM) {
				static char dump[4*1024];
				char *dump_p = dump;
				int i;
				for (i=0; i<len && (3*i+9)<sizeof(dump); i++) dump_p+=sprintf(dump_p, "%02X ", data[i]);
				PDEBUG("Unknown chunk %04X: %s", id, dump);
			}
			#endif
			break;
		}
		data    += len;
		datalen -= len;
	}
	return totaldata;
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_isoc:   Isochronous USB transfer related routines ********* */

/*
 * On my system (Acer Travelmate 332T, usb-ohci) there happens frequently
 * errors. Most common are:
 * -18	EXDEV	(even inside individual frames)
 * -84	EILSEQ
 * -71	EPROTO
 * -110	ETIMEDOUT
 * -75	EOVERFLOW ??
 */
/* {{{ [fold] qc_isoc_handler(struct urb *urb) */
/* This is URB completion handler and is called in interrupt context.
 * For each submitted URB, this function is guaranteed to be called exactly once.
 * This function may not be called reentrantly for the same qc (should be ok, IRQs don't nest).
 * It will resubmit the same urb, unless
 * - Isochronous URB stream is disabled
 * - Camera was disconnected
 * - There are too many transfer errors
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void qc_isoc_handler(struct urb *urb, struct pt_regs *ptregs)
#else
static void qc_isoc_handler(struct urb *urb)
#endif
{
	struct quickcam *qc;
	int payload = 0;	/* Amount of all data camera sent */
	int totaldata = 0;	/* Amount of image data camera sent */
	int i;
#ifdef DEBUG
	/* Check for nested interrupts, shouldn't happen */
	volatile static Bool in_progress = FALSE;
	TEST_BUG(in_progress);
	in_progress = TRUE;
#endif

	if (qcdebug&QC_DEBUGINTERRUPTS) PDEBUG("[INTR] qc_isoc_handler(urb=%p)",urb);
	TEST_BUG(urb==NULL);
	qc = urb->context;
	TEST_BUG(qc==NULL);
	IDEBUG_TEST(qc->isoc_data);

	if (!qc->connected || !qc->isoc_data.streaming) {
		/* Camera was disconnected or isochronous stream just disabled--must not resubmit urb */
		PDEBUG("Ignoring isoc interrupt, dev=%p streaming=%i status=%i", qc->dev, qc->isoc_data.streaming, urb->status);
		goto out;
	}

	if (urb->status<0) {
		qc->isoc_data.errorcount++;
		switch (urb->status) {
		case -EXDEV:		/* Partially completed, look at individual frame status */
			break;
		default:
			/* Seen here: -EOVERFLOW (75): Value too large for defined data type */
		case -EPROTO:		/* Bitstuff error or unknown USB error */
		case -EILSEQ:		/* CRC mismatch */
		case -ETIMEDOUT:	/* Transfer timed out */
		case -EREMOTEIO:	/* Short packet detected */
		case -EPIPE:		/* Babble detect or endpoint stalled */
		case -ECONNRESET:	/* Asynchronous unlink, should not happen, but does with 2.6.x */
			if (qcdebug&QC_DEBUGERRORS) PRINTK(KERN_ERR,"isoc URB error %i, resubmitting",urb->status);
			goto resubmit;
		case -ESHUTDOWN:
		case -ENOENT:		/* URB was unlinked */
		case -ENODEV:		/* Device was removed */
			if (qcdebug&QC_DEBUGERRORS) PRINTK(KERN_ERR,"isoc URB error %i, returning",urb->status);
			goto out;
		}
	}

	for (i=0; i<urb->number_of_packets; i++) {
		if ((int)urb->iso_frame_desc[i].status<0) {			/* Note that the cast to int MUST be here! */
			if (qcdebug&QC_DEBUGERRORS) PDEBUG("USB transfer error %i", urb->iso_frame_desc[i].status);
			qc->isoc_data.errorcount++;
			qc_stream_error(qc);
			continue;
		}
		qc->isoc_data.errorcount = 0;
		payload += urb->iso_frame_desc[i].actual_length;
#if PARANOID
{
int xx = urb->iso_frame_desc[i].actual_length;
if (xx>2000) {
PDEBUG("i=%i status=%i transfer_buffer=%p transfer_buffer_length=%i actual_length=%i number_of_packets=%i", 
 i, urb->status, urb->transfer_buffer, urb->transfer_buffer_length, urb->actual_length, urb->number_of_packets);
PDEBUG("offset=%i length=%i actual_length=%i pstatus=%i", 
urb->iso_frame_desc[i].offset, urb->iso_frame_desc[i].length, urb->iso_frame_desc[i].actual_length, urb->iso_frame_desc[i].status);
goto out;
}
}
#endif
		totaldata += qc_stream_add(qc, urb->transfer_buffer + urb->iso_frame_desc[i].offset,
			urb->iso_frame_desc[i].actual_length);
	}
	if (qcdebug&QC_DEBUGBITSTREAM) PDEBUG("payload=%i  totaldata=%i",payload,totaldata);
	if (qcdebug&(QC_DEBUGBITSTREAM|QC_DEBUGERRORS)) if (payload==0) PDEBUG("USB interrupt, but no data received!");
resubmit:
	/* Resubmit URB */
	if (qc->isoc_data.errorcount < ISOC_PACKETS*ISOC_URBS*8) {
		urb->dev = qc->dev;			/* Required for 2.4.x */
		i = usb_submit_urb(urb,GFP_ATOMIC);
		if (i) PDEBUG("failed to resubmit URB, code=%i, dev=%p",i,urb->dev);
	} else {
		PDEBUG("Too many errors, giving up");
	}
out:
#ifdef DEBUG
	in_progress = FALSE;
#endif
	return;
}
/* }}} */
/* {{{ [fold] qc_isoc_start(struct quickcam *qc) */
/*
 * Start USB isochronous image transfer from camera to computer
 * (Set USB camera interface and allocate URBs and submit them)
 * Sensor must be initialized beforehand (qc_init_sensor)
 */
static int qc_isoc_start(struct quickcam *qc)
{
	struct qc_isoc_data *id = &qc->isoc_data;
	int ret = -ENOMEM;		/* Return value on error */
	struct urb *urb;
	int i, b;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_isoc_start(qc=%p)",qc);
	TEST_BUGR_MSG(qc==NULL || id==NULL, "qc||id==NULL");
	IDEBUG_TEST(*id);

	if (id->streaming) return 0;		/* Already started */
	id->streaming = TRUE;
	id->errorcount = 0;

	/* Allocate transfer buffer */
	id->buffer = kmalloc(ISOC_URBS * ISOC_PACKETS * ISOC_PACKET_SIZE, GFP_KERNEL);
	CHECK_ERROR(!id->buffer, fail1, "Out of memory allocating id->buffer");

	/* Allocate URBs, fill them, and put them in the URB array */
	for (b=0; b<ISOC_URBS; b++) {
		urb = id->urbs[b] = usb_alloc_urb(ISOC_PACKETS,GFP_KERNEL);	/* Zeroes the allocated data up to iso_frame_desc[], *not* including the last! */
		CHECK_ERROR(!urb, fail2, "Out of memory allocating urbs");
		urb->dev                    = qc->dev;
		urb->context                = qc;
		urb->pipe                   = usb_rcvisocpipe(qc->dev, QUICKCAM_ISOPIPE);
		urb->transfer_flags         = URB_ISO_ASAP;
		urb->complete               = qc_isoc_handler;
		urb->number_of_packets      = ISOC_PACKETS;
		urb->transfer_buffer        = id->buffer;
		urb->transfer_buffer_length = ISOC_URBS * ISOC_PACKETS * ISOC_PACKET_SIZE;
		urb->interval               = 1;			/* See Table 9-10 of the USB 1.1 specification */
		for (i=0; i<ISOC_PACKETS; i++) {
			urb->iso_frame_desc[i].offset = b*ISOC_PACKETS*ISOC_PACKET_SIZE + i*ISOC_PACKET_SIZE;
			urb->iso_frame_desc[i].length = ISOC_PACKET_SIZE;
		}
	}

	/* Alternate interface 3 is the biggest frame size */
	/* JFC use 1: but do not know why */
	/* QuickCam Web: Interface 0, alternate 1, endpoint 0x81 -tuukkat */
	qc_i2c_wait(qc);			/* There must not be control URBs going when calling set_interface() */
	ret = usb_set_interface(qc->dev, qc->iface, 1);
	CHECK_ERROR(ret<0, fail3, "set_interface failed");

	/* Submit URBs */
	for (b=0; b<ISOC_URBS; b++) {
		ret = usb_submit_urb(id->urbs[b],GFP_KERNEL);
		CHECK_ERROR(ret<0, fail4, "submit urbs failed");
	}

	/* Tell camera to start sending data */
	ret = qc->sensor_data.sensor->start(qc);	/* Start current frame */
	CHECK_ERROR(ret<0, fail5, "sensor_data.start failed");
	ret = qc_stv_set(qc, STV_ISO_ENABLE, 1);	/* Start isochronous streaming */
	CHECK_ERROR(ret<0, fail6, "qc_stv_set() failed");
	return 0;

	/* Cleanup and return error code on failure */
fail6:	qc->sensor_data.sensor->stop(qc);		/* stop current frame. */
fail5:	b = ISOC_URBS;
fail4:	while (--b >= 0) qc_unlink_urb_sync(id->urbs[b]);
	usb_set_interface(qc->dev, qc->iface, 0);	/* Set packet size to 0 (Interface 0, alternate 0, endpoint 0x81 -tuukkat) */
fail3:	b = ISOC_URBS;
fail2:	while (--b >= 0) usb_free_urb(id->urbs[b]);
	kfree(id->buffer);
fail1:	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGERRORS) PDEBUG("failed qc_isoc_init()=%i",ret);
	return ret;
}
/* }}} */
/* {{{ [fold] qc_isoc_stop(struct quickcam *qc) */
/*
 * Stop USB isochronous image transfer from camera to computer
 * (Tell camera to stop sending images, set idle USB interface and free URBs)
 * There must be no more isochronous transfer interrupts after this returns
 * nor any running handlers anymore.
 */
static void qc_isoc_stop(struct quickcam *qc)
{
	struct qc_isoc_data *id = &qc->isoc_data;
	int b, r;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_isoc_stop(quickcam=%p)",qc);
	TEST_BUG_MSG(qc==NULL || id==NULL, "qc||id==NULL");
	IDEBUG_TEST(*id);

	if (!id->streaming) return;					/* Already stopped */
	if (qc->connected) {
		if ((r=qc_stv_set(qc, STV_ISO_ENABLE, 0))<0)		/* stop ISO-streaming. */
			PRINTK(KERN_ERR,"qc_stv_set error %i",r);
		if ((r=qc->sensor_data.sensor->stop(qc))<0)		/* stop current frame. */
			PRINTK(KERN_ERR,"sensor_data.stop error %i",r);
		qc_i2c_wait(qc);					/* When calling set_interface(), there must not be control URBs on way */
		if (usb_set_interface(qc->dev, qc->iface, 0) < 0)	/* Set packet size to 0 (Interface 0, alternate 0, endpoint 0x81 -tuukkat) */
			PRINTK(KERN_ERR,"usb_set_interface error");
	}
	id->streaming = FALSE;						/* Ensure that no more isochronous URBs will be submitted from the interrupt handler */
	mb();
	for (b=0; b<ISOC_URBS; b++) {					/* Unschedule all of the iso td's */
		PDEBUG("isoc urb[%i]->status = %i", b, id->urbs[b]->status);
		qc_unlink_urb_sync(id->urbs[b]);
		usb_free_urb(id->urbs[b]);
		POISON(id->urbs[b]);
	}

	kfree(id->buffer);
	POISON(id->buffer);
	return;
}
/* }}} */
/* {{{ [fold] qc_isoc_init(struct quickcam *qc) */
/*
 * Initialize isochronous streaming functions
 */
static int qc_isoc_init(struct quickcam *qc)
{
	struct qc_isoc_data *id = &qc->isoc_data;
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_isoc_init(quickcam=%p)",qc);
	TEST_BUGR_MSG(qc==NULL || id==NULL, "qc||id==NULL");
	IDEBUG_INIT(*id);
	id->streaming = FALSE;
	return 0;
}
/* }}} */
/* {{{ [fold] qc_isoc_exit(struct quickcam *qc) */
/*
 * Uninitialize isochronous streaming functions
 */
static inline void qc_isoc_exit(struct quickcam *qc)
{
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_isoc_exit(quickcam=%p)",qc);
	qc_isoc_stop(qc);
	IDEBUG_EXIT(qc->isoc_data);
}
/* }}} */
/* {{{ [fold] Bool qc_isoc_streaming(struct quickcam *qc) */
static inline Bool qc_isoc_streaming(struct quickcam *qc)
{
	struct qc_isoc_data *id = &qc->isoc_data;

	TEST_BUGR_MSG(qc==NULL || id==NULL, "qc||id==NULL");
	IDEBUG_TEST(*id);
	return id->streaming;
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_sensor: Common routines for all sensors ******************* */

/* {{{ [fold] qc_sensor_setsize0(struct quickcam *qc, unsigned int width, unsigned int height) */
/* Called when the application requests a specific image size. Should set the
 * actual delivered image size to as close to the requested as possible.
 * The image size, as delivered from the camera, can be also set to reduce
 * required bandwidth, if possible, but it is not necessary.
 * This is a private function to qc_sensor_*, other modules should use qc_sensor_setsize()
 * If capt is TRUE, then qc_capt_get may be called (and qc_capt_init must be called before).
 */
static int qc_sensor_setsize0(struct quickcam *qc, unsigned int width, unsigned int height, Bool capt)
{
	unsigned char *f;
	int r;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_sensor_setsize(qc=%p,width=%i,height=%i)",qc,width,height);
	TEST_BUGR_MSG(qc==NULL, "qc==NULL!");

	if (width < min_framewidth || width > qc->sensor_data.maxwidth) return -EINVAL;
	if (height < min_frameheight || height > qc->sensor_data.maxheight) return -EINVAL;

	/* Applications require, when using Xvideo extension, that the
	 * frame size is multiple of 8. This is a bug in apps or Xvideo. -tuukkat */
	if (qc->settings.compat_16x) {
		width  = (width /16)*16;
		height = (height/16)*16;
	}
	/* Set the size only if changed */
	if (qc->vwin.width==width && qc->vwin.height==height) return 0;

	/* For HDCS-1000 we must wait for frame before setting size */
	if (capt) qc_capt_get(qc, &f);

	qc->sensor_data.width = width;		/* The sensor-specific code may modify these if not suitable */
	qc->sensor_data.height = height;
	if ((r = qc->sensor_data.sensor->set_size(qc, width, height))<0) {
		PDEBUG("set_size sensor failed");
		return r;
	}

	/* Set the closest size we can actually deliver to application */
	qc->vwin.width  = width;
	qc->vwin.height = height;
	if ((r = qc_i2c_wait(qc))<0) return r;
	qc_frame_flush(qc);	
	return 0;
}
/* }}} */
/* {{{ [fold] qc_sensor_setsize(struct quickcam *qc, unsigned int width, unsigned int height) */
/* Called when the application requests a specific image size. Should set the
 * actual delivered image size to as close to the requested as possible.
 * The image size, as delivered from the camera, can be also set to reduce
 * required bandwidth, if possible, but it is not necessary.
 * qc_isoc_init() and qc_capt_init() have to be called before this function.
 */
static inline int qc_sensor_setsize(struct quickcam *qc, unsigned int width, unsigned int height)
{
	int r;
	r = qc_sensor_setsize0(qc, width, height, qc_isoc_streaming(qc));
	return r;
}
/* }}} */
/* {{{ [fold] qc_sensor_init(struct quickcam *qc) */
/*
 * Initialise sensor. Initializes all data in qc->sensor which is common to all
 * types of sensors and calls the sensor-specific initialization routine.
 * The Photobit starts the pixel integration immediately after the reset.
 * Note: must call qc_i2c_init() and qc_frame_init() before this function!
 */
static int qc_sensor_init(struct quickcam *qc)
{
	int r;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_init_sensor(qc=%p)",qc);
	TEST_BUGR_MSG(qc==NULL, "qc==NULL!");

	qc->sensor_data.width     = -1;
	qc->sensor_data.height    = -1;
	qc->sensor_data.exposure  = -1;
	qc->sensor_data.rgain     = -1;
	qc->sensor_data.ggain     = -1;
	qc->sensor_data.bgain     = -1;
	qc->sensor_data.subsample = qc->settings.subsample;
	qc->sensor_data.compress  = qc->settings.compress;

	if ((r = qc->sensor_data.sensor->init(qc))<0) goto fail;
	if ((r = qc_stv_set(qc, STV_ISO_ENABLE, 0))<0) goto fail;		/* Stop isochronous streaming */
	if ((r = qc->sensor_data.sensor->stop(qc))<0) goto fail;		/* Stop current frame */

	/* Set capture size */
	qc->vwin.width  = 0;			/* Set to illegal value (ensures resetting) */
	qc->vwin.height = 0;
	if ((r = qc_sensor_setsize0(qc, qc->sensor_data.maxwidth, qc->sensor_data.maxheight, FALSE))<0) goto fail;

	/* Set brightness settings */
	if ((r = qc->sensor_data.sensor->set_levels(qc, qc->vpic.brightness, qc->vpic.contrast, qc->vpic.hue, qc->vpic.colour))<0) goto fail;
	if (qc->sensor_data.sensor->set_target!=NULL)
		if ((r = qc->sensor_data.sensor->set_target(qc, qc->vpic.brightness))<0) goto fail;
	return 0;

fail:	PRINTK(KERN_ERR,"sensor initialization failed: %i",r);
	return r;
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_capt:   User image capturing functions ******************** */

/* {{{ [fold] qc_capt_get(struct quickcam *qc, unsigned char **frame) */
/* Wait until next image is ready and return the image length in bytes
 * and set "frame" to point to the image. If error happens,
 * return standard Linux negative error number. The image will be in
 * palette and size requested by the user (quickcam->vpic,vwin).
 */
static int qc_capt_get(struct quickcam *qc, unsigned char **frame)
{
	struct qc_capt_data *cd = &qc->capt_data;
	unsigned char *rawdata;		/* Raw data from camera */
	int rawdatalen;
	int retrycount = qc->settings.retryerrors ? 8 : 0;
	int settlecount = cd->settled ? 0 : qc->settings.settle;	/* If the picture has already settled, do not wait for it again */
	int midvalue;
	int r;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_capt_get(quickcam=%p)",qc);
	IDEBUG_TEST(*cd);
	if ((r = qc_isoc_start(qc))<0) goto fail;		/* Start receiving data */

	do {
		r = qc_frame_get(qc, &rawdata);
		if (r < 0) goto error;
		rawdatalen = r;
		r = qc_fmt_convert(qc, rawdata, rawdatalen, cd->frame, MAX_FRAME_SIZE, &midvalue);
		if (r < 0) {
			qc_frame_free(qc);
			goto error;
		}

		if (qc->vpic_pending) {
			qc->vpic_pending = FALSE;
			if (!qc->settings.adaptive) {
				/* Set new values now */
				qc->sensor_data.sensor->set_levels(qc, qc->vpic.brightness, qc->vpic.contrast, qc->vpic.hue, qc->vpic.colour);
			} else {
				if (qc->sensor_data.sensor->set_target!=NULL)
					qc->sensor_data.sensor->set_target(qc, qc->vpic.brightness);
			}
		}

		if (qc->settings.adaptive && !qc->sensor_data.sensor->autoexposure && r>=0 && midvalue>=0) {
			int ex, gn;
			qc_adapt(qc, midvalue, qc->vpic.brightness>>8, &ex, &gn);
			qc->sensor_data.sensor->set_levels(qc, ex, gn, qc->vpic.hue, qc->vpic.colour);
		}
		qc_frame_free(qc);

		if (qc_adapt_hassettled(qc) || settlecount<=0) break;
		settlecount--;

error:		if (r < 0) {
			if (qcdebug&QC_DEBUGERRORS) PDEBUG("retrying failed qc_frame_get... rounds=%i", retrycount);
			if (r==-ERESTARTSYS || retrycount<=0) break;
			retrycount--;
		}
		qc_i2c_flush(qc);				/* Send all pending I2C transfers */
		schedule();
	} while (TRUE);
	if (r<0) goto fail;
	qc_i2c_flush(qc);				/* Send all pending I2C transfers */
	cd->settled = TRUE;
	if (frame) *frame = cd->frame;
	return r;

fail:	if (qcdebug&(QC_DEBUGERRORS|QC_DEBUGLOGIC)) PDEBUG("failed qc_capt_get()=%i", r);
	return r;
}
/* }}} */
/* {{{ [fold] qc_capt_frameaddr(struct quickcam *qc, unsigned char **frame) */
/* Return size and address of the capture buffer that is suitable for mmapping,
 * Standard Linux errno on error */
static inline int qc_capt_frameaddr(struct quickcam *qc, unsigned char **frame)
{
	IDEBUG_TEST(qc->capt_data);
	if (frame!=NULL) *frame = qc->capt_data.frame;
	return MAX_FRAME_SIZE;
}
/* }}} */
/* {{{ [fold] qc_capt_test(struct quickcam *qc) */
/* Return TRUE if next image is immediately available, FALSE otherwise.
 * Also starts streaming video from camera if not already done so.
 * Before calling this function, qc_isoc_init() must be called first. */
static inline Bool qc_capt_test(struct quickcam *qc)
{
	int e;
	IDEBUG_TEST(qc->capt_data);
	e = qc_isoc_start(qc);
	if (qcdebug&QC_DEBUGERRORS && e<0) PDEBUG("qc_capt_test: qc_isoc_start failed");
	return qc_frame_test(qc);
}
/* }}} */
/* {{{ [fold] qc_capt_init(struct quickcam *qc) */
static int qc_capt_init(struct quickcam *qc)
{
	struct qc_capt_data *cd = &qc->capt_data;
	int r;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_capt_init(quickcam=%p)",qc);

	cd->settled = !(qc->settings.settle>0 && qc->settings.adaptive);

	/* Allocate memory for the (mmappable) capture buffer */
	cd->frame = qc_mm_rvmalloc(MAX_FRAME_SIZE);
	if (!cd->frame) {
		PRINTK(KERN_ERR, "unable to allocate frame");
		r = -ENOMEM;
		goto fail1;
	}

	/* Initialize submodules */
	if ((r=qc_frame_init(qc))<0) goto fail2;	/* Must be before sensor_init() */
	r = qc_sensor_init(qc);				/* Start the sensor (must be after qc_i2c_init but before qc_adapt_init) */
	if (r<0 && qc->settings.compress) {
		/* Sensor init failed with compression. Try again without compression */
		PRINTK(KERN_NOTICE, "sensor init failed, disabling compression");
		qc->settings.compress = 0;
		r = qc_sensor_init(qc);
	}
	if (r<0) goto fail3;
	if ((r=qc_stream_init(qc))<0) goto fail3;
	if ((r=qc_fmt_init(qc))<0) goto fail4;
	if ((r=qc_isoc_init(qc))<0) goto fail5;
	IDEBUG_INIT(*cd);
	return 0;

fail5:	qc_fmt_exit(qc);
fail4:	qc_stream_exit(qc);
fail3:	qc_frame_exit(qc);
fail2:	qc_mm_rvfree(cd->frame, MAX_FRAME_SIZE);
fail1:	PDEBUG("failed qc_capt_init()=%i",r);
	return r;
}
/* }}} */
/* {{{ [fold] qc_capt_exit(struct quickcam *qc) */
static void qc_capt_exit(struct quickcam *qc)
{
	struct qc_capt_data *cd = &qc->capt_data;
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_capt_exit(quickcam=%p)",qc);
	qc_isoc_exit(qc);
	qc_fmt_exit(qc);
	qc_stream_exit(qc);
	qc_frame_exit(qc);
	qc_mm_rvfree(cd->frame, MAX_FRAME_SIZE);
	POISON(cd->frame);
	IDEBUG_EXIT(*cd);
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc_v4l:    Start of Video 4 Linux API ************************ */

/* {{{ [fold] qc_v4l_poll(struct video_device *dev, struct file *file, poll_table *wait) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static unsigned int qc_v4l_poll(struct file *file, poll_table *wait)
#else
static unsigned int qc_v4l_poll(struct video_device *dev, struct file *file, poll_table *wait)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	struct video_device *dev = video_devdata(file);
#endif
	struct quickcam *qc = (struct quickcam *)dev->priv;
	struct qc_frame_data *fd = &qc->frame_data;
	int mask;

	if (qcdebug&QC_DEBUGUSER) PDEBUG("qc_v4l_poll(dev=%p,file=%p,wait=%p)",dev,file,wait);
	if (down_interruptible(&qc->lock)) return -ERESTARTSYS;
	poll_wait(file, &fd->wq, wait);
	mask = qc_capt_test(qc) ? (POLLIN | POLLRDNORM) : 0;
	up(&qc->lock);
	return mask;
}
/* }}} */
/* {{{ [fold] qc_v4l_init(struct quickcam *qc) */
/* Called when the device is opened */
static int qc_v4l_init(struct quickcam *qc)
{
	int r, fps;

	if (!qc->settings.keepsettings) {
		/* Reset brightness settings */
		qc->vpic.brightness = 32768;
		qc->vpic.hue        = 32768;
		qc->vpic.colour     = 32768;
		qc->vpic.contrast   = 32768;
		qc->vpic.whiteness  = 32768;
		qc_adapt_reset(qc);				/* qc_adapt_init() is called from qc_usb_init() */
	}
	qc->vpic.palette = VIDEO_PALETTE_RGB24;
	qc->vpic.depth   = qc_fmt_getdepth(qc->vpic.palette);
	qc->vpic_pending = FALSE;

	fps = qc->settings.subsample ? 30 : 8;	/* May actually vary depending on image size */
	fps = qc->settings.compress ? 15 : fps;	/* Actually 7.5 fps, but we must round it */
	qc->vwin.flags = fps << 16;		/* Bits 22..16 contain framerate in Philips driver. We do the same. */

	if ((r = qc_capt_init(qc))<0) goto fail;
	return 0;

fail:	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGERRORS) PDEBUG("failed qc_v4l_init()=%i",r);
	return r;
}
/* }}} */
/* {{{ [fold] qc_v4l_open(struct video_device *dev, int flags) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int qc_v4l_open(struct inode *inode, struct file *file)
#else
static int qc_v4l_open(struct video_device *dev, int flags)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	struct video_device *dev = video_devdata(file);
#endif
	struct quickcam *qc = dev->priv;
	int r;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGUSER) PDEBUG("qc_v4l_open(qc=%p)", qc);
//PDEBUG("sleeping 10 sec...");
//qc_usleep(1000000*10);
//PDEBUG("sleep done");
	// FIXME: if the module is tried to be unloaded at this point,
	// v4l_close() and MOD_DEC_USE_COUNT will never be called
	// According to "Linux Device drivers" pg.70, it's ok if called before sleeping?
	// 2.2 will crash, 2.4 will hang and show "quickcam 1 (deleted)" if sleeping
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("MOD_INC_USE_COUNT in qc_v4l_open() : %i",GET_USE_COUNT(THIS_MODULE));
	MOD_INC_USE_COUNT;

	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down_intr(quickcam_list) in qc_v4l_open() : %i", sem_getcount(&quickcam_list_lock));

	r = qc_lock(qc);
	if (r<0) goto fail1;

	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down_intr(%p) in qc_v4l_open() : %i", qc, sem_getcount(&qc->lock));
	if (down_interruptible(&qc->lock)) {
		r = -ERESTARTSYS;
		goto fail2;
	}
	if (!qc->connected) {
		r = -ENODEV;
		goto fail3;
	}
	qc->users++;
	PDEBUG("open users=%i", qc->users);
	if (qc->users == 1) {
		if (qcdebug&QC_DEBUGLOGIC) PDEBUG("First user, initializing");
		if ((r = qc_v4l_init(qc))<0) goto fail4;
	}
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_v4l_open() : %i",qc, sem_getcount(&qc->lock));
	up(&qc->lock);
	up(&quickcam_list_lock);
	return 0;

fail4:	qc->users--;
fail3:	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_v4l_open()=failed : %i",qc, sem_getcount(&qc->lock));
	up(&qc->lock);
fail2:	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(quickcam_list) in qc_v4l_open()=failed : %i", sem_getcount(&qc->lock));
	up(&quickcam_list_lock);
fail1:	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("MOD_DEC_USE_COUNT in qc_v4l_open() : %i",GET_USE_COUNT(THIS_MODULE));
	MOD_DEC_USE_COUNT;
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGERRORS) PDEBUG("failed qc_v4l_open()=%i",r);
	return r;
}
/* }}} */
/* {{{ [fold] qc_v4l_exit(struct quickcam *qc) */
/* Release all resources allocated at qc_v4l_init() */
static inline void qc_v4l_exit(struct quickcam *qc)
{
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_v4l_cleanup(%p)", qc);
	qc_capt_exit(qc);
}
/* }}} */
/* {{{ [fold] qc_v4l_close(struct video_device *dev) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int qc_v4l_close(struct inode *inode, struct file *file)
#else
static void qc_v4l_close(struct video_device *dev)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	struct video_device *dev = video_devdata(file);
#endif
	struct quickcam *qc = (struct quickcam *)dev->priv;
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGUSER) PDEBUG("qc_v4l_close(dev=%p,qc=%p)",dev,qc);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	TEST_BUGR_MSG(qc==NULL, "qc==NULL");
#else
	TEST_BUG_MSG(qc==NULL, "qc==NULL");
#endif
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down(quickcam_list) in qc_v4l_close() : %i", sem_getcount(&quickcam_list_lock));
	down(&quickcam_list_lock);	/* Can not interrupt, we must success */
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down(%p) in qc_v4l_close() : %i", qc, sem_getcount(&qc->lock));
	down(&qc->lock);		/* Can not interrupt, we must success */
	qc->users--;
	PDEBUG("close users=%i", qc->users);
	if (qc->users == 0) {
		/* No more users, device is deallocated */
		qc_v4l_exit(qc);
		if (qc->dev == NULL) {		/* Test qc->dev instead of qc->connected because disconnection routine sets the latter before locking camera */
			/* Camera was unplugged and freeing was postponed: free resources now here */
			if (qcdebug&QC_DEBUGLOGIC) PDEBUG("Performing postponed free");
			qc_usb_exit(qc);
			qc = NULL;
		}
	}
	if (qc) {
		if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_v4l_close() : %i", qc, sem_getcount(&qc->lock));
		up(&qc->lock);
	}
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(quickcam_list) in qc_v4l_close() : %i", sem_getcount(&quickcam_list_lock));
	up(&quickcam_list_lock);
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("MOD_DEC_USE_COUNT in qc_v4l_close() : %i", GET_USE_COUNT(THIS_MODULE));
	MOD_DEC_USE_COUNT;
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("v4l_close() ok");
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,0)
	return 0;
#endif
}
/* }}} */
/* {{{ [fold] qc_v4l_read(struct video_device *dev, char *buf, unsigned long count, int noblock) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int qc_v4l_read(struct file *file, char *buf, size_t count, loff_t *ppos)
#else
static long qc_v4l_read(struct video_device *dev, char *buf, unsigned long count, int noblock)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	struct video_device *dev = video_devdata(file);
	int noblock = file->f_flags & O_NONBLOCK;
#endif
	struct quickcam *qc = (struct quickcam *)dev->priv;
	int frame_len;
	unsigned char *frame;
	long r = 0;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGUSER)
		PDEBUG("qc_v4l_read(dev=%p,buf=%p,count=%li,noblock=%i,qc=%p)",dev,buf,(long)count,noblock,qc);
	if (!qc || !buf) {
		PDEBUG("qc_read: no video_device available or no buffer attached :( EFAULT");
		return -EFAULT;
	}
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down_intr(%p) in qc_v4l_read() : %i", qc, sem_getcount(&qc->lock));
	if (down_interruptible(&qc->lock)) return -ERESTARTSYS;
	if (!qc->connected) {
		r = -ENODEV;
		goto fail;
	}
	if (noblock && !qc_capt_test(qc)) {
		r = -EAGAIN;
		goto fail;
	}
	frame_len = qc_capt_get(qc, &frame);
	if (frame_len < 0) {
		r = frame_len;
		goto fail;
	}
	if (count > frame_len) count = frame_len;
	if (copy_to_user(buf, frame, count)) {
		r = -EFAULT;
		goto fail;
	}
	r = count;

fail:	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_v4l_read() : %i", qc, sem_getcount(&qc->lock));
	up(&qc->lock);
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGERRORS) if (r<0) PDEBUG("failed qc_v4l_read()=%i", (int)r);
	return r;
}
/* }}} */
/* {{{ [fold] qc_v4l_mmap(struct vm_area_struct *vma, struct video_device *dev, const char *adr, unsigned long size) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int qc_v4l_mmap(struct file *file, struct vm_area_struct *vma)
#else
static int qc_v4l_mmap(
#if HAVE_VMA
	struct vm_area_struct *vma,
#endif
	struct video_device *dev, const char *start, unsigned long size)
#endif /* 2.6.x */
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	struct video_device *dev = video_devdata(file);
	const void *start = (void *)vma->vm_start;
	unsigned long size  = vma->vm_end - vma->vm_start;
#endif
	struct quickcam *qc = (struct quickcam *)dev->priv;
	unsigned char *frame;
	int ret = 0,  frame_size;
#if !HAVE_VMA && LINUX_VERSION_CODE<KERNEL_VERSION(2,6,0)
	struct vm_area_struct *vma = NULL;
#endif
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGUSER) PDEBUG("qc_v4l_mmap(dev=%p,size=%li,qc=%p)",dev,size,qc);
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down_intr(%p) in qc_v4l_mmap() : %i", qc, sem_getcount(&qc->lock));
	if (down_interruptible(&qc->lock)) return -ERESTARTSYS;
	if (!qc->connected) { ret = -ENODEV; goto fail; }
	frame_size = qc_capt_frameaddr(qc, &frame);
	if (frame_size<0) { ret = frame_size; goto fail; }		/* Should never happen */
	ret = qc_mm_remap(vma, frame, frame_size, start, size);

fail:	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_v4l_mmap() : %i", qc, sem_getcount(&qc->lock));
	up(&qc->lock);
	if (ret<0) if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGERRORS) PDEBUG("failed qc_v4l_mmap()=%i",ret);
	return ret;
}
/* }}} */
/* {{{ [fold] qc_v4l_ioctl(struct video_device *dev, unsigned int cmd, void *arg) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int qc_v4l_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
#else
static int qc_v4l_ioctl(struct video_device *dev, unsigned int cmd, void *argp)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	struct video_device *dev = video_devdata(file);
	void *argp = (void *)arg;
#endif
	struct quickcam *qc = (struct quickcam *)dev->priv;
	int i, retval = 0;

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGUSER) PDEBUG("qc_v4l_ioctl(dev=%p,cmd=%u,arg=%p,qc=%p)",dev,cmd,argp,qc);
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down_intr(%p) in qc_v4l_ioctl() : %i", qc, sem_getcount(&qc->lock));
	if (down_interruptible(&qc->lock)) return -ERESTARTSYS;
	if (!qc->connected) {
		retval = -ENODEV;
		goto fail;
	}
	switch (cmd) {
/* {{{ [fold] VIDIOCGCAP:     Capability query */
		case VIDIOCGCAP:	/* Capability query */
		{
			struct video_capability b;
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCGCAP");
			memset(&b, 0, sizeof(b));
			strcpy(b.name, "Logitech QuickCam USB");	/* Max 31 characters */
			b.type      = qc->vdev.type;
			b.channels  = 1;
			b.audios    = 0;
			b.maxwidth  = qc->sensor_data.maxwidth;
			b.maxheight = qc->sensor_data.maxheight;
			if (qc->settings.compat_16x) {
				b.maxwidth  = (b.maxwidth /16)*16;
				b.maxheight = (b.maxheight/16)*16;
			}
			b.minwidth  = min_framewidth;
			b.minheight = min_frameheight;
			if (copy_to_user(argp, &b, sizeof(b))) retval = -EFAULT;
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCGCHAN:    Get properties of the specified channel */
		case VIDIOCGCHAN:	/* Get properties of the specified channel */
		{
			struct video_channel v;
			if (copy_from_user(&v, argp, sizeof(v))) {
				retval = -EFAULT;
				break;
			}
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCGCHAN channel:%i",v.channel);
			if (v.channel != 0) {
				retval = -EINVAL;
				break;
			}
			v.flags = 0;
			v.tuners = 0;
			v.type = VIDEO_TYPE_CAMERA;
			strcpy(v.name, "Camera");
			if (copy_to_user(argp, &v, sizeof(v))) retval = -EFAULT;
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCSCHAN:    Select channel to capture */
		case VIDIOCSCHAN:	/* Select channel to capture */
		{
			if (copy_from_user(&i, argp, sizeof(i))) {
				retval = -EFAULT;
				break;
			}
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCSCHAN channel:%i",i);
			if (i != 0) retval = -EINVAL;
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCGPICT:    Get image properties (brightness, palette, etc.) */
		case VIDIOCGPICT:	/* Get image properties */
		{
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCGPICT");
			if (copy_to_user(argp, &qc->vpic, sizeof(qc->vpic))) retval = -EFAULT;
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCSPICT:    Set image properties */
		case VIDIOCSPICT:	/* Set image properties */
		{
			struct video_picture p;
			if (copy_from_user(&p, argp, sizeof(p))) {
				retval = -EFAULT;
				break;
			}
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCSPICT depth:%d palette:%s(%i) bright=%i",p.depth,qc_fmt_getname(p.palette),p.palette,p.brightness);

			if (p.palette != 0) {		/* 0 = do not change palette */
				retval = qc_fmt_issupported(p.palette);
				if (retval<0) break;
				qc->vpic.palette = p.palette;
				qc->vpic.depth   = qc_fmt_getdepth(p.palette);
				if (qc->vpic.depth != p.depth) PDEBUG("warning: palette depth mismatch");
			}
			qc->vpic.brightness = p.brightness;
			qc->vpic.hue        = p.hue;
			qc->vpic.colour     = p.colour;
			qc->vpic.contrast   = p.contrast;
			qc->vpic.whiteness  = p.whiteness;		/* Used for sharpness */
			qc->vpic_pending    = TRUE;
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCSWIN:     Set capture area width and height */
		case VIDIOCSWIN:	/* Set capture area width and height */
		{
			struct video_window vw;
			int fps;
			if (copy_from_user(&vw, argp, sizeof(vw))) {
				retval = -EFAULT;
				break;
			}
			fps = (vw.flags>>16) & 0x3F;		/* 6 bits for framerate */
			if (fps && ((qc->vwin.flags>>16)&0x3F)!=fps) {
				PDEBUG("Application tries to change framerate");
			}
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCSWIN width:%i height:%i flags:%d clipcount:%d",vw.width,vw.height,vw.flags,vw.clipcount);
			retval = qc_sensor_setsize(qc, vw.width, vw.height);
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCGWIN:     Get current capture area */
		case VIDIOCGWIN:	/* Get current capture area */
		{
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCGWIN");
			if (copy_to_user(argp, &qc->vwin, sizeof(qc->vwin))) retval = -EFAULT;
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCGMBUF:    Get mmap buffer size and frame offsets */
		case VIDIOCGMBUF:	/* Get mmap buffer size and frame offsets */
		{
			struct video_mbuf vm;
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCGMBUF");
			memset(&vm, 0, sizeof(vm));
			vm.size = qc_capt_frameaddr(qc, NULL);
			if (vm.size<0) {	/* Negative value denotes error */
				retval = vm.size;
				break;
			}
			vm.frames = 1;
			vm.offsets[0] = 0;
			if (qc->settings.compat_dblbuf) {
				/* Really many applications are broken and don't work with a single buffer */
				vm.frames = 2;
				vm.offsets[1] = 0;
			}
			if (copy_to_user(argp, &vm, sizeof(vm))) retval = -EFAULT;
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCMCAPTURE: Start capturing specified frame in the mmap buffer with specified size */
		case VIDIOCMCAPTURE:	/* Start capturing specified frame in the mmap buffer with specified size */
		{
			struct video_mmap vm;
			if (copy_from_user(&vm, argp, sizeof(vm))) {
				retval = -EFAULT;
				break;
			}
			/* Bug in V4L: sometimes it's called palette, sometimes format. We'll stick with palette */
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCMCAPTURE frame:%d size:%dx%d palette:%s", vm.frame, vm.width, vm.height, qc_fmt_getname(vm.format));
			if (vm.frame!=0 && !(qc->settings.compat_dblbuf)) {
				PRINTK(KERN_NOTICE,"Bug detected in user program, use qcset compat=dblbuf");
				retval = -EINVAL;
				break;
			}
			if (vm.format!=0 && qc->vpic.palette!=vm.format) {	/* 0 = do not change palette */
				retval = qc_fmt_issupported(vm.format);
				if (retval) {
					if (qcdebug&QC_DEBUGERRORS) PDEBUG("unsupported image format");
					break;
				}
				qc->vpic.palette = vm.format;
				qc->vpic.depth   = qc_fmt_getdepth(vm.format);
			}
			retval = qc_sensor_setsize(qc, vm.width, vm.height);
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCSYNC:     Wait until specified frame in the mmap buffer has been captured */
		case VIDIOCSYNC:	/* Wait until specified frame in the mmap buffer has been captured */
		{
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCSYNC");
			retval = qc_capt_get(qc, NULL);
			if (retval>0) retval = 0;
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCGFBUF:    Get currently used frame buffer parameters */
		case VIDIOCGFBUF:	/* Get currently used frame buffer parameters */
		{
			struct video_buffer vb;
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCGFBUF");
			memset(&vb, 0, sizeof(vb));
			if (copy_to_user(argp, &vb, sizeof(vb))) retval = -EFAULT;
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCKEY:      Undocumented? */
		case VIDIOCKEY:		/* Undocumented? */
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCKEY");
			retval = -EINVAL;
			break;
/* }}} */
/* {{{ [fold] VIDIOCCAPTURE:  Activate overlay capturing directly to framebuffer */
		case VIDIOCCAPTURE:	/* Activate overlay capturing directly to framebuffer */
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCCAPTURE");
			retval = -EINVAL;
			break;
/* }}} */
/* {{{ [fold] VIDIOCSFBUF:    Set frame buffer parameters for the capture card */
		case VIDIOCSFBUF:	/* Set frame buffer parameters for the capture card */
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCSFBUF");
			retval = -EINVAL;
			break;
/* }}} */
/* {{{ [fold] VIDIOCxTUNER:   Get properties of the specified tuner / Select tuner to use */
		case VIDIOCGTUNER:	/* Get properties of the specified tuner */
		case VIDIOCSTUNER:	/* Select tuner to use */
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxTUNER");
			retval = -EINVAL;
			break;
/* }}} */
/* {{{ [fold] VIDIOCxFREQ:    Get current tuner frequency / Set tuner frequency */
		case VIDIOCGFREQ:	/* Get current tuner frequency */
		case VIDIOCSFREQ:	/* Set tuner frequency */
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxFREQ");
			retval = -EINVAL;
			break;
/* }}} */
/* {{{ [fold] VIDIOCxAUDIO:   Get/Set audio properties */
		case VIDIOCGAUDIO:	/* Get audio properties */
		case VIDIOCSAUDIO:	/* Set audio properties */
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxAUDIO");
			retval = -EINVAL;
			break;
/* }}} */
		/********** Private IOCTLs ***********/
/* {{{ [fold] VIDIOCQCxDEBUG:        Sets/gets the qcdebug output (1,2,4,8,16,32) */
		case VIDIOCQCSDEBUG:		/* Sets the qcdebug output (1,2,4,8,16,32) */
			if (get_user(qcdebug, (int *)argp)) { retval=-EFAULT; break; }
		case VIDIOCQCGDEBUG:		/* Gets the qcdebug output (1,2,4,8,16,32) */
			if (put_user(qcdebug, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxDEBUG");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxKEEPSETTINGS: Set/get keep gain settings across one open to another (0-1) */
		case VIDIOCQCSKEEPSETTINGS:	/* Set keep gain settings across one open to another (0-1) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.keepsettings = i;
		case VIDIOCQCGKEEPSETTINGS:	/* Get keep gain settings across one open to another (0-1) */
			i = qc->settings.keepsettings;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxKEEPSETTINGS");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxSETTLE:       Set/get if we let image brightness to settle (0-1) */
		case VIDIOCQCSSETTLE:		/* Set if we let image brightness to settle (0-1) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.settle = i;
		case VIDIOCQCGSETTLE:		/* Get if we let image brightness to settle (0-1) */
			i = qc->settings.settle;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxSETTLE");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxSUBSAMPLE:    Sets/gets the speed (0-1) */
		case VIDIOCQCSSUBSAMPLE:	/* Sets the speed (0-1) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.subsample = i;
		case VIDIOCQCGSUBSAMPLE:	/* Gets the speed (0-1) */
			i = qc->settings.subsample;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxSUBSAMPLE");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxCOMPRESS:     Sets/gets the compression mode (0-1) */
		case VIDIOCQCSCOMPRESS:		/* Sets the compression mode (0-1) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.compress = i;
		case VIDIOCQCGCOMPRESS:		/* Gets the compression mode (0-1) */
			i = qc->settings.compress;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxCOMPRESS");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxFRAMESKIP:    Set/get frame capture frequency (0-10) */
		case VIDIOCQCSFRAMESKIP:	/* Set frame capture frequency (0-10) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.frameskip = i;
		case VIDIOCQCGFRAMESKIP:	/* Get frame capture frequency (0-10) */
			i = qc->settings.frameskip;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxFRAMESKIP");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxQUALITY:      Sets/gets the interpolation mode (0-2) */
		case VIDIOCQCSQUALITY:		/* Sets the interpolation mode (0-5) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.quality = i;
		case VIDIOCQCGQUALITY:		/* Gets the interpolation mode (0-5) */
			i = qc->settings.quality;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxQUALITY");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxADAPTIVE:     Set/get automatic adaptive brightness control (0-1) */
		case VIDIOCQCSADAPTIVE:		/* Set automatic adaptive brightness control (0-1) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.adaptive = i;
		case VIDIOCQCGADAPTIVE:		/* Get automatic adaptive brightness control (0-1) */
			i = qc->settings.adaptive;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxADAPTIVE");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxEQUALIZE:     Set/get equalize image (0-1) */
		case VIDIOCQCSEQUALIZE:		/* Set equalize image (0-1) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.equalize = i;
		case VIDIOCQCGEQUALIZE:		/* Get equalize image (0-1) */
			i = qc->settings.equalize;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxEQUALIZE");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxUSERLUT:      Set/get user-specified lookup-table */
		case VIDIOCQCSUSERLUT:			/* Set user-specified lookup-table [struct qc_userlut] */
		{
			unsigned int flags;
			retval = -EFAULT;
			if (get_user(flags, &(((struct qc_userlut*)argp)->flags))) break;
			if (flags & QC_USERLUT_DEFAULT) {
				userlut = ((flags & QC_USERLUT_ENABLE) != 0);
			} else {
				qc->settings.userlut = ((flags & QC_USERLUT_ENABLE) != 0);
			}
			if (flags & QC_USERLUT_VALUES) {
				for (i=0; i<QC_LUT_SIZE; i++) {
					unsigned char p;
					if (get_user(p, &(((struct qc_userlut*)argp)->lut[i]))) break;
					if (flags & QC_USERLUT_DEFAULT) {
						userlut_contents[i] = p;
					} else {
						qc->fmt_data.userlut[i] = p;
					}
				}
				if (i < QC_LUT_SIZE) break;
			}
			retval = 0;
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCSUSERLUT");
			break;
		}
		case VIDIOCQCGUSERLUT:			/* Get user-specified lookup-table [struct qc_userlut] */
		{
			unsigned int flags;
			retval = -EFAULT;
			if (get_user(flags, &(((struct qc_userlut*)argp)->flags))) break;
			flags &= (~QC_USERLUT_ENABLE);
			if ((flags & QC_USERLUT_DEFAULT) ? userlut : qc->settings.userlut) flags |= QC_USERLUT_ENABLE;
			if (put_user(flags,  &(((struct qc_userlut*)argp)->flags))) break;
			if (flags & QC_USERLUT_VALUES) {
				for (i=0; i<QC_LUT_SIZE; i++) {
					unsigned char p;
					if (flags & QC_USERLUT_DEFAULT) {
						p = userlut_contents[i];
					} else {
						p = qc->fmt_data.userlut[i];
					}
					if (put_user(p, &(((struct qc_userlut*)argp)->lut[i]))) break;
				}
				if (i < QC_LUT_SIZE) break;
			}
			retval = 0;
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCGUSERLUT");
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCQCxRETRYERRORS:  Set/get if we retry when error happen in capture (0-1) */
		case VIDIOCQCSRETRYERRORS:	/* Set if we retry when error happen in capture (0-1) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.retryerrors = i;
		case VIDIOCQCGRETRYERRORS:	/* Get if we retry when error happen in capture (0-1) */
			i = qc->settings.retryerrors;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxRETRYERRORS");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxCOMPATIBLE:   Set enable workaround for Xawtv/Motv bugs (0-1) */
		case VIDIOCQCSCOMPATIBLE:	/* Set enable workaround for Xawtv/Motv bugs (0-1) */
			if (get_user(i, (int *)argp)) { retval=-EFAULT; break; }
			qc->settings.compat_16x    = (i & QC_COMPAT_16X)    != 0;
			qc->settings.compat_dblbuf = (i & QC_COMPAT_DBLBUF) != 0;
			qc->settings.compat_torgb  = (i & QC_COMPAT_TORGB)  != 0;
		case VIDIOCQCGCOMPATIBLE:	/* Get enable workaround for Xawtv/Motv bugs (0-1) */
			i  = ~(qc->settings.compat_16x   -1) & QC_COMPAT_16X;
			i |= ~(qc->settings.compat_dblbuf-1) & QC_COMPAT_DBLBUF;
			i |= ~(qc->settings.compat_torgb -1) & QC_COMPAT_TORGB;
			if (put_user(i, (int *)argp)) { retval=-EFAULT; break; }
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCxCOMPATIBLE");
			break;
/* }}} */
/* {{{ [fold] VIDIOCQCxVIDEONR:      Set videodevice number (/dev/videoX) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,5)
		case VIDIOCQCSVIDEONR:		/* Set videodevice number (/dev/videoX) */
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCSVIDEONR");
			retval = -EINVAL;	/* Can not set after the module is loaded */
			break;
		case VIDIOCQCGVIDEONR:		/* Get videodevice number (/dev/videoX) */
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCGVIDEONR");
			if (put_user(video_nr, (int *)argp)) { retval=-EFAULT; break; }
			break;
#endif
/* }}} */
/* {{{ [fold] VIDIOCQCxSTV:          Read/write STV chip register value */
		/* Encoding: bits 31..16 of the int argument contain register value, 15..0 the reg number */
		case VIDIOCQCGSTV:		/* Read STV chip register value */
		{
			int reg, val;
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCQCGSTV");
			if (get_user(reg, (int *)argp)) { retval=-EFAULT; break; }
			reg &= 0xFFFF;
			val = qc_stv_get(qc, reg);
			if (val<0) { retval=val; break; }
			val = (val<<16) | reg;
			if (put_user(val, (int *)argp)) { retval=-EFAULT; break; }
			break;
		}
		case VIDIOCQCSSTV:		/* Write STV chip register value */
		{
			int regval;
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCQCSSTV");
			if (!capable(CAP_SYS_RAWIO)) { retval=-EPERM; break; }
			if (get_user(regval, (int *)argp)) { retval=-EFAULT; break; }
			retval = qc_stv_set(qc, regval & 0xFFFF, regval >> 16);
			break;
		}
/* }}} */
/* {{{ [fold] VIDIOCQCxI2C:          Read/write sensor chip register value via I2C */
		case VIDIOCQCGI2C:		/* Read sensor chip register value via I2C */
		{
			int reg, val;
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCQCGI2C");
			if (get_user(reg, (int *)argp)) { retval=-EFAULT; break; }
			reg &= 0xFFFF;
			val = qc_get_i2c(qc, qc->sensor_data.sensor, reg);
			if (val<0) { retval=val; break; }
			val = (val<<16) | reg;
			if (put_user(val, (int *)argp)) { retval=-EFAULT; break; }
			break;
		}
		case VIDIOCQCSI2C:		/* Write sensor chip register value via I2C */
		{
			int regval;
			if (qcdebug&QC_DEBUGUSER) PDEBUG("VIDIOCQCSI2C");
			if (!capable(CAP_SYS_RAWIO)) { retval=-EPERM; break; }
			if (get_user(regval, (int *)argp)) { retval=-EFAULT; break; }
			retval = qc_i2c_set(qc, regval & 0xFFFF, regval >> 16);
			if (retval<0) break;
			retval = qc_i2c_wait(qc);
			break;
		}
/* }}} */
		default:
			if (qcdebug&QC_DEBUGUSER) PDEBUG("Unknown IOCTL %08X",cmd);
			retval = -ENOIOCTLCMD;
			break;
	}
fail:	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_v4l_ioctl() : %i", qc, sem_getcount(&qc->lock));
	up(&qc->lock);
	if (retval<0) if (qcdebug&(QC_DEBUGLOGIC|QC_DEBUGUSER|QC_DEBUGERRORS)) PDEBUG("failed qc_v4l_ioctl()=%i",retval);
	return retval;
}
/* }}} */
/* {{{ [fold] qc_v4l_write(struct video_device *dev, const char *buf, unsigned long count, int noblock) */
#if LINUX_VERSION_CODE<KERNEL_VERSION(2,6,0)
static long qc_v4l_write(struct video_device *dev, const char *buf, unsigned long count, int noblock)
{
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGUSER) PDEBUG("qc_v4l_write()");
	return -EINVAL;
}
#endif
/* }}} */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void qc_v4l_release(struct video_device *vfd) { }
static struct file_operations qc_v4l_fops = {
	owner:		THIS_MODULE,
	open:		qc_v4l_open,
	release:	qc_v4l_close,
	read:		qc_v4l_read,
//	write:		qc_v4l_write,
	ioctl:		qc_v4l_ioctl,
	mmap:		qc_v4l_mmap,
	poll:		qc_v4l_poll,
};
#endif

static struct video_device qc_v4l_template = {
	name:		"QuickCam USB",
	type:		VID_TYPE_CAPTURE | VID_TYPE_SUBCAPTURE,
	hardware:	VID_HARDWARE_QCAM_USB,
	minor:		-1,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	release:	qc_v4l_release,
	fops:		&qc_v4l_fops,
#else
	initialize:	NULL,
	open:		qc_v4l_open,
	close:		qc_v4l_close,
	read:		qc_v4l_read,
	write:		qc_v4l_write,
	ioctl:		qc_v4l_ioctl,
	mmap:		qc_v4l_mmap,
	poll:		qc_v4l_poll,
#endif
};
/* }}} */
/* {{{ [fold] **** qc_usb:    Start of USB API ********************************** */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int qc_usb_probe(struct usb_interface *intf, const struct usb_device_id *id);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
static void *qc_usb_probe(struct usb_device *dev, unsigned int iface, const struct usb_device_id *id);
#else
static void *qc_usb_probe(struct usb_device *dev, unsigned int iface);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void qc_usb_disconnect(struct usb_interface *intf);
#else
static void qc_usb_disconnect(struct usb_device *dev, void *ptr);
#endif

static struct usb_driver qc_usb_driver = {
	name:		qc_name,
	probe:		qc_usb_probe,
	disconnect:	qc_usb_disconnect,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	owner:		THIS_MODULE,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
	id_table:	qc_device_table,
#endif
};

/* {{{ [fold] qc_usb_init(struct usb_device *dev, unsigned int ifacenum) */
/* Detect sensor, initialize the quickcam structure, register V4L device, create /proc entry.
 * Return pointer to the allocated quickcam structure or NULL on error.
 * If there is some camera already open but disconnected, reuse the quickcam structure. */
static struct quickcam *qc_usb_init(struct usb_device *usbdev, unsigned int ifacenum)
{
	struct quickcam *qc;
	Bool reuse_qc;
	int i, r = 0;

	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_usb_init(usbdev=%p)", usbdev);
	if (PARANOID && usbdev==NULL) { PRINTK(KERN_CRIT,"usbdev==NULL"); return NULL; }

	/* Check if there is already a suitable quickcam struct that can be reused */
	reuse_qc = FALSE;
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down_intr(quickcam_list_lock) in qc_usb_init() : %i", sem_getcount(&quickcam_list_lock));
	if (down_interruptible(&quickcam_list_lock)) return NULL;
	list_for_each_entry(qc, &quickcam_list, list) {
		if (qc->dev!=NULL) continue;			/* quickcam_list_lock protects this test */
		if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down_intr(%p) in qc_usb_init() : %i",qc, sem_getcount(&qc->lock));
		if (down_interruptible(&qc->lock)) {
			/* Failed to lock the camera. Move on in the list, skipping this camera */
			if (qcdebug&QC_DEBUGMUTEX) PDEBUG("failed locking the camera %p in qc_usb_init() : %i",qc,sem_getcount(&qc->lock));
			continue;
		}
		if (qc->users<=0) {
			PRINTK(KERN_NOTICE, "Unplugged unused camera detected!");
			if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_usb_init() : %i",qc, sem_getcount(&qc->lock));
			up(&qc->lock);
			continue;
		}
		/* Found and locked unplugged but used camera */
		reuse_qc = TRUE;
		break;
	}

	if (reuse_qc) {
		/* Reuse existing quickcam (which is already opened) */
		if (qcdebug&QC_DEBUGLOGIC) PDEBUG("Reusing existing quickcam");
		if (PARANOID && qc->users<=0) PRINTK(KERN_CRIT, "Unplugged JUST closed camera detected!");
		qc_isoc_stop(qc);
		qc_i2c_wait(qc);
		qc_frame_flush(qc);
	} else {
		/* Allocate and initialize some members of the new qc */
		if (qcdebug&QC_DEBUGLOGIC) PDEBUG("Allocating new quickcam");
		qc = kmalloc(sizeof(*qc), GFP_KERNEL);
		CHECK_ERROR(qc==NULL, fail1, "couldn't kmalloc quickcam struct");
		memset(qc, 0, sizeof(*qc));		/* No garbage to user */
PDEBUG("poisoning qc in qc_usb_init");
		POISON(*qc);
		if (qcdebug&QC_DEBUGMUTEX) PDEBUG("init down(%p) in qc_usb_init()", qc);
		init_MUTEX_LOCKED(&qc->lock);
		qc->users = 0;
		if ((r=qc_i2c_init(qc))<0) goto fail2;
	}
	qc->dev       = usbdev;
	qc->iface     = ifacenum;
	qc->connected = TRUE;

	/* Probe for the sensor type */
	qc_i2c_wait(qc);						/* Necessary before set_interface() */
	if ((r=usb_set_interface(usbdev, qc->iface, 0))<0) goto fail3;	/* Set altsetting 0 */
	if ((r=qc_stv_set(qc, STV_ISO_ENABLE, 0))<0) goto fail3;	/* Disable isochronous stream */
	for (i=0; i<SIZE(sensors); i++) {
		if ((r = qc_get_i2c(qc, sensors[i], sensors[i]->id_reg))<0) goto fail3;
		r = (r >> (sensors[i]->length_id-1) * 8) & 0xFF;	/* Get MSB of received value */
		if (qcdebug&QC_DEBUGCAMERA) PDEBUG("Probing %s: expecting %02X, got %02X", sensors[i]->name, sensors[i]->id, r);
		if (r == sensors[i]->id) break;
	}
	if (i>=SIZE(sensors)) {
		PRINTK(KERN_INFO,"unsupported sensor");
		goto fail3;
	}
	qc->sensor_data.sensor = sensors[i];
	PRINTK(KERN_INFO,"Sensor %s detected", sensors[i]->name);

	if ((r=qc_stv_set(qc, STV_ISO_ENABLE, 0))<0) goto fail3;	/* Disable isochronous streaming */
	if ((r=qc_stv_set(qc, STV_REG23, 1))<0) goto fail3;

	if (!reuse_qc) {
		/* Set default settings */
		qc->vpic.brightness = 32768;
		qc->vpic.hue        = 32768;
		qc->vpic.colour     = 32768;
		qc->vpic.contrast   = 32768;
		qc->vpic.whiteness  = 32768;				/* Used for sharpness at quality=5 */
		qc->settings.keepsettings  = keepsettings;
		qc->settings.settle        = settle;
		qc->settings.subsample     = subsample;
		qc->settings.compress      = compress;
		qc->settings.frameskip     = frameskip;
		qc->settings.quality       = quality;
		qc->settings.adaptive      = adaptive;
		qc->settings.equalize      = equalize;
		qc->settings.userlut       = userlut;
		qc->settings.retryerrors   = retryerrors;
		qc->settings.compat_16x    = compatible & QC_COMPAT_16X    ? 1 : 0;
		qc->settings.compat_dblbuf = compatible & QC_COMPAT_DBLBUF ? 1 : 0;
		qc->settings.compat_torgb  = compatible & QC_COMPAT_TORGB  ? 1 : 0;
		memcpy(&qc->fmt_data.userlut, userlut_contents, sizeof(qc->fmt_data.userlut));

		/* Register V4L video device */
		memcpy(&qc->vdev, &qc_v4l_template, sizeof(qc_v4l_template));
		qc->vdev.priv = qc;
		r = video_register_device(&qc->vdev, VFL_TYPE_GRABBER, video_nr);
		if (r<0) goto fail3;
		PRINTK(KERN_INFO, "Registered device: /dev/video%i", qc->vdev.minor);
		if ((r=qc_adapt_init(qc))<0) goto fail4;
		qc_proc_create(qc);				/* Create /proc entry, ignore if it fails */
		list_add(&qc->list, &quickcam_list);
	}

	if (reuse_qc && qc->frame_data.waiting>0) {
		/* Restart capturing */
		int width = qc->vwin.width;
		int height = qc->vwin.height;
//qc_usleep(1000000);
		qc_isoc_stop(qc);
		r = qc_sensor_init(qc);
		r = qc_isoc_start(qc);
		r = qc_sensor_setsize(qc, width, height);
		/* Ignore return codes for now, if it fails, too bad, but shouldn't crash */
		/* FIXME: proper error handling */

/*qc_usleep(1000000);
 qc_sensor_setsize(qc, width, height);
qc_usleep(1000000);
 qc_sensor_setsize(qc, 32, 32);
qc_usleep(1000000);
 qc_sensor_setsize(qc, width, height);
qc_usleep(1000000);*/

#if 0
/* The following tries to initialize VV6410 really hard. still doesn't work */
{
int r,c;
for(c=0;c<10;c++) {
//r = qc_sensor_init(qc);
//PDEBUG("c=%i  init=%i",c,r);
//r = qc_sensor_setsize(qc, width, height);
//PDEBUG("size=%i",r);
//r = usb_set_interface(qc->dev, qc->iface, 1);
//PDEBUG("set_interf=%i",r);
//r = qc->sensor_data.sensor->start(qc);	/* Start current frame */
//PDEBUG("start=%i",r);
//r = qc_stv_set(qc, STV_ISO_ENABLE, 1);
//PDEBUG("stv_set=%i",r);
//qc_isoc_stop(qc);
//qc_usleep(1000000);
//qc_isoc_start(qc);
//qc_usleep(1000000);
}}
#endif
	}
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_usb_init() : %i",qc, sem_getcount(&qc->lock));
	up(&qc->lock);
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(quickcam_list) in qc_usb_init() : %i", sem_getcount(&quickcam_list_lock));
	up(&quickcam_list_lock);
	return qc;

fail4:	video_unregister_device(&qc->vdev);
fail3:	if (!reuse_qc) qc_i2c_exit(qc);
	qc->dev = NULL;
	qc->connected = FALSE;
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_usb_init()=failed : %i",qc, sem_getcount(&qc->lock));
	up(&qc->lock);
fail2:	if (!reuse_qc) kfree(qc);
fail1:	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGERRORS) PDEBUG("failed qc_usb_init()=%i",r);
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(quickcam_list) in qc_usb_init()=failed : %i", sem_getcount(&quickcam_list_lock));
	up(&quickcam_list_lock);
	return NULL;
}
/* }}} */
/* FIXME: can usb_disconnect and usb_probe pre-empt other kernel mode processes? Assume no */
/* {{{ [fold] qc_usb_probe(...) */
/* Called when any USB device is connected, check if it is a supported camera */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int qc_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
static void *qc_usb_probe(struct usb_device *usbdev, unsigned int ifacenum, const struct usb_device_id *id)
#else /* 2.2.x */
static void *qc_usb_probe(struct usb_device *usbdev, unsigned int ifacenum)
#endif
{
	struct quickcam *qc;
	struct usb_interface_descriptor *ifacedesc;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	unsigned int ifacenum;
	struct usb_device *usbdev = interface_to_usbdev(interface);
	static const int ERROR_CODE = -ENODEV;
#else
	static void * const ERROR_CODE = NULL;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
	/* Check if the device has a product number that we support */
	struct usb_device_id *i;
	for (i=qc_device_table; i->idVendor; i++) {
		if (usbdev->descriptor.idVendor == i->idVendor &&
		    usbdev->descriptor.idProduct == i->idProduct) break;
	}
	if (!i->idVendor) return ERROR_CODE;
#endif
	if (PARANOID && usbdev==NULL) { PRINTK(KERN_CRIT,"usbdev==NULL"); return ERROR_CODE; }

	/* We don't handle multi-config cameras */
	if (usbdev->descriptor.bNumConfigurations != 1) return ERROR_CODE;

	/*
	 * Checking vendor/product is not enough
	 * In case on QuickCam Web the audio is at class 1 and subclass 1/2.
	 * one /dev/dsp and one /dev/mixer
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	ifacedesc = &interface->altsetting[0].desc;
	ifacenum  = ifacedesc->bInterfaceNumber;
#else
	ifacedesc = &usbdev->actconfig->interface[ifacenum].altsetting[0];
#endif
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGCAMERA) PDEBUG("qc_usb_probe(usbdev=%p,ifacenum=%i)", usbdev, ifacenum);
	if (PARANOID && ifacedesc->bInterfaceNumber!=ifacenum) PRINTK(KERN_CRIT,"bInterfaceNumber(%i)!=ifacenum(%i)!!",ifacedesc->bInterfaceNumber,ifacenum);
	if (ifacedesc->bInterfaceClass != 0xFF) return ERROR_CODE;
	if (ifacedesc->bInterfaceSubClass != 0xFF) return ERROR_CODE;

	/* We found a QuickCam */
	PRINTK(KERN_INFO,"QuickCam USB camera found (driver version %s)", VERSION);
	PRINTK(KERN_INFO,"Kernel:%s bus:%i class:%02X subclass:%02X vendor:%04X product:%04X",
		UTS_RELEASE, usbdev->bus->busnum, ifacedesc->bInterfaceClass, ifacedesc->bInterfaceSubClass,
		usbdev->descriptor.idVendor, usbdev->descriptor.idProduct);

	/* The interface is claimed (bound) automatically to us when we return from this function (without error code) */
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("MOD_INC_USE_COUNT in qc_usb_probe() : %i",GET_USE_COUNT(THIS_MODULE));
	MOD_INC_USE_COUNT;	/* Increase count to 1, which locks the module--it can't be removed */
	qc = qc_usb_init(usbdev, ifacenum);
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("MOD_DEC_USE_COUNT in qc_usb_probe() : %i",GET_USE_COUNT(THIS_MODULE));
	MOD_DEC_USE_COUNT;	/* Release lock: module can be now removed again */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	if (!qc) return ERROR_CODE;
	usb_set_intfdata(interface, qc);	/* FIXME: why? */
	return 0;
#else
	return qc;
#endif
}
/* }}} */
/* {{{ [fold] qc_usb_exit(struct quickcam *qc) */
/* Free up resources allocated in qc_usb_init() when not needed anymore
 * Note: quickcam_list_lock and qc->lock must be acquired before entering this function!
 * qc may not be accessed after this function returns! 
 */
static void qc_usb_exit(struct quickcam *qc)
{
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_usb_exit(qc=%p)",qc);
	TEST_BUG_MSG(qc==NULL, "qc==NULL");

	qc_proc_destroy(qc);
	qc_adapt_exit(qc);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("video_unregister_device(%p)", &qc->vdev);
	video_unregister_device(&qc->vdev);
	qc_i2c_exit(qc);
	list_del(&qc->list);
PDEBUG("poisoning qc in qc_usb_exit");
	POISON(*qc);
	kfree(qc);
	if (qcdebug&QC_DEBUGLOGIC) PDEBUG("qc_usb_exit() done");
}
/* }}} */
/* {{{ [fold] qc_usb_disconnect(...) */
/* Called when the camera is disconnected. We might not free struct quickcam here,
 * because the camera might be in use (open() called). In that case, the freeing is
 * postponed to the last close() call. However, all submitted URBs must be unlinked.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void qc_usb_disconnect(struct usb_interface *interface)
#else
static void qc_usb_disconnect(struct usb_device *usbdev, void *ptr)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	struct quickcam *qc = usb_get_intfdata(interface);
#ifdef DEBUG
	struct usb_device *usbdev = interface_to_usbdev(interface);
#endif
#else
	struct quickcam *qc = (struct quickcam *)ptr;
#endif

	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGCAMERA) PDEBUG("qc_usb_disconnect(qc=%p)",qc);
	TEST_BUG_MSG(qc==NULL, "qc==NULL in qc_usb_disconnect!");
	TEST_BUG_MSG(qc->dev==NULL || qc->connected==FALSE, "disconnecting disconnected device!!");
	TEST_BUG_MSG(usbdev!=qc->dev, "disconnecting not our device!!");

	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("MOD_INC_USE_COUNT in qc_usb_disconnect() : %i",GET_USE_COUNT(THIS_MODULE));
	MOD_INC_USE_COUNT;			/* Increase count to 1, which locks the module--it can't be removed */

	/*
	 * When the camera is unplugged (maybe even when it is capturing), quickcam->connected is set to FALSE.
	 * All functions called from user mode and all _exit functions must check for this.
	 */
	qc->connected = FALSE;

	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down(quickcam_list) in qc_usb_disconnect() : %i", sem_getcount(&quickcam_list_lock));
	down(&quickcam_list_lock);		/* Also avoids race condition with open() */
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("down(%p) in qc_usb_disconnect() : %i", qc, sem_getcount(&qc->lock));
	down(&qc->lock);			/* Can not interrupt, we must success */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	usb_set_intfdata(interface, NULL);	/* FIXME: why? */
#endif
	if (qc->users <= 0) {
		/* Free resources */
		qc_usb_exit(qc);
	} else {
		/* Can not free resources if device is open: postpone to when it is closed */
		if (qcdebug&QC_DEBUGLOGIC) PDEBUG("Disconnect while device open: postponing cleanup");
		qc_isoc_stop(qc);		/* Unlink and free isochronous URBs */
		qc_i2c_wait(qc);		/* Wait until there are no more I2C packets on way */
		qc->dev = NULL;			/* Must be set to NULL only after interrupts are guaranteed to be disabled! */
		if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(%p) in qc_usb_disconnect() : %i",qc, sem_getcount(&qc->lock));
		up(&qc->lock);
	}
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("up(quickcam_list) in qc_usb_disconnect() : %i", sem_getcount(&quickcam_list_lock));
	up(&quickcam_list_lock);
	if (qcdebug&QC_DEBUGMUTEX) PDEBUG("MOD_DEC_USE_COUNT in qc_usb_disconnect() : %i",GET_USE_COUNT(THIS_MODULE));
	MOD_DEC_USE_COUNT;	/* Release lock--if device is not open, module can be now freed */
	/* The interface is released automatically when we return from this function */
}
/* }}} */

/* }}} */
/* {{{ [fold] **** qc:        Start of module API ******************************* */

/* {{{ [fold] qc_init(void) */
static int __init qc_init(void)
{
	int r;
	if (qcdebug) PDEBUG("----------LOADING QUICKCAM MODULE------------");
	if (qcdebug) PDEBUG("struct quickcam size: %i", (int)sizeof(struct quickcam));
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_init()");
	qc_proc_init();				/* Ignore if procfs entry creation fails */
	r = usb_register(&qc_usb_driver);
	if (r<0) qc_proc_exit();
	if (r<0) if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGERRORS) PDEBUG("failed qc_init()=%i",r);
	return r;
}
/* }}} */
/* {{{ [fold] qc_exit(void) */
static void __exit qc_exit(void)
{
	if (qcdebug&QC_DEBUGLOGIC || qcdebug&QC_DEBUGINIT) PDEBUG("qc_exit()");
	usb_deregister(&qc_usb_driver);		/* Will also call qc_usb_disconnect() if necessary */
	qc_proc_exit();
}
/* }}} */

module_init(qc_init);
module_exit(qc_exit);
/* }}} */

/* End of file */
