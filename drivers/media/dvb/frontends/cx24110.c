/*
    cx24110 - Single Chip Satellite Channel Receiver driver module
               used on the the Pinnacle PCTV Sat cards

    Copyright (C) 2002 Peter Hettkamp <peter.hettkamp@t-online.de> based on
    work
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

/* currently drives the Conexant cx24110 and cx24106 QPSK decoder chips,
   connected via i2c to a Conexant Fusion 878 (this uses the standard
   linux bttv driver). The tuner chip is supposed to be the Conexant
   cx24108 digital satellite tuner, driven through the tuner interface
   of the cx24110. SEC is also supplied by the cx24110.

   Oct-2002: Migrate to API V3 (formerly known as NEWSTRUCT)
*/

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "dvb_frontend.h"
#include "dvb_functions.h"

static int debug = 0;
#define dprintk	if (debug) printk


static struct dvb_frontend_info cx24110_info = {
	.name = "Conexant CX24110 with CX24108 tuner, aka HM1221/HM1811",
	.type = FE_QPSK,
	.frequency_min = 950000,
	.frequency_max = 2150000,
	.frequency_stepsize = 1011,  /* kHz for QPSK frontends, can be reduced
					to 253kHz on the cx24108 tuner */
	.frequency_tolerance = 29500,
	.symbol_rate_min = 1000000,
	.symbol_rate_max = 45000000,
/*      .symbol_rate_tolerance = ???,*/
	.notifier_delay = 50,                /* 1/20 s */
	.caps = FE_CAN_INVERSION_AUTO |
		FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QPSK |
		FE_CAN_CLEAN_SETUP
};
/* fixme: are these values correct? especially ..._tolerance and caps */


static struct {u8 reg; u8 data;} cx24110_regdata[]=
                      /* Comments beginning with @ denote this value should
                         be the default */
        {{0x09,0x01}, /* SoftResetAll */
         {0x09,0x00}, /* release reset */
         {0x01,0xe8}, /* MSB of code rate 27.5MS/s */
         {0x02,0x17}, /* middle byte " */
         {0x03,0x29}, /* LSB         " */
         {0x05,0x03}, /* @ DVB mode, standard code rate 3/4 */
         {0x06,0xa5}, /* @ PLL 60MHz */
         {0x07,0x01}, /* @ Fclk, i.e. sampling clock, 60MHz */
         {0x0a,0x00}, /* @ partial chip disables, do not set */
         {0x0b,0x01}, /* set output clock in gapped mode, start signal low
                         active for first byte */
         {0x0c,0x11}, /* no parity bytes, large hold time, serial data out */
         {0x0d,0x6f}, /* @ RS Sync/Unsync thresholds */
         {0x10,0x40}, /* chip doc is misleading here: write bit 6 as 1
                         to avoid starting the BER counter. Reset the
                         CRC test bit. Finite counting selected */
         {0x15,0xff}, /* @ size of the limited time window for RS BER
                         estimation. It is <value>*256 RS blocks, this
                         gives approx. 2.6 sec at 27.5MS/s, rate 3/4 */
         {0x16,0x00}, /* @ enable all RS output ports */
         {0x17,0x04}, /* @ time window allowed for the RS to sync */
         {0x18,0xae}, /* @ allow all standard DVB code rates to be scanned
                         for automatically */
                      /* leave the current code rate and normalization
                         registers as they are after reset... */
         {0x21,0x10}, /* @ during AutoAcq, search each viterbi setting
                         only once */
         {0x23,0x18}, /* @ size of the limited time window for Viterbi BER
                         estimation. It is <value>*65536 channel bits, i.e.
                         approx. 38ms at 27.5MS/s, rate 3/4 */
         {0x24,0x24}, /* do not trigger Viterbi CRC test. Finite count window */
                      /* leave front-end AGC parameters at default values */
                      /* leave decimation AGC parameters at default values */
         {0x35,0x40}, /* disable all interrupts. They are not connected anyway */
         {0x36,0xff}, /* clear all interrupt pending flags */
         {0x37,0x00}, /* @ fully enable AutoAcqq state machine */
         {0x38,0x07}, /* @ enable fade recovery, but not autostart AutoAcq */
                      /* leave the equalizer parameters on their default values */
                      /* leave the final AGC parameters on their default values */
         {0x41,0x00}, /* @ MSB of front-end derotator frequency */
         {0x42,0x00}, /* @ middle bytes " */
         {0x43,0x00}, /* @ LSB          " */
                      /* leave the carrier tracking loop parameters on default */
                      /* leave the bit timing loop parameters at gefault */
         {0x56,0x4d}, /* set the filtune voltage to 2.7V, as recommended by */
                      /* the cx24108 data sheet for symbol rates above 15MS/s */
         {0x57,0x00}, /* @ Filter sigma delta enabled, positive */
         {0x61,0x95}, /* GPIO pins 1-4 have special function */
         {0x62,0x05}, /* GPIO pin 5 has special function, pin 6 is GPIO */
         {0x63,0x00}, /* All GPIO pins use CMOS output characteristics */
         {0x64,0x20}, /* GPIO 6 is input, all others are outputs */
         {0x6d,0x30}, /* tuner auto mode clock freq 62kHz */
         {0x70,0x15}, /* use auto mode, tuner word is 21 bits long */
         {0x73,0x00}, /* @ disable several demod bypasses */
         {0x74,0x00}, /* @  " */
         {0x75,0x00}  /* @  " */
                      /* the remaining registers are for SEC */
	};


