/*
 * Driver for mobile USB Budget DVB-T devices based on reference
 * design made by DiBcom (http://www.dibcom.fr/)
 *
 * dvb-dibusb.c
 *
 * Copyright (C) 2004 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * based on GPL code from DiBcom, which has
 * Copyright (C) 2004 Amaury Demol for DiBcom (ademol@dibcom.fr)
 *
 * Remote control code added by David Matthews (dm@prolingua.co.uk)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * Acknowledgements
 *
 *  Amaury Demol (ademol@dibcom.fr) from DiBcom for providing specs and driver
 *  sources, on which this driver (and the dib3000mb/mc/p frontends) are based.
 *
 * see Documentation/dvb/README.dibusb for more information
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/input.h>

#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_filter.h"
#include "dvb_net.h"
#include "dvb_frontend.h"
#include "dib3000.h"

#include "dvb-dibusb.h"


/* debug */
#ifdef CONFIG_DVB_DIBCOM_DEBUG
#define dprintk(level,args...) \
	    do { if ((debug & level)) { printk(args); } } while (0)

#define debug_dump(b,l) if (debug) {\
	int i; deb_xfer("%s: %d > ",__FUNCTION__,l); \
	for (i = 0; i < l; i++) deb_xfer("%02x ", b[i]); \
	deb_xfer("\n");\
}

static int debug;
module_param(debug, int, 0x644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,2=xfer,4=alotmore,8=ts,16=err,32=rc (|-able)).");
#else
#define dprintk(args...)
#define debug_dump(b,l)
#endif

#define deb_info(args...) dprintk(0x01,args)
#define deb_xfer(args...) dprintk(0x02,args)
#define deb_alot(args...) dprintk(0x04,args)
#define deb_ts(args...)   dprintk(0x08,args)
#define deb_err(args...)   dprintk(0x10,args)
#define deb_rc(args...)   dprintk(0x20,args)

static int pid_parse;
module_param(pid_parse, int, 0x644);
MODULE_PARM_DESC(pid_parse, "enable pid parsing (filtering) when running at USB2.0");

/* Version information */
#define DRIVER_VERSION "0.1"
#define DRIVER_DESC "Driver for DiBcom based USB Budget DVB-T device"
#define DRIVER_AUTHOR "Patrick Boettcher, patrick.boettcher@desy.de"

static int dibusb_readwrite_usb(struct usb_dibusb *dib,
		u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	int actlen,ret = -ENOMEM;

	if (wbuf == NULL || wlen == 0)
		return -EINVAL;

	if ((ret = down_interruptible(&dib->usb_sem)))
		return ret;

	if (dib->feedcount &&
		wbuf[0] == DIBUSB_REQ_I2C_WRITE &&
		dib->dibdev->parm->type == DIBUSB1_1)
		deb_err("BUG: writing to i2c, while TS-streaming destroys the stream."
				"(%x reg: %x %x)\n", wbuf[0],wbuf[2],wbuf[3]);
			
	debug_dump(wbuf,wlen);

	ret = usb_bulk_msg(dib->udev,usb_sndbulkpipe(dib->udev,
			dib->dibdev->parm->cmd_pipe), wbuf,wlen,&actlen,
			DIBUSB_I2C_TIMEOUT);

	if (ret)
		err("bulk message failed: %d (%d/%d)",ret,wlen,actlen);
	else
		ret = actlen != wlen ? -1 : 0;

	/* an answer is expected, and no error before */
	if (!ret && rbuf && rlen) {
		ret = usb_bulk_msg(dib->udev,usb_rcvbulkpipe(dib->udev,
				dib->dibdev->parm->result_pipe),rbuf,rlen,&actlen,
				DIBUSB_I2C_TIMEOUT);

		if (ret)
			err("recv bulk message failed: %d",ret);
		else {
			deb_alot("rlen: %d\n",rlen);
			debug_dump(rbuf,actlen);
		}
	}

	up(&dib->usb_sem);
	return ret;
}

static int dibusb_i2c_msg(struct usb_dibusb *dib, u8 addr,
		u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	u8 sndbuf[wlen+4]; /* lead(1) devaddr,direction(1) addr(2) data(wlen) (len(2) (when reading)) */
	/* write only ? */
	int wo = (rbuf == NULL || rlen == 0),
		len = 2 + wlen + (wo ? 0 : 2);

	deb_alot("wo: %d, wlen: %d, len: %d\n",wo,wlen,len);

	sndbuf[0] = wo ? DIBUSB_REQ_I2C_WRITE : DIBUSB_REQ_I2C_READ;
	sndbuf[1] = (addr & 0xfe) | (wo ? 0 : 1);

	memcpy(&sndbuf[2],wbuf,wlen);

	if (!wo) {
		sndbuf[wlen+2] = (rlen >> 8) & 0xff;
		sndbuf[wlen+3] = rlen & 0xff;
	}

	return dibusb_readwrite_usb(dib,sndbuf,len,rbuf,rlen);
}

