/*
    Universal driver for STV0299/TDA5059/SL1935 based
    DVB QPSK frontends

    Alps BSRU6, LG TDQB-S00x

    Copyright (C) 2001-2002 Convergence Integrated Media GmbH
	<ralph@convergence.de>,
	<holger@convergence.de>,
	<js@convergence.de>
    
    Philips SU1278/SH

    Copyright (C) 2002 by Peter Schildmann
        <peter.schildmann@web.de>

    LG TDQF-S001F

    Copyright (C) 2002 Felix Domke <tmbinc@elitedvb.net>
                     & Andreas Oberritter <andreas@oberritter.de>

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

#include <linux/init.h>
#include <linux/module.h>

#include "dvb_frontend.h"

static int debug = 0;
#define dprintk	if (debug) printk

/* frontend types */
#define UNKNOWN_FRONTEND  -1
#define PHILIPS_SU1278SH   0
#define ALPS_BSRU6         1
#define LG_TDQF_S001F      2

/* Master Clock = 88 MHz */
#define M_CLK (88000000UL) 

static
struct dvb_frontend_info uni0299_info = {
	name: "STV0299/TSA5059/SL1935 based",
	type: FE_QPSK,
	frequency_min: 950000,
	frequency_max: 2150000,
	frequency_stepsize: 125,   /* kHz for QPSK frontends */
	frequency_tolerance: M_CLK/2000,
	symbol_rate_min: 1000000,
	symbol_rate_max: 45000000,
	symbol_rate_tolerance: 500,  /* ppm */
	notifier_delay: 0,
	caps: FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
	      FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
	      FE_CAN_QPSK |
	      FE_CAN_FEC_AUTO | FE_CAN_INVERSION_AUTO |
	      FE_CAN_CLEAN_SETUP
};


static
u8 init_tab [] = {
        /* clock registers */
        0x01, 0x15,   /* K = 0, DIRCLK = 0, M = 0x15                  */
	0x02, 0x30,   /* STDBY = 0, VCO = 0 (ON), SERCLK = 0, P = 0   */
                      /* f_VCO = 4MHz * 4 * (M+1) / (K+1) = 352 MHz   */
	0x03, 0x00,   /* auxiliary clock not used                     */
	0x04, 0x7d,   /* F22FR = 0x7d                                 */
		      /* F22 = f_VCO / 128 / 0x7d = 22 kHz            */

        /* I2C bus repeater */
	0x05, 0x35,   /* I2CT = 0, SCLT = 1, SDAT = 1                 */

 	/* general purpose DAC registers */
	0x06, 0x40,   /* DAC not used, set to high impendance mode    */
	0x07, 0x00,   /* DAC LSB                                      */

	/* DiSEqC registers */
	0x08, 0x40,   /* DiSEqC off                                   */
	0x09, 0x00,   /* FIFO                                         */

        /* Input/Output configuration register */
	0x0c, 0x51,   /* OP1 ctl = Normal, OP1 val = 1 (LNB Power ON) */
	              /* OP0 ctl = Normal, OP0 val = 1 (18 V)         */
                      /* Nyquist filter = 00, QPSK reverse = 0        */
                   
        /* AGC1 control register */
	0x0d, 0x82,   /* DC offset compensation = ON, beta_agc1 = 2   */

        /* Timing loop register */
	0x0e, 0x23,   /* alpha_tmg = 2, beta_tmg = 3                  */

	0x10, 0x3f,   // AGC2  0x3d
	0x11, 0x84,
	0x12, 0xb5,   // Lock detect: -64  Carrier freq detect:on
	0x13, 0xb6,   // alpha_car b:4 a:0  noise est:256ks  derot:on
	0x14, 0x93,   // beat carc:0 d:0 e:0xf  phase detect algo: 1
	0x15, 0xc9,   // lock detector threshold

	0x16, 0x1d,   /* AGC1 integrator value                        */
	0x17, 0x00,
	0x18, 0x14,
	0x19, 0xf2,

	0x1a, 0x11,

	0x1b, 0x9c,
	0x1c, 0x00,
	0x1d, 0x00,
	0x1e, 0x0b,
	0x1f, 0x50,

	0x20, 0x00,
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x24, 0xff,
	0x25, 0xff,
	0x26, 0xff,

	0x28, 0x00,  // out imp: normal  out type: parallel FEC mode:0
	0x29, 0x1e,  // 1/2 threshold
	0x2a, 0x14,  // 2/3 threshold
	0x2b, 0x0f,  // 3/4 threshold
	0x2c, 0x09,  // 5/6 threshold
	0x2d, 0x05,  // 7/8 threshold
	0x2e, 0x01,

	0x31, 0x1f,  // test all FECs

	0x32, 0x19,  // viterbi and synchro search
	0x33, 0xfc,  // rs control
	0x34, 0x93,  // error control

	0x0b, 0x00,
	0x27, 0x00,
	0x2f, 0x00,
	0x30, 0x00,
	0x35, 0x00,
	0x36, 0x00,
	0x37, 0x00,
	0x38, 0x00,
	0x39, 0x00,
	0x3a, 0x00,
	0x3b, 0x00,
	0x3c, 0x00,
	0x3d, 0x00,
	0x3e, 0x00,
	0x3f, 0x00,
	0x40, 0x00,
	0x41, 0x00,
	0x42, 0x00,
	0x43, 0x00,
	0x44, 0x00,
	0x45, 0x00,
	0x46, 0x00,
	0x47, 0x00,
	0x48, 0x00,
	0x49, 0x00,
	0x4a, 0x00,
	0x4b, 0x00,
	0x4c, 0x00,
	0x4d, 0x00,
	0x4e, 0x00,
	0x4f, 0x00
};