static int cx24110_writereg (struct dvb_i2c_bus *i2c, int reg, int data)
{
        u8 buf [] = { reg, data };
	struct i2c_msg msg = { .addr = 0x55, .flags = 0, .buf = buf, .len = 2 };
/* fixme (medium): HW allows any i2c address. 0x55 is the default, but the
   cx24110 might show up at any address */
	int err;

        if ((err = i2c->xfer (i2c, &msg, 1)) != 1) {
		dprintk ("%s: writereg error (err == %i, reg == 0x%02x, data == 0x%02x)\n", __FUNCTION__, err, reg, data);
		return -EREMOTEIO;
	}

        return 0;
}


static u8 cx24110_readreg (struct dvb_i2c_bus *i2c, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = 0x55, .flags = 0, .buf = b0, .len = 1 },
			   { .addr = 0x55, .flags = I2C_M_RD, .buf = b1, .len = 1 } };
/* fixme (medium): address might be different from 0x55 */
	ret = i2c->xfer (i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];
}


static int cx24108_write (struct dvb_i2c_bus *i2c, u32 data)
{
/* tuner data is 21 bits long, must be left-aligned in data */
/* tuner cx24108 is written through a dedicated 3wire interface on the demod chip */
/* FIXME (low): add error handling, avoid infinite loops if HW fails... */

dprintk("cx24110 debug: cx24108_write(%8.8x)\n",data);

        cx24110_writereg(i2c,0x6d,0x30); /* auto mode at 62kHz */
        cx24110_writereg(i2c,0x70,0x15); /* auto mode 21 bits */
        /* if the auto tuner writer is still busy, clear it out */
        while (cx24110_readreg(i2c,0x6d)&0x80)
		cx24110_writereg(i2c,0x72,0);
        /* write the topmost 8 bits */
        cx24110_writereg(i2c,0x72,(data>>24)&0xff);
        /* wait for the send to be completed */
        while ((cx24110_readreg(i2c,0x6d)&0xc0)==0x80)
		;
        /* send another 8 bytes */
        cx24110_writereg(i2c,0x72,(data>>16)&0xff);
        while ((cx24110_readreg(i2c,0x6d)&0xc0)==0x80)
		;
        /* and the topmost 5 bits of this byte */
        cx24110_writereg(i2c,0x72,(data>>8)&0xff);
        while ((cx24110_readreg(i2c,0x6d)&0xc0)==0x80)
		;
        /* now strobe the enable line once */
        cx24110_writereg(i2c,0x6d,0x32);
        cx24110_writereg(i2c,0x6d,0x30);

        return 0;
}


