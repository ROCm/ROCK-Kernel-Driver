/*
    VES1820  - Single Chip Cable Channel Receiver driver module
               used on the the Siemens DVB-C cards

    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>

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

#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"

/* I2C_DRIVERID_VES1820 is already defined in i2c-id.h */

#if 0
static int debug = 0;
#define dprintk	if (debug) printk
#endif

static int verbose;

struct ves1820_state {
	int pwm;
	u8 reg0;
	int tuner;
	u8 demod_addr;
	struct i2c_adapter *i2c;
	struct dvb_adapter *dvb;
};

/* possible ves1820 adresses */
static u8 addr[] = { 0x61, 0x62 };

#if defined(CONFIG_DBOX2)
#define XIN 69600000UL
#define DISABLE_INVERSION(reg0)		do { reg0 &= ~0x20; } while (0)
#define ENABLE_INVERSION(reg0)		do { reg0 |= 0x20; } while (0)
#define HAS_INVERSION(reg0)		(reg0 & 0x20)
#else				/* PCI cards */
#define XIN 57840000UL
#define DISABLE_INVERSION(reg0)		do { reg0 |= 0x20; } while (0)
#define ENABLE_INVERSION(reg0)		do { reg0 &= ~0x20; } while (0)
#define HAS_INVERSION(reg0)		(!(reg0 & 0x20))
#endif

#define FIN (XIN >> 4)

static struct dvb_frontend_info ves1820_info = {
	.name = "VES1820 based DVB-C frontend",
	.type = FE_QAM,
	.frequency_stepsize = 62500,
	.frequency_min = 51000000,
	.frequency_max = 858000000,
	.symbol_rate_min = (XIN / 2) / 64,	/* SACLK/64 == (XIN/2)/64 */
	.symbol_rate_max = (XIN / 2) / 4,	/* SACLK/4 */
#if 0
	.frequency_tolerance = ? ? ?,
	.symbol_rate_tolerance = ? ? ?,	/* ppm *//* == 8% (spec p. 5) */
	.notifier_delay = ?,
#endif
	.caps = FE_CAN_QAM_16 |
		FE_CAN_QAM_32 |
		FE_CAN_QAM_64 |
		FE_CAN_QAM_128 |
		FE_CAN_QAM_256 |
		FE_CAN_FEC_AUTO |
		FE_CAN_INVERSION_AUTO,
};

static u8 ves1820_inittab[] = {
	0x69, 0x6A, 0x9B, 0x12, 0x12, 0x46, 0x26, 0x1A,
	0x43, 0x6A, 0xAA, 0xAA, 0x1E, 0x85, 0x43, 0x20,
	0xE0, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x40
};

static int ves1820_writereg(struct ves1820_state *state, u8 reg, u8 data)
{
	u8 buf[] = { 0x00, reg, data };
	struct i2c_msg msg = {.addr = state->demod_addr,.flags = 0,.buf = buf,.len = 3 };
	int ret;

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		printk("ves1820: %s(): writereg error (reg == 0x%02x,"
			"val == 0x%02x, ret == %i)\n", __FUNCTION__, reg, data, ret);

	msleep(10);
	return (ret != 1) ? -EREMOTEIO : 0;
}

static u8 ves1820_readreg(struct ves1820_state *state, u8 reg)
{
	u8 b0[] = { 0x00, reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{.addr = state->demod_addr,.flags = 0,.buf = b0,.len = 2},
		{.addr = state->demod_addr,.flags = I2C_M_RD,.buf = b1,.len = 1}
	};
	int ret;

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		printk("ves1820: %s(): readreg error (reg == 0x%02x,"
		"ret == %i)\n", __FUNCTION__, reg, ret);

	return b1[0];
}

static int tuner_write(struct ves1820_state *state, u8 addr, u8 data[4])
{
	int ret;
	struct i2c_msg msg = {.addr = addr,.flags = 0,.buf = data,.len = 4 };

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		printk("ves1820: %s(): i/o error (ret == %i)\n", __FUNCTION__, ret);

	return (ret != 1) ? -EREMOTEIO : 0;
}

