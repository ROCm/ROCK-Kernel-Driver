/* 
    Frontend-driver for TwinHan DST Frontend

    Copyright (C) 2003 Jamie Honan



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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "dvb_functions.h"
#include "dst-bt878.h"

unsigned int dst_debug = 0;
unsigned int dst_verbose = 0;

MODULE_PARM(dst_verbose, "i");
MODULE_PARM_DESC(dst_verbose,
		 "verbose startup messages, default is 1 (yes)");
MODULE_PARM(dst_debug, "i");
MODULE_PARM_DESC(dst_debug, "debug messages, default is 0 (no)");

#define DST_MAX_CARDS	6
unsigned int dst_cur_no = 0;

unsigned int dst_type[DST_MAX_CARDS] = { [0 ... (DST_MAX_CARDS-1)] = (-1U)};
unsigned int dst_type_flags[DST_MAX_CARDS] = { [0 ... (DST_MAX_CARDS-1)] = (-1U)};
MODULE_PARM(dst_type, "1-" __stringify(DST_MAX_CARDS) "i");
MODULE_PARM_DESC(dst_type,
		"Type of DST card, 0 Satellite, 1 terrestial TV, 2 Cable, default driver determined");
MODULE_PARM(dst_type_flags, "1-" __stringify(DST_MAX_CARDS) "i");
MODULE_PARM_DESC(dst_type_flags,
		"Type flags of DST card, bitfield 1=10 byte tuner, 2=TS is 204, 4=symdiv");

#define dprintk	if (dst_debug) printk

#define DST_TYPE_IS_SAT		0
#define DST_TYPE_IS_TERR	1
#define DST_TYPE_IS_CABLE	2

#define DST_TYPE_HAS_NEWTUNE	1
#define DST_TYPE_HAS_TS204	2
#define DST_TYPE_HAS_SYMDIV	4

#define HAS_LOCK	1
#define ATTEMPT_TUNE	2
#define HAS_POWER	4

struct dst_data {
	u8	tx_tuna[10];
	u8	rx_tuna[10];
	u8	rxbuffer[10];
	u8	diseq_flags;
	u8	dst_type;
	u32	type_flags;
	u32 frequency;     /* intermediate frequency in kHz for QPSK */
        fe_spectral_inversion_t inversion;
        u32   symbol_rate;  /* symbol rate in Symbols per second */
	fe_code_rate_t  fec;
	fe_sec_voltage_t voltage;
	fe_sec_tone_mode_t tone;
	u32 decode_freq;
	u8  decode_lock;
	u16 decode_strength;
	u16 decode_snr;
	unsigned long cur_jiff;
	u8  k22;
	fe_bandwidth_t bandwidth;
	struct bt878 *bt;
	struct dvb_i2c_bus *i2c;
} ;

static struct dvb_frontend_info dst_info_sat = {
	.name 			= "DST SAT",
	.type 			= FE_QPSK,
	.frequency_min 		= 950000,
	.frequency_max 		= 2150000,
	.frequency_stepsize 	= 1000,           /* kHz for QPSK frontends */
	.frequency_tolerance 	= 29500,
	.symbol_rate_min	= 1000000,
	.symbol_rate_max	= 45000000,
/*     . symbol_rate_tolerance	= 	???,*/
	.notifier_delay		= 50,                /* 1/20 s */
	.caps = FE_CAN_FEC_AUTO |
		FE_CAN_QPSK
};

static struct dvb_frontend_info dst_info_cable = {
	.name 			= "DST CABLE",
	.type 			= FE_QAM,
        .frequency_stepsize 	= 62500,
	.frequency_min 		= 51000000,
	.frequency_max 		= 858000000,
	.symbol_rate_min	= 1000000,
	.symbol_rate_max	= 45000000,
/*     . symbol_rate_tolerance	= 	???,*/
	.notifier_delay		= 50,                /* 1/20 s */
	.caps = FE_CAN_FEC_AUTO |
		FE_CAN_QAM_AUTO
};

static struct dvb_frontend_info dst_info_tv = {
	.name 			= "DST TERR",
	.type 			= FE_OFDM,
	.frequency_min 		= 137000000,
	.frequency_max 		= 858000000,
	.frequency_stepsize 	= 166667,
	.caps = FE_CAN_FEC_AUTO |
	    FE_CAN_QAM_AUTO |
	    FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO
};

static void dst_packsize(struct dst_data *dst, int psize)
{
	union dst_gpio_packet bits;

	bits.psize = psize;
	bt878_device_control(dst->bt, DST_IG_TS, &bits);
}

static int dst_gpio_outb(struct dst_data *dst, u32 mask, u32 enbb, u32 outhigh)
{
	union dst_gpio_packet enb;
	union dst_gpio_packet bits;
	int err;

	enb.enb.mask = mask;
	enb.enb.enable = enbb;
        if ((err = bt878_device_control(dst->bt, DST_IG_ENABLE, &enb)) < 0) {
		dprintk ("%s: dst_gpio_enb error (err == %i, mask == 0x%02x, enb == 0x%02x)\n", __FUNCTION__, err, mask, enbb);
		return -EREMOTEIO;
	}

	/* because complete disabling means no output, no need to do
	 * output packet */
	if (enbb == 0)
		return 0;

	bits.outp.mask = enbb;
	bits.outp.highvals = outhigh;

        if ((err = bt878_device_control(dst->bt, DST_IG_WRITE, &bits)) < 0) {
		dprintk ("%s: dst_gpio_outb error (err == %i, enbb == 0x%02x, outhigh == 0x%02x)\n", __FUNCTION__, err, enbb, outhigh);
		return -EREMOTEIO;
	}
        return 0;
}

