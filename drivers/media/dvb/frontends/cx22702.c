/*
    Conexant 22702 DVB OFDM frontend driver

    based on:
        Alps TDMB7 DVB OFDM frontend driver

    Copyright (C) 2001-2002 Convergence Integrated Media GmbH
	  Holger Waechtler <holger@convergence.de>

    Copyright (C) 2004 Steven Toth <steve@toth.demon.co.uk>

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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "dvb_frontend.h"

#define FRONTEND_NAME "dvbfe_cx22702"

#define I2C_EEPROM_SLAVE_ADDR 0x50

#define PLLTYPE_DTT7592 1
#define PLLTYPE_DTT7595 2
#define PLLTYPE_DTT7579 3

static int debug = 0;

#define dprintk	if (debug) printk

static struct dvb_frontend_info cx22702_info = {
	.name			= "CX22702 Demod Thomson 759x/7579 PLL",
	.type			= FE_OFDM,
	.frequency_min		= 177000000,
	.frequency_max		= 858000000,
	.frequency_stepsize	= 166666,
	.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
	FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
	FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
	FE_CAN_HIERARCHY_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | 
	FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_RECOVER
};

struct cx22702_state {
	struct i2c_adapter *i2c;
	struct dvb_adapter *dvb;
	struct dvb_frontend_info cx22702_info;
   	char pll_type;
	int pll_addr;
	int demod_addr;
	u8 prevUCBlocks;
};

/* Register values to initialise the demod */
static u8 init_tab [] = {
	0x00, 0x00, /* Stop aquisition */
	0x0B, 0x06,
	0x09, 0x01,
	0x0D, 0x41,
	0x16, 0x32,
	0x20, 0x0A,
	0x21, 0x17,
	0x24, 0x3e,
	0x26, 0xff,
	0x27, 0x10,
	0x28, 0x00,
	0x29, 0x00,
	0x2a, 0x10,
	0x2b, 0x00,
	0x2c, 0x10,
	0x2d, 0x00,
	0x48, 0xd4,
	0x49, 0x56,
	0x6b, 0x1e,
	0xc8, 0x02,
	0xf8, 0x02,
	0xf9, 0x00,
	0xfa, 0x00,
	0xfb, 0x00,
	0xfc, 0x00,
	0xfd, 0x00,
};

static struct i2c_client client_template;

static int cx22702_writereg (struct i2c_adapter *i2c, int demod_addr, u8 reg, u8 data)
{
	int ret;
	u8 buf [] = { reg, data };
	struct i2c_msg msg = { .addr = demod_addr, .flags = 0, .buf = buf, .len = 2 };

	ret = i2c_transfer(i2c, &msg, 1);

	if (ret != 1) 
		printk("%s: writereg error (reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			__FUNCTION__, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}

static u8 cx22702_readreg (struct i2c_adapter *i2c, int demod_addr, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };

	struct i2c_msg msg [] = {
		{ .addr = demod_addr, .flags = 0, .buf = b0, .len = 1 },
		{ .addr = demod_addr, .flags = I2C_M_RD, .buf = b1, .len = 1 } };
        
	ret = i2c_transfer(i2c, msg, 2);
        
	if (ret != 2) 
		printk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return b1[0];
}

static int pll_readreg(struct i2c_adapter *i2c, int pll_addr, int demod_addr, u8 reg)
{
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };

	struct i2c_msg msg [] = {
		{ .addr = pll_addr, .flags = 0,        .buf = b0, .len = 1 },
		{ .addr = pll_addr, .flags = I2C_M_RD, .buf = b1, .len = 1 }
	};

	cx22702_writereg (i2c, demod_addr, 0x0D, cx22702_readreg(i2c,demod_addr,0x0D) &0xfe); // Enable PLL bus
	if (i2c_transfer(i2c, msg, 2) != 2) {
		printk ("%s i2c pll request failed\n", __FUNCTION__);
		cx22702_writereg (i2c, demod_addr, 0x0D, cx22702_readreg(i2c,demod_addr,0x0D) | 1); // Disable PLL bus
		return -ENODEV;
	}
	cx22702_writereg (i2c, demod_addr, 0x0D, cx22702_readreg(i2c,demod_addr,0x0D) | 1); // Disable PLL bus

	return b1[0];
}

