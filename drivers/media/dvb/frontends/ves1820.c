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

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "dvb_functions.h"


#if 0
#define dprintk(x...) printk(x)
#else
#define dprintk(x...)
#endif

#define MAX_UNITS 4
static int pwm[MAX_UNITS] = { -1, -1, -1, -1 };
static int verbose;

/**
 *  since we need only a few bits to store internal state we don't allocate
 *  extra memory but use frontend->data as bitfield
 */

#define SET_PWM(data,pwm) do { 		\
	(long) data &= ~0xff; 		\
	(long) data |= pwm; 		\
} while (0)

#define SET_REG0(data,reg0) do {	\
	(long) data &= ~(0xff << 8); 	\
	(long) data |= reg0 << 8; 	\
} while (0)

#define SET_TUNER(data,type) do {	\
	(long) data &= ~(0xff << 16); 	\
	(long) data |= type << 16;	\
} while (0)

#define SET_DEMOD_ADDR(data,type) do {	\
	(long) data &= ~(0xff << 24); 	\
	(long) data |= type << 24;	\
} while (0)

#define GET_PWM(data) ((u8) ((long) data & 0xff))
#define GET_REG0(data) ((u8) (((long) data >> 8) & 0xff))
#define GET_TUNER(data) ((u8) (((long) data >> 16) & 0xff))
#define GET_DEMOD_ADDR(data) ((u8) (((long) data >> 24) & 0xff))

#define XIN 57840000UL
#define FIN (XIN >> 4)



static struct dvb_frontend_info ves1820_info = {
	.name = "VES1820 based DVB-C frontend",
	.type = FE_QAM,
	.frequency_stepsize = 62500,
	.frequency_min = 51000000,
	.frequency_max = 858000000,
	.symbol_rate_min = (XIN/2)/64,     /* SACLK/64 == (XIN/2)/64 */
	.symbol_rate_max = (XIN/2)/4,      /* SACLK/4 */
#if 0
	.frequency_tolerance = ???,
	.symbol_rate_tolerance = ???,  /* ppm */  /* == 8% (spec p. 5) */
	.notifier_delay = ?,
#endif
	.caps = FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 |
		FE_CAN_QAM_128 | FE_CAN_QAM_256 | 
		FE_CAN_FEC_AUTO | FE_CAN_INVERSION_AUTO |
		FE_CAN_CLEAN_SETUP | FE_CAN_RECOVER
};



static u8 ves1820_inittab [] =
{
	0x69, 0x6A, 0x9B, 0x12, 0x12, 0x46, 0x26, 0x1A,
	0x43, 0x6A, 0xAA, 0xAA, 0x1E, 0x85, 0x43, 0x28,
	0xE0, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x40
};


static int ves1820_writereg (struct dvb_frontend *fe, u8 reg, u8 data)
{
	u8 addr = GET_DEMOD_ADDR(fe->data);
        u8 buf[] = { 0x00, reg, data };
	struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = buf, .len = 3 };
	struct dvb_i2c_bus *i2c = fe->i2c;
        int ret;

	ret = i2c->xfer (i2c, &msg, 1);

	if (ret != 1)
		printk("DVB: VES1820(%d): %s, writereg error "
			"(reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			fe->i2c->adapter->num, __FUNCTION__, reg, data, ret);

	dvb_delay(10);
	return (ret != 1) ? -EREMOTEIO : 0;
}


static u8 ves1820_readreg (struct dvb_frontend *fe, u8 reg)
{
	u8 b0 [] = { 0x00, reg };
	u8 b1 [] = { 0 };
	u8 addr = GET_DEMOD_ADDR(fe->data);
	struct i2c_msg msg [] = { { .addr = addr, .flags = 0, .buf = b0, .len = 2 },
	                   { .addr = addr, .flags = I2C_M_RD, .buf = b1, .len = 1 } };
	struct dvb_i2c_bus *i2c = fe->i2c;
	int ret;

	ret = i2c->xfer (i2c, msg, 2);

	if (ret != 2)
		printk("DVB: VES1820(%d): %s: readreg error (ret == %i)\n",
				fe->i2c->adapter->num, __FUNCTION__, ret);

	return b1[0];
}


static int tuner_write (struct dvb_i2c_bus *i2c, u8 addr, u8 data [4])
{
        int ret;
        struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = data, .len = 4 };

        ret = i2c->xfer (i2c, &msg, 1);

        if (ret != 1)
                printk("DVB: VES1820(%d): %s: i/o error (ret == %i)\n",
				i2c->adapter->num, __FUNCTION__, ret);

        return (ret != 1) ? -EREMOTEIO : 0;
}


/**
 *   set up the downconverter frequency divisor for a
 *   reference clock comparision frequency of 62.5 kHz.
 */