static
int stv0299_writereg (struct dvb_i2c_bus *i2c, u8 reg, u8 data)
{
	int ret;
	u8 buf [] = { reg, data };
	struct i2c_msg msg = { addr: 0x68, flags: 0, buf: buf, len: 2 };

	dprintk ("%s\n", __FUNCTION__);

	ret = i2c->xfer (i2c, &msg, 1);

	if (ret != 1) 
		dprintk("%s: writereg error (reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			__FUNCTION__, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}


static
u8 stv0299_readreg (struct dvb_i2c_bus *i2c, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { addr: 0x68, flags: 0, buf: b0, len: 1 },
			   { addr: 0x68, flags: I2C_M_RD, buf: b1, len: 1 } };

	dprintk ("%s\n", __FUNCTION__);

	ret = i2c->xfer (i2c, msg, 2);
        
	if (ret != 2) 
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];
}


static
int stv0299_readregs (struct dvb_i2c_bus *i2c, u8 reg1, u8 *b, u8 len)
{
        int ret;
        struct i2c_msg msg [] = { { addr: 0x68, flags: 0, buf: &reg1, len: 1 },
                           { addr: 0x68, flags: I2C_M_RD, buf: b, len: len } };

	dprintk ("%s\n", __FUNCTION__);

        ret = i2c->xfer (i2c, msg, 2);

        if (ret != 2)
                dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

        return ret == 2 ? 0 : -1;
}


static
int pll_write (struct dvb_i2c_bus *i2c, u8 data [4], int ftype)
{
	int ret;
	u8 rpt1 [] = { 0x05, 0xb5 };  /*  enable i2c repeater on stv0299  */
	/* TSA5059 i2c-bus address */
	u8 addr = (ftype == PHILIPS_SU1278SH) ? 0x60 : 0x61;
	struct i2c_msg msg [] = {{ addr: 0x68, flags: 0, buf: rpt1, len: 2 },
			         { addr: addr, flags: 0, buf: data, len: 4 }};

	dprintk ("%s\n", __FUNCTION__);

	if (ftype == LG_TDQF_S001F || ftype == ALPS_BSRU6) {
		ret  = i2c->xfer (i2c, &msg[0], 1);
		ret += i2c->xfer (i2c, &msg[1], 1);
	}
	else {
		ret = i2c->xfer (i2c, msg, 2);
	}

	if (ret != 2)
		dprintk("%s: i/o error (ret == %i)\n", __FUNCTION__, ret);

	return (ret != 2) ? -1 : 0;
}


