/*
 * TTUSB DVB driver
 *
 * Copyright (c) 2002 Holger Waechtler <holger@convergence.de>
 * Copyright (c) 2003 Felix Domke <tmbinc@elitedvb.net>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <asm/semaphore.h>

#include "dvb_frontend.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_net.h"

#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <linux/pci.h>

#include "dvb_functions.h"

/*
  TTUSB_HWSECTIONS:
    the DSP supports filtering in hardware, however, since the "muxstream"
    is a bit braindead (no matching channel masks or no matching filter mask),
    we won't support this - yet. it doesn't event support negative filters,
    so the best way is maybe to keep TTUSB_HWSECTIONS undef'd and just
    parse TS data. USB bandwith will be a problem when having large
    datastreams, especially for dvb-net, but hey, that's not my problem.
	
  TTUSB_DISEQC, TTUSB_TONE:
    let the STC do the diseqc/tone stuff. this isn't supported at least with
    my TTUSB, so let it undef'd unless you want to implement another
    frontend. never tested.
		
  DEBUG:
    define it to > 3 for really hardcore debugging. you probably don't want
    this unless the device doesn't load at all. > 2 for bandwidth statistics.
*/

static int debug = 0;

#define dprintk(x...) do { if (debug) printk(KERN_DEBUG x); } while (0)

#define ISO_BUF_COUNT      4
#define FRAMES_PER_ISO_BUF 4
#define ISO_FRAME_SIZE     912
#define TTUSB_MAXCHANNEL   32
#ifdef TTUSB_HWSECTIONS
#define TTUSB_MAXFILTER    16	/* ??? */
#endif

#define TTUSB_BUDGET_NAME "ttusb_stc_fw"

/**
 *  since we're casting (struct ttusb*) <-> (struct dvb_demux*) around
 *  the dvb_demux field must be the first in struct!!
 */
struct ttusb {
	struct dvb_demux dvb_demux;
	struct dmxdev dmxdev;
	struct dvb_net dvbnet;

	/* our semaphore, for channel allocation/deallocation */
	struct semaphore sem;
	/* and one for USB access. */
	struct semaphore semusb;

	struct dvb_adapter *adapter;
	struct usb_device *dev;

	int disconnecting;
	int iso_streaming;

	unsigned int bulk_out_pipe;
	unsigned int bulk_in_pipe;
	unsigned int isoc_in_pipe;

	void *iso_buffer;
	dma_addr_t iso_dma_handle;

	struct urb *iso_urb[ISO_BUF_COUNT];

	int running_feed_count;
	int last_channel;
	int last_filter;

	u8 c;			/* transaction counter, wraps around...  */
	fe_sec_tone_mode_t tone;
	fe_sec_voltage_t voltage;

	int mux_state;		// 0..2 - MuxSyncWord, 3 - nMuxPacks,    4 - muxpack
	u8 mux_npacks;
	u8 muxpack[256 + 8];
	int muxpack_ptr, muxpack_len;

	int insync;

	int cc;			/* MuxCounter - will increment on EVERY MUX PACKET */
	/* (including stuffing. yes. really.) */


	u8 last_result[32];

	struct ttusb_channel {
		struct ttusb *ttusb;
		struct dvb_demux_feed *dvbdmxfeed;

		int active;
		int id;
		int pid;
		int type;	/* 1 - TS, 2 - Filter */
#ifdef TTUSB_HWSECTIONS
		int filterstate[TTUSB_MAXFILTER];	/* 0: not busy, 1: busy */
#endif
	} channel[TTUSB_MAXCHANNEL];
#if 0
	devfs_handle_t stc_devfs_handle;
#endif
};

/* ugly workaround ... don't know why it's neccessary to read */
/* all result codes. */

#define DEBUG 0
static int ttusb_cmd(struct ttusb *ttusb,
	      const u8 * data, int len, int needresult)
{
	int actual_len;
	int err;
#if DEBUG >= 3
	int i;

	printk(">");
	for (i = 0; i < len; ++i)
		printk(" %02x", data[i]);
	printk("\n");
#endif

	if (down_interruptible(&ttusb->semusb) < 0)
		return -EAGAIN;

	err = usb_bulk_msg(ttusb->dev, ttusb->bulk_out_pipe,
			   (u8 *) data, len, &actual_len, HZ);
	if (err != 0) {
		dprintk("%s: usb_bulk_msg(send) failed, err == %i!\n",
			__FUNCTION__, err);
		up(&ttusb->semusb);
		return err;
	}
	if (actual_len != len) {
		dprintk("%s: only wrote %d of %d bytes\n", __FUNCTION__,
			actual_len, len);
		up(&ttusb->semusb);
		return -1;
	}

	err = usb_bulk_msg(ttusb->dev, ttusb->bulk_in_pipe,
			   ttusb->last_result, 32, &actual_len, HZ);

	if (err != 0) {
		printk("%s: failed, receive error %d\n", __FUNCTION__,
		       err);
		up(&ttusb->semusb);
		return err;
	}
#if DEBUG >= 3
	actual_len = ttusb->last_result[3] + 4;
	printk("<");
	for (i = 0; i < actual_len; ++i)
		printk(" %02x", ttusb->last_result[i]);
	printk("\n");
#endif
	if (!needresult)
		up(&ttusb->semusb);
	return 0;
}

