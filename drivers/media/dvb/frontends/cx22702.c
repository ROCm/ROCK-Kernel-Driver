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
#include "dvb-pll.h"
#include "cx22702.h"

#define FRONTEND_NAME "dvbfe_cx22702"

#define dprintk	if (debug) printk

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable verbose debug messages");

/* ------------------------------------------------------------------ */

struct cx22702_state {
	struct i2c_client               demod;
	struct i2c_client               pll;
	struct dvb_pll_desc             *pll_desc;
	struct dvb_frontend             fe;
	struct dvb_frontend_ops         ops;
	struct dvb_frontend_parameters  p;
	u8 prevUCBlocks;
};

static struct i2c_client demod_template;
static struct i2c_client pll_template;

/* Register values to initialise the demod */
static u8 init_tab[] = {
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

/* ------------------------------------------------------------------ */

static int writereg(struct i2c_client *c, u8 reg, u8 data)
{
	u8 buf[] = { reg, data };
	int ret;

	ret = i2c_master_send(c, buf, 2);
	if (ret != 2) {
		printk("%s: writereg error (reg == 0x%02x, val == 0x%02x, ret == %i)\n",
		       __FUNCTION__, reg, data, ret);
		return -1;
	}
	return 0;
}

static u8 readreg(struct i2c_client *c, u8 reg)
{
	u8 wr [] = { reg };
	u8 rd [] = { 0 };
	struct i2c_msg msg [] = {
		{ .addr = c->addr, .flags = 0,        .buf = wr, .len = 1 },
		{ .addr = c->addr, .flags = I2C_M_RD, .buf = rd, .len = 1 },
	};
	int ret;

	ret = i2c_transfer(c->adapter, msg, 2);
	if (ret != 2) {
		printk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);
		return -1;
	}
	return rd[0];
}

/* ------------------------------------------------------------------ */

#define PLL_ENABLE(cx)  writereg(&cx->demod, 0x0D, readreg(&cx->demod, 0x0D) & 0xfe)
#define PLL_DISABLE(cx) writereg(&cx->demod, 0x0D, readreg(&cx->demod, 0x0D) | 0x01)

