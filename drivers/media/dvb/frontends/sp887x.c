/*
   Driver for the Microtune 7202D Frontend
*/

/*   
 * This driver needs external firmware. Please use the command
 * "<kerneldir>/Documentation/dvb/get_dvb_firmware sp887x" to
 * download/extract it, and then copy it to /usr/lib/hotplug/firmware.
 */
#define SP887X_DEFAULT_FIRMWARE "dvb-fe-sp887x.fw"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/firmware.h>

#include "dvb_frontend.h"

#define FRONTEND_NAME "dvbfe_sp887x"

#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG FRONTEND_NAME ": " args); \
	} while (0)

static int debug;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

#if 0
#define LOG(dir,addr,buf,len) 					\
	do {							\
		int i;						\
		printk("%s (%02x):", dir, addr & 0xff);		\
		for (i=0; i<len; i++)				\
			printk(" 0x%02x,", buf[i] & 0xff);	\
		printk("\n");					\
	} while (0)
#else
#define LOG(dir,addr,buf,len)
#endif

static struct dvb_frontend_info sp887x_info = {
	.name = "Microtune MT7202DTF",
	.type = FE_OFDM,
	.frequency_min =  50500000,
	.frequency_max = 858000000,
	.frequency_stepsize = 166666,
	.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
                FE_CAN_RECOVER
};

struct sp887x_state {
	struct i2c_adapter *i2c;
	struct dvb_adapter *dvb;
};

static int i2c_writebytes (struct i2c_adapter *i2c, u8 addr, u8 *buf, u8 len)
{
	struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = buf, .len = len };
	int err;

	LOG("i2c_writebytes", msg.addr, msg.buf, msg.len);

	if ((err = i2c_transfer (i2c, &msg, 1)) != 1) {
		printk ("%s: i2c write error (addr %02x, err == %i)\n",
			__FUNCTION__, addr, err);
		return -EREMOTEIO;
	}

	return 0;
}

static int sp887x_writereg (struct i2c_adapter *i2c, u16 reg, u16 data)
{
	u8 b0 [] = { reg >> 8 , reg & 0xff, data >> 8, data & 0xff };
	struct i2c_msg msg = { .addr = 0x70, .flags = 0, .buf = b0, .len = 4 };
	int ret;

	LOG("sp887x_writereg", msg.addr, msg.buf, msg.len);

	if ((ret = i2c_transfer(i2c, &msg, 1)) != 1) {
		/**
		 *  in case of soft reset we ignore ACK errors...
		 */
		if (!(reg == 0xf1a && data == 0x000 &&
			(ret == -EREMOTEIO || ret == -EFAULT)))
		{
			printk("%s: writereg error "
			       "(reg %03x, data %03x, ret == %i)\n",
			       __FUNCTION__, reg & 0xffff, data & 0xffff, ret);
			return ret;
		}
	}

	return 0;
}

static u16 sp887x_readreg (struct i2c_adapter *i2c, u16 reg)
{
	u8 b0 [] = { reg >> 8 , reg & 0xff };
	u8 b1 [2];
	int ret;
	struct i2c_msg msg[] = {{ .addr = 0x70, .flags = 0, .buf = b0, .len = 2 },
		         { .addr = 0x70, .flags = I2C_M_RD, .buf = b1, .len = 2 }};

	LOG("sp887x_readreg (w)", msg[0].addr, msg[0].buf, msg[0].len);
	LOG("sp887x_readreg (r)", msg[1].addr, msg[1].buf, msg[1].len);

	if ((ret = i2c_transfer(i2c, msg, 2)) != 2)
		printk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return (((b1[0] << 8) | b1[1]) & 0xfff);
}

static void sp887x_microcontroller_stop (struct i2c_adapter *fe)
{
	dprintk("%s\n", __FUNCTION__);
	sp887x_writereg(fe, 0xf08, 0x000);
	sp887x_writereg(fe, 0xf09, 0x000);

	/* microcontroller STOP */
	sp887x_writereg(fe, 0xf00, 0x000);
}

static void sp887x_microcontroller_start (struct i2c_adapter *fe)
{
	dprintk("%s\n", __FUNCTION__);
	sp887x_writereg(fe, 0xf08, 0x000);
	sp887x_writereg(fe, 0xf09, 0x000);

	/* microcontroller START */
	sp887x_writereg(fe, 0xf00, 0x001);
}

