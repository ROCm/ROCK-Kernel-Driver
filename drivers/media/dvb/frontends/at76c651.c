/*
 * at76c651.c
 * 
 * Atmel DVB-C Frontend Driver (at76c651/tua6010xs)
 *
 * Copyright (C) 2001 fnbrd <fnbrd@gmx.de>
 *             & 2002-2004 Andreas Oberritter <obi@linuxtv.org>
 *             & 2003 Wolfram Joost <dbox2@frokaschwei.de>
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * AT76C651
 * http://www.nalanda.nitc.ac.in/industry/datasheets/atmel/acrobat/doc1293.pdf
 * http://www.atmel.com/atmel/acrobat/doc1320.pdf
 *
 * TUA6010XS
 * http://www.infineon.com/cgi/ecrm.dll/ecrm/scripts/public_download.jsp?oid=19512
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>

#if defined(__powerpc__)
#include <asm/bitops.h>
#endif

#include "dvb_frontend.h"

#define FRONTEND_NAME "dvbfe_at76c651"

#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG FRONTEND_NAME ": " args); \
	} while (0)

static int debug;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");


static struct dvb_frontend_info at76c651_info = {
	.name = "Atmel AT76C651B with TUA6010XS",
	.type = FE_QAM,
	.frequency_min = 48250000,
	.frequency_max = 863250000,
	.frequency_stepsize = 62500,
	/*.frequency_tolerance = */	/* FIXME: 12% of SR */
	.symbol_rate_min = 0,		/* FIXME */
	.symbol_rate_max = 9360000,	/* FIXME */
	.symbol_rate_tolerance = 4000,
	.notifier_delay = 0,
	.caps = FE_CAN_INVERSION_AUTO |
	    FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
	    FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
	    FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO |
	    FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 | FE_CAN_QAM_128 |
	    FE_CAN_MUTE_TS | FE_CAN_QAM_256 | FE_CAN_RECOVER
};

struct at76c651_state {
	u8 revision;
	u8 qam;
	struct i2c_adapter *i2c;
	struct dvb_adapter *dvb;
};

#if ! defined(__powerpc__)
static __inline__ int __ilog2(unsigned long x)
{
	int i;

	if (x == 0)
		return -1;

	for (i = 0; x != 0; i++)
		x >>= 1;

	return i - 1;
}
#endif

