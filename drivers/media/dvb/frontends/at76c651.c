/*
 * at76c651.c
 * 
 * Atmel DVB-C Frontend Driver (at76c651/dat7021)
 *
 * Copyright (C) 2001 fnbrd <fnbrd@gmx.de>
 *             & 2002 Andreas Oberritter <andreas@oberritter.de>
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
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>

#if defined(__powerpc__)
#include <asm/bitops.h>
#endif

#include "dvb_frontend.h"
#include "dvb_i2c.h"
#include "dvb_functions.h"

static int debug = 0;
static u8 at76c651_qam;
static u8 at76c651_revision;

#define dprintk	if (debug) printk

/*
 * DAT7021
 * -------
 * Input Frequency Range (RF): 48.25 MHz to 863.25 MHz
 * Band Width: 8 MHz
 * Level Input (Range for Digital Signals): -61 dBm to -41 dBm
 * Output Frequency (IF): 36 MHz
 *
 * (see http://www.atmel.com/atmel/acrobat/doc1320.pdf)
 */

static struct dvb_frontend_info at76c651_info = {

	.name = "Atmel AT76C651(B) with DAT7021",
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
	    FE_CAN_QAM_256 /* | FE_CAN_QAM_512 | FE_CAN_QAM_1024 */ |
	    FE_CAN_RECOVER | FE_CAN_CLEAN_SETUP | FE_CAN_MUTE_TS

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

static int at76c651_writereg(struct dvb_i2c_bus *i2c, u8 reg, u8 data)
{

	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = 0x1a >> 1, .flags = 0, .buf = buf, .len = 2 };