static int pll_write4(struct i2c_client *c, u8 *data)
{
	int ret;

	ret = i2c_master_send(c, data, 4);
	if (ret != 4) {
		printk("%s: i/o error (addr == 0x%02x, ret == %i)\n",
		       __FUNCTION__, c->addr, ret);
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */

static int cx22702_reset(struct cx22702_state *state)
{
	int i;

	dprintk("%s\n",__FUNCTION__);
	writereg(&state->demod, 0x00, 0x02);
	msleep(10);

	for (i=0; i<ARRAY_SIZE(init_tab); i+=2)
		writereg(&state->demod, init_tab[i], init_tab[i+1]);
	return 0;
}

static int cx22702_set_inversion(struct cx22702_state *state, int inversion)
{
	u8 val;

	switch (inversion) {
	case INVERSION_AUTO:
		return -EOPNOTSUPP;

	case INVERSION_ON:
		val = readreg(&state->demod, 0x0C);
		return writereg(&state->demod, 0x0C, val | 0x01);

	case INVERSION_OFF:
		val = readreg(&state->demod, 0x0C);
		return writereg(&state->demod, 0x0C, val & 0xfe);

	default:
		return -EINVAL;
	}
}

/* Talk to the demod, set the FEC, GUARD, QAM settings etc */
static int cx22702_set_tps(struct cx22702_state *state)
{
	u8 val;
	u8 pllbuf[4];

	dprintk("%s\n",__FUNCTION__);

	/* set PLL */
	dvb_pll_configure(state->pll_desc, pllbuf,
			  state->p.frequency,
			  state->p.u.ofdm.bandwidth);
	PLL_ENABLE(state);
	pll_write4(&state->pll,pllbuf);
	PLL_DISABLE(state);

	/* set inversion */
	cx22702_set_inversion(state, state->p.inversion);

	/* set bandwidth */
	switch(state->p.u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		writereg(&state->demod, 0x0C, (readreg(&state->demod, 0x0C) & 0xcf) | 0x20 );
		break;
	case BANDWIDTH_7_MHZ:
		writereg(&state->demod, 0x0C, (readreg(&state->demod, 0x0C) & 0xcf) | 0x10 );
		break;
	case BANDWIDTH_8_MHZ:
		writereg(&state->demod, 0x0C, readreg(&state->demod, 0x0C) &0xcf );
		break;
	default:
		dprintk ("%s: invalid bandwidth\n",__FUNCTION__);
		return -EINVAL;
	}


	state->p.u.ofdm.code_rate_LP = FEC_AUTO; //temp hack as manual not working

	/* use auto configuration? */
	if((state->p.u.ofdm.hierarchy_information==HIERARCHY_AUTO) ||
	   (state->p.u.ofdm.constellation==QAM_AUTO) ||
	   (state->p.u.ofdm.code_rate_HP==FEC_AUTO) ||
	   (state->p.u.ofdm.code_rate_LP==FEC_AUTO) ||
	   (state->p.u.ofdm.guard_interval==GUARD_INTERVAL_AUTO) ||
	   (state->p.u.ofdm.transmission_mode==TRANSMISSION_MODE_AUTO) ) {

		/* TPS Source - use hardware driven values */
		writereg(&state->demod, 0x06, 0x10);
		writereg(&state->demod, 0x07, 0x9);
		writereg(&state->demod, 0x08, 0xC1);
		writereg(&state->demod, 0x0B, readreg(&state->demod, 0x0B) & 0xfc );
		writereg(&state->demod, 0x0C, (readreg(&state->demod, 0x0C) & 0xBF) | 0x40 );
		writereg(&state->demod, 0x00, 0x01); /* Begin aquisition */
		dprintk("%s: Autodetecting\n",__FUNCTION__);
		return 0;
	}

   	/* manually programmed values */
	val=0;
	switch(state->p.u.ofdm.constellation) {
		case   QPSK: val = (val&0xe7); break;
		case QAM_16: val = (val&0xe7)|0x08; break;
		case QAM_64: val = (val&0xe7)|0x10; break;
		default:
			dprintk ("%s: invalid constellation\n",__FUNCTION__);
			return -EINVAL;
	}
	switch(state->p.u.ofdm.hierarchy_information) {
		case HIERARCHY_NONE: val = (val&0xf8); break;
		case    HIERARCHY_1: val = (val&0xf8)|1; break;
		case    HIERARCHY_2: val = (val&0xf8)|2; break;
		case    HIERARCHY_4: val = (val&0xf8)|3; break;
		default:
			dprintk ("%s: invalid hierarchy\n",__FUNCTION__);
			return -EINVAL;
	}
	writereg(&state->demod, 0x06, val);

	val=0;
	switch(state->p.u.ofdm.code_rate_HP) {
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
	switch(state->p.u.ofdm.code_rate_LP) {
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
	writereg(&state->demod, 0x07, val);

	val=0;
	switch(state->p.u.ofdm.guard_interval) {
		case GUARD_INTERVAL_1_32: val = (val&0xf3); break;
		case GUARD_INTERVAL_1_16: val = (val&0xf3)|0x04; break;
		case  GUARD_INTERVAL_1_8: val = (val&0xf3)|0x08; break;
		case  GUARD_INTERVAL_1_4: val = (val&0xf3)|0x0c; break;
		default:
			dprintk ("%s: invalid guard_interval\n",__FUNCTION__);
			return -EINVAL;
	}
	switch(state->p.u.ofdm.transmission_mode) {
		case TRANSMISSION_MODE_2K: val = (val&0xfc); break;
		case TRANSMISSION_MODE_8K: val = (val&0xfc)|1; break;
		default:
			dprintk ("%s: invalid transmission_mode\n",__FUNCTION__);
			return -EINVAL;
	}
	writereg(&state->demod, 0x08, val);
	writereg(&state->demod, 0x0B, (readreg(&state->demod, 0x0B) & 0xfc) | 0x02 );
	writereg(&state->demod, 0x0C, (readreg(&state->demod, 0x0C) & 0xBF) | 0x40 );

	/* Begin channel aquisition */
	writereg(&state->demod, 0x00, 0x01);

	return 0;
}

/* Retrieve the demod settings */
static int cx22702_get_tps(struct cx22702_state *state,
			   struct dvb_ofdm_parameters *p)
{
	u8 val;

	/* Make sure the TPS regs are valid */
	if (!(readreg(&state->demod, 0x0A) & 0x20))
		return -EAGAIN;

	val = readreg(&state->demod, 0x01);
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

	val = readreg(&state->demod, 0x02);
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

	val = readreg(&state->demod, 0x03);
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

/* ------------------------------------------------------------------ */

/* Validate the demod, make sure we understand the hardware */
static int cx22702_validate_demod(struct i2c_client *c)
{
	int type = readreg(c, 0x1f);

	if (0x03 != type) {
		printk ("%s i2c demod type 0x%02x not known\n",
			__FUNCTION__, type);
		return -ENODEV;
	}
	return 0;
}

/* Validate the tuner PLL, make sure we understand the hardware */
static int cx22702_validate_pll(struct cx22702_state *state)
{
	int result=0;

	PLL_ENABLE(state);
	result = readreg(&state->pll,0xc2);
	PLL_DISABLE(state);
	return result;
}

/* ------------------------------------------------------------------ */

static int cx22702_init(struct dvb_frontend* fe)
{
	struct cx22702_state *state = fe->demodulator_priv;

	cx22702_reset(state);
	return 0;
}

static int cx22702_sleep(struct dvb_frontend* fe)
{
	struct cx22702_state *state = fe->demodulator_priv;
	u8 pllbuf[4];

	dprintk("%s\n",__FUNCTION__);

	dvb_pll_configure(state->pll_desc, pllbuf, 0, 0);
	PLL_ENABLE(state);
	pll_write4(&state->pll,pllbuf);
	PLL_DISABLE(state);
	return 0;
}

static int cx22702_set_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters* params)
{
	struct cx22702_state *state = fe->demodulator_priv;
	int ret;

	state->p = *params;
	ret=cx22702_set_tps(state);
	if (debug && ret < 0)
		printk("%s: set_tps failed ret=%d\n",__FUNCTION__,ret);
	return ret;
}

static int cx22702_get_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters* params)
{
	struct cx22702_state *state = fe->demodulator_priv;
	u8 reg0C = readreg(&state->demod, 0x0C);
	params->inversion = reg0C & 0x1 ? INVERSION_ON : INVERSION_OFF;
	return cx22702_get_tps(state, &params->u.ofdm);
}

#if 0
static int cx22702_get_tune_settings(struct dvb_frontend* fe,
				     struct dvb_frontend_tune_settings* settings)
{
	struct cx22702_state *state = fe->demodulator_priv;
}
#endif

static int cx22702_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct cx22702_state *state = fe->demodulator_priv;
	u8 reg0A = readreg(&state->demod, 0x0A);
	u8 reg23 = readreg(&state->demod, 0x23);

	*status = 0;
	if(reg0A & 0x10)
		*status |= FE_HAS_LOCK| FE_HAS_VITERBI | FE_HAS_SYNC;
	if(reg0A & 0x20)
		*status |= FE_HAS_CARRIER;
	if(reg23 < 0xf0)
		*status |= FE_HAS_SIGNAL;

	dprintk ("%s: status demod=0x%02x agc=0x%02x status=0x%x\n",
		 __FUNCTION__,reg0A,reg23,*status);
	return 0;
}

static int cx22702_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct cx22702_state *state = fe->demodulator_priv;

	*ber = (readreg(&state->demod, 0xDE) & 0x7F) << 7;
	*ber |= readreg(&state->demod, 0xDF) & 0x7F;
	return 0;
}

static int cx22702_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct cx22702_state *state = fe->demodulator_priv;

	*strength = readreg(&state->demod, 0x23);
	return 0;
}