static int ttusb_result(struct ttusb *ttusb, u8 * data, int len)
{
	memcpy(data, ttusb->last_result, len);
	up(&ttusb->semusb);
	return 0;
}

static int ttusb_i2c_msg(struct ttusb *ttusb,
		  u8 addr, u8 * snd_buf, u8 snd_len, u8 * rcv_buf,
		  u8 rcv_len)
{
	u8 b[0x28];
	u8 id = ++ttusb->c;
	int i, err;

	if (snd_len > 0x28 - 7 || rcv_len > 0x20 - 7)
		return -EINVAL;

	b[0] = 0xaa;
	b[1] = id;
	b[2] = 0x31;
	b[3] = snd_len + 3;
	b[4] = addr << 1;
	b[5] = snd_len;
	b[6] = rcv_len;

	for (i = 0; i < snd_len; i++)
		b[7 + i] = snd_buf[i];

	err = ttusb_cmd(ttusb, b, snd_len + 7, 1);

	if (err)
		return -EREMOTEIO;

	err = ttusb_result(ttusb, b, 0x20);

        /* check if the i2c transaction was successful */
        if ((snd_len != b[5]) || (rcv_len != b[6])) return -EREMOTEIO;

	if (rcv_len > 0) {

		if (err || b[0] != 0x55 || b[1] != id) {
			dprintk
			    ("%s: usb_bulk_msg(recv) failed, err == %i, id == %02x, b == ",
			     __FUNCTION__, err, id);
			return -EREMOTEIO;
		}

		for (i = 0; i < rcv_len; i++)
			rcv_buf[i] = b[7 + i];
	}

	return rcv_len;
}

static int ttusb_i2c_xfer(struct dvb_i2c_bus *i2c, const struct i2c_msg msg[],
		   int num)
{
	struct ttusb *ttusb = i2c->data;
	int i = 0;
	int inc;

	if (down_interruptible(&ttusb->sem) < 0)
		return -EAGAIN;

	while (i < num) {
		u8 addr, snd_len, rcv_len, *snd_buf, *rcv_buf;
		int err;

		if (num > i + 1 && (msg[i + 1].flags & I2C_M_RD)) {
			addr = msg[i].addr;
			snd_buf = msg[i].buf;
			snd_len = msg[i].len;
			rcv_buf = msg[i + 1].buf;
			rcv_len = msg[i + 1].len;
			inc = 2;
		} else {
			addr = msg[i].addr;
			snd_buf = msg[i].buf;
			snd_len = msg[i].len;
			rcv_buf = NULL;
			rcv_len = 0;
			inc = 1;
		}

		err = ttusb_i2c_msg(ttusb, addr,
				    snd_buf, snd_len, rcv_buf, rcv_len);

		if (err < rcv_len) {
			dprintk("%s: i == %i\n", __FUNCTION__, i);
			break;
		}

		i += inc;
	}

	up(&ttusb->sem);
	return i;
}

#include "dvb-ttusb-dspbootcode.h"

static int ttusb_boot_dsp(struct ttusb *ttusb)
{
	int i, err;
	u8 b[40];

	/* BootBlock */
	b[0] = 0xaa;
	b[2] = 0x13;
	b[3] = 28;

	/* upload dsp code in 32 byte steps (36 didn't work for me ...) */
	/* 32 is max packet size, no messages should be splitted. */
	for (i = 0; i < sizeof(dsp_bootcode); i += 28) {
		memcpy(&b[4], &dsp_bootcode[i], 28);

		b[1] = ++ttusb->c;

		err = ttusb_cmd(ttusb, b, 32, 0);
		if (err)
			goto done;
	}

	/* last block ... */
	b[1] = ++ttusb->c;
	b[2] = 0x13;
	b[3] = 0;

	err = ttusb_cmd(ttusb, b, 4, 0);
	if (err)
		goto done;

	/* BootEnd */
	b[1] = ++ttusb->c;
	b[2] = 0x14;
	b[3] = 0;

	err = ttusb_cmd(ttusb, b, 4, 0);

      done:
	if (err) {
		dprintk("%s: usb_bulk_msg() failed, return value %i!\n",
			__FUNCTION__, err);
	}

	return err;
}

static int ttusb_set_channel(struct ttusb *ttusb, int chan_id, int filter_type,
		      int pid)
{
	int err;
	/* SetChannel */
	u8 b[] = { 0xaa, ++ttusb->c, 0x22, 4, chan_id, filter_type,
		(pid >> 8) & 0xff, pid & 0xff
	};

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	return err;
}