static
int sl1935_set_tv_freq (struct dvb_i2c_bus *i2c, u32 freq, int ftype)
{
	u8 buf[4];
	u32 div;

	u32 ratios[] = { 2000, 1000, 500, 250, 125 };
	u8 ratio;

	for (ratio = 4; ratio > 0; ratio--)
		if ((freq / ratios[ratio]) <= 0x3fff)
			break;

	div = freq / ratios[ratio];

	buf[0] = (freq >> 8) & 0x7f;
	buf[1] = freq & 0xff;
	buf[2] = 0x80 | ratio;

	if (freq < 1531000)
		buf[3] = 0x10;
	else
		buf[3] = 0x00;

	return pll_write (i2c, buf, ftype);
}

/**
 *   set up the downconverter frequency divisor for a 
 *   reference clock comparision frequency of 125 kHz.
 */
static
int tsa5059_set_tv_freq	(struct dvb_i2c_bus *i2c, u32 freq, int ftype)
{
        u32 div = freq / 125;

	u8 buf[4] = { (div >> 8) & 0x7f, div & 0xff, 0x84 };

	if (ftype == PHILIPS_SU1278SH)
		/* activate f_xtal/f_comp signal output */
		/* charge pump current C0/C1 = 00 */
		buf[3] = 0x20;
	else
		buf[3] = freq > 1530000 ? 0xc0 : 0xc4;

	dprintk ("%s\n", __FUNCTION__);

	return pll_write (i2c, buf, ftype);
}

static
int pll_set_tv_freq (struct dvb_i2c_bus *i2c, u32 freq, int ftype)
{
	if (ftype == LG_TDQF_S001F)
		return sl1935_set_tv_freq(i2c, freq, ftype);
	else
		return tsa5059_set_tv_freq(i2c, freq, ftype);
}