/*
 * DVB stuff
 */
static void dibusb_urb_complete(struct urb *urb, struct pt_regs *ptregs)
{
	struct usb_dibusb *dib = urb->context;

	deb_ts("urb complete feedcount: %d, status: %d\n",dib->feedcount,urb->status);

	if (dib->feedcount > 0 && urb->status == 0) {
		deb_ts("URB return len: %d\n",urb->actual_length);
		if (urb->actual_length % 188)
			deb_ts("TS Packets: %d, %d\n", urb->actual_length/188,urb->actual_length % 188);

		/* Francois recommends to drop not full-filled packets, even if they may 
		 * contain valid TS packets
		 */
		if (urb->actual_length == dib->dibdev->parm->default_size && dib->dvb_is_ready)
		dvb_dmx_swfilter_packets(&dib->demux, (u8*) urb->transfer_buffer,urb->actual_length/188);
		else
			deb_ts("URB dropped because of the " 
					"actual_length or !dvb_is_ready (%d).\n",dib->dvb_is_ready);
	} else 
		deb_ts("URB dropped because of feedcount or status.\n");

		usb_submit_urb(urb,GFP_KERNEL);
}

static int dibusb_ctrl_feed(struct usb_dibusb *dib, int pid, int onoff)
{
	if (dib->dibdev->parm->firmware_bug && dib->feedcount) {
		deb_ts("stop feeding\n");
		if (dib->xfer_ops.fifo_ctrl != NULL) {
			if (dib->xfer_ops.fifo_ctrl(dib->fe,0)) {
				err("error while inhibiting fifo.");
				return -ENODEV;
			}
		} else {
			err("fifo_ctrl is not set.");
			return -ENODEV;
		}
	}

	dib->feedcount += onoff ? 1 : -1;

	if (dib->pid_parse) {
	if (dib->xfer_ops.pid_ctrl != NULL) {
		if (dib->xfer_ops.pid_ctrl(dib->fe,pid,onoff) < 0) {
		err("no free pid in list.");
		return -ENODEV;
	}
	} else {
		err("no pid ctrl callback.");
		return -ENODEV;
	}
	}
	/*
	 * start the feed, either if there is the firmware bug or
	 * if this was the first pid to set.
	 */
	if (dib->dibdev->parm->firmware_bug || dib->feedcount == onoff) {

		deb_ts("controlling pid parser\n");
		if (dib->xfer_ops.pid_parse != NULL) {
			if (dib->xfer_ops.pid_parse(dib->fe,dib->pid_parse) < 0) {
				err("could not handle pid_parser");
			}
		}

		deb_ts("start feeding\n");
		if (dib->xfer_ops.fifo_ctrl != NULL) {
			if (dib->xfer_ops.fifo_ctrl(dib->fe,1)) {
				err("error while enabling fifo.");
				return -ENODEV;
			}
		} else {
			err("fifo_ctrl is not set.");
			return -ENODEV;
}
	}
	return 0;
}

static int dibusb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct usb_dibusb *dib = dvbdmxfeed->demux->priv;
	deb_ts("pid: 0x%04x, feedtype: %d\n", dvbdmxfeed->pid,dvbdmxfeed->type);
	dvbdmxfeed->priv = dib;
	return dibusb_ctrl_feed(dib,dvbdmxfeed->pid,1);
}

static int dibusb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct usb_dibusb *dib = (struct usb_dibusb *) dvbdmxfeed->priv;
	if (dib == NULL) {
		err("dib in dmxfeed->priv was NULL");
		return -EINVAL;
}
	deb_ts("dvbdmxfeed pid: 0x%04x, feedtype: %d\n",
			dvbdmxfeed->pid, dvbdmxfeed->type);
	return dibusb_ctrl_feed(dib,dvbdmxfeed->pid,0);
}

/* Table to map raw key codes to key events.  This should not be hard-wired
   into the kernel.  */