static int at76c651_writereg(struct i2c_adapter *i2c, u8 reg, u8 data)
{
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg =
		{ .addr = 0x1a >> 1, .flags = 0, .buf = buf, .len = 2 };

	ret = i2c_transfer(i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: writereg error "
			"(reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			__FUNCTION__, reg, data, ret);

	msleep(10);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static u8 at76c651_readreg(struct i2c_adapter *i2c, u8 reg)
{
	int ret;
	u8 val;
	struct i2c_msg msg[] = {
		{ .addr = 0x1a >> 1, .flags = 0, .buf = &reg, .len = 1 },
		{ .addr = 0x1a >> 1, .flags = I2C_M_RD, .buf = &val, .len = 1 }
	};

	ret = i2c_transfer(i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return val;
}

static int at76c651_reset(struct i2c_adapter *i2c)
{
	return at76c651_writereg(i2c, 0x07, 0x01);
}

static void at76c651_disable_interrupts(struct i2c_adapter *i2c)
{
	at76c651_writereg(i2c, 0x0b, 0x00);
}

static int at76c651_set_auto_config(struct at76c651_state *state)
{
	struct i2c_adapter *i2c = state->i2c;

	/*
	 * Autoconfig
	 */

	at76c651_writereg(i2c, 0x06, 0x01);

	/*
	 * Performance optimizations, should be done after autoconfig
	 */

	at76c651_writereg(i2c, 0x10, 0x06);
	at76c651_writereg(i2c, 0x11, ((state->qam == 5) || (state->qam == 7)) ? 0x12 : 0x10);
	at76c651_writereg(i2c, 0x15, 0x28);
	at76c651_writereg(i2c, 0x20, 0x09);
	at76c651_writereg(i2c, 0x24, ((state->qam == 5) || (state->qam == 7)) ? 0xC0 : 0x90);
	at76c651_writereg(i2c, 0x30, 0x90);
	if (state->qam == 5)
		at76c651_writereg(i2c, 0x35, 0x2A);

	/*
	 * Initialize A/D-converter
	 */

	if (state->revision == 0x11) {
		at76c651_writereg(i2c, 0x2E, 0x38);
		at76c651_writereg(i2c, 0x2F, 0x13);
	}

	at76c651_disable_interrupts(i2c);

	/*
	 * Restart operation
	 */

	at76c651_reset(i2c);

	return 0;
}

static void at76c651_set_bbfreq(struct i2c_adapter *i2c)
{
	at76c651_writereg(i2c, 0x04, 0x3f);
	at76c651_writereg(i2c, 0x05, 0xee);
}

static int at76c651_pll_write(struct i2c_adapter *i2c, u8 *buf, size_t len)
{
	int ret;
	struct i2c_msg msg =
	    { .addr = 0xc2 >> 1, .flags = 0, .buf = buf, .len = len };

	at76c651_writereg(i2c, 0x0c, 0xc3);

	ret = i2c_transfer(i2c, &msg, 1);

	at76c651_writereg(i2c, 0x0c, 0xc2);

	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EREMOTEIO;

	return 0;
}

static int tua6010_setfreq(struct i2c_adapter *i2c, u32 freq)
{
	u32 div;
	u8 buf[4];
	u8 vu, p2, p1, p0;

	if ((freq < 50000000) || (freq > 900000000))
		return -EINVAL;

	div = (freq + 36125000) / 62500;

	if (freq > 400000000)
		vu = 1, p2 = 1, p1 = 0, p0 = 1;
	else if (freq > 140000000)
		vu = 0, p2 = 1, p1 = 1, p0 = 0;
	else
		vu = 0, p2 = 0, p1 = 1, p0 = 1;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = (div >> 0) & 0xff;
	buf[2] = 0x8e;
	buf[3] = (vu << 7) | (p2 << 2) | (p1 << 1) | p0;

	return at76c651_pll_write(i2c, buf, 4);
}

static int at76c651_set_symbol_rate(struct i2c_adapter *i2c, u32 symbol_rate)
{
	u8 exponent;
	u32 mantissa;

	if (symbol_rate > 9360000)
		return -EINVAL;

	/*
	 * FREF = 57800 kHz
	 * exponent = 10 + floor (log2(symbol_rate / FREF))
	 * mantissa = (symbol_rate / FREF) * (1 << (30 - exponent))
	 */

	exponent = __ilog2((symbol_rate << 4) / 903125);
	mantissa = ((symbol_rate / 3125) * (1 << (24 - exponent))) / 289;

	at76c651_writereg(i2c, 0x00, mantissa >> 13);
	at76c651_writereg(i2c, 0x01, mantissa >> 5);
	at76c651_writereg(i2c, 0x02, (mantissa << 3) | exponent);

	return 0;
}

static int at76c651_set_qam(struct at76c651_state *state, fe_modulation_t qam)
{
	switch (qam) {
	case QPSK:
		state->qam = 0x02;
		break;
	case QAM_16:
		state->qam = 0x04;
		break;
	case QAM_32:
		state->qam = 0x05;
		break;
	case QAM_64:
		state->qam = 0x06;
		break;
	case QAM_128:
		state->qam = 0x07;
		break;
	case QAM_256:
		state->qam = 0x08;
		break;
#if 0
	case QAM_512:
		state->qam = 0x09;
		break;
	case QAM_1024:
		state->qam = 0x0A;
		break;
#endif
	default:
		return -EINVAL;

	}

	return at76c651_writereg(state->i2c, 0x03, state->qam);
}

static int at76c651_set_inversion(struct i2c_adapter *i2c,
				  fe_spectral_inversion_t inversion)
{
	u8 feciqinv = at76c651_readreg(i2c, 0x60);

	switch (inversion) {
	case INVERSION_OFF:
		feciqinv |= 0x02;
		feciqinv &= 0xFE;
		break;

	case INVERSION_ON:
		feciqinv |= 0x03;
		break;

	case INVERSION_AUTO:
		feciqinv &= 0xFC;
		break;

	default:
		return -EINVAL;
	}

	return at76c651_writereg(i2c, 0x60, feciqinv);
}

static int at76c651_set_parameters(struct at76c651_state *state,
				   struct dvb_frontend_parameters *p)
{
	struct i2c_adapter *i2c = state->i2c;
	int ret;

	if ((ret = tua6010_setfreq(i2c, p->frequency)))
		return ret;

	if ((ret = at76c651_set_symbol_rate(i2c, p->u.qam.symbol_rate)))
		return ret;

	if ((ret = at76c651_set_inversion(i2c, p->inversion)))
		return ret;

	return at76c651_set_auto_config(state);
}

static int at76c651_set_defaults(struct at76c651_state *state)
{
	struct i2c_adapter *i2c = state->i2c;

	at76c651_set_symbol_rate(i2c, 6900000);
	at76c651_set_qam(state, QAM_64);
	at76c651_set_bbfreq(i2c);
	at76c651_set_auto_config(state);

	return 0;
}

static int at76c651_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct at76c651_state *state = fe->data;
	struct i2c_adapter *i2c = state->i2c;

	switch (cmd) {
	case FE_GET_INFO:
		memcpy(arg, &at76c651_info, sizeof(struct dvb_frontend_info));
		break;

	case FE_READ_STATUS:
	{
		fe_status_t *status = arg;
		u8 sync;

		/*
		 * Bits: FEC, CAR, EQU, TIM, AGC2, AGC1, ADC, PLL (PLL=0) 
		 */
		sync = at76c651_readreg(i2c, 0x80);

		*status = 0;

		if (sync & (0x04 | 0x10))	/* AGC1 || TIM */
			*status |= FE_HAS_SIGNAL;
		if (sync & 0x10)		/* TIM */
			*status |= FE_HAS_CARRIER;
		if (sync & 0x80)		/* FEC */
			*status |= FE_HAS_VITERBI;
		if (sync & 0x40)		/* CAR */
			*status |= FE_HAS_SYNC;
		if ((sync & 0xF0) == 0xF0)	/* TIM && EQU && CAR && FEC */
			*status |= FE_HAS_LOCK;
		break;
	}

	case FE_READ_BER:
	{
		u32 *ber = arg;
		*ber = (at76c651_readreg(i2c, 0x81) & 0x0F) << 16;
		*ber |= at76c651_readreg(i2c, 0x82) << 8;
		*ber |= at76c651_readreg(i2c, 0x83);
		*ber *= 10;
		break;
	}

	case FE_READ_SIGNAL_STRENGTH:
	{
		u8 gain = ~at76c651_readreg(i2c, 0x91);
		*(u16 *)arg = (gain << 8) | gain;
		break;
	}

	case FE_READ_SNR:
		*(u16 *)arg = 0xFFFF -
		    ((at76c651_readreg(i2c, 0x8F) << 8) |
		     at76c651_readreg(i2c, 0x90));
		break;

	case FE_READ_UNCORRECTED_BLOCKS:
		*(u32 *)arg = at76c651_readreg(i2c, 0x82);
		break;

	case FE_SET_FRONTEND:
		return at76c651_set_parameters(state, arg);

	case FE_GET_FRONTEND:
		break;

	case FE_SLEEP:
		break;

	case FE_INIT:
		return at76c651_set_defaults(state);

	case FE_GET_TUNE_SETTINGS:
	{
	        struct dvb_frontend_tune_settings *fesettings = arg;
	        fesettings->min_delay_ms = 50;
	        fesettings->step_size = 0;
	        fesettings->max_drift = 0;
		break;
	}

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static struct i2c_client client_template;

static int attach_adapter(struct i2c_adapter *adapter)
{
	struct at76c651_state *state;
	struct i2c_client *client;
	int ret;

	if (at76c651_readreg(adapter, 0x0E) != 0x65)
		return -ENODEV;

	if (!(state = kmalloc(sizeof(struct at76c651_state), GFP_KERNEL)))
		return -ENOMEM;

	state->i2c = adapter;
	state->revision = at76c651_readreg(adapter, 0x0F) & 0xFE;

	switch (state->revision) {
	case 0x10:
		at76c651_info.name[14] = 'A';
		break;
	case 0x11:
		at76c651_info.name[14] = 'B';
		break;
	default:
		kfree(state);
		return -ENODEV;
	}

	if (!(client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL))) {
		kfree(state);
		return -ENOMEM;
	}

	memcpy(client, &client_template, sizeof(struct i2c_client));
	client->adapter = adapter;
	client->addr = 0x1a >> 1;
	i2c_set_clientdata(client, state);

	ret = i2c_attach_client(client);
	if (ret) {
		kfree(state);
		kfree(client);
		return ret;
	}

	BUG_ON(!state->dvb);

	ret = dvb_register_frontend(at76c651_ioctl, state->dvb, state,
					&at76c651_info, THIS_MODULE);
	if (ret) {
		i2c_detach_client(client);
		kfree(client);
		kfree(state);
		return ret;
	}

	return 0;
}

static int detach_client(struct i2c_client *client)
{
	struct at76c651_state *state = i2c_get_clientdata(client);

	dvb_unregister_frontend(at76c651_ioctl, state->dvb);
	i2c_detach_client(client);
	BUG_ON(state->dvb);
	kfree(client);
	kfree(state);

	return 0;
}

static int command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct at76c651_state *state = i2c_get_clientdata(client);

	switch (cmd) {
	case FE_REGISTER:
		state->dvb = arg;
		break;
	case FE_UNREGISTER:
		state->dvb = NULL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct i2c_driver driver = {
	.owner		= THIS_MODULE,
	.name		= FRONTEND_NAME,
	.id		= I2C_DRIVERID_DVBFE_AT76C651,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= attach_adapter,
	.detach_client	= detach_client,
	.command	= command,
};

static struct i2c_client client_template = {
	.name		= FRONTEND_NAME,
	.flags		= I2C_CLIENT_ALLOW_USE,
	.driver		= &driver,
};

static int __init at76c651_init(void)
{
	return i2c_add_driver(&driver);
}

static void __exit at76c651_exit(void)
{
	if (i2c_del_driver(&driver))
		printk(KERN_ERR "at76c651: driver deregistration failed.\n");
}

module_init(at76c651_init);
module_exit(at76c651_exit);

MODULE_DESCRIPTION("at76c651/tua6010xs dvb-c frontend driver");
MODULE_AUTHOR("Andreas Oberritter <obi@linuxtv.org>");
MODULE_LICENSE("GPL");