	ret = i2c->xfer(i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: writereg error "
			"(reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			__FUNCTION__, reg, data, ret);

	dvb_delay(10);

	return (ret != 1) ? -EREMOTEIO : 0;

}

static u8 at76c651_readreg(struct dvb_i2c_bus *i2c, u8 reg)
{

	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = { {.addr =  0x1a >> 1, .flags =  0, .buf =  b0, .len = 1},
			  {.addr =  0x1a >> 1, .flags =  I2C_M_RD, .buf =  b1, .len = 1} };

	ret = i2c->xfer(i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];

}

static int at76c651_reset(struct dvb_i2c_bus *i2c)
{

	return at76c651_writereg(i2c, 0x07, 0x01);

}

static int at76c651_disable_interrupts(struct dvb_i2c_bus *i2c)
{

	return at76c651_writereg(i2c, 0x0b, 0x00);

}

static int at76c651_set_auto_config(struct dvb_i2c_bus *i2c)
{

	/*
	 * Autoconfig
	 */

	at76c651_writereg(i2c, 0x06, 0x01);

	/*
	 * Performance optimizations, should be done after autoconfig
	 */

	at76c651_writereg(i2c, 0x10, 0x06);
	at76c651_writereg(i2c, 0x11, ((at76c651_qam == 5) || (at76c651_qam == 7)) ? 0x12 : 0x10);
	at76c651_writereg(i2c, 0x15, 0x28);
	at76c651_writereg(i2c, 0x20, 0x09);
	at76c651_writereg(i2c, 0x24, ((at76c651_qam == 5) || (at76c651_qam == 7)) ? 0xC0 : 0x90);
	at76c651_writereg(i2c, 0x30, 0x90);
	if (at76c651_qam == 5)
		at76c651_writereg(i2c, 0x35, 0x2A);

	/*
	 * Initialize A/D-converter
	 */

	if (at76c651_revision == 0x11) {
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

static int at76c651_set_bbfreq(struct dvb_i2c_bus *i2c)
{

	at76c651_writereg(i2c, 0x04, 0x3f);
	at76c651_writereg(i2c, 0x05, 0xee);

	return 0;

}

static int at76c651_switch_tuner_i2c(struct dvb_i2c_bus *i2c, u8 enable)
{

	if (enable)
		return at76c651_writereg(i2c, 0x0c, 0xc2 | 0x01);
	else
		return at76c651_writereg(i2c, 0x0c, 0xc2);

}

static int dat7021_write(struct dvb_i2c_bus *i2c, u32 tw)
{

	int ret;
	struct i2c_msg msg =
	    { .addr = 0xc2 >> 1, .flags = 0, .buf = (u8 *) & tw, .len = sizeof (tw) };

#ifdef __LITTLE_ENDIAN
	tw = __cpu_to_be32(tw);
#endif

	at76c651_switch_tuner_i2c(i2c, 1);

	ret = i2c->xfer(i2c, &msg, 1);

	at76c651_switch_tuner_i2c(i2c, 0);

	if (ret != 4)
		return -EFAULT;

	at76c651_reset(i2c);

	return 0;

}

static int dat7021_set_tv_freq(struct dvb_i2c_bus *i2c, u32 freq)
{

	u32 dw;

	freq /= 1000;

	if ((freq < 48250) || (freq > 863250))
		return -EINVAL;

	/*
	 * formula: dw=0x17e28e06+(freq-346000UL)/8000UL*0x800000
	 *      or: dw=0x4E28E06+(freq-42000) / 125 * 0x20000
	 */

	dw = (freq - 42000) * 4096;
	dw = dw / 125;
	dw = dw * 32;

	if (freq > 394000)
		dw += 0x4E28E85;
	else
		dw += 0x4E28E06;

	return dat7021_write(i2c, dw);

}

static int at76c651_set_symbolrate(struct dvb_i2c_bus *i2c, u32 symbolrate)
{

	u8 exponent;
	u32 mantissa;

	if (symbolrate > 9360000)
		return -EINVAL;

	/*
	 * FREF = 57800 kHz
	 * exponent = 10 + floor ( log2 ( symbolrate / FREF ) )
	 * mantissa = ( symbolrate / FREF) * ( 1 << ( 30 - exponent ) )
	 */

	exponent = __ilog2((symbolrate << 4) / 903125);
	mantissa = ((symbolrate / 3125) * (1 << (24 - exponent))) / 289;

	at76c651_writereg(i2c, 0x00, mantissa >> 13);
	at76c651_writereg(i2c, 0x01, mantissa >> 5);
	at76c651_writereg(i2c, 0x02, (mantissa << 3) | exponent);

	return 0;

}

static int at76c651_set_qam(struct dvb_i2c_bus *i2c, fe_modulation_t qam)
{

	switch (qam) {
	case QPSK:
		at76c651_qam = 0x02;
		break;
	case QAM_16:
		at76c651_qam = 0x04;
		break;
	case QAM_32:
		at76c651_qam = 0x05;
		break;
	case QAM_64:
		at76c651_qam = 0x06;
		break;
	case QAM_128:
		at76c651_qam = 0x07;
		break;
	case QAM_256:
		at76c651_qam = 0x08;
		break;
#if 0
	case QAM_512:
		at76c651_qam = 0x09;
		break;
	case QAM_1024:
		at76c651_qam = 0x0A;
		break;
#endif
	default:
		return -EINVAL;

	}

	return at76c651_writereg(i2c, 0x03, at76c651_qam);

}

static int at76c651_set_inversion(struct dvb_i2c_bus *i2c,
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

static int at76c651_set_parameters(struct dvb_i2c_bus *i2c,
			struct dvb_frontend_parameters *p)
{

	dat7021_set_tv_freq(i2c, p->frequency);
	at76c651_set_symbolrate(i2c, p->u.qam.symbol_rate);
	at76c651_set_inversion(i2c, p->inversion);
	at76c651_set_auto_config(i2c);

	return 0;

}

static int at76c651_set_defaults(struct dvb_i2c_bus *i2c)
{

	at76c651_set_symbolrate(i2c, 6900000);
	at76c651_set_qam(i2c, QAM_64);
	at76c651_set_bbfreq(i2c);
	at76c651_set_auto_config(i2c);

	return 0;

}

static int at76c651_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{

	switch (cmd) {

	case FE_GET_INFO:
		memcpy(arg, &at76c651_info, sizeof (struct dvb_frontend_info));
		break;

	case FE_READ_STATUS:
		{

			fe_status_t *status = (fe_status_t *) arg;
			u8 sync;

			/*
			 * Bits: FEC, CAR, EQU, TIM, AGC2, AGC1, ADC, PLL (PLL=0) 
			 */
			sync = at76c651_readreg(fe->i2c, 0x80);

			*status = 0;

			if (sync & (0x04 | 0x10))	/* AGC1 || TIM */
				*status |= FE_HAS_SIGNAL;

			if (sync & 0x10)	/* TIM */
				*status |= FE_HAS_CARRIER;

			if (sync & 0x80)	/* FEC */
				*status |= FE_HAS_VITERBI;

			if (sync & 0x40)	/* CAR */
				*status |= FE_HAS_SYNC;

			if ((sync & 0xF0) == 0xF0)	/* TIM && EQU && CAR && FEC */
				*status |= FE_HAS_LOCK;

			break;

		}

	case FE_READ_BER:
		{
			u32 *ber = (u32 *) arg;

			*ber = (at76c651_readreg(fe->i2c, 0x81) & 0x0F) << 16;
			*ber |= at76c651_readreg(fe->i2c, 0x82) << 8;
			*ber |= at76c651_readreg(fe->i2c, 0x83);
			*ber *= 10;

			break;
		}

	case FE_READ_SIGNAL_STRENGTH:
		{
			u8 gain = ~at76c651_readreg(fe->i2c, 0x91);

			*(u16 *) arg = (gain << 8) | gain;
			break;
		}

	case FE_READ_SNR:
		*(u16 *) arg =
		    0xFFFF -
		    ((at76c651_readreg(fe->i2c, 0x8F) << 8) |
		     at76c651_readreg(fe->i2c, 0x90));
		break;

	case FE_READ_UNCORRECTED_BLOCKS:
		*(u32 *) arg = at76c651_readreg(fe->i2c, 0x82);
		break;

	case FE_SET_FRONTEND:
		return at76c651_set_parameters(fe->i2c, arg);

	case FE_GET_FRONTEND:
		break;

	case FE_SLEEP:
		break;

	case FE_INIT:
		return at76c651_set_defaults(fe->i2c);

	case FE_RESET:
		return at76c651_reset(fe->i2c);

	default:
		return -ENOIOCTLCMD;
	}

	return 0;

}

static int at76c651_attach(struct dvb_i2c_bus *i2c, void **data)
{
	if ( (at76c651_readreg(i2c, 0x0E) != 0x65) ||
	     ( ( (at76c651_revision = at76c651_readreg(i2c, 0x0F)) & 0xFE) != 0x10) )
{
		dprintk("no AT76C651(B) found\n");
		return -ENODEV;
	}

	if (at76c651_revision == 0x10)
	{
		dprintk("AT76C651A found\n");
		strcpy(at76c651_info.name,"Atmel AT76C651A with DAT7021");
		}
	else
	{
		strcpy(at76c651_info.name,"Atmel AT76C651B with DAT7021");
		dprintk("AT76C651B found\n");
	}

	at76c651_set_defaults(i2c);

	return dvb_register_frontend(at76c651_ioctl, i2c, NULL, &at76c651_info);

}

static void at76c651_detach(struct dvb_i2c_bus *i2c, void *data)
{

	dvb_unregister_frontend(at76c651_ioctl, i2c);

}

static int __init at76c651_init(void)
{

	return dvb_register_i2c_device(THIS_MODULE, at76c651_attach,
				       at76c651_detach);

}

static void __exit at76c651_exit(void)
{

	dvb_unregister_i2c_device(at76c651_attach);

}

module_init(at76c651_init);
module_exit(at76c651_exit);

MODULE_DESCRIPTION("at76c651/dat7021 dvb-c frontend driver");
MODULE_AUTHOR("Andreas Oberritter <andreas@oberritter.de>");
MODULE_LICENSE("GPL");
MODULE_PARM(debug, "i");