static const struct { u8 c0, c1, c2; uint32_t key; } rc_keys [] =
{
	/* Key codes for the little Artec T1/Twinhan/HAMA/ remote. */
	{ 0x00, 0xff, 0x16, KEY_POWER },
	{ 0x00, 0xff, 0x10, KEY_MUTE },
	{ 0x00, 0xff, 0x03, KEY_1 },
	{ 0x00, 0xff, 0x01, KEY_2 },
	{ 0x00, 0xff, 0x06, KEY_3 },
	{ 0x00, 0xff, 0x09, KEY_4 },
	{ 0x00, 0xff, 0x1d, KEY_5 },
	{ 0x00, 0xff, 0x1f, KEY_6 },
	{ 0x00, 0xff, 0x0d, KEY_7 },
	{ 0x00, 0xff, 0x19, KEY_8 },
	{ 0x00, 0xff, 0x1b, KEY_9 },
	{ 0x00, 0xff, 0x15, KEY_0 },
	{ 0x00, 0xff, 0x05, KEY_CHANNELUP },
	{ 0x00, 0xff, 0x02, KEY_CHANNELDOWN },
	{ 0x00, 0xff, 0x1e, KEY_VOLUMEUP },
	{ 0x00, 0xff, 0x0a, KEY_VOLUMEDOWN },
	{ 0x00, 0xff, 0x11, KEY_RECORD },
	{ 0x00, 0xff, 0x17, KEY_FAVORITES }, /* Heart symbol - Channel list. */
	{ 0x00, 0xff, 0x14, KEY_PLAY },
	{ 0x00, 0xff, 0x1a, KEY_STOP },
	{ 0x00, 0xff, 0x40, KEY_REWIND },
	{ 0x00, 0xff, 0x12, KEY_FASTFORWARD },
	{ 0x00, 0xff, 0x0e, KEY_PREVIOUS }, /* Recall - Previous channel. */
	{ 0x00, 0xff, 0x4c, KEY_PAUSE },
	{ 0x00, 0xff, 0x4d, KEY_SCREEN }, /* Full screen mode. */
	{ 0x00, 0xff, 0x54, KEY_AUDIO }, /* MTS - Switch to secondary audio. */
	/* additional keys TwinHan VisionPlus, the Artec seemingly not have */
	{ 0x00, 0xff, 0x0c, KEY_CANCEL }, /* Cancel */
	{ 0x00, 0xff, 0x1c, KEY_EPG }, /* EPG */
	{ 0x00, 0xff, 0x00, KEY_TAB }, /* Tab */
	{ 0x00, 0xff, 0x48, KEY_INFO }, /* Preview */
	{ 0x00, 0xff, 0x04, KEY_LIST }, /* RecordList */
	{ 0x00, 0xff, 0x0f, KEY_TEXT }, /* Teletext */
	/* Key codes for the KWorld/ADSTech/JetWay remote. */
	{ 0x86, 0x6b, 0x12, KEY_POWER },
	{ 0x86, 0x6b, 0x0f, KEY_SELECT }, /* source */
	{ 0x86, 0x6b, 0x0c, KEY_UNKNOWN }, /* scan */
	{ 0x86, 0x6b, 0x0b, KEY_EPG },
	{ 0x86, 0x6b, 0x10, KEY_MUTE },
	{ 0x86, 0x6b, 0x01, KEY_1 },
	{ 0x86, 0x6b, 0x02, KEY_2 },
	{ 0x86, 0x6b, 0x03, KEY_3 },
	{ 0x86, 0x6b, 0x04, KEY_4 },
	{ 0x86, 0x6b, 0x05, KEY_5 },
	{ 0x86, 0x6b, 0x06, KEY_6 },
	{ 0x86, 0x6b, 0x07, KEY_7 },
	{ 0x86, 0x6b, 0x08, KEY_8 },
	{ 0x86, 0x6b, 0x09, KEY_9 },
	{ 0x86, 0x6b, 0x0a, KEY_0 },
	{ 0x86, 0x6b, 0x18, KEY_ZOOM },
	{ 0x86, 0x6b, 0x1c, KEY_UNKNOWN }, /* preview */
	{ 0x86, 0x6b, 0x13, KEY_UNKNOWN }, /* snap */
	{ 0x86, 0x6b, 0x00, KEY_UNDO },
	{ 0x86, 0x6b, 0x1d, KEY_RECORD },
	{ 0x86, 0x6b, 0x0d, KEY_STOP },
	{ 0x86, 0x6b, 0x0e, KEY_PAUSE },
	{ 0x86, 0x6b, 0x16, KEY_PLAY },
	{ 0x86, 0x6b, 0x11, KEY_BACK },
	{ 0x86, 0x6b, 0x19, KEY_FORWARD },
	{ 0x86, 0x6b, 0x14, KEY_UNKNOWN }, /* pip */
	{ 0x86, 0x6b, 0x15, KEY_ESC },
	{ 0x86, 0x6b, 0x1a, KEY_UP },
	{ 0x86, 0x6b, 0x1e, KEY_DOWN },
	{ 0x86, 0x6b, 0x1f, KEY_LEFT },
	{ 0x86, 0x6b, 0x1b, KEY_RIGHT },
};

/*
 * Read the remote control and feed the appropriate event.
 * NEC protocol is used for remote controls
 */