static int pll_write (struct i2c_adapter *i2c, int pll_addr, int demod_addr, u8 data [4])
{
	int ret=0;
	struct i2c_msg msg = { .addr = pll_addr, .flags = 0, .buf = data, .len = 4 };

	cx22702_writereg (i2c, demod_addr, 0x0D, cx22702_readreg(i2c,demod_addr,0x0D) &0xfe);  // Enable PLL bus
	ret = i2c_transfer(i2c, &msg, 1);
	cx22702_writereg (i2c, demod_addr, 0x0D, cx22702_readreg(i2c,demod_addr,0x0D) | 1); // Disable PLL bus

	if (ret != 1)
		printk("%s: i/o error (addr == 0x%02x, ret == %i)\n", __FUNCTION__, msg.addr, ret);

	return (ret != 1) ? -1 : 0;
}

static int pll_dtt759x_set_tv_freq (struct i2c_adapter *i2c, struct cx22702_state *state, u32 freq, int bandwidth)
{
	int ret;

	u32 div = (freq + 36166667) / 166666;

	/* dividerhigh, dividerlow, control, bandwidth switch tuner args */
	unsigned char buf [4] = {
		(div >> 8) & 0x7f,
		div & 0xff,
		0x84,
		0x00
	};

	if(freq < 470000000) {
		buf[3] = 0x02;
	} else {
		buf[3] = 0x08;
	}

	if(bandwidth == BANDWIDTH_7_MHZ) {
		buf[3] |= 0x10;
	}

	// Now compensate for the charge pump osc
	if(freq <= 264000000) {
		buf[2] = buf[2] | 0x30;
	} else if (freq <= 735000000) {
		buf[2] = buf[2] | 0x38;
	} else if (freq <= 835000000) {
		buf[2] = buf[2] | 0x70;
	} else if (freq <= 896000000) {
		buf[2] = buf[2] | 0x78;
	}	
   
	dprintk ("%s: freq == %i, div == 0x%04x\n", __FUNCTION__, (int) freq, (int) div);
   
	ret= pll_write (i2c, state->pll_addr, state->demod_addr, buf);
	if(ret<0) {
		dprintk ("%s: first pll_write failed\n",__FUNCTION__);
		return ret;
	}

	/* Set the AGC during search */
	buf[2]=(buf[2] & 0xc7) | 0x18;
	buf[3]=0xa0;
	ret=pll_write (i2c, state->pll_addr, state->demod_addr, buf);
	if(ret<0) {
		dprintk ("%s: second pll_write failed\n",__FUNCTION__);
		return ret;
	}

	/* Tuner needs a small amount of time */
	msleep(100);

	/* Set the AGC post-search */   
	buf[3]=0x20;
	ret=pll_write (i2c, state->pll_addr, state->demod_addr, buf);
	if(ret<0) {
		dprintk ("%s: third pll_write failed\n",__FUNCTION__);
		return ret;
	}

	return ret;

}