static int ttusb_del_channel(struct ttusb *ttusb, int channel_id)
{
	int err;
	/* DelChannel */
	u8 b[] = { 0xaa, ++ttusb->c, 0x23, 1, channel_id };

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	return err;
}

#ifdef TTUSB_HWSECTIONS
static int ttusb_set_filter(struct ttusb *ttusb, int filter_id,
		     int associated_chan, u8 filter[8], u8 mask[8])
{
	int err;
	/* SetFilter */
	u8 b[] = { 0xaa, 0, 0x24, 0x1a, filter_id, associated_chan,
		filter[0], filter[1], filter[2], filter[3],
		filter[4], filter[5], filter[6], filter[7],
		filter[8], filter[9], filter[10], filter[11],
		mask[0], mask[1], mask[2], mask[3],
		mask[4], mask[5], mask[6], mask[7],
		mask[8], mask[9], mask[10], mask[11]
	};

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	return err;
}

static int ttusb_del_filter(struct ttusb *ttusb, int filter_id)
{
	int err;
	/* DelFilter */
	u8 b[] = { 0xaa, ++ttusb->c, 0x25, 1, filter_id };

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	return err;
}
#endif

static int ttusb_init_controller(struct ttusb *ttusb)
{
	u8 b0[] = { 0xaa, ++ttusb->c, 0x15, 1, 0 };
	u8 b1[] = { 0xaa, ++ttusb->c, 0x15, 1, 1 };
	u8 b2[] = { 0xaa, ++ttusb->c, 0x32, 1, 0 };
	/* i2c write read: 5 bytes, addr 0x10, 0x02 bytes write, 1 bytes read. */
	u8 b3[] =
	    { 0xaa, ++ttusb->c, 0x31, 5, 0x10, 0x02, 0x01, 0x00, 0x1e };
	u8 b4[] =
	    { 0x55, ttusb->c, 0x31, 4, 0x10, 0x02, 0x01, 0x00, 0x1e };

	u8 get_version[] = { 0xaa, ++ttusb->c, 0x17, 5, 0, 0, 0, 0, 0 };
	u8 get_dsp_version[0x20] =
	    { 0xaa, ++ttusb->c, 0x26, 28, 0, 0, 0, 0, 0 };
	int err;

	/* reset board */
	if ((err = ttusb_cmd(ttusb, b0, sizeof(b0), 0)))
		return err;

	/* reset board (again?) */
	if ((err = ttusb_cmd(ttusb, b1, sizeof(b1), 0)))
		return err;

	ttusb_boot_dsp(ttusb);

	/* set i2c bit rate */
	if ((err = ttusb_cmd(ttusb, b2, sizeof(b2), 0)))
		return err;

	if ((err = ttusb_cmd(ttusb, b3, sizeof(b3), 1)))
		return err;

	err = ttusb_result(ttusb, b4, sizeof(b4));

	if ((err = ttusb_cmd(ttusb, get_version, sizeof(get_version), 1)))
		return err;

	if ((err = ttusb_result(ttusb, get_version, sizeof(get_version))))
		return err;

	dprintk("%s: stc-version: %c%c%c%c%c\n", __FUNCTION__,
		get_version[4], get_version[5], get_version[6],
		get_version[7], get_version[8]);

	if (memcmp(get_version + 4, "V 0.0", 5) &&
	    memcmp(get_version + 4, "V 1.1", 5) &&
   	    memcmp(get_version + 4, "V 2.1", 5)) {
		printk
		    ("%s: unknown STC version %c%c%c%c%c, please report!\n",
		     __FUNCTION__, get_version[4], get_version[5],
		     get_version[6], get_version[7], get_version[8]);
	}

	err =
	    ttusb_cmd(ttusb, get_dsp_version, sizeof(get_dsp_version), 1);
	if (err)
		return err;

	err =
	    ttusb_result(ttusb, get_dsp_version, sizeof(get_dsp_version));
	if (err)
		return err;
	printk("%s: dsp-version: %c%c%c\n", __FUNCTION__,
	       get_dsp_version[4], get_dsp_version[5], get_dsp_version[6]);
	return 0;
}

#ifdef TTUSB_DISEQC
static int ttusb_send_diseqc(struct ttusb *ttusb,
		      const struct dvb_diseqc_master_cmd *cmd)
{
	u8 b[12] = { 0xaa, ++ttusb->c, 0x18 };

	int err;

	b[3] = 4 + 2 + cmd->msg_len;
	b[4] = 0xFF;		/* send diseqc master, not burst */
	b[5] = cmd->msg_len;

	memcpy(b + 5, cmd->msg, cmd->msg_len);

	/* Diseqc */
	if ((err = ttusb_cmd(ttusb, b, 4 + b[3], 0))) {
		dprintk("%s: usb_bulk_msg() failed, return value %i!\n",
			__FUNCTION__, err);
	}

	return err;
}
#endif