static int dibusb_read_remote_control(struct usb_dibusb *dib)
{
	u8 b[1] = { DIBUSB_REQ_POLL_REMOTE }, rb[5];
	int ret;
	int i;
	if ((ret = dibusb_readwrite_usb(dib,b,1,rb,5)))
		return ret;

	switch (rb[0]) {
		case DIBUSB_RC_NEC_KEY_PRESSED:
			/* rb[1-3] is the actual key, rb[4] is a checksum */
			deb_rc("raw key code 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
				rb[1], rb[2], rb[3], rb[4]);

			if ((0xff - rb[3]) != rb[4]) {
				deb_rc("remote control checksum failed.\n");
				break;
			}

			/* See if we can match the raw key code. */
			for (i = 0; i < sizeof(rc_keys)/sizeof(rc_keys[0]); i++) {
				if (rc_keys[i].c0 == rb[1] &&
					rc_keys[i].c1 == rb[2] &&
				    rc_keys[i].c2 == rb[3]) {
					dib->rc_input_event = rc_keys[i].key;
					deb_rc("Translated key 0x%04x\n", dib->rc_input_event);
					/* Signal down and up events for this key. */
					input_report_key(&dib->rc_input_dev, dib->rc_input_event, 1);
					input_report_key(&dib->rc_input_dev, dib->rc_input_event, 0);
					input_sync(&dib->rc_input_dev);
					break;
				}
			}
			break;
		case DIBUSB_RC_NEC_EMPTY: /* No (more) remote control keys. */
			break;
		case DIBUSB_RC_NEC_KEY_REPEATED:
			/* rb[1]..rb[4] are always zero.*/
			/* Repeats often seem to occur so for the moment just ignore this. */
			deb_rc("Key repeat\n");
			break;
		default:
			break;
	}
	
	return 0;
}

#define RC_QUERY_INTERVAL (100)	/* milliseconds */

/* Remote-control poll function - called every RC_QUERY_INTERVAL ms to see
   whether the remote control has received anything. */
static void dibusb_query_rc (void *data)
{
	struct usb_dibusb *dib = (struct usb_dibusb *) data;
	/* TODO: need a lock here.  We can simply skip checking for the remote control
	   if we're busy. */
	dibusb_read_remote_control(dib);
	schedule_delayed_work(&dib->rc_query_work,
			      msecs_to_jiffies(RC_QUERY_INTERVAL));
}

/*
 * Cypress controls
 */

#if 0
/*
 * #if 0'ing the following functions as they are not in use _now_,
 * but probably will be sometime.
 */

/*
 * do not use this, just a workaround for a bug,
 * which will hopefully never occur :).
 */
static int dibusb_interrupt_read_loop(struct usb_dibusb *dib)
{
	u8 b[1] = { DIBUSB_REQ_INTR_READ };
	return dibusb_write_usb(dib,b,1);
}

/*
 * ioctl for power control
 */
static int dibusb_hw_sleep(struct usb_dibusb *dib)
{
	u8 b[1] = { DIBUSB_IOCTL_POWER_SLEEP };
	return dibusb_ioctl_cmd(dib,DIBUSB_IOCTL_CMD_POWER_MODE, b,1);
}

#endif
static int dibusb_write_usb(struct usb_dibusb *dib, u8 *buf, u16 len)
{
	return dibusb_readwrite_usb(dib,buf,len,NULL,0);
}

/*
 * ioctl for the firmware
 */
static int dibusb_ioctl_cmd(struct usb_dibusb *dib, u8 cmd, u8 *param, int plen)
{
	u8 b[34];
	int size = plen > 32 ? 32 : plen;
	b[0] = DIBUSB_REQ_SET_IOCTL;
	b[1] = cmd;
	memcpy(&b[2],param,size);

	return dibusb_write_usb(dib,b,2+size);
}

static int dibusb_hw_wakeup(struct usb_dibusb *dib)
{
	u8 b[1] = { DIBUSB_IOCTL_POWER_WAKEUP };
	return dibusb_ioctl_cmd(dib,DIBUSB_IOCTL_CMD_POWER_MODE, b,1);
}

/*
 * I2C
 */
static int dibusb_i2c_xfer(struct i2c_adapter *adap,struct i2c_msg msg[],int num)
{
	struct usb_dibusb *dib = i2c_get_adapdata(adap);
	int i;

	if (down_interruptible(&dib->i2c_sem) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		/* write/read request */
		if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			if (dibusb_i2c_msg(dib, msg[i].addr, msg[i].buf,msg[i].len,
						msg[i+1].buf,msg[i+1].len) < 0)
				break;
			i++;
		} else
			if (dibusb_i2c_msg(dib, msg[i].addr, msg[i].buf,msg[i].len,NULL,0) < 0)
				break;
	}

	up(&dib->i2c_sem);
	return i;
}