static int pll_dtt7579_set_tv_freq (struct i2c_adapter *i2c, struct cx22702_state *state, u32 freq, int bandwidth)
{
	int ret;

	u32 div = (freq + 36166667) / 166666;

	/* dividerhigh, dividerlow */
	unsigned char buf [4] = {
		div >> 8,
		div & 0xff,
		0x00,
		0x00
	};

	// FIXME: bandwidth setting unknown
   
	// Now compensate for the charge pump osc
	if(freq <= 506000000) {
		buf[2] = 0xb4;
	   	buf[3] = 0x02;
	} else if (freq <= 735000000) {
   		buf[2] = 0xbc;
	   	buf[3] = 0x08;
	} else if (freq <= 835000000) {
      		buf[2] = 0xf4;
	   	buf[3] = 0x08;
	} else if (freq <= 896000000) {
		buf[2] = 0xfc;
	   	buf[3] = 0x08;
	}

	dprintk ("%s: freq == %i, div == 0x%04x\n", __FUNCTION__, (int) freq, (int) div);

	ret= pll_write (i2c, state->pll_addr, state->demod_addr, buf);
	if(ret<0) {
		dprintk ("%s: first pll_write failed\n",__FUNCTION__);
		return ret;
	}

	/* Set the AGC to search */
	buf[2]=(buf[2] & 0xdc) | 0x9c;
	buf[3]=0xa0;
	ret=pll_write (i2c, state->pll_addr, state->demod_addr, buf);
	if(ret<0) {
		dprintk ("%s: second pll_write failed\n",__FUNCTION__);
		return ret;
	}

	return ret;

}

/* Reset the demod hardware and reset all of the configuration registers
   to a default state. */
static int cx22702_init (struct i2c_adapter *i2c, struct cx22702_state *state)
{
	int i;

	cx22702_writereg (i2c, state->demod_addr, 0x00, 0x02);

	msleep(10);
	
	for (i=0; i<sizeof(init_tab); i+=2)
		cx22702_writereg (i2c, state->demod_addr, init_tab[i], init_tab[i+1]);

	return 0;	
}

static int cx22702_set_inversion (struct i2c_adapter *i2c, struct cx22702_state *state, int inversion)
{
	u8 val;

	switch (inversion) {

		case INVERSION_AUTO:
			return -EOPNOTSUPP;

		case INVERSION_ON:
			val = cx22702_readreg (i2c, state->demod_addr, 0x0C);
			return cx22702_writereg (i2c, state->demod_addr, 0x0C, val | 0x01);

		case INVERSION_OFF:
			val = cx22702_readreg (i2c, state->demod_addr, 0x0C);
			return cx22702_writereg (i2c, state->demod_addr, 0x0C, val & 0xfe);

		default:
			return -EINVAL;

	}

}

/* Talk to the demod, set the FEC, GUARD, QAM settings etc */
static int cx22702_set_tps (struct i2c_adapter *i2c, struct cx22702_state *state,
			    struct dvb_frontend_parameters *p)
{
	u8 val;

	/* set PLL */
	switch(state->pll_type) {
	case PLLTYPE_DTT7592:
	case PLLTYPE_DTT7595:
		pll_dtt759x_set_tv_freq (i2c, state, p->frequency, p->u.ofdm.bandwidth);
		break;

	case PLLTYPE_DTT7579:
		pll_dtt7579_set_tv_freq (i2c, state, p->frequency, p->u.ofdm.bandwidth);
		break;
	}
   
	/* set inversion */
	cx22702_set_inversion (i2c, state, p->inversion);

	/* set bandwidth */
	switch(p->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		cx22702_writereg(i2c, state->demod_addr, 0x0C, (cx22702_readreg(i2c, state->demod_addr, 0x0C) & 0xcf) | 0x20 );
		break;
	case BANDWIDTH_7_MHZ:
		cx22702_writereg(i2c, state->demod_addr, 0x0C, (cx22702_readreg(i2c, state->demod_addr, 0x0C) & 0xcf) | 0x10 );
		break;
	case BANDWIDTH_8_MHZ:
		cx22702_writereg(i2c, state->demod_addr, 0x0C, cx22702_readreg(i2c, state->demod_addr, 0x0C) &0xcf );
		break;
	default:
		dprintk ("%s: invalid bandwidth\n",__FUNCTION__);
		return -EINVAL;
	}

   
	p->u.ofdm.code_rate_LP = FEC_AUTO; //temp hack as manual not working
     