static int ttusb_update_lnb(struct ttusb *ttusb)
{
	u8 b[] = { 0xaa, ++ttusb->c, 0x16, 5, /*power: */ 1,
		ttusb->voltage == SEC_VOLTAGE_18 ? 0 : 1,
		ttusb->tone == SEC_TONE_ON ? 1 : 0, 1, 1
	};
	int err;

	/* SetLNB */
	if ((err = ttusb_cmd(ttusb, b, sizeof(b), 0))) {
		dprintk("%s: usb_bulk_msg() failed, return value %i!\n",
			__FUNCTION__, err);
	}

	return err;
}

static int ttusb_set_voltage(struct ttusb *ttusb, fe_sec_voltage_t voltage)
{
	ttusb->voltage = voltage;
	return ttusb_update_lnb(ttusb);
}

#ifdef TTUSB_TONE
static int ttusb_set_tone(struct ttusb *ttusb, fe_sec_tone_mode_t tone)
{
	ttusb->tone = tone;
	return ttusb_update_lnb(ttusb);
}
#endif

static int ttusb_lnb_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct ttusb *ttusb = fe->i2c->data;

	switch (cmd) {
	case FE_SET_VOLTAGE:
		return ttusb_set_voltage(ttusb, (fe_sec_voltage_t) arg);
#ifdef TTUSB_TONE
	case FE_SET_TONE:
		return ttusb_set_tone(ttusb, (fe_sec_tone_mode_t) arg);
#endif
#ifdef TTUSB_DISEQC
	case FE_DISEQC_SEND_MASTER_CMD:
		return ttusb_send_diseqc(ttusb,
					 (struct dvb_diseqc_master_cmd *)
					 arg);
#endif
	default:
		return -EOPNOTSUPP;
	};
}

#if 0
static void ttusb_set_led_freq(struct ttusb *ttusb, u8 freq)
{
	u8 b[] = { 0xaa, ++ttusb->c, 0x19, 1, freq };
	int err, actual_len;

	err = ttusb_cmd(ttusb, b, sizeof(b), 0);
	if (err) {
		dprintk("%s: usb_bulk_msg() failed, return value %i!\n",
			__FUNCTION__, err);
	}
}
#endif

/*****************************************************************************/

#ifdef TTUSB_HWSECTIONS
static void ttusb_handle_ts_data(struct ttusb_channel *channel,
				 const u8 * data, int len);
static void ttusb_handle_sec_data(struct ttusb_channel *channel,
				  const u8 * data, int len);
#endif

int numpkt = 0, lastj, numts, numstuff, numsec, numinvalid;

static void ttusb_process_muxpack(struct ttusb *ttusb, const u8 * muxpack,
			   int len)
{
	u16 csum = 0, cc;
	int i;
	for (i = 0; i < len; i += 2)
		csum ^= le16_to_cpup((u16 *) (muxpack + i));
	if (csum) {
		printk("%s: muxpack with incorrect checksum, ignoring\n",
		       __FUNCTION__);
		numinvalid++;
		return;
	}

	cc = (muxpack[len - 4] << 8) | muxpack[len - 3];
	cc &= 0x7FFF;
	if ((cc != ttusb->cc) && (ttusb->cc != -1))
		printk("%s: cc discontinuity (%d frames missing)\n",
		       __FUNCTION__, (cc - ttusb->cc) & 0x7FFF);
	ttusb->cc = (cc + 1) & 0x7FFF;
	if (muxpack[0] & 0x80) {
#ifdef TTUSB_HWSECTIONS
		/* section data */
		int pusi = muxpack[0] & 0x40;
		int channel = muxpack[0] & 0x1F;
		int payload = muxpack[1];
		const u8 *data = muxpack + 2;
		/* check offset flag */
		if (muxpack[0] & 0x20)
			data++;

		ttusb_handle_sec_data(ttusb->channel + channel, data,
				      payload);
		data += payload;

		if ((!!(ttusb->muxpack[0] & 0x20)) ^
		    !!(ttusb->muxpack[1] & 1))
			data++;
#warning TODO: pusi
		printk("cc: %04x\n", (data[0] << 8) | data[1]);
#endif
		numsec++;
	} else if (muxpack[0] == 0x47) {
#ifdef TTUSB_HWSECTIONS
		/* we have TS data here! */
		int pid = ((muxpack[1] & 0x0F) << 8) | muxpack[2];
		int channel;
		for (channel = 0; channel < TTUSB_MAXCHANNEL; ++channel)
			if (ttusb->channel[channel].active
			    && (pid == ttusb->channel[channel].pid))
				ttusb_handle_ts_data(ttusb->channel +
						     channel, muxpack,
						     188);
#endif
		numts++;
		dvb_dmx_swfilter_packets(&ttusb->dvb_demux, muxpack, 1);
	} else if (muxpack[0] != 0) {
		numinvalid++;
		printk("illegal muxpack type %02x\n", muxpack[0]);
	} else
		numstuff++;
}