static u32 dibusb_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static int thomson_cable_eu_pll_set(struct dvb_frontend* fe, struct
		dvb_frontend_parameters* params);

static struct dib3000_config thomson_cable_eu_config = {
	.demod_address = 0x10,
	.pll_addr = 194,
	.pll_set = thomson_cable_eu_pll_set,
};

static int thomson_cable_eu_pll_set(struct dvb_frontend* fe, struct
		dvb_frontend_parameters* params)
{
	struct usb_dibusb* dib = (struct usb_dibusb*) fe->dvb->priv;
	u8 buf[4];
	struct i2c_msg msg = {
		.addr = thomson_cable_eu_config.pll_addr,
		.flags = 0,
		.buf = buf,
		.len = sizeof(buf)
	};
	u32 tfreq = (params->frequency + 36125000) / 62500;
	int vu,p0,p1,p2;

	if (params->frequency > 403250000)
		vu = 1, p2 = 1, p1 = 0, p0 = 1;
	else if (params->frequency > 115750000)
		vu = 0, p2 = 1, p1 = 1, p0 = 0;
	else if (params->frequency > 44250000)
		vu = 0, p2 = 0, p1 = 1, p0 = 1;
	else
		return -EINVAL;

	buf[0] = (tfreq >> 8) & 0x7f;
	buf[1] = tfreq & 0xff;
   	buf[2] = 0x8e;
   	buf[3] = (vu << 7) | (p2 << 2) | (p1 << 1) | p0;

	if (i2c_transfer (&dib->i2c_adap, &msg, 1) != 1)
		return -EIO;

	msleep(1);
	return 0;
}

static int panasonic_cofdm_env57h1xd5_pll_set(struct dvb_frontend *fe, struct
		dvb_frontend_parameters *params);

static struct dib3000_config panasonic_cofdm_env57h1xd5 = {
	.demod_address = 0x18,
	.pll_addr = 192,
	.pll_set = panasonic_cofdm_env57h1xd5_pll_set,
};

static int panasonic_cofdm_env57h1xd5_pll_set(struct dvb_frontend *fe, struct
		dvb_frontend_parameters *params)
{
	struct usb_dibusb* dib = (struct usb_dibusb*) fe->dvb->priv;
	u8 buf[4];
	u32 freq = params->frequency;
	u32 tfreq = (freq + 36125000) / 1000000 * 6 + 1;
	u8 TA, T210, R210, ctrl1, cp210, p4321;
	struct i2c_msg msg = {
		.addr = panasonic_cofdm_env57h1xd5.pll_addr,
		.flags = 0,
		.buf = buf,
		.len = sizeof(buf)
	};

	if (freq > 858000000) {
		err("frequency cannot be larger than 858 MHz.");
		return -EINVAL;
	}

	// contol data 1 : 1 | T/A=1 | T2,T1,T0 = 0,0,0 | R2,R1,R0 = 0,1,0
	TA = 1;
	T210 = 0;
	R210 = 0x2;
	ctrl1 = (1 << 7) | (TA << 6) | (T210 << 3) | R210;

// ********    CHARGE PUMP CONFIG vs RF FREQUENCIES     *****************
	if (freq < 470000000)
		cp210 = 2;  // VHF Low and High band ch E12 to E4 to E12
	else if (freq < 526000000)
		cp210 = 4;  // UHF band Ch E21 to E27
	else // if (freq < 862000000)
		cp210 = 5;  // UHF band ch E28 to E69

//*********************    BW select  *******************************
	if (freq < 153000000)
		p4321  = 1; // BW selected for VHF low
	else if (freq < 470000000)
		p4321  = 2; // BW selected for VHF high E5 to E12
	else // if (freq < 862000000)
		p4321  = 4; // BW selection for UHF E21 to E69

	buf[0] = (tfreq >> 8) & 0xff;
	buf[1] = (tfreq >> 0) & 0xff;
	buf[2] = 0xff & ctrl1;
	buf[3] =  (cp210 << 5) | (p4321);

	if (i2c_transfer (&dib->i2c_adap, &msg, 1) != 1)
		return -EIO;

	msleep(1);
	return 0;
}

static struct i2c_algorithm dibusb_algo = {
	.name			= "DiBcom USB i2c algorithm",
	.id				= I2C_ALGO_BIT,
	.master_xfer	= dibusb_i2c_xfer,
	.functionality	= dibusb_i2c_func,
};

