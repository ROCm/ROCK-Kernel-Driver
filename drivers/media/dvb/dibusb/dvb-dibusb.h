/*
 * dvb-dibusb.h
 *
 * Copyright (C) 2004 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * for more information see dvb-dibusb.c .
 */

#ifndef __DVB_DIBUSB_H__
#define __DVB_DIBUSB_H__

#include "dib3000.h"

typedef enum {
	DIBUSB1_1 = 0,
	DIBUSB2_0,
	DIBUSB1_1_AN2235,
} dibusb_type;

static const char * dibusb_fw_filenames1_1[] = {
	"dvb-dibusb-5.0.0.11.fw"
};

static const char * dibusb_fw_filenames1_1_an2235[] = {
	"dvb-dibusb-an2235-1.fw"
};

static const char * dibusb_fw_filenames2_0[] = {
	"dvb-dibusb-6.0.0.5.fw"
};

struct dibusb_device_parameter {
	dibusb_type type;
	u8 demod_addr;
	const char **fw_filenames;
	const char *usb_controller;
	u16 usb_cpu_csreg;

	int num_urbs;
	int urb_buf_size;
	int default_size;
	int firmware_bug;

	int cmd_pipe;
	int result_pipe;
	int data_pipe;
};

static struct dibusb_device_parameter dibusb_dev_parm[3] = {
	{	.type = DIBUSB1_1,
		.demod_addr = 0x10,
		.fw_filenames = dibusb_fw_filenames1_1,
		.usb_controller = "Cypress AN2135",
		.usb_cpu_csreg = 0x7f92,

		.num_urbs = 3,
		.urb_buf_size = 4096,
		.default_size = 188*21,
		.firmware_bug = 1,

		.cmd_pipe = 0x01,
		.result_pipe = 0x81,
		.data_pipe = 0x82,
	},
	{	.type = DIBUSB2_0,
		.demod_addr = 0x18,
		.fw_filenames = dibusb_fw_filenames2_0,
		.usb_controller = "Cypress FX2",
		.usb_cpu_csreg = 0xe600,

		.num_urbs = 3,
		.urb_buf_size = 40960,
		.default_size = 188*210,
		.firmware_bug = 0,

		.cmd_pipe = 0x01,
		.result_pipe = 0x81,
		.data_pipe = 0x86,
	},
	{	.type = DIBUSB1_1_AN2235,
		.demod_addr = 0x10,
		.fw_filenames = dibusb_fw_filenames1_1_an2235,
		.usb_controller = "Cypress CY7C64613 (AN2235)",
		.usb_cpu_csreg = 0x7f92,

		.num_urbs = 3,
		.urb_buf_size = 4096,
		.default_size = 188*21,
		.firmware_bug = 1,

		.cmd_pipe = 0x01,
		.result_pipe = 0x81,
		.data_pipe = 0x82,
	}
};

struct dibusb_device {
	const char *name;
	u16 cold_product_id;
	u16 warm_product_id;
	struct dibusb_device_parameter *parm;
};

/* Vendor IDs */
#define USB_VID_ANCHOR						0x0547
#define USB_VID_AVERMEDIA					0x14aa
#define USB_VID_COMPRO						0x185b
#define USB_VID_COMPRO_UNK					0x145f
#define USB_VID_CYPRESS						0x04b4
#define USB_VID_DIBCOM						0x10b8
#define USB_VID_EMPIA						0xeb1a
#define USB_VID_GRANDTEC					0x5032
#define USB_VID_HYPER_PALTEK				0x1025
#define USB_VID_IMC_NETWORKS				0x13d3
#define USB_VID_TWINHAN						0x1822
#define USB_VID_ULTIMA_ELECTRONIC			0x05d8

/* Product IDs */
#define USB_PID_AVERMEDIA_DVBT_USB_COLD		0x0001
#define USB_PID_AVERMEDIA_DVBT_USB_WARM		0x0002
#define USB_PID_COMPRO_DVBU2000_COLD		0xd000
#define USB_PID_COMPRO_DVBU2000_WARM		0xd001
#define USB_PID_COMPRO_DVBU2000_UNK_COLD	0x010c
#define USB_PID_COMPRO_DVBU2000_UNK_WARM	0x010d
#define USB_PID_DIBCOM_MOD3000_COLD			0x0bb8
#define USB_PID_DIBCOM_MOD3000_WARM			0x0bb9
#define USB_PID_DIBCOM_MOD3001_COLD			0x0bc6
#define USB_PID_DIBCOM_MOD3001_WARM			0x0bc7
#define USB_PID_GRANDTEC_DVBT_USB_COLD		0x0fa0
#define USB_PID_GRANDTEC_DVBT_USB_WARM		0x0fa1
#define USB_PID_KWORLD_VSTREAM_COLD			0x17de
#define USB_PID_KWORLD_VSTREAM_WARM			0x17df
#define USB_PID_TWINHAN_VP7041_COLD			0x3201
#define USB_PID_TWINHAN_VP7041_WARM			0x3202
#define USB_PID_ULTIMA_TVBOX_COLD			0x8105
#define USB_PID_ULTIMA_TVBOX_WARM			0x8106
#define USB_PID_ULTIMA_TVBOX_AN2235_COLD	0x8107
#define USB_PID_ULTIMA_TVBOX_AN2235_WARM	0x8108
#define USB_PID_ULTIMA_TVBOX_ANCHOR_COLD	0x2235
#define USB_PID_ULTIMA_TVBOX_USB2_COLD		0x8109
#define USB_PID_ULTIMA_TVBOX_USB2_FX_COLD	0x8613
#define USB_PID_ULTIMA_TVBOX_USB2_FX_WARM	0x1002
#define USB_PID_UNK_HYPER_PALTEK_COLD		0x005e
#define USB_PID_UNK_HYPER_PALTEK_WARM		0x005f
#define USB_PID_YAKUMO_DTT200U_COLD			0x0201
#define USB_PID_YAKUMO_DTT200U_WARM			0x0301