static void ttusb_process_frame(struct ttusb *ttusb, u8 * data, int len)
{
	int maxwork = 1024;
	while (len) {
		if (!(maxwork--)) {
			printk("%s: too much work\n", __FUNCTION__);
			break;
		}

		switch (ttusb->mux_state) {
		case 0:
		case 1:
		case 2:
			len--;
			if (*data++ == 0xAA)
				++ttusb->mux_state;
			else {
				ttusb->mux_state = 0;
#if DEBUG > 3
				if (ttusb->insync)
					printk("%02x ", data[-1]);
#else
				if (ttusb->insync) {
					printk("%s: lost sync.\n",
					       __FUNCTION__);
					ttusb->insync = 0;
				}
#endif
			}
			break;
		case 3:
			ttusb->insync = 1;
			len--;
			ttusb->mux_npacks = *data++;
			++ttusb->mux_state;
			ttusb->muxpack_ptr = 0;
			/* maximum bytes, until we know the length */
			ttusb->muxpack_len = 2;
			break;
		case 4:
			{
				int avail;
				avail = len;
				if (avail >
				    (ttusb->muxpack_len -
				     ttusb->muxpack_ptr))
					avail =
					    ttusb->muxpack_len -
					    ttusb->muxpack_ptr;
				memcpy(ttusb->muxpack + ttusb->muxpack_ptr,
				       data, avail);
				ttusb->muxpack_ptr += avail;
				if (ttusb->muxpack_ptr > 264)
					BUG();
				data += avail;
				len -= avail;
				/* determine length */
				if (ttusb->muxpack_ptr == 2) {
					if (ttusb->muxpack[0] & 0x80) {
						ttusb->muxpack_len =
						    ttusb->muxpack[1] + 2;
						if (ttusb->
						    muxpack[0] & 0x20)
							ttusb->
							    muxpack_len++;
						if ((!!
						     (ttusb->
						      muxpack[0] & 0x20)) ^
						    !!(ttusb->
						       muxpack[1] & 1))
							ttusb->
							    muxpack_len++;
						ttusb->muxpack_len += 4;
					} else if (ttusb->muxpack[0] ==
						   0x47)
						ttusb->muxpack_len =
						    188 + 4;
					else if (ttusb->muxpack[0] == 0x00)
						ttusb->muxpack_len =
						    ttusb->muxpack[1] + 2 +
						    4;
					else {
						dprintk
						    ("%s: invalid state: first byte is %x\n",
						     __FUNCTION__,
						     ttusb->muxpack[0]);
						ttusb->mux_state = 0;
					}
				}

			/**
			 * if length is valid and we reached the end:
			 * goto next muxpack
			 */
				if ((ttusb->muxpack_ptr >= 2) &&
				    (ttusb->muxpack_ptr ==
				     ttusb->muxpack_len)) {
					ttusb_process_muxpack(ttusb,
							      ttusb->
							      muxpack,
							      ttusb->
							      muxpack_ptr);
					ttusb->muxpack_ptr = 0;
					/* maximum bytes, until we know the length */
					ttusb->muxpack_len = 2;

				/**
				 * no muxpacks left?
				 * return to search-sync state
				 */
					if (!ttusb->mux_npacks--) {
						ttusb->mux_state = 0;
						break;
					}
				}
				break;
			}
		default:
			BUG();
			break;
		}
	}
}

static void ttusb_iso_irq(struct urb *urb, struct pt_regs *ptregs)
{
	struct ttusb *ttusb = urb->context;

	if (!ttusb->iso_streaming)
		return;

#if 0
	printk("%s: status %d, errcount == %d, length == %i\n",
	       __FUNCTION__,
	       urb->status, urb->error_count, urb->actual_length);
#endif

	if (!urb->status) {
		int i;
		for (i = 0; i < urb->number_of_packets; ++i) {
			struct usb_iso_packet_descriptor *d;
			u8 *data;
			int len;
			numpkt++;
			if ((jiffies - lastj) >= HZ) {
#if DEBUG > 2
				printk
				    ("frames/s: %d (ts: %d, stuff %d, sec: %d, invalid: %d, all: %d)\n",
				     numpkt * HZ / (jiffies - lastj),
				     numts, numstuff, numsec, numinvalid,
				     numts + numstuff + numsec +
				     numinvalid);
#endif
				numts = numstuff = numsec = numinvalid = 0;
				lastj = jiffies;
				numpkt = 0;
			}
			d = &urb->iso_frame_desc[i];
			data = urb->transfer_buffer + d->offset;
			len = d->actual_length;
			d->actual_length = 0;
			d->status = 0;
			ttusb_process_frame(ttusb, data, len);
		}
	}
	usb_submit_urb(urb, GFP_KERNEL);
}