static void sp887x_setup_agc (struct i2c_adapter *fe)
{
	/* setup AGC parameters */
	dprintk("%s\n", __FUNCTION__);
	sp887x_writereg(fe, 0x33c, 0x054);
	sp887x_writereg(fe, 0x33b, 0x04c);
	sp887x_writereg(fe, 0x328, 0x000);
	sp887x_writereg(fe, 0x327, 0x005);
	sp887x_writereg(fe, 0x326, 0x001);
	sp887x_writereg(fe, 0x325, 0x001);
	sp887x_writereg(fe, 0x324, 0x001);
	sp887x_writereg(fe, 0x318, 0x050);
	sp887x_writereg(fe, 0x317, 0x3fe);
	sp887x_writereg(fe, 0x316, 0x001);
	sp887x_writereg(fe, 0x313, 0x005);
	sp887x_writereg(fe, 0x312, 0x002);
	sp887x_writereg(fe, 0x306, 0x000);
	sp887x_writereg(fe, 0x303, 0x000);
}

#define BLOCKSIZE 30
#define FW_SIZE 0x4000
/**
 *  load firmware and setup MPEG interface...
 */
static int sp887x_initial_setup (struct i2c_adapter *fe, const struct firmware *fw)
{
	u8 buf [BLOCKSIZE+2];
	int i;
	int fw_size = fw->size;
	unsigned char *mem = fw->data;

	dprintk("%s\n", __FUNCTION__);

	/* ignore the first 10 bytes, then we expect 0x4000 bytes of firmware */
	if (fw_size < FW_SIZE+10)
		return -ENODEV;

	mem = fw->data + 10;

	/* soft reset */
	sp887x_writereg(fe, 0xf1a, 0x000);

	sp887x_microcontroller_stop (fe);

	printk ("%s: firmware upload... ", __FUNCTION__);

	/* setup write pointer to -1 (end of memory) */
	/* bit 0x8000 in address is set to enable 13bit mode */
	sp887x_writereg(fe, 0x8f08, 0x1fff);

	/* dummy write (wrap around to start of memory) */
	sp887x_writereg(fe, 0x8f0a, 0x0000);

	for (i = 0; i < FW_SIZE; i += BLOCKSIZE) {
		int c = BLOCKSIZE;
		int err;

		if (i+c > FW_SIZE)
			c = FW_SIZE - i;

		/* bit 0x8000 in address is set to enable 13bit mode */
		/* bit 0x4000 enables multibyte read/write transfers */
		/* write register is 0xf0a */
		buf[0] = 0xcf;
		buf[1] = 0x0a;

		memcpy(&buf[2], mem + i, c);

		if ((err = i2c_writebytes (fe, 0x70, buf, c+2)) < 0) {
			printk ("failed.\n");
			printk ("%s: i2c error (err == %i)\n", __FUNCTION__, err);
			return err;
		}
	}

	/* don't write RS bytes between packets */
	sp887x_writereg(fe, 0xc13, 0x001);

	/* suppress clock if (!data_valid) */
	sp887x_writereg(fe, 0xc14, 0x000);

	/* setup MPEG interface... */
	sp887x_writereg(fe, 0xc1a, 0x872);
	sp887x_writereg(fe, 0xc1b, 0x001);
	sp887x_writereg(fe, 0xc1c, 0x000); /* parallel mode (serial mode == 1) */
	sp887x_writereg(fe, 0xc1a, 0x871);

	/* ADC mode, 2 for MT8872, 3 for SP8870/SP8871 */
	sp887x_writereg(fe, 0x301, 0x002);

	sp887x_setup_agc(fe);

	/* bit 0x010: enable data valid signal */
	sp887x_writereg(fe, 0xd00, 0x010);
	sp887x_writereg(fe, 0x0d1, 0x000);

	printk ("done.\n");
	return 0;
};

/**
 *  returns the actual tuned center frequency which can be used
 *  to initialise the AFC registers
 */
static int tsa5060_setup_pll (struct i2c_adapter *fe, int freq)
{
	u8 cfg, cpump, band_select;
	u8 buf [4];
	u32 div;

	div = (36000000 + freq + 83333) / 166666;
	cfg = 0x88;

	cpump = freq < 175000000 ? 2 : freq < 390000000 ? 1 :
		freq < 470000000 ? 2 : freq < 750000000 ? 2 : 3;

	band_select = freq < 175000000 ? 0x0e : freq < 470000000 ? 0x05 : 0x03;

	buf [0] = (div >> 8) & 0x7f;
	buf [1] = div & 0xff;
	buf [2] = ((div >> 10) & 0x60) | cfg;
	buf [3] = cpump | band_select;

	/* open i2c gate for PLL message transmission... */
	sp887x_writereg(fe, 0x206, 0x001);
	i2c_writebytes(fe, 0x60, buf, 4);
	sp887x_writereg(fe, 0x206, 0x000);

	return (div * 166666 - 36000000);
}