#if 0
static
int tsa5059_read_status	(struct dvb_i2c_bus *i2c)
{
	int ret;
	u8 rpt1 [] = { 0x05, 0xb5 };
	u8 stat [] = { 0 };

	struct i2c_msg msg [] = {{ addr: 0x68, flags: 0, buf: rpt1, len: 2 },
			  { addr: 0x60, flags: I2C_M_RD, buf: stat, len: 1 }};

	dprintk ("%s\n", __FUNCTION__);

	ret = i2c->xfer (i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return stat[0];
}
#endif


static
int stv0299_init (struct dvb_i2c_bus *i2c, int ftype)
{
	int i;

	dprintk("stv0299: init chip\n");

	for (i=0; i<sizeof(init_tab); i+=2)
		stv0299_writereg (i2c, init_tab[i], init_tab[i+1]);

        /* AGC1 reference register setup */
	if (ftype == PHILIPS_SU1278SH)
	  stv0299_writereg (i2c, 0x0f, 0xd2);  /* Iagc = Inverse, m1 = 18 */
	else
	  stv0299_writereg (i2c, 0x0f, 0x52);  /* Iagc = Normal,  m1 = 18 */

	return 0;
}


static
int stv0299_check_inversion (struct dvb_i2c_bus *i2c)
{
	dprintk ("%s\n", __FUNCTION__);

	if ((stv0299_readreg (i2c, 0x1b) & 0x98) != 0x98) {
		u8 val = stv0299_readreg (i2c, 0x0c);
		return stv0299_writereg (i2c, 0x0c, val ^ 0x01);
	}

	return 0;
}


static
int stv0299_set_FEC (struct dvb_i2c_bus *i2c, fe_code_rate_t fec)
{
	dprintk ("%s\n", __FUNCTION__);

	switch (fec) {
	case FEC_AUTO:
		return stv0299_writereg (i2c, 0x31, 0x1f);
	case FEC_1_2:
		return stv0299_writereg (i2c, 0x31, 0x01);
	case FEC_2_3:
		return stv0299_writereg (i2c, 0x31, 0x02);
	case FEC_3_4:
		return stv0299_writereg (i2c, 0x31, 0x04);
	case FEC_5_6:
		return stv0299_writereg (i2c, 0x31, 0x08);
	case FEC_7_8:
		return stv0299_writereg (i2c, 0x31, 0x10);
	default:
		return -EINVAL;
	}
}


static
fe_code_rate_t stv0299_get_fec (struct dvb_i2c_bus *i2c)
{
	static fe_code_rate_t fec_tab [] = { FEC_2_3, FEC_3_4, FEC_5_6,
					     FEC_7_8, FEC_1_2 };
	u8 index;

	dprintk ("%s\n", __FUNCTION__);

	index = stv0299_readreg (i2c, 0x1b);
	index &= 0x7;

	if (index > 4)
		return FEC_AUTO;

	return fec_tab [index];
}


static
int stv0299_wait_diseqc_fifo (struct dvb_i2c_bus *i2c, int timeout)
{
	unsigned long start = jiffies;

	dprintk ("%s\n", __FUNCTION__);

	while (stv0299_readreg(i2c, 0x0a) & 1) {
		if (jiffies - start > timeout) {
			dprintk ("%s: timeout!!\n", __FUNCTION__);
			return -ETIMEDOUT;
		}
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout (1);
	};

	return 0;
}


static
int stv0299_wait_diseqc_idle (struct dvb_i2c_bus *i2c, int timeout)
{
	unsigned long start = jiffies;

	dprintk ("%s\n", __FUNCTION__);

	while ((stv0299_readreg(i2c, 0x0a) & 3) != 2 ) {
		if (jiffies - start > timeout) {
			dprintk ("%s: timeout!!\n", __FUNCTION__);
			return -ETIMEDOUT;
		}
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout (1);
	};

	return 0;
}


static
int stv0299_send_diseqc_msg (struct dvb_i2c_bus *i2c,
			     struct dvb_diseqc_master_cmd *m)
{
	u8 val;
	int i;

	dprintk ("%s\n", __FUNCTION__);

	if (stv0299_wait_diseqc_idle (i2c, 100) < 0)
		return -ETIMEDOUT;

	val = stv0299_readreg (i2c, 0x08);

	if (stv0299_writereg (i2c, 0x08, (val & ~0x7) | 0x6))  /* DiSEqC mode */
		return -EREMOTEIO;

	for (i=0; i<m->msg_len; i++) {
		if (stv0299_wait_diseqc_fifo (i2c, 100) < 0)
			return -ETIMEDOUT;

		if (stv0299_writereg (i2c, 0x09, m->msg[i]))
			return -EREMOTEIO;
	}

	if (stv0299_wait_diseqc_idle (i2c, 100) < 0)
		return -ETIMEDOUT;

	return 0;
}


static
int stv0299_send_diseqc_burst (struct dvb_i2c_bus *i2c, fe_sec_mini_cmd_t burst)
{
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	if (stv0299_wait_diseqc_idle (i2c, 100) < 0)
		return -ETIMEDOUT;

	val = stv0299_readreg (i2c, 0x08);

	if (stv0299_writereg (i2c, 0x08, (val & ~0x7) | 0x2))   /* burst mode */
		return -EREMOTEIO;

	if (stv0299_writereg (i2c, 0x09, burst == SEC_MINI_A ? 0x00 : 0xff))
		return -EREMOTEIO;

	if (stv0299_wait_diseqc_idle (i2c, 100) < 0)
		return -ETIMEDOUT;

	if (stv0299_writereg (i2c, 0x08, val))
		return -EREMOTEIO;

	return 0;
}


static
int stv0299_set_tone (struct dvb_i2c_bus *i2c, fe_sec_tone_mode_t tone)
{
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	if (stv0299_wait_diseqc_idle (i2c, 100) < 0)
		return -ETIMEDOUT;

	val = stv0299_readreg (i2c, 0x08);

	switch (tone) {
	case SEC_TONE_ON:
		return stv0299_writereg (i2c, 0x08, val | 0x3);
	case SEC_TONE_OFF:
		return stv0299_writereg (i2c, 0x08, (val & ~0x3) | 0x02);
	default:
		return -EINVAL;
	};
}


static
int stv0299_set_voltage (struct dvb_i2c_bus *i2c, fe_sec_voltage_t voltage)
{
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	val = stv0299_readreg (i2c, 0x0c);
	val &= 0x0f;
	val |= 0x40;   /* LNB power on */

	switch (voltage) {
	case SEC_VOLTAGE_13:
		return stv0299_writereg (i2c, 0x0c, val);
	case SEC_VOLTAGE_18:
		return stv0299_writereg (i2c, 0x0c, val | 0x10);
	default:
		return -EINVAL;
	};
}


static
int stv0299_set_symbolrate (struct dvb_i2c_bus *i2c, u32 srate)
{
	u32 ratio;
	u32 tmp;
	u8 aclk = 0xb4, bclk = 0x51;

	if (srate > M_CLK)
		srate = M_CLK;
        if (srate < 500000)
		srate = 500000;

	if (srate < 30000000) { aclk = 0xb6; bclk = 0x53; }
	if (srate < 14000000) { aclk = 0xb7; bclk = 0x53; }
	if (srate < 7000000) { aclk = 0xb7; bclk = 0x4f; }
	if (srate < 3000000) { aclk = 0xb7; bclk = 0x4b; }
	if (srate < 1500000) { aclk = 0xb7; bclk = 0x47; }

#define FIN (M_CLK >> 4)

	tmp = srate << 4;
	ratio = tmp / FIN;
        
	tmp = (tmp % FIN) << 8;
	ratio = (ratio << 8) + tmp / FIN;
        
	tmp = (tmp % FIN) << 8;
	ratio = (ratio << 8) + tmp / FIN;
  
	stv0299_writereg (i2c, 0x13, aclk);
	stv0299_writereg (i2c, 0x14, bclk);
	stv0299_writereg (i2c, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg (i2c, 0x20, (ratio >>  8) & 0xff);
	stv0299_writereg (i2c, 0x21, (ratio      ) & 0xf0);

	return 0;
}


static
int stv0299_get_symbolrate (struct dvb_i2c_bus *i2c)
{
	u32 Mclk = M_CLK / 4096L;
	u32 srate;
	s32 offset;
	u8 sfr[3];
	s8 rtf;

	dprintk ("%s\n", __FUNCTION__);

	stv0299_readregs (i2c, 0x1f, sfr, 3);
	stv0299_readregs (i2c, 0x1a, &rtf, 1);

	srate = (sfr[0] << 8) | sfr[1];
	srate *= Mclk;
	srate /= 16;
	srate += (sfr[2] >> 4) * Mclk / 256;

	offset = (s32) rtf * (srate / 4096L);
	offset /= 128;

	srate += offset;

	srate += 1000;
	srate /= 2000;
	srate *= 2000;

	return srate;
}


static
int uni0299_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
        int tuner_type = (int)fe->data;
	struct dvb_i2c_bus *i2c = fe->i2c;

	dprintk ("%s\n", __FUNCTION__);

	switch (cmd) {
	case FE_GET_INFO:
		memcpy (arg, &uni0299_info, sizeof(struct dvb_frontend_info));
		break;

	case FE_READ_STATUS:
	{
		fe_status_t *status = (fe_status_t *) arg;
		u8 signal = 0xff - stv0299_readreg (i2c, 0x18);
		u8 sync = stv0299_readreg (i2c, 0x1b);

		dprintk ("VSTATUS: 0x%02x\n", sync);

		*status = 0;

		if (signal > 10)
			*status |= FE_HAS_SIGNAL;

		if (sync & 0x80)
			*status |= FE_HAS_CARRIER;

		if (sync & 0x10)
			*status |= FE_HAS_VITERBI;

		if (sync & 0x08)
			*status |= FE_HAS_SYNC;

		if ((sync & 0x98) == 0x98)
			*status |= FE_HAS_LOCK;

		break;
	}

        case FE_READ_BER:
		*((u32*) arg) = (stv0299_readreg (i2c, 0x1d) << 8)
			       | stv0299_readreg (i2c, 0x1e);
		break;

	case FE_READ_SIGNAL_STRENGTH:
	{
		s32 signal =  0xffff - ((stv0299_readreg (i2c, 0x18) << 8)
			               | stv0299_readreg (i2c, 0x19));

		dprintk ("AGC2I: 0x%02x%02x, signal=0x%04x\n",
			 stv0299_readreg (i2c, 0x18),
			 stv0299_readreg (i2c, 0x19), signal);

		signal = signal * 5 / 4;
		*((u16*) arg) = (signal > 0xffff) ? 0xffff :
				(signal < 0) ? 0 : signal;
		break;
	}
        case FE_READ_SNR:
	{
		s32 snr = 0xffff - ((stv0299_readreg (i2c, 0x24) << 8)
			           | stv0299_readreg (i2c, 0x25));
		snr = 3 * (snr - 0xa100);
		*((u16*) arg) = (snr > 0xffff) ? 0xffff :
				(snr < 0) ? 0 : snr;
		break;
	}
	case FE_READ_UNCORRECTED_BLOCKS: 
		*((u32*) arg) = 0;    /* the stv0299 can't measure BER and */
		return -EOPNOTSUPP;   /* errors at the same time.... */

        case FE_SET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;

		pll_set_tv_freq (i2c, p->frequency, tuner_type);
                stv0299_set_FEC (i2c, p->u.qpsk.fec_inner);
                stv0299_set_symbolrate (i2c, p->u.qpsk.symbol_rate);
		stv0299_check_inversion (i2c);
		/* pll_set_tv_freq (i2c, p->frequency, tuner_type); */
		stv0299_writereg (i2c, 0x22, 0x00);
		stv0299_writereg (i2c, 0x23, 0x00);
		stv0299_readreg (i2c, 0x23);
		stv0299_writereg (i2c, 0x12, 0xb9);

		/* printk ("%s: tsa5059 status: %x\n", __FUNCTION__, tsa5059_read_status(i2c)); */
                break;
        }

        case FE_GET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;
		s32 derot_freq;

		derot_freq = (s32)(s16) ((stv0299_readreg (i2c, 0x22) << 8)
					| stv0299_readreg (i2c, 0x23));

		derot_freq *= (M_CLK >> 16);
		derot_freq += 500;
		derot_freq /= 1000;

		p->frequency += derot_freq;
		p->inversion = (stv0299_readreg (i2c, 0x0c) & 1) ?
						INVERSION_OFF : INVERSION_ON;
		p->u.qpsk.fec_inner = stv0299_get_fec (i2c);
		p->u.qpsk.symbol_rate = stv0299_get_symbolrate (i2c);
                break;
        }

        case FE_SLEEP:
		stv0299_writereg (i2c, 0x0c, 0x00);  /*  LNB power off! */
		stv0299_writereg (i2c, 0x02, 0x80);
		break;

        case FE_INIT:
		return stv0299_init (i2c, tuner_type);

        case FE_RESET:
		stv0299_writereg (i2c, 0x22, 0x00);
		stv0299_writereg (i2c, 0x23, 0x00);
		stv0299_readreg (i2c, 0x23);
		stv0299_writereg (i2c, 0x12, 0xb9);
                break;

	case FE_DISEQC_SEND_MASTER_CMD:
		return stv0299_send_diseqc_msg (i2c, arg);

	case FE_DISEQC_SEND_BURST:
		return stv0299_send_diseqc_burst (i2c, (fe_sec_mini_cmd_t) arg);

	case FE_SET_TONE:
		return stv0299_set_tone (i2c, (fe_sec_tone_mode_t) arg);

	case FE_SET_VOLTAGE:
		return stv0299_set_voltage (i2c, (fe_sec_voltage_t) arg);

	default:
		return -EOPNOTSUPP;
	};

	return 0;
}