	/* use auto configuration? */
	if((p->u.ofdm.hierarchy_information==HIERARCHY_AUTO) || 
	   (p->u.ofdm.constellation==QAM_AUTO) ||
	   (p->u.ofdm.code_rate_HP==FEC_AUTO) || 
	   (p->u.ofdm.code_rate_LP==FEC_AUTO) || 
	   (p->u.ofdm.guard_interval==GUARD_INTERVAL_AUTO) || 
	   (p->u.ofdm.transmission_mode==TRANSMISSION_MODE_AUTO) ) {

		/* TPS Source - use hardware driven values */
		cx22702_writereg(i2c, state->demod_addr, 0x06, 0x10);
		cx22702_writereg(i2c, state->demod_addr, 0x07, 0x9);
		cx22702_writereg(i2c, state->demod_addr, 0x08, 0xC1);
		cx22702_writereg(i2c, state->demod_addr, 0x0B, cx22702_readreg(i2c, state->demod_addr, 0x0B) & 0xfc );
		cx22702_writereg(i2c, state->demod_addr, 0x0C, (cx22702_readreg(i2c, state->demod_addr, 0x0C) & 0xBF) | 0x40 );
		cx22702_writereg(i2c, state->demod_addr, 0x00, 0x01); /* Begin aquisition */
		printk("%s: Autodetecting\n",__FUNCTION__);
		return 0;
	}

   	/* manually programmed values */
	val=0;
	switch(p->u.ofdm.constellation) {
		case   QPSK: val = (val&0xe7); break;
		case QAM_16: val = (val&0xe7)|0x08; break;
		case QAM_64: val = (val&0xe7)|0x10; break;
		default:
			dprintk ("%s: invalid constellation\n",__FUNCTION__);
			return -EINVAL;
	}
	switch(p->u.ofdm.hierarchy_information) {
		case HIERARCHY_NONE: val = (val&0xf8); break;
		case    HIERARCHY_1: val = (val&0xf8)|1; break;
		case    HIERARCHY_2: val = (val&0xf8)|2; break;
		case    HIERARCHY_4: val = (val&0xf8)|3; break;
		default:
			dprintk ("%s: invalid hierarchy\n",__FUNCTION__);
			return -EINVAL;
	}
	cx22702_writereg (i2c, state->demod_addr, 0x06, val);

	val=0;
	switch(p->u.ofdm.code_rate_HP) {
		case FEC_NONE:
		case FEC_1_2: val = (val&0xc7); break;
		case FEC_2_3: val = (val&0xc7)|0x08; break;
		case FEC_3_4: val = (val&0xc7)|0x10; break;
		case FEC_5_6: val = (val&0xc7)|0x18; break;
		case FEC_7_8: val = (val&0xc7)|0x20; break;
		default:
			dprintk ("%s: invalid code_rate_HP\n",__FUNCTION__);
			return -EINVAL;
	}
	switch(p->u.ofdm.code_rate_LP) {
		case FEC_NONE:
		case FEC_1_2: val = (val&0xf8); break;
		case FEC_2_3: val = (val&0xf8)|1; break;
		case FEC_3_4: val = (val&0xf8)|2; break;
		case FEC_5_6: val = (val&0xf8)|3; break;
		case FEC_7_8: val = (val&0xf8)|4; break;
		default:
			dprintk ("%s: invalid code_rate_LP\n",__FUNCTION__);
			return -EINVAL;
	}
	cx22702_writereg (i2c, state->demod_addr, 0x07, val);

	val=0;
	switch(p->u.ofdm.guard_interval) {
		case GUARD_INTERVAL_1_32: val = (val&0xf3); break;
		case GUARD_INTERVAL_1_16: val = (val&0xf3)|0x04; break;
		case  GUARD_INTERVAL_1_8: val = (val&0xf3)|0x08; break;
		case  GUARD_INTERVAL_1_4: val = (val&0xf3)|0x0c; break;
		default:
			dprintk ("%s: invalid guard_interval\n",__FUNCTION__);
			return -EINVAL;
	}
	switch(p->u.ofdm.transmission_mode) {
		case TRANSMISSION_MODE_2K: val = (val&0xfc); break;
		case TRANSMISSION_MODE_8K: val = (val&0xfc)|1; break;
		default:
			dprintk ("%s: invalid transmission_mode\n",__FUNCTION__);
			return -EINVAL;
	}
	cx22702_writereg (i2c, state->demod_addr, 0x08, val);
	cx22702_writereg(i2c, state->demod_addr, 0x0B, (cx22702_readreg(i2c, state->demod_addr, 0x0B) & 0xfc) | 0x02 );
	cx22702_writereg(i2c, state->demod_addr, 0x0C, (cx22702_readreg(i2c, state->demod_addr, 0x0C) & 0xBF) | 0x40 );