static int dst_gpio_inb(struct dst_data *dst, u8 *result)
{
	union dst_gpio_packet rd_packet;
	int err;

	*result = 0;

        if ((err = bt878_device_control(dst->bt, DST_IG_READ, &rd_packet)) < 0) {
		dprintk ("%s: dst_gpio_inb error (err == %i)\n", __FUNCTION__, err);
		return -EREMOTEIO;
	}
	*result = (u8)rd_packet.rd.value;
        return 0;
}

#define DST_I2C_ENABLE	1
#define DST_8820  	2

static int
dst_reset8820(struct dst_data *dst)
{
int retval;
	/* pull 8820 gpio pin low, wait, high, wait, then low */
	// dprintk ("%s: reset 8820\n", __FUNCTION__);
	retval = dst_gpio_outb(dst, DST_8820, DST_8820, 0);
	if (retval < 0)
		return retval;
	dvb_delay(10);
	retval = dst_gpio_outb(dst, DST_8820, DST_8820, DST_8820);
	if (retval < 0)
		return retval;
	/* wait for more feedback on what works here *
	dvb_delay(10);
	retval = dst_gpio_outb(dst, DST_8820, DST_8820, 0);
	if (retval < 0)
		return retval;
	*/
	return 0;
}

static int
dst_i2c_enable(struct dst_data *dst)
{
int retval;
	/* pull I2C enable gpio pin low, wait */
	// dprintk ("%s: i2c enable\n", __FUNCTION__);
	retval = dst_gpio_outb(dst, ~0, DST_I2C_ENABLE, 0);
	if (retval < 0)
		return retval;
	// dprintk ("%s: i2c enable delay\n", __FUNCTION__);
	dvb_delay(33);
	return 0;
}

static int
dst_i2c_disable(struct dst_data *dst)
{
int retval;
	/* release I2C enable gpio pin, wait */
	// dprintk ("%s: i2c disable\n", __FUNCTION__);
	retval = dst_gpio_outb(dst, ~0, 0, 0);
	if (retval < 0)
		return retval;
	// dprintk ("%s: i2c disable delay\n", __FUNCTION__);
	dvb_delay(33);
	return 0;
}

static int
dst_wait_dst_ready(struct dst_data *dst)
{
u8 reply;
int retval;
int i;
	for (i = 0; i < 200; i++) {
		retval = dst_gpio_inb(dst, &reply);
		if (retval < 0)
			return retval;
		if ((reply & DST_I2C_ENABLE) == 0) {
			dprintk ("%s: dst wait ready after %d\n", __FUNCTION__, i);
			return 1;
		}
		dvb_delay(5);
	}
	dprintk ("%s: dst wait NOT ready after %d\n", __FUNCTION__, i);
	return 0;
}

#define DST_I2C_ADDR 0x55

static int write_dst (struct dst_data *dst, u8 *data, u8 len)
{
	struct i2c_msg msg = {
		.addr = DST_I2C_ADDR, .flags = 0, .buf = data, .len = len };
	int err;
	int cnt;

	if (dst_debug && dst_verbose) {
		u8 i;
		dprintk("%s writing",__FUNCTION__);
		for (i = 0 ; i < len ; i++) {
			dprintk(" 0x%02x", data[i]);
		}
		dprintk("\n");
	}
	dvb_delay(30);
	for (cnt = 0; cnt < 4; cnt++) {
		if ((err = dst->i2c->xfer (dst->i2c, &msg, 1)) < 0) {
			dprintk ("%s: write_dst error (err == %i, len == 0x%02x, b0 == 0x%02x)\n", __FUNCTION__, err, len, data[0]);
			dst_i2c_disable(dst);
			dvb_delay(500);
			dst_i2c_enable(dst);
			dvb_delay(500);
			continue;
		} else
			break;
	}
	if (cnt >= 4)
		return -EREMOTEIO;
        return 0;
}

static int read_dst (struct dst_data *dst, u8 *ret, u8 len)
{
	struct i2c_msg msg = 
		{ .addr = DST_I2C_ADDR, .flags = I2C_M_RD, .buf = ret, .len = len };
	int err;
	int cnt;

	for (cnt = 0; cnt < 4; cnt++) {
		if ((err = dst->i2c->xfer (dst->i2c, &msg, 1)) < 0) {
			dprintk ("%s: read_dst error (err == %i, len == 0x%02x, b0 == 0x%02x)\n", __FUNCTION__, err, len, ret[0]);
			dst_i2c_disable(dst);
			dst_i2c_enable(dst);
			continue;
		} else
			break;
	}
	if (cnt >= 4)
		return -EREMOTEIO;
	dprintk("%s reply is 0x%x\n", __FUNCTION__, ret[0]);
	if (dst_debug && dst_verbose) {
		for (err = 1; err < len; err++)
			dprintk(" 0x%x", ret[err]);
		if (err > 1)
			dprintk("\n");
	}
	return 0;
}