#define DIBUSB_SUPPORTED_DEVICES	15

/* USB Driver stuff */
static struct dibusb_device dibusb_devices[DIBUSB_SUPPORTED_DEVICES] = {
	{	.name = "TwinhanDTV USB1.1 / Magic Box / HAMA USB1.1 DVB-T device",
		.cold_product_id = USB_PID_TWINHAN_VP7041_COLD,
		.warm_product_id = USB_PID_TWINHAN_VP7041_WARM,
		.parm = &dibusb_dev_parm[0],
	},
	{	.name = "KWorld V-Stream XPERT DTV - DVB-T USB1.1",
		.cold_product_id = USB_PID_KWORLD_VSTREAM_COLD,
		.warm_product_id = USB_PID_KWORLD_VSTREAM_WARM,
		.parm = &dibusb_dev_parm[0],
	},
	{	.name = "Grandtec USB1.1 DVB-T/DiBcom USB1.1 DVB-T reference design (MOD3000)",
		.cold_product_id = USB_PID_DIBCOM_MOD3000_COLD,
		.warm_product_id = USB_PID_DIBCOM_MOD3000_WARM,
		.parm = &dibusb_dev_parm[0],
	},
	{	.name = "Artec T1 USB1.1 TVBOX with AN2135",
		.cold_product_id = USB_PID_ULTIMA_TVBOX_COLD,
		.warm_product_id = USB_PID_ULTIMA_TVBOX_WARM,
		.parm = &dibusb_dev_parm[0],
	},
	{	.name = "Artec T1 USB1.1 TVBOX with AN2235",
		.cold_product_id = USB_PID_ULTIMA_TVBOX_AN2235_COLD,
		.warm_product_id = USB_PID_ULTIMA_TVBOX_AN2235_WARM,
		.parm = &dibusb_dev_parm[2],
	},
	{	.name = "Artec T1 USB1.1 TVBOX with AN2235 (misdesigned)",
		.cold_product_id = USB_PID_ULTIMA_TVBOX_ANCHOR_COLD,
		.warm_product_id = 0, /* undefined, this design becomes USB_PID_DIBCOM_MOD3000_WARM in warm state */
		.parm = &dibusb_dev_parm[2],
	},
	{	.name = "Artec T1 USB2.0 TVBOX (please report the warm ID)",
		.cold_product_id = USB_PID_ULTIMA_TVBOX_USB2_COLD,
		.warm_product_id = 0, /* don't know, it is most likely that the device will get another USB ID in warm state */
		.parm = &dibusb_dev_parm[1],
	},
	{	.name = "Artec T1 USB2.0 TVBOX with FX2 IDs (misdesigned, please report the warm ID)",
		.cold_product_id = USB_PID_ULTIMA_TVBOX_USB2_FX_COLD,
		.warm_product_id = USB_PID_ULTIMA_TVBOX_USB2_FX_WARM, /* undefined, it could be that the device will get another USB ID in warm state */
		.parm = &dibusb_dev_parm[1],
	},
	{	.name = "Compro Videomate DVB-U2000 - DVB-T USB1.1",
		.cold_product_id = USB_PID_COMPRO_DVBU2000_COLD,
		.warm_product_id = USB_PID_COMPRO_DVBU2000_WARM,
		.parm = &dibusb_dev_parm[0],
	},
	{	.name = "Compro Videomate DVB-U2000 - DVB-T USB1.1 (really ?? please report the name!)",
		.cold_product_id = USB_PID_COMPRO_DVBU2000_UNK_COLD,
		.warm_product_id = USB_PID_COMPRO_DVBU2000_UNK_WARM,
		.parm = &dibusb_dev_parm[0],
	},
	{	.name = "Unkown USB1.1 DVB-T device ???? please report the name to the author",
		.cold_product_id = USB_PID_UNK_HYPER_PALTEK_COLD,
		.warm_product_id = USB_PID_UNK_HYPER_PALTEK_WARM,
		.parm = &dibusb_dev_parm[0],
	},
	{	.name = "DiBcom USB2.0 DVB-T reference design (MOD3000P)",
		.cold_product_id = USB_PID_DIBCOM_MOD3001_COLD,
		.warm_product_id = USB_PID_DIBCOM_MOD3001_WARM,
		.parm = &dibusb_dev_parm[1],
	},
	{	.name = "Grandtec DVB-T USB1.1",
		.cold_product_id = USB_PID_GRANDTEC_DVBT_USB_COLD,
		.warm_product_id = USB_PID_GRANDTEC_DVBT_USB_WARM,
		.parm = &dibusb_dev_parm[0],
	},
	{	.name = "Avermedia AverTV DVBT USB1.1",
		.cold_product_id = USB_PID_AVERMEDIA_DVBT_USB_COLD,
		.warm_product_id = USB_PID_AVERMEDIA_DVBT_USB_WARM,
		.parm = &dibusb_dev_parm[0],
	},
	{	.name = "Yakumo DVB-T mobile USB2.0",
		.cold_product_id = USB_PID_YAKUMO_DTT200U_COLD,
		.warm_product_id = USB_PID_YAKUMO_DTT200U_WARM,
		.parm = &dibusb_dev_parm[1],
	}
};

