/* 
    Driver for Zarlink MT312 Satellite Channel Decoder

    Copyright (C) 2003 Andreas Oberritter <obi@saftware.de>

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

    References:
    http://products.zarlink.com/product_profiles/MT312.htm
    http://products.zarlink.com/product_profiles/SL1935.htm
*/

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "dvb_frontend.h"
#include "mt312.h"

#define I2C_ADDR_MT312		0x0e
#define I2C_ADDR_SL1935		0x61
#define I2C_ADDR_TSA5059	0x61

#define MT312_DEBUG		0

#define MT312_SYS_CLK		90000000UL	/* 90 MHz */
#define MT312_PLL_CLK		10000000UL	/* 10 MHz */

/* number of active frontends */
static int mt312_count = 0;

static struct dvb_frontend_info mt312_info = {
	.name = "Zarlink MT312",
	.type = FE_QPSK,
	.frequency_min = 950000,
	.frequency_max = 2150000,
	.frequency_stepsize = (MT312_PLL_CLK / 1000) / 128,
	/*.frequency_tolerance = 29500,         FIXME: binary compatibility waste? */
	.symbol_rate_min = MT312_SYS_CLK / 128,
	.symbol_rate_max = MT312_SYS_CLK / 2,
	/*.symbol_rate_tolerance = 500,         FIXME: binary compatibility waste? 2% */
	.notifier_delay = 0,
	.caps =
	    FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
	    FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
	    FE_CAN_FEC_AUTO | FE_CAN_QPSK | FE_CAN_RECOVER |
	    FE_CAN_CLEAN_SETUP | FE_CAN_MUTE_TS
};

static int mt312_read(struct dvb_i2c_bus *i2c,
		      const enum mt312_reg_addr reg, void *buf,
		      const size_t count)
{
	int ret;
	struct i2c_msg msg[2];
	u8 regbuf[1] = { reg };

	msg[0].addr = I2C_ADDR_MT312;
	msg[0].flags = 0;
	msg[0].buf = regbuf;
	msg[0].len = 1;
	msg[1].addr = I2C_ADDR_MT312;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = count;

	ret = i2c->xfer(i2c, msg, 2);

	if ((ret != 2) && (mt312_count != 0)) {
		printk(KERN_ERR "%s: ret == %d\n", __FUNCTION__, ret);
		return -EREMOTEIO;
	}
#if MT312_DEBUG
	{
		int i;
		printk(KERN_INFO "R(%d):", reg & 0x7f);
		for (i = 0; i < count; i++)
			printk(" %02x", ((const u8 *) buf)[i]);
		printk("\n");
	}
#endif

	return 0;
}

static int mt312_write(struct dvb_i2c_bus *i2c,
		       const enum mt312_reg_addr reg, const void *src,
		       const size_t count)
{
	int ret;
	u8 buf[count + 1];
	struct i2c_msg msg;

#if MT312_DEBUG
	{
		int i;
		printk(KERN_INFO "W(%d):", reg & 0x7f);
		for (i = 0; i < count; i++)
			printk(" %02x", ((const u8 *) src)[i]);
		printk("\n");
	}
#endif

	buf[0] = reg;
	memcpy(&buf[1], src, count);

	msg.addr = I2C_ADDR_MT312;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = count + 1;

	ret = i2c->xfer(i2c, &msg, 1);

	if (ret != 1) {
		printk(KERN_ERR "%s: ret == %d\n", __FUNCTION__, ret);
		return -EREMOTEIO;
	}

	return 0;
}

static inline int mt312_readreg(struct dvb_i2c_bus *i2c,
				const enum mt312_reg_addr reg, u8 * val)
{
	return mt312_read(i2c, reg, val, 1);
}

static inline int mt312_writereg(struct dvb_i2c_bus *i2c,
				 const enum mt312_reg_addr reg, const u8 val)
{
	return mt312_write(i2c, reg, &val, 1);
}