static int cx24108_set_tv_freq (struct dvb_i2c_bus *i2c, u32 freq)
{
/* fixme (low): error handling */
        int i, a, n, pump;
        u32 band, pll;


        static const u32 osci[]={ 950000,1019000,1075000,1178000,
			         1296000,1432000,1576000,1718000,
				 1856000,2036000,2150000};
        static const u32 bandsel[]={0,0x00020000,0x00040000,0x00100800,
				      0x00101000,0x00102000,0x00104000,
				      0x00108000,0x00110000,0x00120000,
				      0x00140000};

#define XTAL 1011100 /* Hz, really 1.0111 MHz and a /10 prescaler */
        dprintk("cx24110 debug: cx24108_set_tv_freq, freq=%d\n",freq);

        if (freq<950000)
		freq=950000; /* kHz */
        if (freq>2150000)
		freq=2150000; /* satellite IF is 950..2150MHz */
        /* decide which VCO to use for the input frequency */
        for (i=1;(i<sizeof(osci)/sizeof(osci[0]))&&(osci[i]<freq);i++)
		;
        dprintk("cx24110 debug: select vco #%d (f=%d)\n",i,freq);
        band=bandsel[i];
        /* the gain values must be set by SetSymbolrate */
        /* compute the pll divider needed, from Conexant data sheet,
           resolved for (n*32+a), remember f(vco) is f(receive) *2 or *4,
           depending on the divider bit. It is set to /4 on the 2 lowest
           bands  */
        n=((i<=2?2:1)*freq*10L)/(XTAL/100);
        a=n%32; n/=32;
	if (a==0)
		n--;
        pump=(freq<(osci[i-1]+osci[i])/2);
        pll=0xf8000000|
            ((pump?1:2)<<(14+11))|
            ((n&0x1ff)<<(5+11))|
            ((a&0x1f)<<11);
        /* everything is shifted left 11 bits to left-align the bits in the
           32bit word. Output to the tuner goes MSB-aligned, after all */
        dprintk("cx24110 debug: pump=%d, n=%d, a=%d\n",pump,n,a);
        cx24108_write(i2c,band);
        /* set vga and vca to their widest-band settings, as a precaution.
           SetSymbolrate might not be called to set this up */
        cx24108_write(i2c,0x500c0000);
        cx24108_write(i2c,0x83f1f800);
        cx24108_write(i2c,pll);
        cx24110_writereg(i2c,0x56,0x7f);

	dvb_delay(HZ/10); /* wait a moment for the tuner pll to lock */

	/* tuner pll lock can be monitored on GPIO pin 4 of cx24110 */
        while (!(cx24110_readreg(i2c,0x66)&0x20)&&i<1000)
		i++;
        dprintk("cx24110 debug: GPIO IN=%2.2x(loop=%d)\n",
                cx24110_readreg(i2c,0x66),i);
        return 0;

}


static int cx24110_init (struct dvb_i2c_bus *i2c)
{
/* fixme (low): error handling */
        int i;

	dprintk("%s: init chip\n", __FUNCTION__);

        for(i=0;i<sizeof(cx24110_regdata)/sizeof(cx24110_regdata[0]);i++) {
		cx24110_writereg(i2c,cx24110_regdata[i].reg,cx24110_regdata[i].data);
        };

	return 0;
}