/**
 *   set up the downconverter frequency divisor for a
 *   reference clock comparision frequency of 62.5 kHz.
 */
static int tuner_set_tv_freq(struct ves1820_state *state, u32 freq)
{
	u32 div, ifreq;
	static u8 byte3[] = { 0x8e, 0x85 };
	int tuner_type = state->tuner;
	u8 buf[4];

	if (tuner_type == 0xff)	/*  PLL not reachable over i2c ...  */
		return 0;

	if (strstr(state->i2c->name, "Technotrend")
	 || strstr(state->i2c->name, "TT-Budget"))
		ifreq = 35937500;
	else
		ifreq = 36125000;

	div = (freq + ifreq + 31250) / 62500;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = byte3[tuner_type];

	if (tuner_type == 1) {
		buf[2] |= (div >> 10) & 0x60;
		buf[3] = (freq < 174000000 ? 0x88 : freq < 470000000 ? 0x84 : 0x81);
	} else {
		buf[3] = (freq < 174000000 ? 0xa1 : freq < 454000000 ? 0x92 : 0x34);
	}

	return tuner_write(state, addr[tuner_type], buf);
}

static int ves1820_setup_reg0(struct ves1820_state *state, u8 reg0, fe_spectral_inversion_t inversion)
{
	reg0 |= state->reg0 & 0x62;

	if (INVERSION_ON == inversion)
		ENABLE_INVERSION(reg0);
	else if (INVERSION_OFF == inversion)
		DISABLE_INVERSION(reg0);

	ves1820_writereg(state, 0x00, reg0 & 0xfe);
	ves1820_writereg(state, 0x00, reg0 | 0x01);

	/**
	 *  check lock and toggle inversion bit if required...
	 */
	if (INVERSION_AUTO == inversion && !(ves1820_readreg(state, 0x11) & 0x08)) {
		mdelay(50);
		if (!(ves1820_readreg(state, 0x11) & 0x08)) {
			reg0 ^= 0x20;
			ves1820_writereg(state, 0x00, reg0 & 0xfe);
			ves1820_writereg(state, 0x00, reg0 | 0x01);
		}
	}

	state->reg0 = reg0;

	return 0;
}

static int ves1820_init(struct ves1820_state *state)
{
	int i;

	ves1820_writereg(state, 0, 0);

#if defined(CONFIG_DBOX2)
	ves1820_inittab[2] &= ~0x08;
#endif

	for (i = 0; i < 53; i++)
		ves1820_writereg(state, i, ves1820_inittab[i]);

	ves1820_writereg(state, 0x34, state->pwm);

	return 0;
}

static int ves1820_set_symbolrate(struct ves1820_state *state, u32 symbolrate)
{
	s32 BDR;
	s32 BDRI;
	s16 SFIL = 0;
	u16 NDEC = 0;
	u32 tmp, ratio;

	if (symbolrate > XIN / 2)
		symbolrate = XIN / 2;

	if (symbolrate < 500000)
		symbolrate = 500000;

	if (symbolrate < XIN / 16)
		NDEC = 1;
	if (symbolrate < XIN / 32)
		NDEC = 2;
	if (symbolrate < XIN / 64)
		NDEC = 3;

	if (symbolrate < (u32) (XIN / 12.3))
		SFIL = 1;
	if (symbolrate < (u32) (XIN / 16))
		SFIL = 0;
	if (symbolrate < (u32) (XIN / 24.6))
		SFIL = 1;
	if (symbolrate < (u32) (XIN / 32))
		SFIL = 0;
	if (symbolrate < (u32) (XIN / 49.2))
		SFIL = 1;
	if (symbolrate < (u32) (XIN / 64))
		SFIL = 0;
	if (symbolrate < (u32) (XIN / 98.4))
		SFIL = 1;

	symbolrate <<= NDEC;
	ratio = (symbolrate << 4) / FIN;
	tmp = ((symbolrate << 4) % FIN) << 8;
	ratio = (ratio << 8) + tmp / FIN;
	tmp = (tmp % FIN) << 8;
	ratio = (ratio << 8) + (tmp + FIN / 2) / FIN;

	BDR = ratio;
	BDRI = (((XIN << 5) / symbolrate) + 1) / 2;

	if (BDRI > 0xFF)
		BDRI = 0xFF;

	SFIL = (SFIL << 4) | ves1820_inittab[0x0E];

	NDEC = (NDEC << 6) | ves1820_inittab[0x03];

	ves1820_writereg(state, 0x03, NDEC);
	ves1820_writereg(state, 0x0a, BDR & 0xff);
	ves1820_writereg(state, 0x0b, (BDR >> 8) & 0xff);
	ves1820_writereg(state, 0x0c, (BDR >> 16) & 0x3f);

	ves1820_writereg(state, 0x0d, BDRI);
	ves1820_writereg(state, 0x0e, SFIL);

	return 0;
}