static int mt312_pll_write(struct dvb_i2c_bus *i2c, const u8 addr,
			   u8 * buf, const u8 len)
{
	int ret;
	struct i2c_msg msg;

	msg.addr = addr;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = len;

	if ((ret = mt312_writereg(i2c, GPP_CTRL, 0x40)) < 0)
		return ret;

	if ((ret = i2c->xfer(i2c, &msg, 1)) != 1)
		printk(KERN_ERR "%s: i/o error (ret == %d)\n", __FUNCTION__, ret);

	if ((ret = mt312_writereg(i2c, GPP_CTRL, 0x00)) < 0)
		return ret;

	return 0;
}

static inline u32 mt312_div(u32 a, u32 b)
{
	return (a + (b / 2)) / b;
}

static int sl1935_set_tv_freq(struct dvb_i2c_bus *i2c, u32 freq, u32 sr)
{
	/* 155 uA, Baseband Path B */
	u8 buf[4] = { 0x00, 0x00, 0x80, 0x00 };

	u8 exp;
	u32 ref;
	u32 div;

	if (sr < 10000000) {	/* 1-10 MSym/s: ratio 2 ^ 3 */
		exp = 3;
		buf[2] |= 0x40;	/* 690 uA */
	} else if (sr < 15000000) {	/* 10-15 MSym/s: ratio 2 ^ 4 */
		exp = 4;
		buf[2] |= 0x20;	/* 330 uA */
	} else {		/* 15-45 MSym/s: ratio 2 ^ 7 */
		exp = 7;
		buf[3] |= 0x08;	/* Baseband Path A */
	}

	div = mt312_div(MT312_PLL_CLK, 1 << exp);
	ref = mt312_div(freq * 1000, div);
	mt312_info.frequency_stepsize = mt312_div(div, 1000);

	buf[0] = (ref >> 8) & 0x7f;
	buf[1] = (ref >> 0) & 0xff;
	buf[2] |= (exp - 1);

	if (freq < 1550000)
		buf[3] |= 0x10;

	printk(KERN_INFO "synth dword = %02x%02x%02x%02x\n", buf[0],
	       buf[1], buf[2], buf[3]);

	return mt312_pll_write(i2c, I2C_ADDR_SL1935, buf, sizeof(buf));
}

static int tsa5059_set_tv_freq(struct dvb_i2c_bus *i2c, u32 freq, u32 sr)
{
	u8 buf[4];

	u32 ref = mt312_div(freq, 125);

	buf[0] = (ref >> 8) & 0x7f;
	buf[1] = (ref >> 0) & 0xff;
	buf[2] = 0x84 | ((ref >> 10) & 0x60);
	buf[3] = 0x80;
	
	if (freq < 1550000)
		buf[3] |= 0x02;

	printk(KERN_INFO "synth dword = %02x%02x%02x%02x\n", buf[0],
	       buf[1], buf[2], buf[3]);

	return mt312_pll_write(i2c, I2C_ADDR_TSA5059, buf, sizeof(buf));
}

static int mt312_reset(struct dvb_i2c_bus *i2c, const u8 full)
{
	return mt312_writereg(i2c, RESET, full ? 0x80 : 0x40);
}

static int mt312_init(struct dvb_i2c_bus *i2c, const long id)
{
	int ret;
	u8 buf[2];

	/* wake up */
	if ((ret = mt312_writereg(i2c, CONFIG, 0x8c)) < 0)
		return ret;

	/* wait at least 150 usec */
	udelay(150);

	/* full reset */
	if ((ret = mt312_reset(i2c, 1)) < 0)
		return ret;

	/* SYS_CLK */
	buf[0] = mt312_div(MT312_SYS_CLK * 2, 1000000);

	/* DISEQC_RATIO */
	buf[1] = mt312_div(MT312_PLL_CLK, 15000 * 4);

	if ((ret = mt312_write(i2c, SYS_CLK, buf, sizeof(buf))) < 0)
		return ret;

	if ((ret = mt312_writereg(i2c, SNR_THS_HIGH, 0x32)) < 0)
		return ret;

	if ((ret = mt312_writereg(i2c, OP_CTRL, 0x53)) < 0)
		return ret;

	/* TS_SW_LIM */
	buf[0] = 0x8c;
	buf[1] = 0x98;

	if ((ret = mt312_write(i2c, TS_SW_LIM_L, buf, sizeof(buf))) < 0)
		return ret;

	if ((ret = mt312_writereg(i2c, CS_SW_LIM, 0x69)) < 0)
		return ret;

	return 0;
}