static void frontend_init(struct usb_dibusb* dib)
{
	switch (dib->dibdev->parm->type) {
		case DIBUSB1_1:
		case DIBUSB1_1_AN2235:
	dib->fe = dib3000mb_attach(&thomson_cable_eu_config, &dib->i2c_adap,&dib->xfer_ops);
			break;
		case DIBUSB2_0:
			dib->fe = dib3000mc_attach(&panasonic_cofdm_env57h1xd5,&dib->i2c_adap, &dib->xfer_ops);
			break;
	}

	if (dib->fe == NULL) {
		printk("dvb-dibusb: A frontend driver was not found for device %04x/%04x\n",
		       dib->udev->descriptor.idVendor,
		       dib->udev->descriptor.idProduct);
	} else {
		if (dvb_register_frontend(dib->adapter, dib->fe)) {
			printk("dvb-dibusb: Frontend registration failed!\n");
			if (dib->fe->ops->release)
				dib->fe->ops->release(dib->fe);
			dib->fe = NULL;
		}
	}
}

static int dibusb_dvb_init(struct usb_dibusb *dib)
{
	int ret;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,4)
    if ((ret = dvb_register_adapter(&dib->adapter, DRIVER_DESC)) < 0) {
#else
    if ((ret = dvb_register_adapter(&dib->adapter, DRIVER_DESC ,
			THIS_MODULE)) < 0) {
#endif
		deb_info("dvb_register_adapter failed: error %d", ret);
		goto err;
	}
	dib->adapter->priv = dib;

	strncpy(dib->i2c_adap.name,dib->dibdev->name,I2C_NAME_SIZE);
#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	dib->i2c_adap.class = I2C_ADAP_CLASS_TV_DIGITAL,
#else
	dib->i2c_adap.class = I2C_CLASS_TV_DIGITAL,
#endif
	dib->i2c_adap.algo 		= &dibusb_algo;
	dib->i2c_adap.algo_data = NULL;
	dib->i2c_adap.id		= I2C_ALGO_BIT;

	i2c_set_adapdata(&dib->i2c_adap, dib);

	if ((i2c_add_adapter(&dib->i2c_adap) < 0)) {
		err("could not add i2c adapter");
		goto err_i2c;
	}

	dib->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;

	dib->demux.priv = (void *)dib;
	/* get pidcount from demod */
	dib->demux.feednum = dib->demux.filternum = 16;
	dib->demux.start_feed = dibusb_start_feed;
	dib->demux.stop_feed = dibusb_stop_feed;
	dib->demux.write_to_decoder = NULL;
	if ((ret = dvb_dmx_init(&dib->demux)) < 0) {
		err("dvb_dmx_init failed: error %d",ret);
		goto err_dmx;
	}

	dib->dmxdev.filternum = dib->demux.filternum;
	dib->dmxdev.demux = &dib->demux.dmx;
	dib->dmxdev.capabilities = 0;
	if ((ret = dvb_dmxdev_init(&dib->dmxdev, dib->adapter)) < 0) {
		err("dvb_dmxdev_init failed: error %d",ret);
		goto err_dmx_dev;
	}

	dvb_net_init(dib->adapter, &dib->dvb_net, &dib->demux.dmx);

	frontend_init(dib);

	/* Start the remote-control polling. */
	schedule_delayed_work(&dib->rc_query_work, msecs_to_jiffies(RC_QUERY_INTERVAL));

	goto success;
err_dmx_dev:
	dvb_dmx_release(&dib->demux);
err_dmx:
	i2c_del_adapter(&dib->i2c_adap);
err_i2c:
	dvb_unregister_adapter(dib->adapter);
err:
	return ret;
success:
	dib->dvb_is_ready = 1;
	return 0;
}

static int dibusb_dvb_exit(struct usb_dibusb *dib)
{
	cancel_delayed_work(&dib->rc_query_work);
	flush_scheduled_work();
	input_unregister_device(&dib->rc_input_dev);

	dib->dvb_is_ready = 0;
	deb_info("unregistering DVB part\n");
	dvb_net_release(&dib->dvb_net);
	dib->demux.dmx.close(&dib->demux.dmx);
	dvb_dmxdev_release(&dib->dmxdev);
	dvb_dmx_release(&dib->demux);
	if (dib->fe != NULL) dvb_unregister_frontend(dib->fe);
	i2c_del_adapter(&dib->i2c_adap);
	dvb_unregister_adapter(dib->adapter);

	return 0;
}

static int dibusb_exit(struct usb_dibusb *dib)
{
	int i;
	if (dib->urb_list != NULL) {
		for (i = 0; i < dib->dibdev->parm->num_urbs; i++) {
			if (dib->urb_list[i] != NULL) {
			deb_info("killing URB no. %d.\n",i);

				/* stop the URBs */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,7)
				usb_unlink_urb(dib->urb_list[i]);
#else
				usb_kill_urb(dib->urb_list[i]);
#endif
			
			deb_info("freeing URB no. %d.\n",i);
				/* free the URBs */
				usb_free_urb(dib->urb_list[i]);
			}
		}
		/* free the urb array */
		kfree(dib->urb_list);
		}

	pci_free_consistent(NULL,
		dib->dibdev->parm->urb_buf_size*dib->dibdev->parm->num_urbs,dib->buffer,
		dib->dma_handle);
	return 0;
}

