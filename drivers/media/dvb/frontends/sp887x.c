/*
   Driver for the Microtune 7202D Frontend
*/

/*
   This driver needs a copy of the Avermedia firmware. The version tested
   is part of the Avermedia DVB-T 1.3.26.3 Application. If the software is
   installed in Windows the file will be in the /Program Files/AVerTV DVB-T/
   directory and is called sc_main.mc. Alternatively it can "extracted" from
   the install cab files. Copy this file to '/usr/lib/hotplug/firmware/sc_main.mc'.
   With this version of the file the first 10 bytes are discarded and the
   next 0x4000 loaded. This may change in future versions.
 */

#define __KERNEL_SYSCALLS__
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/syscalls.h>

#include "dvb_frontend.h"
#include "dvb_functions.h"

#ifndef DVB_SP887X_FIRMWARE_FILE
#define DVB_SP887X_FIRMWARE_FILE "/usr/lib/hotplug/firmware/sc_main.mc"
#endif

static char *sp887x_firmware = DVB_SP887X_FIRMWARE_FILE;

#if 0
#define dprintk(x...) printk(x)
#else
#define dprintk(x...)
#endif

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


static
struct dvb_frontend_info sp887x_info = {
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

static int errno;

static
int i2c_writebytes (struct dvb_frontend *fe, u8 addr, u8 *buf, u8 len)
{
	struct dvb_i2c_bus *i2c = fe->i2c;
	struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = buf, .len = len };
	int err;

	LOG("i2c_writebytes", msg.addr, msg.buf, msg.len);

	if ((err = i2c->xfer (i2c, &msg, 1)) != 1) {
		printk ("%s: i2c write error (addr %02x, err == %i)\n",
			__FUNCTION__, addr, err);
		return -EREMOTEIO;
	}

	return 0;
}