static
int probe_tuner (struct dvb_i2c_bus *i2c)
{
	int type;

        /* read the status register of TSA5059 */
	u8 rpt[] = { 0x05, 0xb5 };
        u8 stat [] = { 0 };
	struct i2c_msg msg1 [] = {{ addr: 0x68, flags: 0, buf: rpt,  len: 2 },
                           { addr: 0x60, flags: I2C_M_RD, buf: stat, len: 1 }};
	struct i2c_msg msg2 [] = {{ addr: 0x68, flags: 0, buf: rpt,  len: 2 },
                           { addr: 0x61, flags: I2C_M_RD, buf: stat, len: 1 }};

	if (i2c->xfer(i2c, msg1, 2) == 2) {
		type = PHILIPS_SU1278SH;
		printk ("%s: setup for tuner SU1278/SH\n", __FILE__);
	} else if (i2c->xfer(i2c, msg2, 2) == 2) {
if (0) { //		if ((stat[0] & 0x3f) == 0) {
			type = LG_TDQF_S001F;
			printk ("%s: setup for tuner TDQF-S001F\n", __FILE__);
		}
		else {
			type = ALPS_BSRU6;
			printk ("%s: setup for tuner BSRU6, TDQB-S00x\n", __FILE__);
		}
	} else {
		type = UNKNOWN_FRONTEND;
		printk ("%s: unknown PLL synthesizer, "
			"please report to <linuxdvb@linuxtv.org>!!\n",
			__FILE__);
	}
	return type;
}