static int dibusb_init(struct usb_dibusb *dib)
{
	int ret,i,bufsize;
	sema_init(&dib->usb_sem, 1);
	sema_init(&dib->i2c_sem, 1);

	/*
	 * when reloading the driver w/o replugging the device
	 * a timeout occures, this helps
	 */
	usb_clear_halt(dib->udev,usb_sndbulkpipe(dib->udev,dib->dibdev->parm->cmd_pipe));
	usb_clear_halt(dib->udev,usb_rcvbulkpipe(dib->udev,dib->dibdev->parm->result_pipe));
	usb_clear_halt(dib->udev,usb_rcvbulkpipe(dib->udev,dib->dibdev->parm->data_pipe));

	/* allocate the array for the data transfer URBs */
	dib->urb_list = kmalloc(dib->dibdev->parm->num_urbs*sizeof(struct urb *),GFP_KERNEL);
	if (dib->urb_list == NULL)
		return -ENOMEM;
	memset(dib->urb_list,0,dib->dibdev->parm->num_urbs*sizeof(struct urb *));

	bufsize = dib->dibdev->parm->num_urbs*dib->dibdev->parm->urb_buf_size;
	deb_info("allocate %d bytes as buffersize for all URBs\n",bufsize);
	/* allocate the actual buffer for the URBs */
	if ((dib->buffer = pci_alloc_consistent(NULL,bufsize,&dib->dma_handle)) == NULL) {
		deb_info("not enough memory.\n");
		dibusb_exit(dib);
		return -ENOMEM;
	}
	deb_info("allocation complete\n");
	memset(dib->buffer,0,bufsize);

	/* allocate and submit the URBs */
	for (i = 0; i < dib->dibdev->parm->num_urbs; i++) {
		if (!(dib->urb_list[i] = usb_alloc_urb(0,GFP_KERNEL))) {
		dibusb_exit(dib);
		return -ENOMEM;
	}
		deb_info("submitting URB no. %d\n",i);

		usb_fill_bulk_urb( dib->urb_list[i], dib->udev,
				usb_rcvbulkpipe(dib->udev,dib->dibdev->parm->data_pipe),
				&dib->buffer[i*dib->dibdev->parm->urb_buf_size],
				dib->dibdev->parm->urb_buf_size,
				dibusb_urb_complete, dib);

		dib->urb_list[i]->transfer_flags = 0;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,7)
		dib->urb_list[i]->timeout = 0;
#endif

		if ((ret = usb_submit_urb(dib->urb_list[i],GFP_KERNEL))) {
			err("could not submit buffer urb no. %d\n",i);
			dibusb_exit(dib);
			return ret;
		}
	}

	dib->dvb_is_ready = 0;

	/* Initialise the remote-control structures.*/
	init_input_dev(&dib->rc_input_dev);

	dib->rc_input_dev.evbit[0] = BIT(EV_KEY);
	dib->rc_input_dev.keycodesize = sizeof(unsigned char);
	dib->rc_input_dev.keycodemax = KEY_MAX;
	dib->rc_input_dev.name = DRIVER_DESC " remote control";

	for (i=0; i<sizeof(rc_keys)/sizeof(rc_keys[0]); i++)
		set_bit(rc_keys[i].key, dib->rc_input_dev.keybit);

	input_register_device(&dib->rc_input_dev);

	dib->rc_input_event = KEY_MAX;

	INIT_WORK(&dib->rc_query_work, dibusb_query_rc, dib);

	dibusb_hw_wakeup(dib);

	if ((ret = dibusb_dvb_init(dib))) {
		dibusb_exit(dib);
		return ret;
	}
	return 0;
}

/*
 * load a firmware packet to the device
 */
static int dibusb_writemem(struct usb_device *udev,u16 addr,u8 *data, u8 len)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev,0),
			0xa0, USB_TYPE_VENDOR, addr, 0x00, data, len, 5*HZ);
}

static int dibusb_loadfirmware(struct usb_device *udev,
		struct dibusb_device *dibdev)
{
	const struct firmware *fw = NULL;
	const char **fws;
	u16 addr;
	u8 *b,*p;
	int ret = 0,i;

	fws = dibdev->parm->fw_filenames;

	for (i = 0; i < sizeof(fws)/sizeof(const char*); i++) {
		if ((ret = request_firmware(&fw, fws[i], &udev->dev)) == 0) {
			info("using firmware file (%s).",fws[i]);
			break;
		}
		deb_info("tried to find '%s' firmware - unsuccessful. (%d)\n",
				fws[i],ret);
	}