static int ves1820_set_parameters(struct ves1820_state *state, struct dvb_frontend_parameters *p)
{
	static const u8 reg0x00[] = { 0x00, 0x04, 0x08, 0x0c, 0x10 };
	static const u8 reg0x01[] = { 140, 140, 106, 100, 92 };
	static const u8 reg0x05[] = { 135, 100, 70, 54, 38 };
	static const u8 reg0x08[] = { 162, 116, 67, 52, 35 };
	static const u8 reg0x09[] = { 145, 150, 106, 126, 107 };
	int real_qam = p->u.qam.modulation - QAM_16;

	if (real_qam < 0 || real_qam > 4)
		return -EINVAL;

	tuner_set_tv_freq(state, p->frequency);
	ves1820_set_symbolrate(state, p->u.qam.symbol_rate);
	ves1820_writereg(state, 0x34, state->pwm);

	ves1820_writereg(state, 0x01, reg0x01[real_qam]);
	ves1820_writereg(state, 0x05, reg0x05[real_qam]);
	ves1820_writereg(state, 0x08, reg0x08[real_qam]);
	ves1820_writereg(state, 0x09, reg0x09[real_qam]);

	ves1820_setup_reg0(state, reg0x00[real_qam], p->inversion);

	/* yes, this speeds things up: userspace reports lock in about 8 ms
	   instead of 500 to 1200 ms after calling FE_SET_FRONTEND. */
	mdelay(50);

	return 0;
}

