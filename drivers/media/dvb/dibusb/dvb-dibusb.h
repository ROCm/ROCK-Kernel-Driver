/*
 * dvb-dibusb.h
 * 
 * Copyright (C) 2004 Patrick Boettcher (patrick.boettcher@desy.de)
 * 
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 *
 * for more information see dvb-dibusb.c .
 */

#ifndef __DVB_DIBUSB_H__
#define __DVB_DIBUSB_H__

/* Vendor IDs */
#define USB_TWINHAN_VENDOR_ID			0x1822
#define USB_IMC_NETWORKS_VENDOR_ID		0x13d3
#define USB_KWORLD_VENDOR_ID			0xeb1a
#define USB_DIBCOM_VENDOR_ID			0x10b8
#define USB_ULTIMA_ELECTRONIC_ID		0x05d8

/* Product IDs before loading the firmware */
#define USB_VP7041_PRODUCT_PREFW_ID		0x3201
#define USB_VSTREAM_PRODUCT_PREFW_ID	0x17de
#define USB_DIBCOM_PRODUCT_PREFW_ID		0x0bb8
#define USB_ULTIMA_ELEC_PROD_PREFW_ID	0x8105

/* product ID afterwards */
#define USB_VP7041_PRODUCT_ID			0x3202
#define USB_VSTREAM_PRODUCT_ID			0x17df
#define USB_DIBCOM_PRODUCT_ID			0x0bb9
#define USB_ULTIMA_ELEC_PROD_ID			0x8106

/* CS register start/stop the usb controller cpu */
#define DIBUSB_CPU_CSREG				0x7F92

// 0x10 is the I2C address of the first demodulator on the board
#define DIBUSB_DEMOD_I2C_ADDR_DEFAULT	0x10
#define DIBUSB_I2C_TIMEOUT 				HZ*5

#define DIBUSB_MAX_PIDS					16

#define DIB3000MB_REG_FIRST_PID			(   153)

struct usb_dibusb;

struct dibusb_pid {
	u16 reg;
	u16 pid;
	int active;
	struct usb_dibusb *dib;
};

struct usb_dibusb {
	/* usb */
	struct usb_device * udev;

	struct dibusb_device * dibdev;

	int streaming;
	int feed_count;
	struct urb *buf_urb;
	u8 *buffer;
	dma_addr_t dma_handle;
	
	spinlock_t pid_list_lock;
	struct dibusb_pid pid_list[DIBUSB_MAX_PIDS];

	/* I2C */
	struct i2c_adapter i2c_adap;
	struct i2c_client i2c_client;

	/* locking */
	struct semaphore usb_sem;
	struct semaphore i2c_sem;

	/* dvb */
	struct dvb_adapter *adapter;
	struct dmxdev dmxdev;
	struct dvb_demux demux;
	struct dvb_net dvb_net;
};


struct dibusb_device {
	u16 cold_product_id;
	u16 warm_product_id;
	u8 demod_addr;
	const char *name;
};

/* static array of valid firmware names, the best one first */
static const char * valid_firmware_filenames[] = {
	"dvb-dibusb-5.0.0.11.fw",
};

#define DIBUSB_SUPPORTED_DEVICES	4

/* USB Driver stuff */
static struct dibusb_device dibusb_devices[DIBUSB_SUPPORTED_DEVICES] = {
	{	.cold_product_id = USB_VP7041_PRODUCT_PREFW_ID, 
		.warm_product_id = USB_VP7041_PRODUCT_ID,
		.name = "Twinhan VisionDTV USB-Ter/HAMA USB DVB-T device", 
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	},
	{	.cold_product_id = USB_VSTREAM_PRODUCT_PREFW_ID,
		.warm_product_id = USB_VSTREAM_PRODUCT_ID,
		.name = "KWorld V-Stream XPERT DTV - DVB-T USB",
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	},
	{	.cold_product_id = USB_DIBCOM_PRODUCT_PREFW_ID,
		.warm_product_id = USB_DIBCOM_PRODUCT_ID,
		.name = "DiBcom USB reference design",
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	},
	{
 		.cold_product_id = USB_ULTIMA_ELEC_PROD_PREFW_ID,
		.warm_product_id = USB_ULTIMA_ELEC_PROD_ID,
		.name = "Ultima Electronic/Artec T1 USB TVBOX",
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	}, 
};

#define COMMAND_PIPE	usb_sndbulkpipe(dib->udev, 0x01)
#define RESULT_PIPE		usb_rcvbulkpipe(dib->udev, 0x81)
#define DATA_PIPE		usb_rcvbulkpipe(dib->udev, 0x82)
/* 
 * last endpoint 0x83 only used for chaining the buffers 
 * of the endpoints in the cypress 
 */
#define CHAIN_PIPE_DO_NOT_USE	usb_rcvbulkpipe(dib->udev, 0x83)

/* types of first byte of each buffer */

#define DIBUSB_REQ_START_READ			0x00
#define DIBUSB_REQ_START_DEMOD			0x01
#define DIBUSB_REQ_I2C_READ  			0x02
#define DIBUSB_REQ_I2C_WRITE 			0x03

/* prefix for reading the current RC key */
#define DIBUSB_REQ_POLL_REMOTE			0x04

/* 0x05 0xXX */
#define DIBUSB_REQ_SET_STREAMING_MODE	0x05

/* interrupt the internal read loop, when blocking */
#define DIBUSB_REQ_INTR_READ		   	0x06

/* IO control 
 * 0x07 <cmd 1 byte> <param 32 bytes>
 */ 
#define DIBUSB_REQ_SET_IOCTL			0x07

/* IOCTL commands */

/* change the power mode in firmware */ 
#define DIBUSB_IOCTL_CMD_POWER_MODE		0x00
#define DIBUSB_IOCTL_POWER_SLEEP			0x00
#define DIBUSB_IOCTL_POWER_WAKEUP			0x01


/* 
 * values from the demodulator which are needed in
 * the usb driver as well 
 */

#define DIB3000MB_REG_FIFO              (   145)
#define DIB3000MB_FIFO_INHIBIT              (     1)
#define DIB3000MB_FIFO_ACTIVATE             (     0)

#define DIB3000MB_ACTIVATE_FILTERING            (0x2000)

#endif
