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
 *
 * for more information see dvb-dibusb.c .
 */

#ifndef __DVB_DIBUSB_H__
#define __DVB_DIBUSB_H__

#define DIBUSB_DEMOD_I2C_ADDR_DEFAULT	0x10

/* Vendor IDs */
#define USB_VID_TWINHAN_ID					0x1822
#define USB_VID_IMC_NETWORKS_ID				0x13d3
#define USB_VID_EMPIA_ID					0xeb1a
#define USB_VID_DIBCOM_ID					0x10b8
#define USB_VID_ULTIMA_ELECTRONIC_ID		0x05d8
#define USB_VID_COMPRO_ID					0x185b
#define USB_VID_HYPER_PALTEK				0x1025

/* Product IDs before loading the firmware */
#define USB_PID_TWINHAN_VP7041_COLD_ID		0x3201
#define USB_PID_KWORLD_VSTREAM_COLD_ID		0x17de
#define USB_PID_DIBCOM_MOD3000_COLD_ID		0x0bb8
#define USB_PID_ULTIMA_TVBOX_COLD_ID		0x8105
#define USB_PID_COMPRO_DVBU2000_COLD_ID		0xd000
#define USB_PID_UNK_HYPER_PALTEK_COLD_ID	0x005e

/* product ID afterwards */
#define USB_PID_TWINHAN_VP7041_WARM_ID		0x3202
#define USB_PID_KWORLD_VSTREAM_WARM_ID		0x17df
#define USB_PID_DIBCOM_MOD3000_WARM_ID		0x0bb9
#define USB_PID_ULTIMA_TVBOX_WARM_ID		0x8106
#define USB_PID_COMPRO_DVBU2000_WARM_ID		0xd001
#define USB_PID_UNK_HYPER_PALTEK_WARM_ID	0x005f

/* static array of valid firmware names, the best one first */
static const char * valid_firmware_filenames[] = {
	"dvb-dibusb-5.0.0.11.fw",
};

struct dibusb_device {
	u16 cold_product_id;
	u16 warm_product_id;
	u8 demod_addr;
	const char *name;
};

#define DIBUSB_SUPPORTED_DEVICES	6

/* USB Driver stuff */
static struct dibusb_device dibusb_devices[DIBUSB_SUPPORTED_DEVICES] = {
	{	.cold_product_id = USB_PID_TWINHAN_VP7041_COLD_ID, 
		.warm_product_id = USB_PID_TWINHAN_VP7041_WARM_ID,
		.name = "TwinhanDTV USB-Ter/Magic Box / HAMA USB DVB-T device", 
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	},
	{	.cold_product_id = USB_PID_KWORLD_VSTREAM_COLD_ID,
		.warm_product_id = USB_PID_KWORLD_VSTREAM_WARM_ID,
		.name = "KWorld V-Stream XPERT DTV - DVB-T USB",
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	},
	{	.cold_product_id = USB_PID_DIBCOM_MOD3000_COLD_ID,
		.warm_product_id = USB_PID_DIBCOM_MOD3000_WARM_ID,
		.name = "DiBcom USB DVB-T reference design (MOD300)",
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	},
	{	.cold_product_id = USB_PID_ULTIMA_TVBOX_COLD_ID,
		.warm_product_id = USB_PID_ULTIMA_TVBOX_WARM_ID,
		.name = "Ultima Electronic/Artec T1 USB TVBOX",
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	},
	{	.cold_product_id = USB_PID_COMPRO_DVBU2000_COLD_ID,
		.warm_product_id = USB_PID_COMPRO_DVBU2000_WARM_ID,
		.name = "Compro Videomate DVB-U2000 - DVB-T USB",
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	},
	{	.cold_product_id = USB_PID_UNK_HYPER_PALTEK_COLD_ID,
		.warm_product_id = USB_PID_UNK_HYPER_PALTEK_WARM_ID,
		.name = "Unkown USB DVB-T device ???? please report the name to linux-dvb or to the author",
		.demod_addr = DIBUSB_DEMOD_I2C_ADDR_DEFAULT,
	}
};

/* USB Driver stuff */
/* table of devices that work with this driver */
static struct usb_device_id dibusb_table [] = {
	{ USB_DEVICE(USB_VID_TWINHAN_ID, 	USB_PID_TWINHAN_VP7041_COLD_ID) },
	{ USB_DEVICE(USB_VID_TWINHAN_ID, 	USB_PID_TWINHAN_VP7041_WARM_ID) },
	{ USB_DEVICE(USB_VID_IMC_NETWORKS_ID,USB_PID_TWINHAN_VP7041_COLD_ID) },
	{ USB_DEVICE(USB_VID_IMC_NETWORKS_ID,USB_PID_TWINHAN_VP7041_WARM_ID) },
	{ USB_DEVICE(USB_VID_EMPIA_ID,		USB_PID_KWORLD_VSTREAM_COLD_ID) },
	{ USB_DEVICE(USB_VID_EMPIA_ID,		USB_PID_KWORLD_VSTREAM_WARM_ID) },
	{ USB_DEVICE(USB_VID_DIBCOM_ID,		USB_PID_DIBCOM_MOD3000_COLD_ID) },
	{ USB_DEVICE(USB_VID_DIBCOM_ID,		USB_PID_DIBCOM_MOD3000_WARM_ID) },
	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC_ID, USB_PID_ULTIMA_TVBOX_COLD_ID) },
	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC_ID, USB_PID_ULTIMA_TVBOX_WARM_ID) },
	{ USB_DEVICE(USB_VID_COMPRO_ID,		USB_PID_COMPRO_DVBU2000_COLD_ID) },
	{ USB_DEVICE(USB_VID_COMPRO_ID,		USB_PID_COMPRO_DVBU2000_WARM_ID) },
	{ USB_DEVICE(USB_VID_HYPER_PALTEK,	USB_PID_UNK_HYPER_PALTEK_COLD_ID) },
	{ USB_DEVICE(USB_VID_HYPER_PALTEK,	USB_PID_UNK_HYPER_PALTEK_WARM_ID) },
	{ }                 /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, dibusb_table);

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

#define DIBUSB_TS_NUM_URBS			3
#define DIBUSB_TS_URB_BUFFER_SIZE	4096
#define DIBUSB_TS_BUFFER_SIZE		(DIBUSB_TS_NUM_URBS * DIBUSB_TS_URB_BUFFER_SIZE)
#define DIBUSB_TS_DEFAULT_SIZE		(188*21)

struct usb_dibusb {
	/* usb */
	struct usb_device * udev;

	struct dibusb_device * dibdev;

	int feedcount;
	int streaming;
	struct urb * buf_urb[DIBUSB_TS_NUM_URBS];
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
	int dvb_is_ready;
	struct dvb_adapter *adapter;
	struct dmxdev dmxdev;
	struct dvb_demux demux;
	struct dvb_net dvb_net;
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

#define DIBUSB_RC_NEC_EMPTY				0x00
#define DIBUSB_RC_NEC_KEY_PRESSED		0x01
#define DIBUSB_RC_NEC_KEY_REPEATED		0x02

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