static int ves1820_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct ves1820_state *state = (struct ves1820_state *) fe->data;

	switch (cmd) {
	case FE_GET_INFO:
		memcpy(arg, &ves1820_info, sizeof(struct dvb_frontend_info));
		break;

	case FE_READ_STATUS:
		{
			fe_status_t *status = (fe_status_t *) arg;
			int sync;

			*status = 0;

			sync = ves1820_readreg(state, 0x11);

			if (sync & 1)
				*status |= FE_HAS_SIGNAL;

			if (sync & 2)
				*status |= FE_HAS_CARRIER;

			if (sync & 2)	/* XXX FIXME! */
				*status |= FE_HAS_VITERBI;

			if (sync & 4)
				*status |= FE_HAS_SYNC;

			if (sync & 8)
				*status |= FE_HAS_LOCK;

			break;
		}

	case FE_READ_BER:
		{
			u32 ber = ves1820_readreg(state, 0x14) |
					(ves1820_readreg(state, 0x15) << 8) |
					((ves1820_readreg(state, 0x16) & 0x0f) << 16);
			*((u32 *) arg) = 10 * ber;
			break;
		}
	case FE_READ_SIGNAL_STRENGTH:
		{
			u8 gain = ves1820_readreg(state, 0x17);
			*((u16 *) arg) = (gain << 8) | gain;
			break;
		}

	case FE_READ_SNR:
		{
			u8 quality = ~ves1820_readreg(state, 0x18);
			*((u16 *) arg) = (quality << 8) | quality;
			break;
		}

	case FE_READ_UNCORRECTED_BLOCKS:
		*((u32 *) arg) = ves1820_readreg(state, 0x13) & 0x7f;
		if (*((u32 *) arg) == 0x7f)
			*((u32 *) arg) = 0xffffffff;
		/* reset uncorrected block counter */
		ves1820_writereg(state, 0x10, ves1820_inittab[0x10] & 0xdf);
		ves1820_writereg(state, 0x10, ves1820_inittab[0x10]);
		break;

	case FE_SET_FRONTEND:
		return ves1820_set_parameters(state, arg);

	case FE_GET_FRONTEND:
		{
			struct dvb_frontend_parameters *p = (struct dvb_frontend_parameters *) arg;
			int sync;
			s8 afc = 0;

			sync = ves1820_readreg(state, 0x11);
			afc = ves1820_readreg(state, 0x19);
			if (verbose) {
				/* AFC only valid when carrier has been recovered */
				printk(sync & 2 ? "ves1820: AFC (%d) %dHz\n" :
					"ves1820: [AFC (%d) %dHz]\n", afc, -((s32) p->u.qam.symbol_rate * afc) >> 10);
			}

			p->inversion = HAS_INVERSION(state->reg0) ? INVERSION_ON : INVERSION_OFF;
			p->u.qam.modulation = ((state->reg0 >> 2) & 7) + QAM_16;

			p->u.qam.fec_inner = FEC_NONE;

			p->frequency = ((p->frequency + 31250) / 62500) * 62500;
			if (sync & 2)
				p->frequency -= ((s32) p->u.qam.symbol_rate * afc) >> 10;
			break;
		}
	case FE_SLEEP:
		ves1820_writereg(state, 0x1b, 0x02);	/* pdown ADC */
		ves1820_writereg(state, 0x00, 0x80);	/* standby */
		break;

	case FE_INIT:
		return ves1820_init(state);

	default:
		return -EINVAL;
	}

	return 0;
}

static long probe_tuner(struct i2c_adapter *i2c)
{
	struct i2c_msg msg1 = {.addr = 0x61,.flags = 0,.buf = NULL,.len = 0 };
	struct i2c_msg msg2 = {.addr = 0x62,.flags = 0,.buf = NULL,.len = 0 };
	int type;

	if (i2c_transfer(i2c, &msg1, 1) == 1) {
		type = 0;
		printk("ves1820: setup for tuner spXXXX\n");
	} else if (i2c_transfer(i2c, &msg2, 1) == 1) {
		type = 1;
		printk("ves1820: setup for tuner sp5659c\n");
	} else {
		type = -1;
	}

	return type;
}

static u8 read_pwm(struct i2c_adapter *i2c)
{
	u8 b = 0xff;
	u8 pwm;
	struct i2c_msg msg[] = { {.addr = 0x50,.flags = 0,.buf = &b,.len = 1},
	{.addr = 0x50,.flags = I2C_M_RD,.buf = &pwm,.len = 1}
	};

	if ((i2c_transfer(i2c, msg, 2) != 2) || (pwm == 0xff))
		pwm = 0x48;

	printk("ves1820: pwm=0x%02x\n", pwm);

	return pwm;
}

static long probe_demod_addr(struct i2c_adapter *i2c)
{
	u8 b[] = { 0x00, 0x1a };
	u8 id;
	struct i2c_msg msg[] = { {.addr = 0x08,.flags = 0,.buf = b,.len = 2},
	{.addr = 0x08,.flags = I2C_M_RD,.buf = &id,.len = 1}
	};

	if (i2c_transfer(i2c, msg, 2) == 2 && (id & 0xf0) == 0x70)
		return msg[0].addr;

	msg[0].addr = msg[1].addr = 0x09;

	if (i2c_transfer(i2c, msg, 2) == 2 && (id & 0xf0) == 0x70)
		return msg[0].addr;

	return -1;
}

static ssize_t attr_read_pwm(struct device *dev, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ves1820_state *state = (struct ves1820_state *) i2c_get_clientdata(client);
	return sprintf(buf, "0x%02x\n", state->pwm);
}