static int cx24110_set_inversion (struct dvb_i2c_bus *i2c, fe_spectral_inversion_t inversion)
{
/* fixme (low): error handling */

	switch (inversion) {
	case INVERSION_OFF:
                cx24110_writereg(i2c,0x37,cx24110_readreg(i2c,0x37)|0x1);
                /* AcqSpectrInvDis on. No idea why someone should want this */
                cx24110_writereg(i2c,0x5,cx24110_readreg(i2c,0x5)&0xf7);
                /* Initial value 0 at start of acq */
                cx24110_writereg(i2c,0x22,cx24110_readreg(i2c,0x22)&0xef);
                /* current value 0 */
                /* The cx24110 manual tells us this reg is read-only.
                   But what the heck... set it ayways */
                break;
	case INVERSION_ON:
                cx24110_writereg(i2c,0x37,cx24110_readreg(i2c,0x37)|0x1);
                /* AcqSpectrInvDis on. No idea why someone should want this */
                cx24110_writereg(i2c,0x5,cx24110_readreg(i2c,0x5)|0x08);
                /* Initial value 1 at start of acq */
                cx24110_writereg(i2c,0x22,cx24110_readreg(i2c,0x22)|0x10);
                /* current value 1 */
                break;
	case INVERSION_AUTO:
                cx24110_writereg(i2c,0x37,cx24110_readreg(i2c,0x37)&0xfe);
                /* AcqSpectrInvDis off. Leave initial & current states as is */
                break;
	default:
		return -EINVAL;
	}

	return 0;
}


static int cx24110_set_fec (struct dvb_i2c_bus *i2c, fe_code_rate_t fec)
{
/* fixme (low): error handling */

        static const int rate[]={-1,1,2,3,5,7,-1};
        static const int g1[]={-1,0x01,0x02,0x05,0x15,0x45,-1};
        static const int g2[]={-1,0x01,0x03,0x06,0x1a,0x7a,-1};

        /* Well, the AutoAcq engine of the cx24106 and 24110 automatically
           searches all enabled viterbi rates, and can handle non-standard
           rates as well. */

        if (fec>FEC_AUTO)
                fec=FEC_AUTO;

        if (fec==FEC_AUTO) { /* (re-)establish AutoAcq behaviour */
		cx24110_writereg(i2c,0x37,cx24110_readreg(i2c,0x37)&0xdf);
		/* clear AcqVitDis bit */
		cx24110_writereg(i2c,0x18,0xae);
		/* allow all DVB standard code rates */
		cx24110_writereg(i2c,0x05,(cx24110_readreg(i2c,0x05)&0xf0)|0x3);
		/* set nominal Viterbi rate 3/4 */
		cx24110_writereg(i2c,0x22,(cx24110_readreg(i2c,0x22)&0xf0)|0x3);
		/* set current Viterbi rate 3/4 */
		cx24110_writereg(i2c,0x1a,0x05); cx24110_writereg(i2c,0x1b,0x06);
		/* set the puncture registers for code rate 3/4 */
		return 0;
        } else {
		cx24110_writereg(i2c,0x37,cx24110_readreg(i2c,0x37)|0x20);
		/* set AcqVitDis bit */
		if(rate[fec]>0) {
			cx24110_writereg(i2c,0x05,(cx24110_readreg(i2c,0x05)&0xf0)|rate[fec]);
			/* set nominal Viterbi rate */
			cx24110_writereg(i2c,0x22,(cx24110_readreg(i2c,0x22)&0xf0)|rate[fec]);
			/* set current Viterbi rate */
			cx24110_writereg(i2c,0x1a,g1[fec]);
			cx24110_writereg(i2c,0x1b,g2[fec]);
			/* not sure if this is the right way: I always used AutoAcq mode */
           } else
		   return -EOPNOTSUPP;
/* fixme (low): which is the correct return code? */
        };
	return 0;
}


static fe_code_rate_t cx24110_get_fec (struct dvb_i2c_bus *i2c)
{
	int i;

	i=cx24110_readreg(i2c,0x22)&0x0f;
	if(!(i&0x08)) {
		return FEC_1_2 + i - 1;
	} else {
/* fixme (low): a special code rate has been selected. In theory, we need to
   return a denominator value, a numerator value, and a pair of puncture
   maps to correctly describe this mode. But this should never happen in
   practice, because it cannot be set by cx24110_get_fec. */
	   return FEC_NONE;
	}
}