static void ttusb_free_iso_urbs(struct ttusb *ttusb)
{
	int i;

	for (i = 0; i < ISO_BUF_COUNT; i++)
		if (ttusb->iso_urb[i])
			usb_free_urb(ttusb->iso_urb[i]);

	pci_free_consistent(NULL,
			    ISO_FRAME_SIZE * FRAMES_PER_ISO_BUF *
			    ISO_BUF_COUNT, ttusb->iso_buffer,
			    ttusb->iso_dma_handle);
}

static int ttusb_alloc_iso_urbs(struct ttusb *ttusb)
{
	int i;

	ttusb->iso_buffer = pci_alloc_consistent(NULL,
						 ISO_FRAME_SIZE *
						 FRAMES_PER_ISO_BUF *
						 ISO_BUF_COUNT,
						 &ttusb->iso_dma_handle);

	memset(ttusb->iso_buffer, 0,
	       ISO_FRAME_SIZE * FRAMES_PER_ISO_BUF * ISO_BUF_COUNT);

	for (i = 0; i < ISO_BUF_COUNT; i++) {
		struct urb *urb;

		if (!
		    (urb =
		     usb_alloc_urb(FRAMES_PER_ISO_BUF, GFP_KERNEL))) {
			ttusb_free_iso_urbs(ttusb);
			return -ENOMEM;
		}

		ttusb->iso_urb[i] = urb;
	}

	return 0;
}

static void ttusb_stop_iso_xfer(struct ttusb *ttusb)
{
	int i;

	for (i = 0; i < ISO_BUF_COUNT; i++)
		usb_unlink_urb(ttusb->iso_urb[i]);

	ttusb->iso_streaming = 0;
}

static int ttusb_start_iso_xfer(struct ttusb *ttusb)
{
	int i, j, err, buffer_offset = 0;

	if (ttusb->iso_streaming) {
		printk("%s: iso xfer already running!\n", __FUNCTION__);
		return 0;
	}

	ttusb->cc = -1;
	ttusb->insync = 0;
	ttusb->mux_state = 0;

	for (i = 0; i < ISO_BUF_COUNT; i++) {
		int frame_offset = 0;
		struct urb *urb = ttusb->iso_urb[i];

		urb->dev = ttusb->dev;
		urb->context = ttusb;
		urb->complete = ttusb_iso_irq;
		urb->pipe = ttusb->isoc_in_pipe;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->interval = 1;
		urb->number_of_packets = FRAMES_PER_ISO_BUF;
		urb->transfer_buffer_length =
		    ISO_FRAME_SIZE * FRAMES_PER_ISO_BUF;
		urb->transfer_buffer = ttusb->iso_buffer + buffer_offset;
		buffer_offset += ISO_FRAME_SIZE * FRAMES_PER_ISO_BUF;

		for (j = 0; j < FRAMES_PER_ISO_BUF; j++) {
			urb->iso_frame_desc[j].offset = frame_offset;
			urb->iso_frame_desc[j].length = ISO_FRAME_SIZE;
			frame_offset += ISO_FRAME_SIZE;
		}
	}

	for (i = 0; i < ISO_BUF_COUNT; i++) {
		if ((err = usb_submit_urb(ttusb->iso_urb[i], GFP_KERNEL))) {
			ttusb_stop_iso_xfer(ttusb);
			printk
			    ("%s: failed urb submission (%i: err = %i)!\n",
			     __FUNCTION__, i, err);
			return err;
		}
	}

	ttusb->iso_streaming = 1;

	return 0;
}

#ifdef TTUSB_HWSECTIONS
static void ttusb_handle_ts_data(struct ttusb_channel *channel, const u8 * data,
			  int len)
{
	struct dvb_demux_feed *dvbdmxfeed = channel->dvbdmxfeed;

	dvbdmxfeed->cb.ts(data, len, 0, 0, &dvbdmxfeed->feed.ts, 0);
}

static void ttusb_handle_sec_data(struct ttusb_channel *channel, const u8 * data,
			   int len)
{
//      struct dvb_demux_feed *dvbdmxfeed = channel->dvbdmxfeed;
#error TODO: handle ugly stuff
//      dvbdmxfeed->cb.sec(data, len, 0, 0, &dvbdmxfeed->feed.sec, 0);
}
#endif

static struct ttusb_channel *ttusb_channel_allocate(struct ttusb *ttusb)
{
	int i;

	if (down_interruptible(&ttusb->sem))
		return NULL;

	/* lock! */
	for (i = 0; i < TTUSB_MAXCHANNEL; ++i) {
		if (!ttusb->channel[i].active) {
			ttusb->channel[i].active = 1;
			up(&ttusb->sem);
			return ttusb->channel + i;
		}
	}