static ssize_t attr_write_pwm(struct device *dev, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ves1820_state *state = (struct ves1820_state *) i2c_get_clientdata(client);
	unsigned long pwm;
	pwm = simple_strtoul(buf, NULL, 0);
	state->pwm = pwm & 0xff;
	return strlen(buf)+1;
}

static struct device_attribute dev_attr_client_name = {
	.attr	= { .name = "pwm", .mode = S_IRUGO|S_IWUGO, .owner = THIS_MODULE },
	.show	= &attr_read_pwm,
	.store  = &attr_write_pwm,
};

static struct i2c_client client_template;

static int attach_adapter(struct i2c_adapter *adapter)
{
	struct i2c_client *client;
	struct ves1820_state *state;
	long demod_addr;
	int tuner_type;
	int ret;

	demod_addr = probe_demod_addr(adapter);
	if (demod_addr < 0)
		return -ENODEV;

	tuner_type = probe_tuner(adapter);
	if (tuner_type < 0) {
		printk("ves1820: demod found, but unknown tuner type.\n");
		return -ENODEV;
	}

	if ((state = kmalloc(sizeof(struct ves1820_state), GFP_KERNEL)) == NULL) {
		return -ENOMEM;
	}

	if (NULL == (client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL))) {
		kfree(state);
		return -ENOMEM;
	}

	memset(state, 0, sizeof(*state));
	state->i2c = adapter;
	state->tuner = tuner_type;
	state->pwm = read_pwm(adapter);
	state->reg0 = ves1820_inittab[0];
	state->demod_addr = demod_addr;

	memcpy(client, &client_template, sizeof(struct i2c_client));
	client->adapter = adapter;
	client->addr = addr[tuner_type];

	i2c_set_clientdata(client, (void *) state);

	ret = i2c_attach_client(client);
	if (ret) {
		kfree(client);
		kfree(state);
		return ret;
	}

	BUG_ON(!state->dvb);

	device_create_file(&client->dev, &dev_attr_client_name);

	ret = dvb_register_frontend(ves1820_ioctl, state->dvb, state, &ves1820_info, THIS_MODULE);
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
	struct ves1820_state *state = (struct ves1820_state *) i2c_get_clientdata(client);
	dvb_unregister_frontend(ves1820_ioctl, state->dvb);
	device_remove_file(&client->dev, &dev_attr_client_name);
	i2c_detach_client(client);
	BUG_ON(state->dvb);
	kfree(client);
	kfree(state);
	return 0;
}

static int command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct ves1820_state *state = (struct ves1820_state *) i2c_get_clientdata(client);

	switch (cmd) {
	case FE_REGISTER:{
			state->dvb = (struct dvb_adapter *) arg;
			break;
		}
	case FE_UNREGISTER:{
			state->dvb = NULL;
			break;
		}
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static struct i2c_driver driver = {
	.owner = THIS_MODULE,
	.name = "ves1820",
	.id = I2C_DRIVERID_VES1820,
	.flags = I2C_DF_NOTIFY,
	.attach_adapter = attach_adapter,
	.detach_client = detach_client,
	.command = command,
};

static struct i2c_client client_template = {
	I2C_DEVNAME("ves1820"),
	.flags = I2C_CLIENT_ALLOW_USE,
	.driver = &driver,
};

static int __init init_ves1820(void)
{
	return i2c_add_driver(&driver);
}

static void __exit exit_ves1820(void)
{
	if (i2c_del_driver(&driver))
		printk("ves1820: driver deregistration failed\n");
}

module_init(init_ves1820);
module_exit(exit_ves1820);

MODULE_PARM(verbose, "i");
MODULE_PARM_DESC(verbose, "print AFC offset after tuning for debugging the PWM setting");

MODULE_DESCRIPTION("VES1820 DVB-C frontend driver");
MODULE_AUTHOR("Ralph Metzler, Holger Waechtler");
MODULE_LICENSE("GPL");