	/* Begin channel aquisition */
	cx22702_writereg(i2c, state->demod_addr, 0x00, 0x01);

	return 0;
}

/* Retrieve the demod settings */
static int cx22702_get_tps (struct i2c_adapter *i2c, struct cx22702_state *state,
			    struct dvb_ofdm_parameters *p)
{
	u8 val;

	/* Make sure the TPS regs are valid */
	if (!(cx22702_readreg(i2c, state->demod_addr, 0x0A) & 0x20))
		return -EAGAIN;

	val = cx22702_readreg (i2c, state->demod_addr, 0x01);
	switch( (val&0x18)>>3) {
		case 0: p->constellation =   QPSK; break;
		case 1: p->constellation = QAM_16; break;
		case 2: p->constellation = QAM_64; break;
	}
	switch( val&0x07 ) {
		case 0: p->hierarchy_information = HIERARCHY_NONE; break;
		case 1: p->hierarchy_information =    HIERARCHY_1; break;
		case 2: p->hierarchy_information =    HIERARCHY_2; break;
		case 3: p->hierarchy_information =    HIERARCHY_4; break;
	}


	val = cx22702_readreg (i2c, state->demod_addr, 0x02);
	switch( (val&0x38)>>3 ) {
		case 0: p->code_rate_HP = FEC_1_2; break;
		case 1: p->code_rate_HP = FEC_2_3; break;
		case 2: p->code_rate_HP = FEC_3_4; break;
		case 3: p->code_rate_HP = FEC_5_6; break;
		case 4: p->code_rate_HP = FEC_7_8; break;
	}
	switch( val&0x07 ) {
		case 0: p->code_rate_LP = FEC_1_2; break;
		case 1: p->code_rate_LP = FEC_2_3; break;
		case 2: p->code_rate_LP = FEC_3_4; break;
		case 3: p->code_rate_LP = FEC_5_6; break;
		case 4: p->code_rate_LP = FEC_7_8; break;
	}


	val = cx22702_readreg (i2c, state->demod_addr, 0x03);
	switch( (val&0x0c)>>2 ) {
		case 0: p->guard_interval = GUARD_INTERVAL_1_32; break;
		case 1: p->guard_interval = GUARD_INTERVAL_1_16; break;
		case 2: p->guard_interval =  GUARD_INTERVAL_1_8; break;
		case 3: p->guard_interval =  GUARD_INTERVAL_1_4; break;
	}
	switch( val&0x03 ) {
		case 0: p->transmission_mode = TRANSMISSION_MODE_2K; break;
		case 1: p->transmission_mode = TRANSMISSION_MODE_8K; break;
	}

	return 0;
}