static int configure_reg0xc05 (struct dvb_frontend_parameters *p, u16 *reg0xc05)
{
	int known_parameters = 1;

	*reg0xc05 = 0x000;

	switch (p->u.ofdm.constellation) {
	case QPSK:
		break;
	case QAM_16:
		*reg0xc05 |= (1 << 10);
		break;
	case QAM_64:
		*reg0xc05 |= (2 << 10);
		break;
	case QAM_AUTO:
		known_parameters = 0;
		break;
	default:
		return -EINVAL;
	};

	switch (p->u.ofdm.hierarchy_information) {
	case HIERARCHY_NONE:
		break;
	case HIERARCHY_1:
		*reg0xc05 |= (1 << 7);
		break;
	case HIERARCHY_2:
		*reg0xc05 |= (2 << 7);
		break;
	case HIERARCHY_4:
		*reg0xc05 |= (3 << 7);
		break;
	case HIERARCHY_AUTO:
		known_parameters = 0;
		break;
	default:
		return -EINVAL;
	};

	switch (p->u.ofdm.code_rate_HP) {
	case FEC_1_2:
		break;
	case FEC_2_3:
		*reg0xc05 |= (1 << 3);
		break;
	case FEC_3_4:
		*reg0xc05 |= (2 << 3);
		break;
	case FEC_5_6:
		*reg0xc05 |= (3 << 3);
		break;
	case FEC_7_8:
		*reg0xc05 |= (4 << 3);
		break;
	case FEC_AUTO:
		known_parameters = 0;
		break;
	default:
		return -EINVAL;
	};

	if (known_parameters)
		*reg0xc05 |= (2 << 1);	/* use specified parameters */
	else
		*reg0xc05 |= (1 << 1);	/* enable autoprobing */

	return 0;
}

/**
 *  estimates division of two 24bit numbers,
 *  derived from the ves1820/stv0299 driver code
 */
static void divide (int n, int d, int *quotient_i, int *quotient_f)
{
	unsigned int q, r;

	r = (n % d) << 8;
	q = (r / d);

	if (quotient_i)
		*quotient_i = q;

	if (quotient_f) {
		r = (r % d) << 8;
		q = (q << 8) | (r / d);
		r = (r % d) << 8;
		*quotient_f = (q << 8) | (r / d);
	}
}

static void sp887x_correct_offsets (struct i2c_adapter *fe,
			     struct dvb_frontend_parameters *p,
			     int actual_freq)
{
	static const u32 srate_correction [] = { 1879617, 4544878, 8098561 };
	int bw_index = p->u.ofdm.bandwidth - BANDWIDTH_8_MHZ;
	int freq_offset = actual_freq - p->frequency;
	int sysclock = 61003; //[kHz]
	int ifreq = 36000000;
	int freq;
	int frequency_shift;

	if (p->inversion == INVERSION_ON)
		freq = ifreq - freq_offset;
	else
		freq = ifreq + freq_offset;

	divide(freq / 333, sysclock, NULL, &frequency_shift);

	if (p->inversion == INVERSION_ON)
		frequency_shift = -frequency_shift;

	/* sample rate correction */
	sp887x_writereg(fe, 0x319, srate_correction[bw_index] >> 12);
	sp887x_writereg(fe, 0x31a, srate_correction[bw_index] & 0xfff);

	/* carrier offset correction */
	sp887x_writereg(fe, 0x309, frequency_shift >> 12);
	sp887x_writereg(fe, 0x30a, frequency_shift & 0xfff);
}

static int sp887x_setup_frontend_parameters (struct i2c_adapter *fe,
				      struct dvb_frontend_parameters *p)
{
	int actual_freq, err;
	u16 val, reg0xc05;

	if (p->u.ofdm.bandwidth != BANDWIDTH_8_MHZ &&
	    p->u.ofdm.bandwidth != BANDWIDTH_7_MHZ &&
	    p->u.ofdm.bandwidth != BANDWIDTH_6_MHZ)
		return -EINVAL;