static int cx22702_read_snr(struct dvb_frontend* fe, u16* snr)
{
	u32 ber;

	/* We don't have an register for this         */
	/* We'll take the inverse of the BER register */
	cx22702_read_ber(fe, &ber);
	*snr = ~ber;
	return 0;
}

static int cx22702_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct cx22702_state *state = fe->demodulator_priv;
	u8 ucb;

	/* RS Uncorrectable Packet Count then reset */
	ucb = readreg(&state->demod, 0xE3);
	if (state->prevUCBlocks < ucb)
		*ucblocks = (ucb - state->prevUCBlocks);
	else
		*ucblocks = 256 + ucb - state->prevUCBlocks;
	state->prevUCBlocks = ucb;
	return 0;
}

static void cx22702_release(struct dvb_frontend* fe)
{
	struct cx22702_state *state = fe->demodulator_priv;

	i2c_detach_client(&state->demod);
	i2c_detach_client(&state->pll);
	kfree(state);
}

static struct dvb_frontend_ops cx22702_fe_ops = {
	.info = {
		.name			= "cx22702 demod",
		.type			= FE_OFDM,
		.frequency_min		= 177000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 166666,
		.caps =
		FE_CAN_FEC_1_2 |
		FE_CAN_FEC_2_3 |
		FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 |
		FE_CAN_FEC_7_8 |
		FE_CAN_FEC_AUTO |
		FE_CAN_QPSK |
		FE_CAN_QAM_16 |
		FE_CAN_QAM_64 |
		FE_CAN_QAM_AUTO |
		FE_CAN_HIERARCHY_AUTO |
		FE_CAN_GUARD_INTERVAL_AUTO |
		FE_CAN_TRANSMISSION_MODE_AUTO |
		FE_CAN_RECOVER,
	},
	.init                 = cx22702_init,
	.sleep                = cx22702_sleep,
	.set_frontend         = cx22702_set_frontend,
	.get_frontend         = cx22702_get_frontend,
	.read_status          = cx22702_read_status,
	.read_ber             = cx22702_read_ber,
	.read_signal_strength = cx22702_read_signal_strength,
	.read_snr             = cx22702_read_snr,
	.read_ucblocks        = cx22702_read_ucblocks,
	.release              = cx22702_release,
};