static int mt312_send_master_cmd(struct dvb_i2c_bus *i2c,
				 const struct dvb_diseqc_master_cmd *c)
{
	int ret;
	u8 diseqc_mode;

	if ((c->msg_len == 0) || (c->msg_len > sizeof(c->msg)))
		return -EINVAL;

	if ((ret = mt312_readreg(i2c, DISEQC_MODE, &diseqc_mode)) < 0)
		return ret;

	if ((ret =
	     mt312_write(i2c, (0x80 | DISEQC_INSTR), c->msg, c->msg_len)) < 0)
		return ret;

	if ((ret =
	     mt312_writereg(i2c, DISEQC_MODE,
			    (diseqc_mode & 0x40) | ((c->msg_len - 1) << 3)
			    | 0x04)) < 0)
		return ret;

	/* set DISEQC_MODE[2:0] to zero if a return message is expected */
	if (c->msg[0] & 0x02)
		if ((ret =
		     mt312_writereg(i2c, DISEQC_MODE, (diseqc_mode & 0x40))) < 0)
			return ret;

	return 0;
}

static int mt312_recv_slave_reply(struct dvb_i2c_bus *i2c,
				  struct dvb_diseqc_slave_reply *r)
{
	/* TODO */
	return -EOPNOTSUPP;
}

static int mt312_send_burst(struct dvb_i2c_bus *i2c, const fe_sec_mini_cmd_t c)
{
	const u8 mini_tab[2] = { 0x02, 0x03 };

	int ret;
	u8 diseqc_mode;

	if (c > SEC_MINI_B)
		return -EINVAL;

	if ((ret = mt312_readreg(i2c, DISEQC_MODE, &diseqc_mode)) < 0)
		return ret;

	if ((ret =
	     mt312_writereg(i2c, DISEQC_MODE,
			    (diseqc_mode & 0x40) | mini_tab[c])) < 0)
		return ret;

	return 0;
}

static int mt312_set_tone(struct dvb_i2c_bus *i2c, const fe_sec_tone_mode_t t)
{
	const u8 tone_tab[2] = { 0x01, 0x00 };

	int ret;
	u8 diseqc_mode;

	if (t > SEC_TONE_OFF)
		return -EINVAL;

	if ((ret = mt312_readreg(i2c, DISEQC_MODE, &diseqc_mode)) < 0)
		return ret;

	if ((ret =
	     mt312_writereg(i2c, DISEQC_MODE,
			    (diseqc_mode & 0x40) | tone_tab[t])) < 0)
		return ret;

	return 0;
}

static int mt312_set_voltage(struct dvb_i2c_bus *i2c, const fe_sec_voltage_t v)
{
	const u8 volt_tab[3] = { 0x00, 0x40, 0x00 };

	if (v > SEC_VOLTAGE_OFF)
		return -EINVAL;

	return mt312_writereg(i2c, DISEQC_MODE, volt_tab[v]);
}

static int mt312_read_status(struct dvb_i2c_bus *i2c, fe_status_t * s)
{
	int ret;
	u8 status[3];

	*s = 0;

	if ((ret = mt312_read(i2c, QPSK_STAT_H, status, sizeof(status))) < 0)
		return ret;

	if (status[0] & 0xc0)
		*s |= FE_HAS_SIGNAL;	/* signal noise ratio */
	if (status[0] & 0x04)
		*s |= FE_HAS_CARRIER;	/* qpsk carrier lock */
	if (status[2] & 0x02)
		*s |= FE_HAS_VITERBI;	/* viterbi lock */
	if (status[2] & 0x04)
		*s |= FE_HAS_SYNC;	/* byte align lock */
	if (status[0] & 0x01)
		*s |= FE_HAS_LOCK;	/* qpsk lock */

	return 0;
}