static int dst_set_freq(struct dst_data *dst, u32 freq)
{
	u8 *val;

	dst->frequency = freq;

	// dprintk("%s: set frequency %u\n", __FUNCTION__, freq);
	if (dst->dst_type == DST_TYPE_IS_SAT) {
		freq = freq / 1000;
		if (freq < 950 || freq > 2150)
			return -EINVAL;
		val = &dst->tx_tuna[0];
		val[2] = (freq >> 8) & 0x7f;
		val[3] = (u8)freq;
		val[4] = 1;
		val[8] &= ~4;
		if (freq < 1531)
			val[8] |= 4;
	} else if (dst->dst_type == DST_TYPE_IS_TERR) {
		freq = freq / 1000;
		if (freq < 137000 || freq > 858000)
			return -EINVAL;
		val = &dst->tx_tuna[0];
		val[2] = (freq >> 16) & 0xff;
		val[3] = (freq >> 8) & 0xff;
		val[4] = (u8)freq;
		val[5] = 0;
		switch (dst->bandwidth) {
		case BANDWIDTH_6_MHZ:
			val[6] = 6;
			break;

		case BANDWIDTH_7_MHZ:
		case BANDWIDTH_AUTO:
			val[6] = 7;
			break;

		case BANDWIDTH_8_MHZ:
			val[6] = 8;
			break;
		}

		val[7] = 0;
		val[8] = 0;
	} else if (dst->dst_type == DST_TYPE_IS_CABLE) {
		/* guess till will get one */
		freq = freq / 1000;
		val = &dst->tx_tuna[0];
		val[2] = (freq >> 16) & 0xff;
		val[3] = (freq >> 8) & 0xff;
		val[4] = (u8)freq;
	} else
		return -EINVAL;
	return 0;
}