static int tuner_set_tv_freq (struct dvb_frontend *fe, u32 freq)
{
        u32 div, ifreq;
	static u8 addr [] = { 0x61, 0x62 };
	static u8 byte3 [] = { 0x8e, 0x85 };
	int tuner_type = GET_TUNER(fe->data);
        u8 buf [4];

	if (tuner_type == 0xff)     /*  PLL not reachable over i2c ...  */
		return 0;

	if (strstr (fe->i2c->adapter->name, "Technotrend") ||
	    strstr (fe->i2c->adapter->name, "TT-Budget"))
		ifreq = 35937500;
	else
		ifreq = 36125000;

	div = (freq + ifreq + 31250) / 62500;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = byte3[tuner_type];

	if (tuner_type == 1) {
		buf[2] |= (div >> 10) & 0x60;
		buf[3] = (freq < 174000000 ? 0x88 :
			  freq < 470000000 ? 0x84 : 0x81);
	} else {
		buf[3] = (freq < 174000000 ? 0xa1 :
			  freq < 454000000 ? 0x92 : 0x34);
	}

        return tuner_write (fe->i2c, addr[tuner_type], buf);
}


static int ves1820_setup_reg0 (struct dvb_frontend *fe, u8 reg0,
			fe_spectral_inversion_t inversion)
{
	reg0 |= GET_REG0(fe->data) & 0x62;
	
	if (INVERSION_ON == inversion)
		reg0 &= ~0x20;
	else if (INVERSION_OFF == inversion)
		reg0 |= 0x20;
	
	ves1820_writereg (fe, 0x00, reg0 & 0xfe);
        ves1820_writereg (fe, 0x00, reg0 | 0x01);

	/**
	 *  check lock and toggle inversion bit if required...
	 */
	if (INVERSION_AUTO == inversion && !(ves1820_readreg (fe, 0x11) & 0x08)) {
		dvb_delay(10);
		if (!(ves1820_readreg (fe, 0x11) & 0x08)) {
			reg0 ^= 0x20;
			ves1820_writereg (fe, 0x00, reg0 & 0xfe);
        		ves1820_writereg (fe, 0x00, reg0 | 0x01);
		}
	}

	SET_REG0(fe->data, reg0);

	return 0;
}


static int ves1820_init (struct dvb_frontend *fe)
{
	int i;
        
        dprintk("DVB: VES1820(%d): init chip\n", fe->i2c->adapter->num);

        ves1820_writereg (fe, 0, 0);

	for (i=0; i<53; i++)
                ves1820_writereg (fe, i, ves1820_inittab[i]);

	ves1820_writereg (fe, 0x34, GET_PWM(fe->data)); 

	return 0;
}


static int ves1820_set_symbolrate (struct dvb_frontend *fe, u32 symbolrate)
{
        s32 BDR; 
        s32 BDRI;
        s16 SFIL=0;
        u16 NDEC = 0;
        u32 tmp, ratio;

        if (symbolrate > XIN/2) 
                symbolrate = XIN/2;

	if (symbolrate < 500000)
                symbolrate = 500000;

        if (symbolrate < XIN/16) NDEC = 1;
        if (symbolrate < XIN/32) NDEC = 2;
        if (symbolrate < XIN/64) NDEC = 3;

        if (symbolrate < (u32)(XIN/12.3)) SFIL = 1;
        if (symbolrate < (u32)(XIN/16))	 SFIL = 0;
        if (symbolrate < (u32)(XIN/24.6)) SFIL = 1;
        if (symbolrate < (u32)(XIN/32))	 SFIL = 0;
        if (symbolrate < (u32)(XIN/49.2)) SFIL = 1;
        if (symbolrate < (u32)(XIN/64))	 SFIL = 0;
        if (symbolrate < (u32)(XIN/98.4)) SFIL = 1;
        
        symbolrate <<= NDEC;
        ratio = (symbolrate << 4) / FIN;
        tmp =  ((symbolrate << 4) % FIN) << 8;
        ratio = (ratio << 8) + tmp / FIN;
        tmp = (tmp % FIN) << 8;
        ratio = (ratio << 8) + (tmp + FIN/2) / FIN;
        
        BDR = ratio;
        BDRI = (((XIN << 5) / symbolrate) + 1) / 2;
        
        if (BDRI > 0xFF) 
                BDRI = 0xFF;
        
        SFIL = (SFIL << 4) | ves1820_inittab[0x0E];
        
        NDEC = (NDEC << 6) | ves1820_inittab[0x03];

        ves1820_writereg (fe, 0x03, NDEC);
        ves1820_writereg (fe, 0x0a, BDR&0xff);
        ves1820_writereg (fe, 0x0b, (BDR>> 8)&0xff);
        ves1820_writereg (fe, 0x0c, (BDR>>16)&0x3f);

        ves1820_writereg (fe, 0x0d, BDRI);
        ves1820_writereg (fe, 0x0e, SFIL);

        return 0;
}