static int cx24110_set_symbolrate (struct dvb_i2c_bus *i2c, u32 srate)
{
/* fixme (low): add error handling */
        u32 ratio;
        u32 tmp, fclk, BDRI;

        static const u32 bands[]={5000000UL,15000000UL,90999000UL/2};
        static const u32 vca[]={0x80f03800,0x81f0f800,0x83f1f800};
        static const u32 vga[]={0x5f8fc000,0x580f0000,0x500c0000};
        static const u8  filtune[]={0xa2,0xcc,0x66};
        int i;

dprintk("cx24110 debug: entering %s(%d)\n",__FUNCTION__,srate);
        if (srate>90999000UL/2)
                srate=90999000UL/2;
        if (srate<500000)
                srate=500000;

        for(i=0;(i<sizeof(bands)/sizeof(bands[0]))&&(srate>bands[i]);i++)
		;
        /* first, check which sample rate is appropriate: 45, 60 80 or 90 MHz,
           and set the PLL accordingly (R07[1:0] Fclk, R06[7:4] PLLmult,
           R06[3:0] PLLphaseDetGain */
        tmp=cx24110_readreg(i2c,0x07)&0xfc;
        if(srate<90999000UL/4) { /* sample rate 45MHz*/
		cx24110_writereg(i2c,0x07,tmp);
		cx24110_writereg(i2c,0x06,0x78);
		fclk=90999000UL/2;
        } else if(srate<60666000UL/2) { /* sample rate 60MHz */
		cx24110_writereg(i2c,0x07,tmp|0x1);
		cx24110_writereg(i2c,0x06,0xa5);
		fclk=60666000UL;
        } else if(srate<80888000UL/2) { /* sample rate 80MHz */
		cx24110_writereg(i2c,0x07,tmp|0x2);
		cx24110_writereg(i2c,0x06,0x87);
		fclk=80888000UL;
        } else { /* sample rate 90MHz */
		cx24110_writereg(i2c,0x07,tmp|0x3);
		cx24110_writereg(i2c,0x06,0x78);
		fclk=90999000UL;
        };
        dprintk("cx24110 debug: fclk %d Hz\n",fclk);
        /* we need to divide two integers with approx. 27 bits in 32 bit
           arithmetic giving a 25 bit result */
        /* the maximum dividend is 90999000/2, 0x02b6446c, this number is
           also the most complex divisor. Hence, the dividend has,
           assuming 32bit unsigned arithmetic, 6 clear bits on top, the
           divisor 2 unused bits at the bottom. Also, the quotient is
           always less than 1/2. Borrowed from VES1893.c, of course */

        tmp=srate<<6;
        BDRI=fclk>>2;
        ratio=(tmp/BDRI);

        tmp=(tmp%BDRI)<<8;
        ratio=(ratio<<8)+(tmp/BDRI);

        tmp=(tmp%BDRI)<<8;
        ratio=(ratio<<8)+(tmp/BDRI);

        tmp=(tmp%BDRI)<<1;
        ratio=(ratio<<1)+(tmp/BDRI);

        dprintk("srate= %d (range %d, up to %d)\n", srate,i,bands[i]);
        dprintk("fclk = %d\n", fclk);
        dprintk("ratio= %08x\n", ratio);

        cx24110_writereg(i2c, 0x1, (ratio>>16)&0xff);
        cx24110_writereg(i2c, 0x2, (ratio>>8)&0xff);
        cx24110_writereg(i2c, 0x3, (ratio)&0xff);

        /* please see the cx24108 data sheet, this controls tuner gain
           and bandwidth settings depending on the symbol rate */
        cx24108_write(i2c,vga[i]);
        cx24108_write(i2c,vca[i]); /* gain is set on tuner chip */
        cx24110_writereg(i2c,0x56,filtune[i]); /* bw is contolled by filtune voltage */

        return 0;

}