	if ((err = configure_reg0xc05(p, &reg0xc05)))
		return err;

	sp887x_microcontroller_stop(fe);

	actual_freq = tsa5060_setup_pll(fe, p->frequency);

	/* read status reg in order to clear pending irqs */
	sp887x_readreg(fe, 0x200);

	sp887x_correct_offsets(fe, p, actual_freq);

	/* filter for 6/7/8 Mhz channel */
	if (p->u.ofdm.bandwidth == BANDWIDTH_6_MHZ)
		val = 2;
	else if (p->u.ofdm.bandwidth == BANDWIDTH_7_MHZ)
		val = 1;
	else
		val = 0;

	sp887x_writereg(fe, 0x311, val);

	/* scan order: 2k first = 0, 8k first = 1 */
	if (p->u.ofdm.transmission_mode == TRANSMISSION_MODE_2K)
		sp887x_writereg(fe, 0x338, 0x000);
	else
		sp887x_writereg(fe, 0x338, 0x001);

	sp887x_writereg(fe, 0xc05, reg0xc05);

	if (p->u.ofdm.bandwidth == BANDWIDTH_6_MHZ)
		val = 2 << 3;
	else if (p->u.ofdm.bandwidth == BANDWIDTH_7_MHZ)
		val = 3 << 3;
	else
		val = 0 << 3;

	/* enable OFDM and SAW bits as lock indicators in sync register 0xf17,
	 * optimize algorithm for given bandwidth...
	 */
	sp887x_writereg(fe, 0xf14, 0x160 | val);
	sp887x_writereg(fe, 0xf15, 0x000);

	sp887x_microcontroller_start(fe);
	return 0;
}

static int sp887x_ioctl(struct dvb_frontend *f, unsigned int cmd, void *arg)
{
	struct sp887x_state *state = (struct sp887x_state *) f->data;
	struct i2c_adapter *fe = state->i2c;

        switch (cmd) {
        case FE_GET_INFO:
		memcpy (arg, &sp887x_info, sizeof(struct dvb_frontend_info));
		break;

        case FE_READ_STATUS:
	{
		u16 snr12 = sp887x_readreg(fe, 0xf16);
		u16 sync0x200 = sp887x_readreg(fe, 0x200);
		u16 sync0xf17 = sp887x_readreg(fe, 0xf17);
		fe_status_t *status = arg;

		*status = 0;

		if (snr12 > 0x00f)
			*status |= FE_HAS_SIGNAL;

		//if (sync0x200 & 0x004)
		//	*status |= FE_HAS_SYNC | FE_HAS_CARRIER;

		//if (sync0x200 & 0x008)
		//	*status |= FE_HAS_VITERBI;

		if ((sync0xf17 & 0x00f) == 0x002) {
			*status |= FE_HAS_LOCK;
			*status |= FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_CARRIER;
		}

		if (sync0x200 & 0x001) {	/* tuner adjustment requested...*/
			int steps = (sync0x200 >> 4) & 0x00f;
			if (steps & 0x008)
				steps = -steps;
			dprintk("sp887x: implement tuner adjustment (%+i steps)!!\n",
			       steps);
		}

		break;

	}

        case FE_READ_BER:
	{
		u32* ber = arg;
		*ber = (sp887x_readreg(fe, 0xc08) & 0x3f) |
		       (sp887x_readreg(fe, 0xc07) << 6);
		sp887x_writereg(fe, 0xc08, 0x000);
		sp887x_writereg(fe, 0xc07, 0x000);
		if (*ber >= 0x3fff0)
			*ber = ~0;
		break;

	}

        case FE_READ_SIGNAL_STRENGTH:		// FIXME: correct registers ?
	{
		u16 snr12 = sp887x_readreg(fe, 0xf16);
		u32 signal = 3 * (snr12 << 4);
		*((u16*) arg) = (signal < 0xffff) ? signal : 0xffff;
		break;
	}

        case FE_READ_SNR:
	{
		u16 snr12 = sp887x_readreg(fe, 0xf16);
		*(u16*) arg = (snr12 << 4) | (snr12 >> 8);
		break;
	}

	case FE_READ_UNCORRECTED_BLOCKS:
	{
		u32 *ublocks = (u32 *) arg;
		*ublocks = sp887x_readreg(fe, 0xc0c);
		if (*ublocks == 0xfff)
			*ublocks = ~0;
		break;
	}

        case FE_SET_FRONTEND:
		return sp887x_setup_frontend_parameters(fe, arg);

	case FE_GET_FRONTEND:  // FIXME: read known values back from Hardware...
		break;

        case FE_SLEEP:
		/* tristate TS output and disable interface pins */
		sp887x_writereg(fe, 0xc18, 0x000);
		break;

        case FE_INIT:
		/* enable TS output and interface pins */
		sp887x_writereg(fe, 0xc18, 0x00d);
		break;

	case FE_GET_TUNE_SETTINGS:
	{
	        struct dvb_frontend_tune_settings* fesettings = (struct dvb_frontend_tune_settings*) arg;
	        fesettings->min_delay_ms = 350;
	        fesettings->step_size = 166666*2;
	        fesettings->max_drift = (166666*2)+1;
	        return 0;
	}

	default:
		return -EOPNOTSUPP;
        };

        return 0;
}