	up(&ttusb->sem);

	return NULL;
}

static int ttusb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct ttusb *ttusb = (struct ttusb *) dvbdmxfeed->demux;
	struct ttusb_channel *channel;

	dprintk("ttusb_start_feed\n");

	switch (dvbdmxfeed->type) {
	case DMX_TYPE_TS:
		break;
	case DMX_TYPE_SEC:
		break;
	default:
		return -EINVAL;
	}

	if (dvbdmxfeed->type == DMX_TYPE_TS) {
		switch (dvbdmxfeed->pes_type) {
		case DMX_TS_PES_VIDEO:
		case DMX_TS_PES_AUDIO:
		case DMX_TS_PES_TELETEXT:
		case DMX_TS_PES_PCR:
		case DMX_TS_PES_OTHER:
			channel = ttusb_channel_allocate(ttusb);
			break;
		default:
			return -EINVAL;
		}
	} else {
		channel = ttusb_channel_allocate(ttusb);
	}

	if (!channel)
		return -EBUSY;

	dvbdmxfeed->priv = channel;
	channel->dvbdmxfeed = dvbdmxfeed;

	channel->pid = dvbdmxfeed->pid;

#ifdef TTUSB_HWSECTIONS
	if (dvbdmxfeed->type == DMX_TYPE_TS) {
		channel->type = 1;
	} else if (dvbdmxfeed->type == DMX_TYPE_SEC) {
		channel->type = 2;
#error TODO: allocate filters
	}
#else
	channel->type = 1;
#endif

	ttusb_set_channel(ttusb, channel->id, channel->type, channel->pid);

	if (0 == ttusb->running_feed_count++)
		ttusb_start_iso_xfer(ttusb);

	return 0;
}

static int ttusb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct ttusb_channel *channel =
	    (struct ttusb_channel *) dvbdmxfeed->priv;
	struct ttusb *ttusb = (struct ttusb *) dvbdmxfeed->demux;

	ttusb_del_channel(channel->ttusb, channel->id);

	if (--ttusb->running_feed_count == 0)
		ttusb_stop_iso_xfer(ttusb);

	channel->active = 0;

	return 0;
}

static int ttusb_setup_interfaces(struct ttusb *ttusb)
{
	usb_set_interface(ttusb->dev, 1, 1);

	ttusb->bulk_out_pipe = usb_sndbulkpipe(ttusb->dev, 1);
	ttusb->bulk_in_pipe = usb_rcvbulkpipe(ttusb->dev, 1);
	ttusb->isoc_in_pipe = usb_rcvisocpipe(ttusb->dev, 2);

	return 0;
}

#if 0
static u8 stc_firmware[8192];

static int stc_open(struct inode *inode, struct file *file)
{
	struct ttusb *ttusb = file->private_data;
	int addr;

	for (addr = 0; addr < 8192; addr += 16) {
		u8 snd_buf[2] = { addr >> 8, addr & 0xFF };
		ttusb_i2c_msg(ttusb, 0x50, snd_buf, 2, stc_firmware + addr,
			      16);
	}

	return 0;
}

static ssize_t stc_read(struct file *file, char *buf, size_t count,
		 loff_t * offset)
{
	int tc = count;

	if ((tc + *offset) > 8192)
		tc = 8192 - *offset;

	if (tc < 0)
		return 0;

	if (copy_to_user(buf, stc_firmware + *offset, tc))
		return -EFAULT;

	*offset += tc;

	return tc;
}

static int stc_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations stc_fops = {
	.owner = THIS_MODULE,
	.read = stc_read,
	.open = stc_open,
	.release = stc_release,
};
#endif

static int ttusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct ttusb *ttusb;
	int result, channel;

	dprintk("%s: TTUSB DVB connected\n", __FUNCTION__);

	udev = interface_to_usbdev(intf);

	/* Device has already been reset; its configuration was chosen.
	 * If this fault happens, use a hotplug script to choose the
	 * right configuration (write bConfigurationValue in sysfs).
	 */
	if (udev->actconfig->desc.bConfigurationValue != 1) {
		dev_err(&intf->dev, "device config is #%d, need #1\n",
			udev->actconfig->desc.bConfigurationValue);
		return -ENODEV;
	}


        if (intf->altsetting->desc.bInterfaceNumber != 1) return -ENODEV;

	if (!(ttusb = kmalloc(sizeof(struct ttusb), GFP_KERNEL)))
		return -ENOMEM;

	memset(ttusb, 0, sizeof(struct ttusb));

	for (channel = 0; channel < TTUSB_MAXCHANNEL; ++channel) {
		ttusb->channel[channel].id = channel;
		ttusb->channel[channel].ttusb = ttusb;
	}

	ttusb->dev = udev;
	ttusb->c = 0;
	ttusb->mux_state = 0;
	sema_init(&ttusb->sem, 0);
	sema_init(&ttusb->semusb, 1);

	ttusb_setup_interfaces(ttusb);

	ttusb_alloc_iso_urbs(ttusb);
	if (ttusb_init_controller(ttusb))
		printk("ttusb_init_controller: error\n");

	up(&ttusb->sem);

	dvb_register_adapter(&ttusb->adapter, "Technotrend/Hauppauge Nova-USB", THIS_MODULE);

	dvb_register_i2c_bus(ttusb_i2c_xfer, ttusb, ttusb->adapter, 0);
	dvb_add_frontend_ioctls(ttusb->adapter, ttusb_lnb_ioctl, NULL,
				ttusb);

	memset(&ttusb->dvb_demux, 0, sizeof(ttusb->dvb_demux));

	ttusb->dvb_demux.dmx.capabilities =
	    DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	ttusb->dvb_demux.priv = 0;