/* USB Driver stuff */
/* table of devices that work with this driver */
static struct usb_device_id dibusb_table [] = {
	{ USB_DEVICE(USB_VID_AVERMEDIA,		USB_PID_AVERMEDIA_DVBT_USB_COLD)},
	{ USB_DEVICE(USB_VID_AVERMEDIA,		USB_PID_AVERMEDIA_DVBT_USB_WARM)},
	{ USB_DEVICE(USB_VID_COMPRO,		USB_PID_COMPRO_DVBU2000_COLD) },
	{ USB_DEVICE(USB_VID_COMPRO,		USB_PID_COMPRO_DVBU2000_WARM) },
	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3000_COLD) },
	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3000_WARM) },
	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3001_COLD) },
	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3001_WARM) },
	{ USB_DEVICE(USB_VID_EMPIA,			USB_PID_KWORLD_VSTREAM_COLD) },
	{ USB_DEVICE(USB_VID_EMPIA,			USB_PID_KWORLD_VSTREAM_WARM) },
	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_GRANDTEC_DVBT_USB_COLD) },
	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_GRANDTEC_DVBT_USB_WARM) },
	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_DIBCOM_MOD3000_COLD) },
	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_DIBCOM_MOD3000_WARM) },
	{ USB_DEVICE(USB_VID_HYPER_PALTEK,	USB_PID_UNK_HYPER_PALTEK_COLD) },
	{ USB_DEVICE(USB_VID_HYPER_PALTEK,	USB_PID_UNK_HYPER_PALTEK_WARM) },
	{ USB_DEVICE(USB_VID_IMC_NETWORKS,	USB_PID_TWINHAN_VP7041_COLD) },
	{ USB_DEVICE(USB_VID_IMC_NETWORKS,	USB_PID_TWINHAN_VP7041_WARM) },
	{ USB_DEVICE(USB_VID_TWINHAN, 		USB_PID_TWINHAN_VP7041_COLD) },
	{ USB_DEVICE(USB_VID_TWINHAN, 		USB_PID_TWINHAN_VP7041_WARM) },
	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_COLD) },
	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_WARM) },
	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_AN2235_COLD) },
	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_AN2235_WARM) },
	{ USB_DEVICE(USB_VID_AVERMEDIA,		USB_PID_YAKUMO_DTT200U_COLD) },
	{ USB_DEVICE(USB_VID_AVERMEDIA,		USB_PID_YAKUMO_DTT200U_WARM) },
	{ USB_DEVICE(USB_PID_COMPRO_DVBU2000_UNK_COLD, USB_VID_COMPRO_UNK) },
	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC,	USB_PID_ULTIMA_TVBOX_USB2_COLD) },

/*
 * activate the following define when you have one of the devices and want to
 * build it from build-2.6 in dvb-kernel
 */
// #define CONFIG_DVB_DIBUSB_MISDESIGNED_DEVICES
#ifdef CONFIG_DVB_DIBUSB_MISDESIGNED_DEVICES
	{ USB_DEVICE(USB_VID_ANCHOR,		USB_PID_ULTIMA_TVBOX_ANCHOR_COLD) },
	{ USB_DEVICE(USB_VID_CYPRESS,		USB_PID_ULTIMA_TVBOX_USB2_FX_COLD) },
	{ USB_DEVICE(USB_VID_ANCHOR,		USB_PID_ULTIMA_TVBOX_USB2_FX_WARM) },
#endif
	{ }                 /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, dibusb_table);

#define DIBUSB_I2C_TIMEOUT 				HZ*5

struct usb_dibusb {
	/* usb */
	struct usb_device * udev;

	struct dibusb_device * dibdev;

	int feedcount;
	int pid_parse;
	struct dib3000_xfer_ops xfer_ops;

	struct urb **urb_list;
	u8 *buffer;
	dma_addr_t dma_handle;

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
	struct dvb_frontend* fe;

	/* remote control */
	struct input_dev rc_input_dev;
	struct work_struct rc_query_work;
	int rc_input_event;
};


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

#endif