static int mt312_read_bercnt(struct dvb_i2c_bus *i2c, u32 * ber)
{
	int ret;
	u8 buf[3];

	if ((ret = mt312_read(i2c, RS_BERCNT_H, buf, 3)) < 0)
		return ret;

	*ber = ((buf[0] << 16) | (buf[1] << 8) | buf[2]) * 64;

	return 0;
}

static int mt312_read_agc(struct dvb_i2c_bus *i2c, u16 * signal_strength)
{
	int ret;
	u8 buf[3];
	u16 agc;
	s16 err_db;

	if ((ret = mt312_read(i2c, AGC_H, buf, sizeof(buf))) < 0)
		return ret;

	agc = (buf[0] << 6) | (buf[1] >> 2);
	err_db = (s16) (((buf[1] & 0x03) << 14) | buf[2] << 6) >> 6;

	*signal_strength = agc;

	printk(KERN_DEBUG "agc=%08x err_db=%hd\n", agc, err_db);

	return 0;
}

static int mt312_read_snr(struct dvb_i2c_bus *i2c, u16 * snr)
{
	int ret;
	u8 buf[2];

	if ((ret = mt312_read(i2c, M_SNR_H, &buf, sizeof(buf))) < 0)
		return ret;

	*snr = 0xFFFF - ((((buf[0] & 0x7f) << 8) | buf[1]) << 1);

	return 0;
}

static int mt312_read_ubc(struct dvb_i2c_bus *i2c, u32 * ubc)
{
	int ret;
	u8 buf[2];

	if ((ret = mt312_read(i2c, RS_UBC_H, &buf, sizeof(buf))) < 0)
		return ret;

	*ubc = (buf[0] << 8) | buf[1];

	return 0;
}

static int mt312_set_frontend(struct dvb_i2c_bus *i2c,
			      const struct dvb_frontend_parameters *p,
			      const long id)
{
	int ret;
	u8 buf[5];
	u16 sr;

	const u8 fec_tab[10] =
	    { 0x00, 0x01, 0x02, 0x04, 0x3f, 0x08, 0x10, 0x20, 0x3f, 0x3f };
	const u8 inv_tab[3] = { 0x00, 0x40, 0x80 };

	int (*set_tv_freq)(struct dvb_i2c_bus *i2c, u32 freq, u32 sr);

	if ((p->frequency < mt312_info.frequency_min)
	    || (p->frequency > mt312_info.frequency_max))
		return -EINVAL;

	if ((p->inversion < INVERSION_OFF)
	    || (p->inversion > INVERSION_AUTO))
		return -EINVAL;

	if ((p->u.qpsk.symbol_rate < mt312_info.symbol_rate_min)
	    || (p->u.qpsk.symbol_rate > mt312_info.symbol_rate_max))
		return -EINVAL;

	if ((p->u.qpsk.fec_inner < FEC_NONE)
	    || (p->u.qpsk.fec_inner > FEC_AUTO))
		return -EINVAL;

	if ((p->u.qpsk.fec_inner == FEC_4_5)
	    || (p->u.qpsk.fec_inner == FEC_8_9))
		return -EINVAL;

	switch (id) {
	case ID_VP310:
		set_tv_freq = tsa5059_set_tv_freq;
		break;
	case ID_MT312:
		set_tv_freq = sl1935_set_tv_freq;
		break;
	default:
		return -EINVAL;
	}

	if ((ret = set_tv_freq(i2c, p->frequency, p->u.qpsk.symbol_rate)) < 0)
		return ret;

	/* sr = (u16)(sr * 256.0 / 1000000.0) */
	sr = mt312_div(p->u.qpsk.symbol_rate * 4, 15625);

	/* SYM_RATE */
	buf[0] = (sr >> 8) & 0x3f;
	buf[1] = (sr >> 0) & 0xff;

	/* VIT_MODE */
	buf[2] = inv_tab[p->inversion] | fec_tab[p->u.qpsk.fec_inner];

	/* QPSK_CTRL */
	buf[3] = 0x40;		/* swap I and Q before QPSK demodulation */

	if (p->u.qpsk.symbol_rate < 10000000)
		buf[3] |= 0x04;	/* use afc mode */

	/* GO */
	buf[4] = 0x01;

	if ((ret = mt312_write(i2c, SYM_RATE_H, buf, sizeof(buf))) < 0)
		return ret;

	return 0;
}