static
int uni0299_attach (struct dvb_i2c_bus *i2c)
{
        int tuner_type;
	u8 id = stv0299_readreg (i2c, 0x00);

	dprintk ("%s\n", __FUNCTION__);

	/* register 0x00 contains 0xa1 for STV0299 and STV0299B */
	/* register 0x00 might contain 0x80 when returning from standby */
	if (id != 0xa1 && id != 0x80)
		return -ENODEV;

	if ((tuner_type = probe_tuner(i2c)) < 0)
		return -ENODEV;

	dvb_register_frontend (uni0299_ioctl, i2c, (void*) tuner_type, 
			       &uni0299_info);

	return 0;
}


static
void uni0299_detach (struct dvb_i2c_bus *i2c)
{
	dprintk ("%s\n", __FUNCTION__);

	dvb_unregister_frontend (uni0299_ioctl, i2c);
}


static
int __init init_uni0299 (void)
{
	dprintk ("%s\n", __FUNCTION__);

	return dvb_register_i2c_device (THIS_MODULE, uni0299_attach, uni0299_detach);
}


static 
void __exit exit_uni0299 (void)
{
	dprintk ("%s\n", __FUNCTION__);

	dvb_unregister_i2c_device (uni0299_attach);
}

module_init (init_uni0299);
module_exit (exit_uni0299);

MODULE_PARM(debug,"i");
MODULE_PARM_DESC(debug, "enable verbose debug messages");

MODULE_DESCRIPTION("Universal STV0299/TSA5059/SL1935 DVB Frontend driver");
MODULE_AUTHOR("Ralph Metzler, Holger Waechtler, Peter Schildmann, Felix Domke, Andreas Oberritter");
MODULE_LICENSE("GPL");