static int cx22702_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	struct cx22702_state *state = fe->data;
	struct i2c_adapter *i2c = state->i2c;
	u8 reg0A;
	u8 reg23;
	u8 ucblocks;

	switch (cmd) {
		case FE_GET_INFO:
			memcpy (arg, &state->cx22702_info, sizeof(struct dvb_frontend_info));
			break;

		case FE_READ_STATUS:
		{
			fe_status_t *status = (fe_status_t *) arg;

			*status = 0;

			reg0A = cx22702_readreg (i2c, state->demod_addr, 0x0A);
			reg23 = cx22702_readreg (i2c, state->demod_addr, 0x23);

			dprintk ("%s: status demod=0x%02x agc=0x%02x\n"
				,__FUNCTION__,reg0A,reg23);

			if(reg0A & 0x10) {
				*status |= FE_HAS_LOCK;
				*status |= FE_HAS_VITERBI;
				*status |= FE_HAS_SYNC;
			}

			if(reg0A & 0x20) 
				*status |= FE_HAS_CARRIER;

			if(reg23 < 0xf0) 
				*status |= FE_HAS_SIGNAL;

			break;

		}

		case FE_READ_BER:
			if(cx22702_readreg (i2c, state->demod_addr, 0xE4) & 0x02) {
				/* Realtime statistics */
				*((u32*) arg) = (cx22702_readreg (i2c, state->demod_addr, 0xDE) & 0x7F) << 7
					| (cx22702_readreg (i2c, state->demod_addr, 0xDF)&0x7F);
			} else {
				/* Averagtine statistics */
				*((u32*) arg) = (cx22702_readreg (i2c, state->demod_addr, 0xDE) & 0x7F) << 7
					| cx22702_readreg (i2c, state->demod_addr, 0xDF);
			}
			break;

		case FE_READ_SIGNAL_STRENGTH:
		{
			u16 ss = cx22702_readreg (i2c, state->demod_addr, 0x23);
			*((u16*) arg) = ss;
			break;
		}

		/* We don't have an register for this */
		/* We'll take the inverse of the BER register */
		case FE_READ_SNR:
		{
			u16 rs_ber=0;
			if(cx22702_readreg (i2c, state->demod_addr, 0xE4) & 0x02) {
				/* Realtime statistics */
				rs_ber = (cx22702_readreg (i2c, state->demod_addr, 0xDE) & 0x7F) << 7
					| (cx22702_readreg (i2c, state->demod_addr, 0xDF)& 0x7F);
			} else {
				/* Averagine statistics */
				rs_ber = (cx22702_readreg (i2c, state->demod_addr, 0xDE) & 0x7F) << 8
					| cx22702_readreg (i2c, state->demod_addr, 0xDF);
			}
			*((u16*) arg) = ~rs_ber;
			break;
		}

		case FE_READ_UNCORRECTED_BLOCKS: 
			/* RS Uncorrectable Packet Count then reset */
			ucblocks = cx22702_readreg (i2c, state->demod_addr, 0xE3);
			if (state->prevUCBlocks < ucblocks) *((u32*) arg) = (ucblocks - state->prevUCBlocks);
			else *((u32*) arg) = state->prevUCBlocks - ucblocks;
	   		state->prevUCBlocks = ucblocks;
			break;

		case FE_SET_FRONTEND:
		{
			struct dvb_frontend_parameters *p = arg;
			int ret;

			if((ret=cx22702_set_tps (i2c, state, p))<0) {
				dprintk ("%s: set_tps failed ret=%d\n",__FUNCTION__,ret);
				return ret;
			}
			break;
		}

		case FE_GET_FRONTEND:
		{
			struct dvb_frontend_parameters *p = arg;
			u8 reg0C = cx22702_readreg (i2c, state->demod_addr, 0x0C);
		
			p->inversion = reg0C & 0x1 ? INVERSION_ON : INVERSION_OFF;
			return cx22702_get_tps (i2c, state, &p->u.ofdm);
		}

		case FE_INIT:
			return cx22702_init (i2c, state);

		default:
			return -EOPNOTSUPP;
	};

	return 0;
}

/* Validate the eeprom contents, make sure content look ok.
   Get the eeprom data. */