static struct i2c_client client_template;

static int attach_adapter(struct i2c_adapter *adapter)
{
	struct i2c_client *client;
	struct sp887x_state *state;
	const struct firmware *fw;
	int ret;

	struct i2c_msg msg = {.addr = 0x70, .flags = 0, .buf = NULL, .len = 0 };

	dprintk ("%s\n", __FUNCTION__);

	if (NULL == (client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL))) {
		return -ENOMEM;
	}

	if (NULL == (state = kmalloc(sizeof(struct sp887x_state), GFP_KERNEL))) {
		kfree(client);
		return -ENOMEM;
	}
	state->i2c = adapter;

	if (i2c_transfer (adapter, &msg, 1) != 1) {
		kfree(state);
		kfree(client);
                return -ENODEV;
	}

	memcpy(client, &client_template, sizeof(struct i2c_client));
	client->adapter = adapter;
	i2c_set_clientdata(client, (void*)state);

	ret = i2c_attach_client(client);
	if (ret) {
		kfree(client);
		kfree(state);
		return ret;
	}

	/* request the firmware, this will block until someone uploads it */
	printk("sp887x: waiting for firmware upload...\n");
	ret = request_firmware(&fw, SP887X_DEFAULT_FIRMWARE, &client->dev);
	if (ret) {
		printk("sp887x: no firmware upload (timeout or file not found?)\n");
		goto out;
	}

	ret = sp887x_initial_setup(adapter, fw);
	if (ret) {
		printk("sp887x: writing firmware to device failed\n");
		goto out;
	}

	ret = dvb_register_frontend(sp887x_ioctl, state->dvb, state,
					&sp887x_info, THIS_MODULE);
	if (ret) {
		printk("sp887x: registering frontend to dvb-core failed.\n");
		goto out;
	}

	return 0;
out:
	release_firmware(fw);
	i2c_detach_client(client);
	kfree(client);
	kfree(state);
	return ret;
}

static int detach_client(struct i2c_client *client)
{
	struct sp887x_state *state = (struct sp887x_state*)i2c_get_clientdata(client);

	dprintk ("%s\n", __FUNCTION__);

	dvb_unregister_frontend (sp887x_ioctl, state->dvb);
	i2c_detach_client(client);
	BUG_ON(state->dvb);
	kfree(client);
	kfree(state);
	return 0;
}

static int command (struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct sp887x_state *state = (struct sp887x_state*)i2c_get_clientdata(client);

	dprintk ("%s\n", __FUNCTION__);

	switch (cmd) {
	case FE_REGISTER:
		state->dvb = (struct dvb_adapter*)arg;
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
	.owner 		= THIS_MODULE,
	.name 		= FRONTEND_NAME,
	.id 		= I2C_DRIVERID_DVBFE_SP887X,
	.flags 		= I2C_DF_NOTIFY,
	.attach_adapter = attach_adapter,
	.detach_client 	= detach_client,
	.command 	= command,
};

static struct i2c_client client_template = {
	.name		= FRONTEND_NAME,
	.flags 		= I2C_CLIENT_ALLOW_USE,
	.driver  	= &driver,
};

static int __init init_sp887x(void)
{
	return i2c_add_driver(&driver);
}

static void __exit exit_sp887x(void)
{
	if (i2c_del_driver(&driver))
		printk("sp887x: driver deregistration failed\n");
}

module_init(init_sp887x);
module_exit(exit_sp887x);

MODULE_DESCRIPTION("sp887x DVB-T demodulator driver");
MODULE_LICENSE("GPL");