static
int sp887x_writereg (struct dvb_frontend *fe, u16 reg, u16 data)
{
	struct dvb_i2c_bus *i2c = fe->i2c;
	u8 b0 [] = { reg >> 8 , reg & 0xff, data >> 8, data & 0xff };
	struct i2c_msg msg = { .addr = 0x70, .flags = 0, .buf = b0, .len = 4 };
	int ret;

	LOG("sp887x_writereg", msg.addr, msg.buf, msg.len);

	if ((ret = i2c->xfer(i2c, &msg, 1)) != 1) {
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


static
u16 sp887x_readreg (struct dvb_frontend *fe, u16 reg)
{
	struct dvb_i2c_bus *i2c = fe->i2c;
	u8 b0 [] = { reg >> 8 , reg & 0xff };
	u8 b1 [2];
	int ret;
	struct i2c_msg msg[] = {{ .addr = 0x70, .flags = 0, .buf = b0, .len = 2 },
		         { .addr = 0x70, .flags = I2C_M_RD, .buf = b1, .len = 2 }};

	LOG("sp887x_readreg (w)", msg[0].addr, msg[0].buf, msg[0].len);
	LOG("sp887x_readreg (r)", msg[1].addr, msg[1].buf, msg[1].len);

	if ((ret = i2c->xfer(i2c, msg, 2)) != 2)
		printk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return (((b1[0] << 8) | b1[1]) & 0xfff);
}


static
void sp887x_microcontroller_stop (struct dvb_frontend *fe)
{
	dprintk("%s\n", __FUNCTION__);
	sp887x_writereg(fe, 0xf08, 0x000);
	sp887x_writereg(fe, 0xf09, 0x000);		

	/* microcontroller STOP */
	sp887x_writereg(fe, 0xf00, 0x000);
}


static
void sp887x_microcontroller_start (struct dvb_frontend *fe)
{
	dprintk("%s\n", __FUNCTION__);
	sp887x_writereg(fe, 0xf08, 0x000);
	sp887x_writereg(fe, 0xf09, 0x000);		

	/* microcontroller START */
	sp887x_writereg(fe, 0xf00, 0x001);
}


static
void sp887x_setup_agc (struct dvb_frontend *fe)
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

/**
 *  load firmware and setup MPEG interface...
 */
static
int sp887x_initial_setup (struct dvb_frontend *fe)
{
	u8 buf [BLOCKSIZE+2];
	unsigned char *firmware = NULL;
	int i;
	int fd;
	int filesize;
	int fw_size;
	mm_segment_t fs;

	dprintk("%s\n", __FUNCTION__);

	/* soft reset */
	sp887x_writereg(fe, 0xf1a, 0x000);

	sp887x_microcontroller_stop (fe);

	fs = get_fs();

	// Load the firmware
	set_fs(get_ds());
	fd = open(sp887x_firmware, 0, 0);
	if (fd < 0) {
		printk(KERN_WARNING "%s: Unable to open firmware %s\n", __FUNCTION__,
		       sp887x_firmware);
		return -EIO;
	}
	filesize = lseek(fd, 0L, 2);
	if (filesize <= 0) {
		printk(KERN_WARNING "%s: Firmware %s is empty\n", __FUNCTION__,
		       sp887x_firmware);
		sys_close(fd);
		return -EIO;
	}

	fw_size = 0x4000;

	// allocate buffer for it
	firmware = vmalloc(fw_size);
	if (firmware == NULL) {
		printk(KERN_WARNING "%s: Out of memory loading firmware\n",
		       __FUNCTION__);
		sys_close(fd);
		return -EIO;
	}

	// read it!
	// read the first 16384 bytes from the file
	// ignore the first 10 bytes
	lseek(fd, 10, 0);
	if (read(fd, firmware, fw_size) != fw_size) {
		printk(KERN_WARNING "%s: Failed to read firmware\n", __FUNCTION__);
		vfree(firmware);
		sys_close(fd);
		return -EIO;
	}
	sys_close(fd);
	set_fs(fs);

	printk ("%s: firmware upload... ", __FUNCTION__);

	/* setup write pointer to -1 (end of memory) */
	/* bit 0x8000 in address is set to enable 13bit mode */
	sp887x_writereg(fe, 0x8f08, 0x1fff);

	/* dummy write (wrap around to start of memory) */
	sp887x_writereg(fe, 0x8f0a, 0x0000);

	for (i=0; i<fw_size; i+=BLOCKSIZE) {
		int c = BLOCKSIZE;
		int err;

		if (i+c > fw_size)
			c = fw_size - i;

		/* bit 0x8000 in address is set to enable 13bit mode */
		/* bit 0x4000 enables multibyte read/write transfers */
		/* write register is 0xf0a */
		buf[0] = 0xcf;
		buf[1] = 0x0a;

		memcpy(&buf[2], firmware + i, c);

		if ((err = i2c_writebytes (fe, 0x70, buf, c+2)) < 0) {
			printk ("failed.\n");
			printk ("%s: i2c error (err == %i)\n", __FUNCTION__, err);
			vfree(firmware);
			return err;
		}
	}

	vfree(firmware);

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
static
int tsa5060_setup_pll (struct dvb_frontend *fe, int freq)
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



static
int configure_reg0xc05 (struct dvb_frontend_parameters *p, u16 *reg0xc05)
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
static
void divide (int n, int d, int *quotient_i, int *quotient_f)
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


static
void sp887x_correct_offsets (struct dvb_frontend *fe,
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


static
int sp887x_setup_frontend_parameters (struct dvb_frontend *fe,
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


static
int sp887x_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
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
		if (fe->data == NULL) {	  /* first time initialisation... */
			fe->data = (void*) ~0;
			sp887x_initial_setup (fe);
		}
		/* enable TS output and interface pins */
		sp887x_writereg(fe, 0xc18, 0x00d);
		break;

	case FE_GET_TUNE_SETTINGS:
	{
	        struct dvb_frontend_tune_settings* fesettings = (struct dvb_frontend_tune_settings*) arg;
	        fesettings->min_delay_ms = 50;
	        fesettings->step_size = 0;
	        fesettings->max_drift = 0;
	        return 0;
	}	    

	default:
		return -EOPNOTSUPP;
        };

        return 0;
}



static
int sp887x_attach (struct dvb_i2c_bus *i2c, void **data)
{
	struct i2c_msg msg = {.addr = 0x70, .flags = 0, .buf = NULL, .len = 0 };

	dprintk ("%s\n", __FUNCTION__);

	if (i2c->xfer (i2c, &msg, 1) != 1)
                return -ENODEV;

	return dvb_register_frontend (sp887x_ioctl, i2c, NULL, &sp887x_info);
}


static
void sp887x_detach (struct dvb_i2c_bus *i2c, void *data)
{
	dprintk ("%s\n", __FUNCTION__);
	dvb_unregister_frontend (sp887x_ioctl, i2c);
}


static
int __init init_sp887x (void)
{
	dprintk ("%s\n", __FUNCTION__);
	return dvb_register_i2c_device (NULL, sp887x_attach, sp887x_detach);
}


static
void __exit exit_sp887x (void)
{
	dprintk ("%s\n", __FUNCTION__);
	dvb_unregister_i2c_device (sp887x_attach);
}


module_init(init_sp887x);
module_exit(exit_sp887x);


MODULE_DESCRIPTION("sp887x DVB-T demodulator driver");
MODULE_LICENSE("GPL");