static int cx24110_set_voltage (struct dvb_i2c_bus *i2c, fe_sec_voltage_t voltage)
{
	switch (voltage) {
	case SEC_VOLTAGE_13:
		return cx24110_writereg(i2c,0x76,(cx24110_readreg(i2c,0x76)&0x3b)|0xc0);
	case SEC_VOLTAGE_18:
		return cx24110_writereg(i2c,0x76,(cx24110_readreg(i2c,0x76)&0x3b)|0x40);
	default:
		return -EINVAL;
	};
}

static void sendDiSEqCMessage(struct dvb_i2c_bus *i2c, struct dvb_diseqc_master_cmd *pCmd)
{
	int i, rv;

	for (i = 0; i < pCmd->msg_len; i++)
		cx24110_writereg(i2c, 0x79 + i, pCmd->msg[i]);

	rv = cx24110_readreg(i2c, 0x76);

	cx24110_writereg(i2c, 0x76, ((rv & 0x90) | 0x40) | ((pCmd->msg_len-3) & 3));
	for (i=500; i-- > 0 && !(cx24110_readreg(i2c,0x76)&0x40);)
		; /* wait for LNB ready */
}


static int cx24110_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct dvb_i2c_bus *i2c = fe->i2c;
	static int lastber=0, lastbyer=0,lastbler=0, lastesn0=0, sum_bler=0;

        switch (cmd) {
        case FE_GET_INFO:
		memcpy (arg, &cx24110_info, sizeof(struct dvb_frontend_info));
		break;

        case FE_READ_STATUS:
	{
		fe_status_t *status = arg;
		int sync = cx24110_readreg (i2c, 0x55);

		*status = 0;

		if (sync & 0x10)
			*status |= FE_HAS_SIGNAL;

		if (sync & 0x08)
			*status |= FE_HAS_CARRIER;

		sync = cx24110_readreg (i2c, 0x08);

		if (sync & 0x40)
			*status |= FE_HAS_VITERBI;

		if (sync & 0x20)
			*status |= FE_HAS_SYNC;

		if ((sync & 0x60) == 0x60)
			*status |= FE_HAS_LOCK;

		if(cx24110_readreg(i2c,0x10)&0x40) {
			/* the RS error counter has finished one counting window */
			cx24110_writereg(i2c,0x10,0x60); /* select the byer reg */
			lastbyer=cx24110_readreg(i2c,0x12)|
				(cx24110_readreg(i2c,0x13)<<8)|
				(cx24110_readreg(i2c,0x14)<<16);
			cx24110_writereg(i2c,0x10,0x70); /* select the bler reg */
			lastbler=cx24110_readreg(i2c,0x12)|
				(cx24110_readreg(i2c,0x13)<<8)|
				(cx24110_readreg(i2c,0x14)<<16);
			cx24110_writereg(i2c,0x10,0x20); /* start new count window */
			sum_bler += lastbler;
		}
		if(cx24110_readreg(i2c,0x24)&0x10) {
			/* the Viterbi error counter has finished one counting window */
			cx24110_writereg(i2c,0x24,0x04); /* select the ber reg */
			lastber=cx24110_readreg(i2c,0x25)|
				(cx24110_readreg(i2c,0x26)<<8);
			cx24110_writereg(i2c,0x24,0x04); /* start new count window */
			cx24110_writereg(i2c,0x24,0x14);
		}
		if(cx24110_readreg(i2c,0x6a)&0x80) {
			/* the Es/N0 error counter has finished one counting window */
			lastesn0=cx24110_readreg(i2c,0x69)|
				(cx24110_readreg(i2c,0x68)<<8);
			cx24110_writereg(i2c,0x6a,0x84); /* start new count window */
		}
		break;
	}

        case FE_READ_BER:
	{
		u32 *ber = (u32 *) arg;

		*ber = lastber;
/* fixme (maybe): value range is 16 bit. Scale? */
		break;
	}

        case FE_READ_SIGNAL_STRENGTH:
	{
/* no provision in hardware. Read the frontend AGC accumulator. No idea how to scale this, but I know it is 2s complement */
		u8 signal = cx24110_readreg (i2c, 0x27)+128;
		*((u16*) arg) = (signal << 8) | signal;
		break;
	}

        case FE_READ_SNR:
	{
/* no provision in hardware. Can be computed from the Es/N0 estimator, but I don't know how. */
		*(u16*) arg = lastesn0;
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS:
	{
		*(u16*) arg = sum_bler&0xffff;
		sum_bler=0;
		break;
	}

        case FE_SET_FRONTEND:
        {
		struct dvb_frontend_parameters *p = arg;

		cx24108_set_tv_freq (i2c, p->frequency);
		cx24110_set_inversion (i2c, p->inversion);
		cx24110_set_fec (i2c, p->u.qpsk.fec_inner);
		cx24110_set_symbolrate (i2c, p->u.qpsk.symbol_rate);
		cx24110_writereg(i2c,0x04,0x05); /* start aquisition */
                break;
        }

	case FE_GET_FRONTEND:
	{
		struct dvb_frontend_parameters *p = arg;
		s32 afc; unsigned sclk;

/* cannot read back tuner settings (freq). Need to have some private storage */

		sclk = cx24110_readreg (i2c, 0x07) & 0x03;
/* ok, real AFC (FEDR) freq. is afc/2^24*fsamp, fsamp=45/60/80/90MHz.
 * Need 64 bit arithmetic. Is thiss possible in the kernel? */
		if (sclk==0) sclk=90999000L/2L;
		else if (sclk==1) sclk=60666000L;
		else if (sclk==2) sclk=80888000L;
		else sclk=90999000L;
		sclk>>=8;
		afc = sclk*(cx24110_readreg (i2c, 0x44)&0x1f)+
		      ((sclk*cx24110_readreg (i2c, 0x45))>>8)+
		      ((sclk*cx24110_readreg (i2c, 0x46))>>16);

		p->frequency += afc;
		p->inversion = (cx24110_readreg (i2c, 0x22) & 0x10) ?
					INVERSION_ON : INVERSION_OFF;
		p->u.qpsk.fec_inner = cx24110_get_fec (i2c);
		break;
	}

        case FE_SLEEP:
/* cannot do this from the FE end. How to communicate this to the place where it can be done? */
		break;
        case FE_INIT:
		return cx24110_init (i2c);

	case FE_RESET:
/* no idea what to do for this call */
/* fixme (medium): fill me in */
		break;

	case FE_SET_TONE:
		return cx24110_writereg(i2c,0x76,(cx24110_readreg(i2c,0x76)&~0x10)|((((fe_sec_tone_mode_t) arg)==SEC_TONE_ON)?0x10:0));
	case FE_SET_VOLTAGE:
		return cx24110_set_voltage (i2c, (fe_sec_voltage_t) arg);

	case FE_DISEQC_SEND_MASTER_CMD:
		sendDiSEqCMessage(i2c, (struct dvb_diseqc_master_cmd*) arg);
		return 0;

	default:
		return -EOPNOTSUPP;
        };

        return 0;
}


static int cx24110_attach (struct dvb_i2c_bus *i2c, void **data)
{
	u8 sig;

	sig=cx24110_readreg (i2c, 0x00);
	if ( sig != 0x5a && sig != 0x69 )
		return -ENODEV;

	return dvb_register_frontend (cx24110_ioctl, i2c, NULL, &cx24110_info);
}


static void cx24110_detach (struct dvb_i2c_bus *i2c, void *data)
{
	dvb_unregister_frontend (cx24110_ioctl, i2c);
}


static int __init init_cx24110 (void)
{
	return dvb_register_i2c_device (THIS_MODULE, cx24110_attach, cx24110_detach);
}


static void __exit exit_cx24110 (void)
{
	dvb_unregister_i2c_device (cx24110_attach);
}


module_init(init_cx24110);
module_exit(exit_cx24110);


MODULE_DESCRIPTION("DVB Frontend driver module for the Conexant cx24108/cx24110 chipset");
MODULE_AUTHOR("Peter Hettkamp");
MODULE_LICENSE("GPL");
MODULE_PARM(debug,"i");