static int mt312_get_inversion(struct dvb_i2c_bus *i2c,
			       fe_spectral_inversion_t * i)
{
	int ret;
	u8 vit_mode;

	if ((ret = mt312_readreg(i2c, VIT_MODE, &vit_mode)) < 0)
		return ret;

	if (vit_mode & 0x80)	/* auto inversion was used */
		*i = (vit_mode & 0x40) ? INVERSION_ON : INVERSION_OFF;

	return 0;
}

static int mt312_get_symbol_rate(struct dvb_i2c_bus *i2c, u32 * sr)
{
	int ret;
	u8 sym_rate_h;
	u8 dec_ratio;
	u16 sym_rat_op;
	u16 monitor;
	u8 buf[2];

	if ((ret = mt312_readreg(i2c, SYM_RATE_H, &sym_rate_h)) < 0)
		return ret;

	if (sym_rate_h & 0x80) {	/* symbol rate search was used */
		if ((ret = mt312_writereg(i2c, MON_CTRL, 0x03)) < 0)
			return ret;

		if ((ret = mt312_read(i2c, MONITOR_H, buf, sizeof(buf))) < 0)
			return ret;

		monitor = (buf[0] << 8) | buf[1];

		printk(KERN_DEBUG "sr(auto) = %u\n",
		       mt312_div(monitor * 15625, 4));
	} else {
		if ((ret = mt312_writereg(i2c, MON_CTRL, 0x05)) < 0)
			return ret;

		if ((ret = mt312_read(i2c, MONITOR_H, buf, sizeof(buf))) < 0)
			return ret;

		dec_ratio = ((buf[0] >> 5) & 0x07) * 32;

		if ((ret = mt312_read(i2c, SYM_RAT_OP_H, buf, sizeof(buf))) < 0)
			return ret;

		sym_rat_op = (buf[0] << 8) | buf[1];

		printk(KERN_DEBUG "sym_rat_op=%d dec_ratio=%d\n",
		       sym_rat_op, dec_ratio);
		printk(KERN_DEBUG "*sr(manual) = %lu\n",
		       (((MT312_PLL_CLK * 8192) / (sym_rat_op + 8192)) *
			2) - dec_ratio);
	}

	return 0;
}

static int mt312_get_code_rate(struct dvb_i2c_bus *i2c, fe_code_rate_t * cr)
{
	const fe_code_rate_t fec_tab[8] =
	    { FEC_1_2, FEC_2_3, FEC_3_4, FEC_5_6, FEC_6_7, FEC_7_8,
		FEC_AUTO, FEC_AUTO };

	int ret;
	u8 fec_status;

	if ((ret = mt312_readreg(i2c, FEC_STATUS, &fec_status)) < 0)
		return ret;

	*cr = fec_tab[(fec_status >> 4) & 0x07];

	return 0;
}

static int mt312_get_frontend(struct dvb_i2c_bus *i2c,
			      struct dvb_frontend_parameters *p)
{
	int ret;

	if ((ret = mt312_get_inversion(i2c, &p->inversion)) < 0)
		return ret;

	if ((ret = mt312_get_symbol_rate(i2c, &p->u.qpsk.symbol_rate)) < 0)
		return ret;