	if (fw == NULL) {
		err("did not find a valid firmware file. "
			"Please see linux/Documentation/dvb/ for more details on firmware-problems.");
		return -EINVAL;
	}
	p = kmalloc(fw->size,GFP_KERNEL);
	if (p != NULL) {
		u8 reset;
		/*
		 * you cannot use the fw->data as buffer for
		 * usb_control_msg, a new buffer has to be
		 * created
		 */
		memcpy(p,fw->data,fw->size);

		/* stop the CPU */
		reset = 1;
		if ((ret = dibusb_writemem(udev,dibdev->parm->usb_cpu_csreg,&reset,1)) != 1)
			err("could not stop the USB controller CPU.");
		for(i = 0; p[i+3] == 0 && i < fw->size; ) {
			b = (u8 *) &p[i];
			addr = *((u16 *) &b[1]);

			ret = dibusb_writemem(udev,addr,&b[4],b[0]);

			if (ret != b[0]) {
				err("error while transferring firmware "
					"(transferred size: %d, block size: %d)",
					ret,b[0]);
				ret = -EINVAL;
				break;
			}
			i += 5 + b[0];
		}
		/* length in ret */
		if (ret > 0)
			ret = 0;
		/* restart the CPU */
		reset = 0;
		if (ret || dibusb_writemem(udev,dibdev->parm->usb_cpu_csreg,&reset,1) != 1) {
			err("could not restart the USB controller CPU.");
			ret = -EINVAL;
		}

		kfree(p);
	} else {
		ret = -ENOMEM;
	}
	release_firmware(fw);

	return ret;
}

/*
 * USB
 */
static int dibusb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_dibusb *dib = NULL;
	struct dibusb_device *dibdev = NULL;

	int ret = -ENOMEM,i,cold=0;

	for (i = 0; i < DIBUSB_SUPPORTED_DEVICES; i++)
		if (dibusb_devices[i].cold_product_id == udev->descriptor.idProduct ||
			dibusb_devices[i].warm_product_id == udev->descriptor.idProduct) {
			dibdev = &dibusb_devices[i];

			cold = dibdev->cold_product_id == udev->descriptor.idProduct;

			if (cold)
				info("found a '%s' in cold state, will try to load a firmware",dibdev->name);
			else
				info("found a '%s' in warm state.",dibdev->name);
		}

	if (dibdev == NULL) {
		err("something went very wrong, "
				"unknown product ID: %.4x",udev->descriptor.idProduct);
		return -ENODEV;
	}

	if (cold)
		ret = dibusb_loadfirmware(udev,dibdev);
	else {
		dib = kmalloc(sizeof(struct usb_dibusb),GFP_KERNEL);
		if (dib == NULL) {
			err("no memory");
			return ret;
		}
		memset(dib,0,sizeof(struct usb_dibusb));

		dib->pid_parse = 1;
		switch (udev->speed) {
			case USB_SPEED_LOW:
				err("cannot handle USB speed because it is to sLOW.");
				break;
			case USB_SPEED_FULL:
				info("running at FULL speed, will use pid parsing.");
				break;
			case USB_SPEED_HIGH:
				if (!pid_parse) {
					dib->pid_parse = 0;
				info("running at HIGH speed, will deliver the complete TS.");
				} else
					info("running at HIGH speed, will use pid_parsing anyway.");
				break;
			case USB_SPEED_UNKNOWN: /* fall through */
			default:
				err("cannot handle USB speed because it is unkown.");
				break;
		}

		dib->udev = udev;
		dib->dibdev = dibdev;

		usb_set_intfdata(intf, dib);

		ret = dibusb_init(dib);
	}

	if (ret == 0)
		info("%s successfully initialized and connected.",dibdev->name);
	else
		info("%s error while loading driver (%d)",dibdev->name,ret);
	return ret;
}

static void dibusb_disconnect(struct usb_interface *intf)
{
	struct usb_dibusb *dib = usb_get_intfdata(intf);
	const char *name = DRIVER_DESC;

	usb_set_intfdata(intf,NULL);
	if (dib != NULL) {
		name = dib->dibdev->name;
		dibusb_dvb_exit(dib);
		dibusb_exit(dib);
		kfree(dib);
	}
	info("%s successfully deinitialized and disconnected.",name);

}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver dibusb_driver = {
	.owner		= THIS_MODULE,
	.name		= "dvb_dibusb",
	.probe 		= dibusb_probe,
	.disconnect = dibusb_disconnect,
	.id_table 	= dibusb_table,
};

/* module stuff */
static int __init usb_dibusb_init(void)
{
	int result;
	if ((result = usb_register(&dibusb_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit usb_dibusb_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&dibusb_driver);
}

module_init (usb_dibusb_init);
module_exit (usb_dibusb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