#ifdef TTUSB_HWSECTIONS
	ttusb->dvb_demux.filternum = TTUSB_MAXFILTER;
#else
	ttusb->dvb_demux.filternum = 32;
#endif
	ttusb->dvb_demux.feednum = TTUSB_MAXCHANNEL;
	ttusb->dvb_demux.start_feed = ttusb_start_feed;
	ttusb->dvb_demux.stop_feed = ttusb_stop_feed;
	ttusb->dvb_demux.write_to_decoder = 0;

	if ((result = dvb_dmx_init(&ttusb->dvb_demux)) < 0) {
		printk("ttusb_dvb: dvb_dmx_init failed (errno = %d)\n",
		       result);
		goto err;
	}
//FIXME dmxdev (nur WAS?)
	ttusb->dmxdev.filternum = ttusb->dvb_demux.filternum;
	ttusb->dmxdev.demux = &ttusb->dvb_demux.dmx;
	ttusb->dmxdev.capabilities = 0;

	if ((result = dvb_dmxdev_init(&ttusb->dmxdev, ttusb->adapter)) < 0) {
		printk("ttusb_dvb: dvb_dmxdev_init failed (errno = %d)\n",
		       result);
		dvb_dmx_release(&ttusb->dvb_demux);
		goto err;
	}

	if (dvb_net_init
	    (ttusb->adapter, &ttusb->dvbnet, &ttusb->dvb_demux.dmx)) {
		printk("ttusb_dvb: dvb_net_init failed!\n");
	}

      err:
#if 0
	ttusb->stc_devfs_handle =
	    devfs_register(ttusb->adapter->devfs_handle, TTUSB_BUDGET_NAME,
			   DEVFS_FL_DEFAULT, 0, 192,
			   S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP
			   | S_IROTH | S_IWOTH, &stc_fops, ttusb);
#endif

	usb_set_intfdata(intf, (void *) ttusb);

	return 0;
}

static void ttusb_disconnect(struct usb_interface *intf)
{
	struct ttusb *ttusb = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	ttusb->disconnecting = 1;

	ttusb_stop_iso_xfer(ttusb);

	ttusb->dvb_demux.dmx.close(&ttusb->dvb_demux.dmx);
	dvb_net_release(&ttusb->dvbnet);
	dvb_dmxdev_release(&ttusb->dmxdev);
	dvb_dmx_release(&ttusb->dvb_demux);

	dvb_unregister_i2c_bus(ttusb_i2c_xfer, ttusb->adapter, 0);
	dvb_unregister_adapter(ttusb->adapter);

	ttusb_free_iso_urbs(ttusb);

	kfree(ttusb);

	dprintk("%s: TTUSB DVB disconnected\n", __FUNCTION__);
}

static struct usb_device_id ttusb_table[] = {
	{USB_DEVICE(0xb48, 0x1003)},
	{USB_DEVICE(0xb48, 0x1004)},	/* to be confirmed ????  */
	{USB_DEVICE(0xb48, 0x1005)},
	{}
};

MODULE_DEVICE_TABLE(usb, ttusb_table);

static struct usb_driver ttusb_driver = {
      .name 		= "Technotrend/Hauppauge USB-Nova",
      .probe 		= ttusb_probe,
      .disconnect 	= ttusb_disconnect,
      .id_table 	= ttusb_table,
};

static int __init ttusb_init(void)
{
	int err;

	if ((err = usb_register(&ttusb_driver)) < 0) {
		printk("%s: usb_register failed! Error number %d",
		       __FILE__, err);
		return err;
	}

	return 0;
}

static void __exit ttusb_exit(void)
{
	usb_deregister(&ttusb_driver);
}

module_init(ttusb_init);
module_exit(ttusb_exit);

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug or not");

MODULE_AUTHOR("Holger Waechtler <holger@convergence.de>");
MODULE_DESCRIPTION("TTUSB DVB Driver");
MODULE_LICENSE("GPL");