	if ((ret = mt312_get_code_rate(i2c, &p->u.qpsk.fec_inner)) < 0)
		return ret;

	return 0;
}

static int mt312_sleep(struct dvb_i2c_bus *i2c)
{
	int ret;
	u8 config;

	/* reset all registers to defaults */
	if ((ret = mt312_reset(i2c, 1)) < 0)
		return ret;

	if ((ret = mt312_readreg(i2c, CONFIG, &config)) < 0)
		return ret;

	/* enter standby */
	if ((ret = mt312_writereg(i2c, CONFIG, config & 0x7f)) < 0)
		return ret;

	return 0;
}

static int mt312_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct dvb_i2c_bus *i2c = fe->i2c;

	switch (cmd) {
	case FE_GET_INFO:
		memcpy(arg, &mt312_info, sizeof(struct dvb_frontend_info));
		break;

	case FE_DISEQC_RESET_OVERLOAD:
		return -EOPNOTSUPP;

	case FE_DISEQC_SEND_MASTER_CMD:
		return mt312_send_master_cmd(i2c, arg);

	case FE_DISEQC_RECV_SLAVE_REPLY:
		if ((long) fe->data == ID_MT312)
			return mt312_recv_slave_reply(i2c, arg);
		else
			return -EOPNOTSUPP;

	case FE_DISEQC_SEND_BURST:
		return mt312_send_burst(i2c, (fe_sec_mini_cmd_t) arg);

	case FE_SET_TONE:
		return mt312_set_tone(i2c, (fe_sec_tone_mode_t) arg);

	case FE_SET_VOLTAGE:
		return mt312_set_voltage(i2c, (fe_sec_voltage_t) arg);

	case FE_ENABLE_HIGH_LNB_VOLTAGE:
		return -EOPNOTSUPP;

	case FE_READ_STATUS:
		return mt312_read_status(i2c, arg);

	case FE_READ_BER:
		return mt312_read_bercnt(i2c, arg);

	case FE_READ_SIGNAL_STRENGTH:
		return mt312_read_agc(i2c, arg);

	case FE_READ_SNR:
		return mt312_read_snr(i2c, arg);

	case FE_READ_UNCORRECTED_BLOCKS:
		return mt312_read_ubc(i2c, arg);

	case FE_SET_FRONTEND:
		return mt312_set_frontend(i2c, arg, (long) fe->data);

	case FE_GET_FRONTEND:
		return mt312_get_frontend(i2c, arg);

	case FE_GET_EVENT:
		return -EOPNOTSUPP;

	case FE_SLEEP:
		return mt312_sleep(i2c);

	case FE_INIT:
		return mt312_init(i2c, (long) fe->data);

	case FE_RESET:
		return mt312_reset(i2c, 0);

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static int mt312_attach(struct dvb_i2c_bus *i2c, void **data)
{
	int ret;
	u8 id;

	if ((ret = mt312_readreg(i2c, ID, &id)) < 0)
		return ret;

	if ((id != ID_VP310) && (id != ID_MT312))
		return -ENODEV;

	if ((ret = dvb_register_frontend(mt312_ioctl, i2c,
				(void *)(long)id, &mt312_info)) < 0)
		return ret;

	mt312_count++;

	return 0;
}

static void mt312_detach(struct dvb_i2c_bus *i2c, void *data)
{
	dvb_unregister_frontend(mt312_ioctl, i2c);

	if (mt312_count)
		mt312_count--;
}

static int __init mt312_module_init(void)
{
	return dvb_register_i2c_device(THIS_MODULE, mt312_attach, mt312_detach);
}

static void __exit mt312_module_exit(void)
{
	dvb_unregister_i2c_device(mt312_attach);
}

module_init(mt312_module_init);
module_exit(mt312_module_exit);

MODULE_DESCRIPTION("MT312 Satellite Channel Decoder Driver");
MODULE_AUTHOR("Andreas Oberritter <obi@saftware.de>");
MODULE_LICENSE("GPL");