static int dst_set_bandwidth(struct dst_data *dst, fe_bandwidth_t bandwidth)
{
	u8 *val;

	dst->bandwidth = bandwidth;

	if (dst->dst_type != DST_TYPE_IS_TERR)
		return 0;

	val = &dst->tx_tuna[0];
        switch (bandwidth) {
	case BANDWIDTH_6_MHZ:
		val[6] = 6;
		break;

	case BANDWIDTH_7_MHZ:
		val[6] = 7;
		break;

	case BANDWIDTH_8_MHZ:
		val[6] = 8;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int dst_set_inversion (struct dst_data *dst, fe_spectral_inversion_t inversion)
{
	u8 *val;

	dst->inversion = inversion;

	val = &dst->tx_tuna[0];

	val[8] &= ~0x80;

	switch (inversion) {
	case INVERSION_OFF:
		break;
	case INVERSION_ON:
		val[8] |= 0x80;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}


static int dst_set_fec (struct dst_data *dst, fe_code_rate_t fec)
{
	dst->fec = fec;
	return 0;
}

static fe_code_rate_t dst_get_fec (struct dst_data *dst)
{
	return dst->fec;
}

static int dst_set_symbolrate (struct dst_data *dst, u32 srate)
{
	u8 *val;
	u32 symcalc;
	u64 sval;

	dst->symbol_rate = srate;

	if (dst->dst_type == DST_TYPE_IS_TERR) {
		return 0;
	}

	// dprintk("%s: set srate %u\n", __FUNCTION__, srate);
	srate /= 1000;
	val = &dst->tx_tuna[0];

	if (dst->type_flags & DST_TYPE_HAS_SYMDIV) {
		sval = srate;
		sval <<= 20;
		do_div(sval, 88000);
	        symcalc = (u32)sval;
		// dprintk("%s: set symcalc %u\n", __FUNCTION__, symcalc);
		val[5] = (u8)(symcalc >> 12);
		val[6] = (u8)(symcalc >> 4);
		val[7] = (u8)(symcalc << 4);
	} else {
		val[5] = (u8)(srate >> 16) & 0x7f;
		val[6] = (u8)(srate >> 8);
		val[7] = (u8)srate;
	}
	val[8] &= ~0x20;
	if (srate > 8000)
		val[8] |= 0x20;
	return 0;
}


static u8 dst_check_sum(u8 *buf, u32 len)
{
	u32 i;
	u8  val = 0;
	if (!len)
		return 0;
	for (i = 0; i < len; i++) {
		val += buf[i];
	}
	return ((~val) + 1);
}

typedef struct dst_types {
	char	*mstr;
	int	offs;
	u8	dst_type;
	u32	type_flags;
} DST_TYPES;

struct dst_types dst_tlist[] = {
	{ "DST-020", 0,  DST_TYPE_IS_SAT,    DST_TYPE_HAS_SYMDIV },
	{ "DST-030", 0,  DST_TYPE_IS_SAT,    DST_TYPE_HAS_TS204|DST_TYPE_HAS_NEWTUNE },
	{ "DST-03T", 0,  DST_TYPE_IS_SAT,    DST_TYPE_HAS_SYMDIV|DST_TYPE_HAS_TS204},
	{ "DST-MOT", 0,  DST_TYPE_IS_SAT,    DST_TYPE_HAS_SYMDIV },
	{ "DST-CI",  1,  DST_TYPE_IS_SAT,    DST_TYPE_HAS_TS204|DST_TYPE_HAS_NEWTUNE },
	{ "DSTMCI",  1,  DST_TYPE_IS_SAT,    DST_TYPE_HAS_NEWTUNE },
	{ "DSTFCI",  1,  DST_TYPE_IS_SAT,    DST_TYPE_HAS_NEWTUNE },
	{ "DCTNEW",  1,  DST_TYPE_IS_CABLE,  DST_TYPE_HAS_NEWTUNE },
	{ "DCT_CI",  1,  DST_TYPE_IS_CABLE,  DST_TYPE_HAS_NEWTUNE|DST_TYPE_HAS_TS204 },
	{ "DTTDIG" , 1,  DST_TYPE_IS_TERR,   0} };
/* DCTNEW and DCT-CI are guesses */

static void dst_type_flags_print(u32 type_flags)
{
	printk("DST type flags :");
	if (type_flags & DST_TYPE_HAS_NEWTUNE)
		printk(" 0x%x newtuner", DST_TYPE_HAS_NEWTUNE);
	if (type_flags & DST_TYPE_HAS_TS204)
		printk(" 0x%x ts204", DST_TYPE_HAS_TS204);
	if (type_flags & DST_TYPE_HAS_SYMDIV)
		printk(" 0x%x symdiv", DST_TYPE_HAS_SYMDIV);
	printk("\n");
}

static int dst_type_print(u8 type)
{
	char *otype;
	switch (type) {
		case DST_TYPE_IS_SAT:
			otype = "satellite";
			break;
		case DST_TYPE_IS_TERR:
			otype = "terrestial TV";
			break;
		case DST_TYPE_IS_CABLE:
			otype = "terrestial TV";
			break;
		default:
			printk("%s: invalid dst type %d\n",
				__FUNCTION__, type);
			return -EINVAL;
	}
	printk("DST type : %s\n", otype);
	return 0;
}

static int dst_check_ci (struct dst_data *dst)
{
	u8 txbuf[8];
	u8 rxbuf[8];
	int retval;
	int i;
	struct dst_types *dsp;
	u8 use_dst_type;
	u32 use_type_flags;

	memset(txbuf, 0, sizeof(txbuf));
	txbuf[1] = 6;
	txbuf[7] = dst_check_sum (txbuf, 7);
 
	dst_i2c_enable(dst);
	dst_reset8820(dst);
	retval = write_dst (dst, txbuf, 8);
	if (retval < 0) {
		dst_i2c_disable(dst);
		dprintk("%s: write not successful, maybe no card?\n", __FUNCTION__);
		return retval;
	}
	dvb_delay(3);
	retval = read_dst (dst, rxbuf, 1);
	dst_i2c_disable(dst);
	if (retval < 0) {
		dprintk("%s: read not successful, maybe no card?\n", __FUNCTION__);
		return retval;
	}
	if (rxbuf[0] != 0xff) {
		dprintk("%s: write reply not 0xff, not ci (%02x)\n", __FUNCTION__, rxbuf[0]);
		return retval;
	}
	if (!dst_wait_dst_ready(dst))
		return 0;
	// dst_i2c_enable(i2c); Dimitri
	retval = read_dst (dst, rxbuf, 8);
	dst_i2c_disable(dst);
	if (retval < 0) {
		dprintk("%s: read not successful\n", __FUNCTION__);
		return retval;
	}
	if (rxbuf[7] != dst_check_sum (rxbuf, 7)) {
		dprintk("%s: checksum failure\n", __FUNCTION__);
		return retval;
	}
	rxbuf[7] = '\0';
	for (i = 0, dsp = &dst_tlist[0]; i < sizeof(dst_tlist) / sizeof(dst_tlist[0]); i++, dsp++) {
		if (!strncmp(&rxbuf[dsp->offs],
				dsp->mstr,
				strlen(dsp->mstr))) {
			use_type_flags = dsp->type_flags;
			use_dst_type = dsp->dst_type;
			printk("%s: recognize %s\n", __FUNCTION__, dsp->mstr);
			break;
		}
	}
	if (i >= sizeof(dst_tlist) / sizeof(dst_tlist[0])) {
		printk("%s: unable to recognize %s or %s\n", __FUNCTION__, &rxbuf[0], &rxbuf[1]);
		printk("%s please email linux-dvb@linuxtv.org with this type in\n", __FUNCTION__);
		use_dst_type = DST_TYPE_IS_SAT;
		use_type_flags = DST_TYPE_HAS_SYMDIV;
	}
	switch (dst_type[dst_cur_no]) {
		case (-1U):
			/* not used */
			break;
		case DST_TYPE_IS_SAT:
		case DST_TYPE_IS_TERR:
		case DST_TYPE_IS_CABLE:
			use_dst_type = (u8)(dst_type[dst_cur_no]);
			break;
		default:
			printk("%s: invalid user override dst type %d, not used\n",
				__FUNCTION__, dst_type[dst_cur_no]);
			break;
	}
	dst_type_print(use_dst_type);
	if (dst_type_flags[dst_cur_no] != (-1U)) {
		printk("%s: user override dst type flags 0x%x\n",
				__FUNCTION__, dst_type_flags[dst_cur_no]);
		use_type_flags = dst_type_flags[dst_cur_no];
	}
	dst->type_flags = use_type_flags;
	dst->dst_type= use_dst_type;
	dst_type_flags_print(dst->type_flags);

	if (dst->type_flags & DST_TYPE_HAS_TS204) {
		dst_packsize(dst, 204);
	}
	return 0;
}

static int dst_command (struct dst_data *dst, u8 *data, u8 len)
{
	int retval;
	u8 reply;

	dst_i2c_enable(dst);
	dst_reset8820(dst);
	retval = write_dst (dst, data, len);
	if (retval < 0) {
		dst_i2c_disable(dst);
		dprintk("%s: write not successful\n", __FUNCTION__);
		return retval;
	}
	dvb_delay(33);
	retval = read_dst (dst, &reply, 1);
	dst_i2c_disable(dst);
	if (retval < 0) {
		dprintk("%s: read verify  not successful\n", __FUNCTION__);
		return retval;
	}
	if (reply != 0xff) {
		dprintk("%s: write reply not 0xff 0x%02x \n", __FUNCTION__, reply);
		return 0;
	}
	if (len >= 2 && data[0] == 0 && (data[1] == 1 || data[1] == 3))
		return 0;
	if (!dst_wait_dst_ready(dst))
		return 0;
	// dst_i2c_enable(i2c); Per dimitri
	retval = read_dst (dst, dst->rxbuffer, 8);
	dst_i2c_disable(dst);
	if (retval < 0) {
		dprintk("%s: read not successful\n", __FUNCTION__);
		return 0;
	}
	if (dst->rxbuffer[7] != dst_check_sum (dst->rxbuffer, 7)) {
		dprintk("%s: checksum failure\n", __FUNCTION__);
		return 0;
	}
	return 0;
}

static int dst_get_signal(struct dst_data *dst)
{
	int retval;
	u8 get_signal[] = {0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfb};

	if ((dst->diseq_flags & ATTEMPT_TUNE) == 0) {
		dst->decode_lock = dst->decode_strength = dst->decode_snr = 0;
		return 0;
	}
	if (0 == (dst->diseq_flags & HAS_LOCK)) {
		dst->decode_lock = dst->decode_strength = dst->decode_snr = 0;
		return 0;
	}
	if (time_after_eq(jiffies, dst->cur_jiff + (HZ/5))) {
		retval =  dst_command(dst, get_signal, 8);
		if (retval < 0)
			return retval;
		if (dst->dst_type == DST_TYPE_IS_SAT) {
			dst->decode_lock = ((dst->rxbuffer[6] & 0x10) == 0) ?
					1 : 0;
			dst->decode_strength = dst->rxbuffer[5] << 8;
			dst->decode_snr = dst->rxbuffer[2] << 8 |
				dst->rxbuffer[3];
		} else if ((dst->dst_type == DST_TYPE_IS_TERR) ||
				(dst->dst_type == DST_TYPE_IS_CABLE)) {
			dst->decode_lock = (dst->rxbuffer[1]) ?
					1 : 0;
			dst->decode_strength = dst->rxbuffer[4] << 8;
			dst->decode_snr = dst->rxbuffer[3] << 8;
		}
		dst->cur_jiff = jiffies;
	}
	return 0;
}

/*
 * line22k0    0x00, 0x09, 0x00, 0xff, 0x01, 0x00, 0x00, 0x00
 * line22k1    0x00, 0x09, 0x01, 0xff, 0x01, 0x00, 0x00, 0x00
 * line22k2    0x00, 0x09, 0x02, 0xff, 0x01, 0x00, 0x00, 0x00
 * tone        0x00, 0x09, 0xff, 0x00, 0x01, 0x00, 0x00, 0x00
 * data        0x00, 0x09, 0xff, 0x01, 0x01, 0x00, 0x00, 0x00
 * power_off   0x00, 0x09, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00
 * power_on    0x00, 0x09, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00
 * Diseqc 1    0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xf0, 0xec
 * Diseqc 2    0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xf4, 0xe8 
 * Diseqc 3    0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xf8, 0xe4 
 * Diseqc 4    0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xfc, 0xe0 
 */

static int dst_set_diseqc (struct dst_data *dst, u8 *cmd, u8 len)
{
	u8 paket[8] =  {0x00, 0x08, 0x04, 0xe0, 0x10, 0x38, 0xf0, 0xec };

	if (dst->dst_type == DST_TYPE_IS_TERR)
		return 0;

	if (len == 0 || len > 4)
		return -EINVAL;
	memcpy(&paket[3], cmd, len);
	paket[7] = dst_check_sum (&paket[0], 7);
	dst_command(dst, paket, 8);
	return 0;
}

static int dst_tone_power_cmd (struct dst_data *dst)
{
	u8 paket[8] =  {0x00, 0x09, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00};

	if (dst->dst_type == DST_TYPE_IS_TERR)
		return 0;

	if (dst->voltage == SEC_VOLTAGE_OFF) 
		paket[4] = 0;
	else
		paket[4] = 1;
	if (dst->tone == SEC_TONE_ON)
		paket[2] = dst->k22;
	else
		paket[2] = 0;
	paket[7] = dst_check_sum (&paket[0], 7);
	dst_command(dst, paket, 8);
	return 0;
}

static int dst_set_voltage (struct dst_data *dst, fe_sec_voltage_t voltage)
{
	u8 *val;
	int need_cmd;

	dst->voltage = voltage;

	if (dst->dst_type == DST_TYPE_IS_TERR)
		return 0;

	need_cmd = 0;
	val = &dst->tx_tuna[0];
	val[8] &= ~0x40;
	switch (voltage) {
	case SEC_VOLTAGE_13:
		if ((dst->diseq_flags & HAS_POWER) == 0)
			need_cmd = 1;
		dst->diseq_flags |= HAS_POWER;
		break;
	case SEC_VOLTAGE_18:
		if ((dst->diseq_flags & HAS_POWER) == 0)
			need_cmd = 1;
		dst->diseq_flags |= HAS_POWER;
		val[8] |= 0x40;
		break;
	case SEC_VOLTAGE_OFF:
		need_cmd = 1;
		dst->diseq_flags &= ~(HAS_POWER|HAS_LOCK|ATTEMPT_TUNE);
		break;
	default:
		return -EINVAL;
	}
	if (need_cmd) {
		dst_tone_power_cmd(dst);
	}
	return 0;
}


static int dst_set_tone (struct dst_data *dst, fe_sec_tone_mode_t tone)
{
	u8 *val;

	dst->tone = tone;

	if (dst->dst_type == DST_TYPE_IS_TERR)
		return 0;

	val = &dst->tx_tuna[0];

	val[8] &= ~0x1;

	switch (tone) {
	case SEC_TONE_OFF:
		break;
	case SEC_TONE_ON:
		val[8] |= 1;
		break;
	default:
		return -EINVAL;
	}
	dst_tone_power_cmd(dst);
	return 0;
}

static int dst_get_tuna (struct dst_data *dst)
{
int retval;
	if ((dst->diseq_flags & ATTEMPT_TUNE) == 0)
		return 0;
	dst->diseq_flags &= ~(HAS_LOCK);
	if (!dst_wait_dst_ready(dst))
		return 0;
	if (dst->type_flags & DST_TYPE_HAS_NEWTUNE) {
		/* how to get variable length reply ???? */
		retval = read_dst (dst, dst->rx_tuna, 10);
	} else {
		retval = read_dst (dst, &dst->rx_tuna[2], 8);
	}
	if (retval < 0) {
		dprintk("%s: read not successful\n", __FUNCTION__);
		return 0;
	}
	if (dst->type_flags & DST_TYPE_HAS_NEWTUNE) {
		if (dst->rx_tuna[9] != dst_check_sum (&dst->rx_tuna[0], 9)) {
			dprintk("%s: checksum failure?\n", __FUNCTION__);
			return 0;
		}
	} else {
		if (dst->rx_tuna[9] != dst_check_sum (&dst->rx_tuna[2], 7)) {
			dprintk("%s: checksum failure?\n", __FUNCTION__);
			return 0;
		}
	}
	if (dst->rx_tuna[2] == 0 && dst->rx_tuna[3] == 0)
		return 0;
	dst->decode_freq = ((dst->rx_tuna[2] & 0x7f) << 8) +  dst->rx_tuna[3];

	dst->decode_lock = 1;
	/*
	dst->decode_n1 = (dst->rx_tuna[4] << 8) +  
			(dst->rx_tuna[5]);

	dst->decode_n2 = (dst->rx_tuna[8] << 8) +  
			(dst->rx_tuna[7]);
	*/
	dst->diseq_flags |= HAS_LOCK;
	/* dst->cur_jiff = jiffies; */
	return 1;
}

static int dst_write_tuna (struct dst_data *dst)
{
	int retval;
	u8 reply;

	dprintk("%s: type_flags 0x%x \n", __FUNCTION__, dst->type_flags);
	dst->decode_freq = 0;
	dst->decode_lock = dst->decode_strength = dst->decode_snr = 0;
	if (dst->dst_type == DST_TYPE_IS_SAT) {
		if (!(dst->diseq_flags & HAS_POWER))
			dst_set_voltage (dst, SEC_VOLTAGE_13);
	}
	dst->diseq_flags &= ~(HAS_LOCK|ATTEMPT_TUNE);
	dst_i2c_enable(dst);
	if (dst->type_flags & DST_TYPE_HAS_NEWTUNE) {
		dst_reset8820(dst);
		dst->tx_tuna[9] = dst_check_sum (&dst->tx_tuna[0], 9);
		retval = write_dst (dst, &dst->tx_tuna[0], 10);
	} else {
		dst->tx_tuna[9] = dst_check_sum (&dst->tx_tuna[2], 7);
		retval = write_dst (dst, &dst->tx_tuna[2], 8);
	}
	if (retval < 0) {
		dst_i2c_disable(dst);
		dprintk("%s: write not successful\n", __FUNCTION__);
		return retval;
	}
	dvb_delay(3);
	retval = read_dst (dst, &reply, 1);
	dst_i2c_disable(dst);
	if (retval < 0) {
		dprintk("%s: read verify  not successful\n", __FUNCTION__);
		return retval;
	}
	if (reply != 0xff) {
		dprintk("%s: write reply not 0xff 0x%02x \n", __FUNCTION__, reply);
		return 0;
	}
	dst->diseq_flags |= ATTEMPT_TUNE;
	return dst_get_tuna(dst);
}

static void dst_init (struct dst_data *dst)
{
static u8 ini_satci_tuna[] = {  9, 0, 3, 0xb6, 1, 0,    0x73, 0x21, 0, 0 };
static u8 ini_satfta_tuna[] = { 0, 0, 3, 0xb6, 1, 0x55, 0xbd, 0x50, 0, 0 };
static u8 ini_tvfta_tuna[] = { 0, 0,  3, 0xb6, 1, 7,    0x0,   0x0, 0, 0 };
static u8 ini_tvci_tuna[] = { 9, 0,  3, 0xb6, 1, 7,    0x0,   0x0, 0, 0 };
static u8 ini_cabfta_tuna[] = { 0, 0,  3, 0xb6, 1, 7,    0x0,   0x0, 0, 0 };
static u8 ini_cabci_tuna[] = { 9, 0,  3, 0xb6, 1, 7,    0x0,   0x0, 0, 0 };
	dst->inversion = INVERSION_ON;
	dst->voltage = SEC_VOLTAGE_13;
	dst->tone = SEC_TONE_OFF;
	dst->symbol_rate = 29473000;
	dst->fec = FEC_AUTO;
	dst->diseq_flags = 0;
	dst->k22 = 0x02;
	dst->bandwidth = BANDWIDTH_7_MHZ;
	dst->cur_jiff = jiffies;
	if (dst->dst_type == DST_TYPE_IS_SAT) {
		dst->frequency = 950000;
		memcpy(dst->tx_tuna, ((dst->type_flags &  DST_TYPE_HAS_NEWTUNE )? 
					ini_satci_tuna : ini_satfta_tuna),
				sizeof(ini_satfta_tuna));
	} else if (dst->dst_type == DST_TYPE_IS_TERR) {
		dst->frequency = 137000000;
		memcpy(dst->tx_tuna, ((dst->type_flags &  DST_TYPE_HAS_NEWTUNE )? 
					ini_tvci_tuna : ini_tvfta_tuna),
				sizeof(ini_tvfta_tuna));
	} else if (dst->dst_type == DST_TYPE_IS_CABLE) {
		dst->frequency = 51000000;
		memcpy(dst->tx_tuna, ((dst->type_flags &  DST_TYPE_HAS_NEWTUNE )? 
					ini_cabci_tuna : ini_cabfta_tuna),
				sizeof(ini_cabfta_tuna));
	}
}

struct lkup {
	unsigned int cmd;
	char *desc;
} looker[] = {
	{FE_GET_INFO,                "FE_GET_INFO:"},
	{FE_READ_STATUS,             "FE_READ_STATUS:" },
	{FE_READ_BER,                "FE_READ_BER:" },
	{FE_READ_SIGNAL_STRENGTH,    "FE_READ_SIGNAL_STRENGTH:" },
	{FE_READ_SNR,                "FE_READ_SNR:" },
	{FE_READ_UNCORRECTED_BLOCKS, "FE_READ_UNCORRECTED_BLOCKS:" },
	{FE_SET_FRONTEND,            "FE_SET_FRONTEND:" },
	{FE_GET_FRONTEND,            "FE_GET_FRONTEND:" },
	{FE_SLEEP,                   "FE_SLEEP:" },
	{FE_INIT,                    "FE_INIT:" },
	{FE_SET_TONE,                "FE_SET_TONE:" },
	{FE_SET_VOLTAGE,             "FE_SET_VOLTAGE:" },
	};

static int dst_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct dst_data *dst = fe->data;
	int retval;
	/*
	char  *cc;
                
	cc = "FE_UNSUPP:";
	for(retval = 0; retval < sizeof(looker) / sizeof(looker[0]); retval++) {
		if (looker[retval].cmd == cmd) {
			cc = looker[retval].desc;
			break;
		}
	}
	dprintk("%s cmd %s (0x%x)\n",__FUNCTION__, cc, cmd);
	*/
	// printk("%s: dst %8.8x bt %8.8x i2c %8.8x\n", __FUNCTION__, dst, dst->bt, dst->i2c);
	/* should be set by attach, but just in case */
	dst->i2c = fe->i2c;
        switch (cmd) {
        case FE_GET_INFO: 
	{
	     struct dvb_frontend_info *info;
		info = &dst_info_sat;
		if (dst->dst_type == DST_TYPE_IS_TERR)
			info = &dst_info_tv;
		else if (dst->dst_type == DST_TYPE_IS_CABLE)
			info = &dst_info_cable;
		memcpy (arg, info, sizeof(struct dvb_frontend_info));
		break;
	}
        case FE_READ_STATUS:
	{
		fe_status_t *status = arg;

		*status = 0;
		if (dst->diseq_flags & HAS_LOCK) {
			dst_get_signal(dst);
			if (dst->decode_lock)
				*status |= FE_HAS_LOCK 
					| FE_HAS_SIGNAL 
					| FE_HAS_CARRIER
					| FE_HAS_SYNC
					| FE_HAS_VITERBI;
		}
		break;
	}

        case FE_READ_BER:
	{
		/* guess */
		// *(u32*) arg = dst->decode_n1;
		*(u32*) arg = 0;
		return -EOPNOTSUPP; 
	}

        case FE_READ_SIGNAL_STRENGTH:
	{
		dst_get_signal(dst);
		*((u16*) arg) = dst->decode_strength;
		break;
	}

        case FE_READ_SNR:
	{
		dst_get_signal(dst);
		*((u16*) arg) = dst->decode_snr;
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS: 
	{
		*((u32*) arg) = 0;    /* the stv0299 can't measure BER and */
		return -EOPNOTSUPP;   /* errors at the same time.... */
	}

        case FE_SET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;

		dst_set_freq (dst, p->frequency);
		dst_set_inversion (dst, p->inversion);
		if (dst->dst_type == DST_TYPE_IS_SAT) {
			dst_set_fec (dst, p->u.qpsk.fec_inner);
			dst_set_symbolrate (dst, p->u.qpsk.symbol_rate);
		} else if (dst->dst_type == DST_TYPE_IS_TERR) {
			dst_set_bandwidth(dst, p->u.ofdm.bandwidth);
		} else if (dst->dst_type == DST_TYPE_IS_CABLE) {
			dst_set_fec (dst, p->u.qam.fec_inner);
			dst_set_symbolrate (dst, p->u.qam.symbol_rate);
		}
		dst_write_tuna (dst);

                break;
        }

	case FE_GET_FRONTEND:
	{
		struct dvb_frontend_parameters *p = arg;


		p->frequency = dst->decode_freq;
		p->inversion = dst->inversion;
		if (dst->dst_type == DST_TYPE_IS_SAT) {
			p->u.qpsk.symbol_rate = dst->symbol_rate;
			p->u.qpsk.fec_inner = dst_get_fec (dst);
		} else if (dst->dst_type == DST_TYPE_IS_TERR) {
			p->u.ofdm.bandwidth = dst->bandwidth;
		} else if (dst->dst_type == DST_TYPE_IS_CABLE) {
			p->u.qam.symbol_rate = dst->symbol_rate;
			p->u.qam.fec_inner = dst_get_fec (dst);
			p->u.qam.modulation = QAM_AUTO;
		}
		break;
	}

        case FE_SLEEP:
		return 0;

        case FE_INIT:
		dst_init(dst);
		break;

	case FE_DISEQC_SEND_MASTER_CMD:
	{
		struct dvb_diseqc_master_cmd *cmd = (struct dvb_diseqc_master_cmd *)arg;
		retval = dst_set_diseqc (dst, cmd->msg, cmd->msg_len);
		if (retval < 0)
			return retval;
		break;
	}
	case FE_SET_TONE:
		retval = dst_set_tone (dst, (fe_sec_tone_mode_t) arg);
		if (retval < 0)
			return retval;
		break;
	case FE_SET_VOLTAGE:
		retval = dst_set_voltage (dst, (fe_sec_voltage_t) arg);
		if (retval < 0)
			return retval;
		break;
	default:
		return -EOPNOTSUPP;
        };
        
        return 0;
} 


static int dst_attach (struct dvb_i2c_bus *i2c, void **data)
{
	struct dst_data *dst;
	struct bt878 *bt;
	struct dvb_frontend_info *info;

	dprintk("%s: check ci\n", __FUNCTION__);
	if (dst_cur_no >= DST_MAX_CARDS) {
		dprintk("%s: can't have more than %d cards\n", __FUNCTION__, DST_MAX_CARDS);
		return -ENODEV;
	}
	bt = bt878_find_by_dvb_adap(i2c->adapter);
	if (!bt)
		return -ENODEV;
	dst = kmalloc(sizeof(struct dst_data), GFP_KERNEL);
	if (dst == NULL) {
		printk(KERN_INFO "%s: Out of memory.\n", __FUNCTION__);
		return -ENOMEM;
	}
	memset(dst, 0, sizeof(*dst));
	*data = dst;
	dst->bt = bt;
	dst->i2c = i2c;
	if (dst_check_ci(dst) < 0) {
		kfree(dst);
		return -ENODEV;
	}

	dst_init (dst);
	dprintk("%s: register dst %8.8x bt %8.8x i2c %8.8x\n", __FUNCTION__, 
			(u32)dst, (u32)(dst->bt), (u32)(dst->i2c));

	info = &dst_info_sat;
	if (dst->dst_type == DST_TYPE_IS_TERR)
		info = &dst_info_tv;
	else if (dst->dst_type == DST_TYPE_IS_CABLE)
		info = &dst_info_cable;

	dvb_register_frontend (dst_ioctl, i2c, dst, info);
	dst_cur_no++;
	return 0;
}

static void dst_detach (struct dvb_i2c_bus *i2c, void *data)
{
	dvb_unregister_frontend (dst_ioctl, i2c);
	dprintk("%s: unregister dst %8.8x\n", __FUNCTION__, (u32)(data));
	if (data)
		kfree(data);
}

static int __init init_dst (void)
{
	return dvb_register_i2c_device (THIS_MODULE, dst_attach, dst_detach);
}

static void __exit exit_dst (void)
{
	dvb_unregister_i2c_device (dst_attach);
}


module_init(init_dst);
module_exit(exit_dst);

MODULE_DESCRIPTION("DST DVB-S Frontend");
MODULE_AUTHOR("Jamie Honan");
MODULE_LICENSE("GPL");