static int cx22702_validate_eeprom(struct i2c_adapter *i2c, int* minfreq, int* pll_type, int* pll_addr, int* demod_addr)
{
	u8 b0 [] = { 0 };
	u8 b1 [128];
	u32 model=0;
	u8 tuner=0;
	int i,j;

	struct i2c_msg msg [] = {
		{ .addr = I2C_EEPROM_SLAVE_ADDR, .flags = 0,        .buf = b0, .len = 1 },
		{ .addr = I2C_EEPROM_SLAVE_ADDR, .flags = I2C_M_RD, .buf = b1, .len = 128 }
	};

	if (i2c_transfer(i2c, msg, 2) != 2) {
		printk ("%s i2c eeprom request failed\n", __FUNCTION__);
		return -ENODEV;
	}

	if(debug) {
		dprintk ("i2c eeprom content:\n");
		j=0;
		for(i=0;i<128;i++) {
			dprintk("%02x ",b1[i]);
			if(j++==16) {
				dprintk("\n");
				j=0;
			}
		}
 		dprintk("\n");
	}

	if( (b1[8]!=0x84) || (b1[10]!=0x00) ) {
		printk ("%s eeprom content is not valid\n", __FUNCTION__);
		return -ENODEV;
	}

	/* Make sure we support the board model */
	model = b1[0x1f] << 24 | b1[0x1e] << 16 | b1[0x1d] << 8 | b1[0x1c];
	switch(model) {
		case 90002:
		case 90500:
		case 90501:
			dprintk ("%s: Model #%d\n",__FUNCTION__,model);
			break;	
		default:
			printk ("%s: Unknown model #%d not supported\n",__FUNCTION__,model);
			return -ENODEV;	
	}

	/* Make sure we support the tuner */
	tuner = b1[0x2d];
	switch(tuner) {
		case 0x4B:
			dprintk ("%s: Tuner Thomson DTT 7595\n",__FUNCTION__);
			*minfreq = 177000000;
			*pll_type = PLLTYPE_DTT7595;
			break;	
		case 0x4C:
			dprintk ("%s: Tuner Thomson DTT 7592\n",__FUNCTION__);
			*minfreq = 474000000;
			*pll_type = PLLTYPE_DTT7592;
			break;	
		default:
			printk ("%s: Unknown tuner 0x%02x not supported\n",__FUNCTION__,tuner);
			return -ENODEV;	
	}
	*pll_addr = 0x61;
	*demod_addr = 0x43;   
	return 0;
}


/* Validate the demod, make sure we understand the hardware */
static int cx22702_validate_demod(struct i2c_adapter *i2c, int demod_addr)
{
	u8 b0 [] = { 0x1f };
	u8 b1 [] = { 0 };

	struct i2c_msg msg [] = {
		{ .addr = demod_addr, .flags = 0,        .buf = b0, .len = 1 },
		{ .addr = demod_addr, .flags = I2C_M_RD, .buf = b1, .len = 1 }
	};

	if (i2c_transfer(i2c, msg, 2) != 2) {
		printk ("%s i2c demod request failed\n", __FUNCTION__);
		return -ENODEV;
	}

	if( (b1[0]!=0x3) ) {
		printk ("%s i2c demod type 0x(%02x) not known\n", __FUNCTION__,b1[0]);
		return -ENODEV;
	}

	return 0;
}

/* Validate the tuner PLL, make sure we understand the hardware */
static int cx22702_validate_pll(struct i2c_adapter *i2c, int pll_addr, int demod_addr)
{
	int result=0;

	if( (result=pll_readreg(i2c,pll_addr,demod_addr,0xc2)) < 0)
		return result;

	if( (result >= 0) && (result&0x30) )
		return 0;

	return result;
}