static int ves1820_set_parameters (struct dvb_frontend *fe,
			    struct dvb_frontend_parameters *p)
{
	static const u8 reg0x00 [] = { 0x00, 0x04, 0x08, 0x0c, 0x10 };
	static const u8 reg0x01 [] = {  140,  140,  106,  100,   92 };
	static const u8 reg0x05 [] = {  135,  100,   70,   54,   38 };
	static const u8 reg0x08 [] = {  162,  116,   67,   52,   35 };
	static const u8 reg0x09 [] = {  145,  150,  106,  126,  107 };
	int real_qam = p->u.qam.modulation - QAM_16;

	if (real_qam < 0 || real_qam > 4)
		return -EINVAL;

	tuner_set_tv_freq (fe, p->frequency);
	ves1820_set_symbolrate (fe, p->u.qam.symbol_rate);
	ves1820_writereg (fe, 0x34, GET_PWM(fe->data));

        ves1820_writereg (fe, 0x01, reg0x01[real_qam]);
        ves1820_writereg (fe, 0x05, reg0x05[real_qam]);
        ves1820_writereg (fe, 0x08, reg0x08[real_qam]);
        ves1820_writereg (fe, 0x09, reg0x09[real_qam]);

	ves1820_setup_reg0 (fe, reg0x00[real_qam], p->inversion);

	return 0;
}



static int ves1820_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
        switch (cmd) {
	case FE_GET_INFO:
		memcpy (arg, &ves1820_info, sizeof(struct dvb_frontend_info));
		break;

        case FE_READ_STATUS:
	{
		fe_status_t *status = (fe_status_t *) arg;
		int sync;

		*status = 0;

                sync = ves1820_readreg (fe, 0x11);

		if (sync & 2)
			*status |= FE_HAS_SIGNAL;

		if (sync & 2)
			*status |= FE_HAS_CARRIER;

		if (sync & 2)           /* XXX FIXME! */
			*status |= FE_HAS_VITERBI;
		
		if (sync & 4)
			*status |= FE_HAS_SYNC;

		if (sync & 8)
			*status |= FE_HAS_LOCK;

		break;
	}

	case FE_READ_BER:
	{
		u32 ber = ves1820_readreg(fe, 0x14) |
			 (ves1820_readreg(fe, 0x15) << 8) |
			 ((ves1820_readreg(fe, 0x16) & 0x0f) << 16);
		*((u32*) arg) = 10 * ber;
		break;
	}
	case FE_READ_SIGNAL_STRENGTH:
	{
		u8 gain = ves1820_readreg(fe, 0x17);
		*((u16*) arg) = (gain << 8) | gain;
		break;
	}

	case FE_READ_SNR:
	{
		u8 quality = ~ves1820_readreg(fe, 0x18);
		*((u16*) arg) = (quality << 8) | quality;
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS:
		*((u32*) arg) = ves1820_readreg (fe, 0x13) & 0x7f;
		if (*((u32*) arg) == 0x7f)
			*((u32*) arg) = 0xffffffff;
		/* reset uncorrected block counter */
		ves1820_writereg (fe, 0x10, ves1820_inittab[0x10] & 0xdf);
	        ves1820_writereg (fe, 0x10, ves1820_inittab[0x10]);
		break;

        case FE_SET_FRONTEND:
		return ves1820_set_parameters (fe, arg);

	case FE_GET_FRONTEND:
	{
		struct dvb_frontend_parameters *p = (struct dvb_frontend_parameters *)arg;
		u8 reg0 = GET_REG0(fe->data);
		int sync;
		s8 afc = 0;
                
                sync = ves1820_readreg (fe, 0x11);
		if (sync & 2)
			/* AFC only valid when carrier has been recovered */
			afc = ves1820_readreg(fe, 0x19);
		if (verbose)
			printk ("DVB: VES1820(%d): AFC (%d) %dHz\n",
					fe->i2c->adapter->num, afc,
				-((s32)(p->u.qam.symbol_rate >> 3) * afc >> 7));

		p->inversion = reg0 & 0x20 ? INVERSION_OFF : INVERSION_ON;
		p->u.qam.modulation = ((reg0 >> 2) & 7) + QAM_16;

		p->u.qam.fec_inner = FEC_NONE;

		p->frequency = ((p->frequency + 31250) / 62500) * 62500;
		// To prevent overflow, shift symbol rate first a
		// couple of bits.
		p->frequency -= (s32)(p->u.qam.symbol_rate >> 3) * afc >> 7;
		break;
	}
	case FE_SLEEP:
		ves1820_writereg (fe, 0x1b, 0x02);  /* pdown ADC */
		ves1820_writereg (fe, 0x00, 0x80);  /* standby */
		break;

        case FE_INIT:
                return ves1820_init (fe);

        default:
                return -EINVAL;
        }

        return 0;
} 