void* cx22702_create(struct i2c_adapter *i2c,
		     int pll_addr, struct dvb_pll_desc *pll_desc,
		     int demod_addr)
{
	struct cx22702_state *state;
	int ret;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (NULL == state)
		return NULL;
	memset(state, 0, sizeof(*state));

	state->ops                 = cx22702_fe_ops;
	state->pll_desc            = pll_desc;
	state->fe.demodulator_priv = state;
	state->fe.ops              = &state->ops;

	state->demod         = demod_template;
	state->demod.adapter = i2c;
	state->demod.addr    = demod_addr;
	state->pll           = pll_template;
	strlcpy(state->pll.name, pll_desc->name, sizeof(state->pll.name));
	state->pll.adapter   = i2c;
	state->pll.addr      = pll_addr;
	i2c_set_clientdata(&state->demod, state);
	i2c_set_clientdata(&state->pll, state);

	/* verify devices */
	ret=cx22702_validate_demod(&state->demod);
	if (ret < 0)
		goto fail_free;
	ret=cx22702_validate_pll(state);
	if(ret < 0)
		goto fail_free;

	/* register i2c */
	ret = i2c_attach_client(&state->demod);
	if (0 != ret) {
		printk("cx22702: i2c demod register failed (%d)\n", ret);
		goto fail_free;
	}
	ret = i2c_attach_client(&state->pll);
	if (0 != ret) {
		printk("cx22702: i2c pll register failed (%d)\n", ret);
		goto fail_unreg1;
	}

	/* all fine ;) */
	return &state->fe;

fail_unreg1:
	i2c_detach_client(&state->demod);
fail_free:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(cx22702_create);

static int cx22702_suspend(struct device * dev, u32 state, u32 level)
{
	struct i2c_client *c = container_of(dev, struct i2c_client, dev);
	struct cx22702_state *st = i2c_get_clientdata(c);

	dprintk("cx22702: suspend\n");
	cx22702_sleep(&st->fe);
	return 0;
}

static int cx22702_resume(struct device * dev, u32 level)
{
	struct i2c_client *c = container_of(dev, struct i2c_client, dev);
	struct cx22702_state *st = i2c_get_clientdata(c);

	dprintk("cx22702: resume\n");
	cx22702_reset(st);
	if (st->p.frequency != 0)
		cx22702_set_tps(st);
	return 0;
}

/* ------------------------------------------------------------------ */

static struct i2c_driver demod_driver = {
	.owner = THIS_MODULE,
	.name  = __stringify(KBUILD_MODNAME) " demod",
	.id    = I2C_DRIVERID_DVBFE_CX22702,
	.driver = {
		.suspend = cx22702_suspend,
		.resume  = cx22702_resume,
	},
};
static struct i2c_client demod_template = {
	.name   = "cx22702",
	.flags  = I2C_CLIENT_ALLOW_USE,
	.driver = &demod_driver,
};

static struct i2c_driver pll_driver = {
	.owner = THIS_MODULE,
	.name  = __stringify(KBUILD_MODNAME) " pll",
	.id    = I2C_DRIVERID_DVBFE_CX22702,
};
static struct i2c_client pll_template = {
	.name   = "unset",
	.flags  = I2C_CLIENT_ALLOW_USE,
	.driver = &pll_driver,
};

static int __init init_cx22702 (void)
{
	i2c_add_driver(&demod_driver);
	i2c_add_driver(&pll_driver);
	return 0;
}

static void __exit exit_cx22702 (void)
{
	i2c_del_driver(&pll_driver);
	i2c_del_driver(&demod_driver);
}

module_init (init_cx22702);
module_exit (exit_cx22702);

MODULE_DESCRIPTION("CX22702 DVB Frontend driver");
MODULE_AUTHOR("Steven Toth");
MODULE_AUTHOR("Gerd Knorr");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 * compile-command: "make DVB=1"
 * End:
 */