/* Check we can see the I2c clients */
static int cx22702_attach_adapter(struct i2c_adapter *adapter)
{
	struct cx22702_state *state;
	struct i2c_client *client;
	int ret;
	int minfreq;
	int pll_type;
	int pll_addr;
	int demod_addr;

	if (0 == (adapter->class & I2C_CLASS_TV_DIGITAL)) {
		dprintk("Ignoring adapter 0x%x:%s (no digital tv card).\n",
			adapter->id, adapter->name);
		return 0;
	}

	dprintk("Trying to attach to adapter 0x%x:%s.\n",
		adapter->id, adapter->name);

	if (!strcmp(adapter->name, "Conexant DVB-T reference design")) {
	   	printk("cx22702: Detected Conexant DVB-T card - PLL Thomson DTT7579\n");
		pll_type = PLLTYPE_DTT7579;
		pll_addr = 0x60;
		demod_addr = 0x43;
		minfreq = 177000000; // guess
	} else {
		// default to Hauppauge Nova-T for the moment
	   	printk("cx22702: Detected Hauppauge Nova-T DVB-T - PLL Thomson DTT759x\n");
		ret=cx22702_validate_eeprom(adapter, &minfreq, &pll_type, &pll_addr, &demod_addr);
		if(ret < 0)
			return ret;
	}

	ret=cx22702_validate_demod(adapter, demod_addr);
	if(ret < 0)
		return ret;

	ret=cx22702_validate_pll(adapter, pll_addr, demod_addr);
	if(ret < 0)
		return ret;

	if ( !(state = kmalloc(sizeof(struct cx22702_state), GFP_KERNEL)) )
		return -ENOMEM;

	memset(state, 0, sizeof(struct cx22702_state));
	state->i2c = adapter;
	memcpy(&state->cx22702_info, &cx22702_info, sizeof(struct dvb_frontend_info));   
	state->cx22702_info.frequency_min = minfreq;
	state->pll_type = pll_type;
	state->pll_addr = pll_addr; 
	state->demod_addr = demod_addr;

	if ( !(client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL)) ) {
		kfree(state);
		return -ENOMEM;
	}

	memcpy(client, &client_template, sizeof(struct i2c_client));
	client->adapter = adapter;
	client->addr = state->demod_addr;
	i2c_set_clientdata(client, state);
   
	if ((ret = i2c_attach_client(client))) {
		printk("cx22702: attach failed %i\n", ret);
		kfree(client);
		kfree(state);
		return ret;
	}
	return 0;	
}

static int cx22702_detach_client(struct i2c_client *client)
{
	struct cx22702_state *state = i2c_get_clientdata(client);

	if (NULL != state->dvb) {
		dvb_unregister_frontend (cx22702_ioctl, state->dvb);
		state->dvb = NULL;
	}
	i2c_detach_client(client);
	kfree(client);
	return 0;
}

static int command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct cx22702_state *state = i2c_get_clientdata(client);
	int rc;

	switch(cmd) {
	case FE_REGISTER:
		if (NULL != state->dvb)
			break;
		state->dvb = arg;
		rc = dvb_register_frontend(cx22702_ioctl, state->dvb, state,
					   &state->cx22702_info, THIS_MODULE);
		if (0 != rc) {
			printk("cx22702: dvb_register_frontend failed with rc=%d\n",rc);
			state->dvb = NULL;
			return rc;
		}
		break;
	case FE_UNREGISTER:
		if (NULL == state->dvb)
			break;
		dvb_unregister_frontend (cx22702_ioctl, state->dvb);
		state->dvb = NULL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct i2c_driver driver = {
	.owner = THIS_MODULE,
	.name  = FRONTEND_NAME,
	.id = I2C_DRIVERID_DVBFE_CX22702,
	.flags = I2C_DF_NOTIFY,
	.attach_adapter = cx22702_attach_adapter,
	.detach_client = cx22702_detach_client,
	.command = command,
};

static struct i2c_client client_template = {
	.name = FRONTEND_NAME,
	.flags = I2C_CLIENT_ALLOW_USE,
	.driver = &driver,
};

static int __init init_cx22702 (void)
{
	return i2c_add_driver(&driver);
}

static void __exit exit_cx22702 (void)
{
	if (i2c_del_driver(&driver))
		printk(KERN_ERR "cx22702: driver deregistration failed.\n");
}

module_init (init_cx22702);
module_exit (exit_cx22702);

MODULE_PARM(debug,"i");
MODULE_PARM_DESC(debug, "Enable verbose debug messages");
MODULE_DESCRIPTION("CX22702 / Thomson DTT 759x / Thomson DTT 7579 PLL DVB Frontend driver");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");