static long probe_tuner (struct dvb_i2c_bus *i2c)
{
	static const struct i2c_msg msg1 = 
		{ .addr = 0x61, .flags = 0, .buf = NULL, .len = 0 };
	static const struct i2c_msg msg2 =
		{ .addr = 0x62, .flags = 0, .buf = NULL, .len = 0 };
	int type;

	if (i2c->xfer(i2c, &msg1, 1) == 1) {
		type = 0;
		printk ("DVB: VES1820(%d): setup for tuner spXXXX\n", i2c->adapter->num);
	} else if (i2c->xfer(i2c, &msg2, 1) == 1) {
		type = 1;
		printk ("DVB: VES1820(%d): setup for tuner sp5659c\n", i2c->adapter->num);
	} else {
		type = -1;
		printk ("DVB: VES1820(%d): unknown PLL, "
			"please report to <linuxdvb@linuxtv.org>!!\n", i2c->adapter->num);
	}

	return type;
}


static u8 read_pwm (struct dvb_i2c_bus *i2c)
{
	u8 b = 0xff;
	u8 pwm;
	struct i2c_msg msg [] = { { .addr = 0x50, .flags = 0, .buf = &b, .len = 1 },
			 { .addr = 0x50, .flags = I2C_M_RD, .buf = &pwm, .len = 1 } };

	i2c->xfer (i2c, msg, 2);

	printk("DVB: VES1820(%d): pwm=0x%02x\n", i2c->adapter->num, pwm);

	if (pwm == 0xff)
		pwm = 0x48;

	return pwm;
}


static long probe_demod_addr (struct dvb_i2c_bus *i2c)
{
	u8 b [] = { 0x00, 0x1a };
	u8 id;
	struct i2c_msg msg [] = { { .addr = 0x08, .flags = 0, .buf = b, .len = 2 },
	                   { .addr = 0x08, .flags = I2C_M_RD, .buf = &id, .len = 1 } };

	if (i2c->xfer(i2c, msg, 2) == 2 && (id & 0xf0) == 0x70)
		return msg[0].addr;

	msg[0].addr = msg[1].addr = 0x09;

	if (i2c->xfer(i2c, msg, 2) == 2 && (id & 0xf0) == 0x70)
		return msg[0].addr;

	return -1;
}


static int ves1820_attach (struct dvb_i2c_bus *i2c, void **data)
{
	void *priv = NULL;
	long demod_addr;
	long tuner_type;

	if ((demod_addr = probe_demod_addr(i2c)) < 0)
		return -ENODEV;

	if ((tuner_type = probe_tuner(i2c)) < 0)
		return -ENODEV;

	if ((i2c->adapter->num < MAX_UNITS) && pwm[i2c->adapter->num] != -1) {
		printk("DVB: VES1820(%d): pwm=0x%02x (user specified)\n",
				i2c->adapter->num, pwm[i2c->adapter->num]);
		SET_PWM(priv, pwm[i2c->adapter->num]);
	}
	else
		SET_PWM(priv, read_pwm(i2c));
	SET_REG0(priv, ves1820_inittab[0]);
	SET_TUNER(priv, tuner_type);
	SET_DEMOD_ADDR(priv, demod_addr);

	return dvb_register_frontend (ves1820_ioctl, i2c, priv, &ves1820_info);
}


static void ves1820_detach (struct dvb_i2c_bus *i2c, void *data)
{
	dvb_unregister_frontend (ves1820_ioctl, i2c);
}


static int __init init_ves1820 (void)
{
	int i;
	for (i = 0; i < MAX_UNITS; i++)
		if (pwm[i] < -1 || pwm[i] > 255)
			return -EINVAL;
	return dvb_register_i2c_device (THIS_MODULE,
					ves1820_attach, ves1820_detach);
}


static void __exit exit_ves1820 (void)
{
	dvb_unregister_i2c_device (ves1820_attach);
}


module_init(init_ves1820);
module_exit(exit_ves1820);

MODULE_PARM(pwm, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM_DESC(pwm, "override PWM value stored in EEPROM (tuner calibration)");
MODULE_PARM(verbose, "i");
MODULE_PARM_DESC(verbose, "print AFC offset after tuning for debugging the PWM setting");

MODULE_DESCRIPTION("VES1820 DVB-C frontend driver");
MODULE_AUTHOR("Ralph Metzler, Holger Waechtler");
MODULE_LICENSE("GPL");

