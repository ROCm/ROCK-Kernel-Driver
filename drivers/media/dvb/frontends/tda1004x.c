  /*
     Driver for Philips tda1004xh OFDM Frontend

     (c) 2003, 2004 Andrew de Quincey & Robert Schlabbach

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

/*
    This driver needs a copy of the DLL "ttlcdacc.dll" from the Haupauge or Technotrend
    windows driver saved as '/usr/lib/hotplug/firmware/tda1004x.bin'.
    You can also pass the complete file name with the module parameter 'tda1004x_firmware'.

    Currently the DLL from v2.15a of the technotrend driver is supported. Other versions can
    be added reasonably painlessly.

    Windows driver URL: http://www.technotrend.de/
 */


#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include "dvb_frontend.h"
#include "dvb_functions.h"

#ifndef DVB_TDA1004X_FIRMWARE_FILE
#define DVB_TDA1004X_FIRMWARE_FILE "/usr/lib/hotplug/firmware/tda1004x.bin"
#endif

static int tda1004x_debug = 0;
static char *tda1004x_firmware = DVB_TDA1004X_FIRMWARE_FILE;

#define MC44BC374_ADDRESS        0x65

#define TDA1004X_CHIPID          0x00
#define TDA1004X_AUTO            0x01
#define TDA1004X_IN_CONF1        0x02
#define TDA1004X_IN_CONF2        0x03
#define TDA1004X_OUT_CONF1       0x04
#define TDA1004X_OUT_CONF2       0x05
#define TDA1004X_STATUS_CD       0x06
#define TDA1004X_CONFC4          0x07
#define TDA1004X_DSSPARE2        0x0C
#define TDA10045H_CODE_IN        0x0D
#define TDA10045H_FWPAGE         0x0E
#define TDA1004X_SCAN_CPT        0x10
#define TDA1004X_DSP_CMD         0x11
#define TDA1004X_DSP_ARG         0x12
#define TDA1004X_DSP_DATA1       0x13
#define TDA1004X_DSP_DATA2       0x14
#define TDA1004X_CONFADC1        0x15
#define TDA1004X_CONFC1          0x16
#define TDA10045H_S_AGC          0x1a
#define TDA10046H_AGC_TUN_LEVEL  0x1a
#define TDA1004X_SNR             0x1c
#define TDA1004X_CONF_TS1        0x1e
#define TDA1004X_CONF_TS2        0x1f
#define TDA1004X_CBER_RESET      0x20
#define TDA1004X_CBER_MSB        0x21
#define TDA1004X_CBER_LSB        0x22
#define TDA1004X_CVBER_LUT       0x23
#define TDA1004X_VBER_MSB        0x24
#define TDA1004X_VBER_MID        0x25
#define TDA1004X_VBER_LSB        0x26
#define TDA1004X_UNCOR           0x27

#define TDA10045H_CONFPLL_P      0x2D
#define TDA10045H_CONFPLL_M_MSB  0x2E
#define TDA10045H_CONFPLL_M_LSB  0x2F
#define TDA10045H_CONFPLL_N      0x30

#define TDA10046H_CONFPLL1       0x2D
#define TDA10046H_CONFPLL2       0x2F
#define TDA10046H_CONFPLL3       0x30
#define TDA10046H_TIME_WREF1     0x31
#define TDA10046H_TIME_WREF2     0x32
#define TDA10046H_TIME_WREF3     0x33
#define TDA10046H_TIME_WREF4     0x34
#define TDA10046H_TIME_WREF5     0x35

#define TDA10045H_UNSURW_MSB     0x31
#define TDA10045H_UNSURW_LSB     0x32
#define TDA10045H_WREF_MSB       0x33
#define TDA10045H_WREF_MID       0x34
#define TDA10045H_WREF_LSB       0x35
#define TDA10045H_MUXOUT         0x36
#define TDA1004X_CONFADC2        0x37

#define TDA10045H_IOFFSET        0x38

#define TDA10046H_CONF_TRISTATE1 0x3B
#define TDA10046H_CONF_TRISTATE2 0x3C
#define TDA10046H_CONF_POLARITY  0x3D
#define TDA10046H_FREQ_OFFSET    0x3E
#define TDA10046H_GPIO_OUT_SEL   0x41
#define TDA10046H_GPIO_SELECT    0x42
#define TDA10046H_AGC_CONF       0x43
#define TDA10046H_AGC_GAINS      0x46
#define TDA10046H_AGC_TUN_MIN    0x47
#define TDA10046H_AGC_TUN_MAX    0x48
#define TDA10046H_AGC_IF_MIN     0x49
#define TDA10046H_AGC_IF_MAX     0x4A

#define TDA10046H_FREQ_PHY2_MSB  0x4D
#define TDA10046H_FREQ_PHY2_LSB  0x4E

#define TDA10046H_CVBER_CTRL     0x4F
#define TDA10046H_AGC_IF_LEVEL   0x52
#define TDA10046H_CODE_CPT       0x57
#define TDA10046H_CODE_IN        0x58


#define FE_TYPE_TDA10045H     0
#define FE_TYPE_TDA10046H     1

#define TUNER_TYPE_TD1344     0
#define TUNER_TYPE_TD1316     1

#define dprintk if (tda1004x_debug) printk

static struct dvb_frontend_info tda10045h_info = {
	.name = "Philips TDA10045H",
	.type = FE_OFDM,
	.frequency_min = 51000000,
	.frequency_max = 858000000,
	.frequency_stepsize = 166667,
	.caps =
	    FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
	    FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
	    FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
	    FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO
};

static struct dvb_frontend_info tda10046h_info = {
        .name = "Philips TDA10046H",
        .type = FE_OFDM,
        .frequency_min = 51000000,
        .frequency_max = 858000000,
        .frequency_stepsize = 166667,
        .caps =
            FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
            FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
            FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
            FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO
};


#pragma pack(1)
struct tda1004x_state {
	u8 tda1004x_address;
	u8 tuner_address;
	u8 initialised:1;
        u8 tuner_type:2;
        u8 fe_type:2;
};
#pragma pack()

struct fwinfo {
	int file_size;
	int fw_offset;
	int fw_size;
};
static struct fwinfo tda10045h_fwinfo[] = { {.file_size = 286720,.fw_offset = 0x34cc5,.fw_size = 30555} };
static int tda10045h_fwinfo_count = sizeof(tda10045h_fwinfo) / sizeof(struct fwinfo);

static struct fwinfo tda10046h_fwinfo[] = { {.file_size = 286720,.fw_offset = 0x3c4f9,.fw_size = 24479} };
static int tda10046h_fwinfo_count = sizeof(tda10046h_fwinfo) / sizeof(struct fwinfo);



static int tda1004x_write_byte(struct dvb_i2c_bus *i2c, struct tda1004x_state *tda_state, int reg, int data)
{
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr=0, .flags=0, .buf=buf, .len=2 };

	dprintk("%s: reg=0x%x, data=0x%x\n", __FUNCTION__, reg, data);

        msg.addr = tda_state->tda1004x_address;
	ret = i2c->xfer(i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: error reg=0x%x, data=0x%x, ret=%i\n",
		       __FUNCTION__, reg, data, ret);

	dprintk("%s: success reg=0x%x, data=0x%x, ret=%i\n", __FUNCTION__,
		reg, data, ret);
	return (ret != 1) ? -1 : 0;
}

static int tda1004x_read_byte(struct dvb_i2c_bus *i2c, struct tda1004x_state *tda_state, int reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {{ .addr=0, .flags=0, .buf=b0, .len=1},
	                        { .addr=0, .flags=I2C_M_RD, .buf=b1, .len = 1}};

	dprintk("%s: reg=0x%x\n", __FUNCTION__, reg);

        msg[0].addr = tda_state->tda1004x_address;
        msg[1].addr = tda_state->tda1004x_address;
	ret = i2c->xfer(i2c, msg, 2);

	if (ret != 2) {
		dprintk("%s: error reg=0x%x, ret=%i\n", __FUNCTION__, reg,
		       ret);
		return -1;
	}

	dprintk("%s: success reg=0x%x, data=0x%x, ret=%i\n", __FUNCTION__,
		reg, b1[0], ret);
	return b1[0];
}

static int tda1004x_write_mask(struct dvb_i2c_bus *i2c, struct tda1004x_state *tda_state, int reg, int mask, int data)
{
        int val;
	dprintk("%s: reg=0x%x, mask=0x%x, data=0x%x\n", __FUNCTION__, reg,
		mask, data);

	// read a byte and check
	val = tda1004x_read_byte(i2c, tda_state, reg);
	if (val < 0)
		return val;

	// mask if off
	val = val & ~mask;
	val |= data & 0xff;

	// write it out again
	return tda1004x_write_byte(i2c, tda_state, reg, val);
}

static int tda1004x_write_buf(struct dvb_i2c_bus *i2c, struct tda1004x_state *tda_state, int reg, unsigned char *buf, int len)
{
	int i;
	int result;

	dprintk("%s: reg=0x%x, len=0x%x\n", __FUNCTION__, reg, len);

	result = 0;
	for (i = 0; i < len; i++) {
		result = tda1004x_write_byte(i2c, tda_state, reg + i, buf[i]);
		if (result != 0)
			break;
	}

	return result;
}

static int tda1004x_enable_tuner_i2c(struct dvb_i2c_bus *i2c, struct tda1004x_state *tda_state)
{
        int result;
	dprintk("%s\n", __FUNCTION__);

	result = tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 2, 2);
	dvb_delay(1);
	return result;
}

static int tda1004x_disable_tuner_i2c(struct dvb_i2c_bus *i2c, struct tda1004x_state *tda_state)
{

	dprintk("%s\n", __FUNCTION__);

	return tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 2, 0);
}


static int tda10045h_set_bandwidth(struct dvb_i2c_bus *i2c,
	                           struct tda1004x_state *tda_state,
		                   fe_bandwidth_t bandwidth)
{
        static u8 bandwidth_6mhz[] = { 0x02, 0x00, 0x3d, 0x00, 0x60, 0x1e, 0xa7, 0x45, 0x4f };
        static u8 bandwidth_7mhz[] = { 0x02, 0x00, 0x37, 0x00, 0x4a, 0x2f, 0x6d, 0x76, 0xdb };
        static u8 bandwidth_8mhz[] = { 0x02, 0x00, 0x3d, 0x00, 0x48, 0x17, 0x89, 0xc7, 0x14 };

        switch (bandwidth) {
	case BANDWIDTH_6_MHZ:
		tda1004x_write_byte(i2c, tda_state, TDA1004X_DSSPARE2, 0x14);
                tda1004x_write_buf(i2c, tda_state, TDA10045H_CONFPLL_P, bandwidth_6mhz, sizeof(bandwidth_6mhz));
		break;

	case BANDWIDTH_7_MHZ:
		tda1004x_write_byte(i2c, tda_state, TDA1004X_DSSPARE2, 0x80);
                tda1004x_write_buf(i2c, tda_state, TDA10045H_CONFPLL_P, bandwidth_7mhz, sizeof(bandwidth_7mhz));
		break;

	case BANDWIDTH_8_MHZ:
		tda1004x_write_byte(i2c, tda_state, TDA1004X_DSSPARE2, 0x14);
                tda1004x_write_buf(i2c, tda_state, TDA10045H_CONFPLL_P, bandwidth_8mhz, sizeof(bandwidth_8mhz));
		break;

	default:
		return -EINVAL;
	}

        tda1004x_write_byte(i2c, tda_state, TDA10045H_IOFFSET, 0);

        // done
        return 0;
}


static int tda10046h_set_bandwidth(struct dvb_i2c_bus *i2c,
                                   struct tda1004x_state *tda_state,
                                   fe_bandwidth_t bandwidth)
{
        static u8 bandwidth_6mhz[] = { 0x80, 0x15, 0xfe, 0xab, 0x8e };
        static u8 bandwidth_7mhz[] = { 0x6e, 0x02, 0x53, 0xc8, 0x25 };
        static u8 bandwidth_8mhz[] = { 0x60, 0x12, 0xa8, 0xe4, 0xbd };

        switch (bandwidth) {
        case BANDWIDTH_6_MHZ:
                tda1004x_write_buf(i2c, tda_state, TDA10046H_TIME_WREF1, bandwidth_6mhz, sizeof(bandwidth_6mhz));
                tda1004x_write_byte(i2c, tda_state, TDA1004X_DSSPARE2, 0);
                break;

        case BANDWIDTH_7_MHZ:
                tda1004x_write_buf(i2c, tda_state, TDA10046H_TIME_WREF1, bandwidth_7mhz, sizeof(bandwidth_7mhz));
                tda1004x_write_byte(i2c, tda_state, TDA1004X_DSSPARE2, 0);
                break;

        case BANDWIDTH_8_MHZ:
                tda1004x_write_buf(i2c, tda_state, TDA10046H_TIME_WREF1, bandwidth_8mhz, sizeof(bandwidth_8mhz));
                tda1004x_write_byte(i2c, tda_state, TDA1004X_DSSPARE2, 0xFF);
                break;

        default:
                return -EINVAL;
        }

        // done
        return 0;
}


static int tda1004x_fwupload(struct dvb_i2c_bus *i2c, struct tda1004x_state *tda_state)
{
	u8 fw_buf[65];
	struct i2c_msg fw_msg = {.addr = 0,.flags = 0,.buf = fw_buf,.len = 0 };
	unsigned char *firmware = NULL;
	int filesize;
	int fd;
	int fwinfo_idx;
	int fw_size = 0;
        int fw_pos, fw_offset;
	int tx_size;
	mm_segment_t fs = get_fs();
        int dspCodeCounterReg=0, dspCodeInReg=0, dspVersion=0;
        int fwInfoCount=0;
        struct fwinfo* fwInfo = NULL;
        unsigned long timeout;

        // DSP parameters
        switch(tda_state->fe_type) {
        case FE_TYPE_TDA10045H:
                dspCodeCounterReg = TDA10045H_FWPAGE;
                dspCodeInReg = TDA10045H_CODE_IN;
                dspVersion = 0x2c;
                fwInfoCount = tda10045h_fwinfo_count;
                fwInfo = tda10045h_fwinfo;
                break;

        case FE_TYPE_TDA10046H:
                dspCodeCounterReg = TDA10046H_CODE_CPT;
                dspCodeInReg = TDA10046H_CODE_IN;
                dspVersion = 0x20;
                fwInfoCount = tda10046h_fwinfo_count;
                fwInfo = tda10046h_fwinfo;
                break;
        }

	// Load the firmware
	set_fs(get_ds());
	fd = sys_open(tda1004x_firmware, 0, 0);
	if (fd < 0) {
		printk("%s: Unable to open firmware %s\n", __FUNCTION__,
		       tda1004x_firmware);
		return -EIO;
	}
	filesize = sys_lseek(fd, 0L, 2);
	if (filesize <= 0) {
		printk("%s: Firmware %s is empty\n", __FUNCTION__,
		       tda1004x_firmware);
		sys_close(fd);
		return -EIO;
	}

        // find extraction parameters for firmware
        for (fwinfo_idx = 0; fwinfo_idx < fwInfoCount; fwinfo_idx++) {
                if (fwInfo[fwinfo_idx].file_size == filesize)
			break;
	}
        if (fwinfo_idx >= fwInfoCount) {
		printk("%s: Unsupported firmware %s\n", __FUNCTION__, tda1004x_firmware);
		sys_close(fd);
		return -EIO;
	}
        fw_size = fwInfo[fwinfo_idx].fw_size;
        fw_offset = fwInfo[fwinfo_idx].fw_offset;

	// allocate buffer for it
	firmware = vmalloc(fw_size);
	if (firmware == NULL) {
		printk("%s: Out of memory loading firmware\n",
		       __FUNCTION__);
		sys_close(fd);
		return -EIO;
	}

	// read it!
        sys_lseek(fd, fw_offset, 0);
	if (sys_read(fd, firmware, fw_size) != fw_size) {
		printk("%s: Failed to read firmware\n", __FUNCTION__);
		vfree(firmware);
		sys_close(fd);
		return -EIO;
	}
	sys_close(fd);
	set_fs(fs);

        // set some valid bandwith parameters before uploading
        switch(tda_state->fe_type) {
        case FE_TYPE_TDA10045H:
                // reset chip
                tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 8, 8);
                tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 8, 0);
                dvb_delay(10);

                // set parameters
                tda10045h_set_bandwidth(i2c, tda_state, BANDWIDTH_8_MHZ);
                break;

        case FE_TYPE_TDA10046H:
                // reset chip
                tda1004x_write_mask(i2c, tda_state, TDA10046H_CONF_TRISTATE1, 1, 0);
                dvb_delay(10);

                // set parameters
                tda1004x_write_byte(i2c, tda_state, TDA10046H_CONFPLL2, 10);
                tda1004x_write_byte(i2c, tda_state, TDA10046H_CONFPLL3, 0);
                tda1004x_write_byte(i2c, tda_state, TDA10046H_FREQ_OFFSET, 99);
                tda1004x_write_byte(i2c, tda_state, TDA10046H_FREQ_PHY2_MSB, 0xd4);
                tda1004x_write_byte(i2c, tda_state, TDA10046H_FREQ_PHY2_LSB, 0x2c);
                tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 8, 8); // going to boot from HOST
                break;
        }

	// do the firmware upload
        tda1004x_write_byte(i2c, tda_state, dspCodeCounterReg, 0); // clear code counter
        fw_msg.addr = tda_state->tda1004x_address;
	fw_pos = 0;
	while (fw_pos != fw_size) {

		// work out how much to send this time
		tx_size = fw_size - fw_pos;
                if (tx_size > 0x10) {
                        tx_size = 0x10;
		}

		// send the chunk
                fw_buf[0] = dspCodeInReg;
		memcpy(fw_buf + 1, firmware + fw_pos, tx_size);
		fw_msg.len = tx_size + 1;
		if (i2c->xfer(i2c, &fw_msg, 1) != 1) {
                        printk("tda1004x: Error during firmware upload\n");
			vfree(firmware);
			return -EIO;
		}
		fw_pos += tx_size;

		dprintk("%s: fw_pos=0x%x\n", __FUNCTION__, fw_pos);
	}
	vfree(firmware);

        // wait for DSP to initialise
        switch(tda_state->fe_type) {
        case FE_TYPE_TDA10045H:
                // DSPREADY doesn't seem to work on the TDA10045H
                dvb_delay(100);
                break;

        case FE_TYPE_TDA10046H:
                timeout = jiffies + HZ;
                while(!(tda1004x_read_byte(i2c, tda_state, TDA1004X_STATUS_CD) & 0x20)) {
                        if (time_after(jiffies, timeout)) {
                                printk("tda1004x: DSP failed to initialised.\n");
                                return -EIO;
                        }

                        dvb_delay(1);
                }
                break;
        }

        // check upload was OK
        tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 0x10, 0); // we want to read from the DSP
	tda1004x_write_byte(i2c, tda_state, TDA1004X_DSP_CMD, 0x67);
	if ((tda1004x_read_byte(i2c, tda_state, TDA1004X_DSP_DATA1) != 0x67) ||
            (tda1004x_read_byte(i2c, tda_state, TDA1004X_DSP_DATA2) != dspVersion)) {
		printk("%s: firmware upload failed!\n", __FUNCTION__);
		return -EIO;
	}

        // success
        return 0;
}


static int tda10045h_init(struct dvb_i2c_bus *i2c, struct tda1004x_state *tda_state)
{
        struct i2c_msg tuner_msg = {.addr = 0,.flags = 0,.buf = 0,.len = 0 };
        static u8 disable_mc44BC374c[] = { 0x1d, 0x74, 0xa0, 0x68 };

        dprintk("%s\n", __FUNCTION__);

        // Disable the MC44BC374C
        tda1004x_enable_tuner_i2c(i2c, tda_state);
        tuner_msg.addr = MC44BC374_ADDRESS;
        tuner_msg.buf = disable_mc44BC374c;
        tuner_msg.len = sizeof(disable_mc44BC374c);
        if (i2c->xfer(i2c, &tuner_msg, 1) != 1) {
                i2c->xfer(i2c, &tuner_msg, 1);
        }
        tda1004x_disable_tuner_i2c(i2c, tda_state);

	// tda setup
        tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 0x20, 0); // disable DSP watchdog timer
        tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 8, 0); // select HP stream
        tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC1, 0x40, 0); // no frequency inversion
        tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC1, 0x80, 0x80); // enable pulse killer
        tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 0x10, 0x10); // enable auto offset
        tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF2, 0xC0, 0x0); // no frequency offset
        tda1004x_write_byte(i2c, tda_state, TDA1004X_CONF_TS1, 0); // setup MPEG2 TS interface
        tda1004x_write_byte(i2c, tda_state, TDA1004X_CONF_TS2, 0); // setup MPEG2 TS interface
        tda1004x_write_mask(i2c, tda_state, TDA1004X_VBER_MSB, 0xe0, 0xa0); // 10^6 VBER measurement bits
        tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC1, 0x10, 0); // VAGC polarity
        tda1004x_write_byte(i2c, tda_state, TDA1004X_CONFADC1, 0x2e);

	// done
	return 0;
}



static int tda10046h_init(struct dvb_i2c_bus *i2c, struct tda1004x_state *tda_state)
{
        struct i2c_msg tuner_msg = {.addr = 0,.flags = 0,.buf = 0,.len = 0 };
        static u8 disable_mc44BC374c[] = { 0x1d, 0x74, 0xa0, 0x68 };

        dprintk("%s\n", __FUNCTION__);

        // Disable the MC44BC374C
        tda1004x_enable_tuner_i2c(i2c, tda_state);
        tuner_msg.addr = MC44BC374_ADDRESS;
        tuner_msg.buf = disable_mc44BC374c;
        tuner_msg.len = sizeof(disable_mc44BC374c);
        if (i2c->xfer(i2c, &tuner_msg, 1) != 1) {
                i2c->xfer(i2c, &tuner_msg, 1);
        }
        tda1004x_disable_tuner_i2c(i2c, tda_state);

        // tda setup
        tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 0x20, 0); // disable DSP watchdog timer
        tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC1, 0x40, 0x40); // TT TDA10046H needs inversion ON
        tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 8, 0); // select HP stream
        tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC1, 0x80, 0); // disable pulse killer
        tda1004x_write_byte(i2c, tda_state, TDA10046H_CONFPLL2, 10); // PLL M = 10
        tda1004x_write_byte(i2c, tda_state, TDA10046H_CONFPLL3, 0); // PLL P = N = 0
        tda1004x_write_byte(i2c, tda_state, TDA10046H_FREQ_OFFSET, 99); // FREQOFFS = 99
        tda1004x_write_byte(i2c, tda_state, TDA10046H_FREQ_PHY2_MSB, 0xd4); // } PHY2 = -11221
        tda1004x_write_byte(i2c, tda_state, TDA10046H_FREQ_PHY2_LSB, 0x2c); // }
        tda1004x_write_byte(i2c, tda_state, TDA10046H_AGC_CONF, 0); // AGC setup
        tda1004x_write_mask(i2c, tda_state, TDA10046H_CONF_POLARITY, 0x60, 0x60); // set AGC polarities
        tda1004x_write_byte(i2c, tda_state, TDA10046H_AGC_TUN_MIN, 0);    // }
        tda1004x_write_byte(i2c, tda_state, TDA10046H_AGC_TUN_MAX, 0xff); // } AGC min/max values
        tda1004x_write_byte(i2c, tda_state, TDA10046H_AGC_IF_MIN, 0);     // }
        tda1004x_write_byte(i2c, tda_state, TDA10046H_AGC_IF_MAX, 0xff);  // }
        tda1004x_write_mask(i2c, tda_state, TDA10046H_CVBER_CTRL, 0x30, 0x10); // 10^6 VBER measurement bits
        tda1004x_write_byte(i2c, tda_state, TDA10046H_AGC_GAINS, 1); // IF gain 2, TUN gain 1
        tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 0x80, 0); // crystal is 50ppm
        tda1004x_write_byte(i2c, tda_state, TDA1004X_CONF_TS1, 7); // MPEG2 interface config
        tda1004x_write_mask(i2c, tda_state, TDA1004X_CONF_TS2, 0x31, 0); // MPEG2 interface config
        tda1004x_write_mask(i2c, tda_state, TDA10046H_CONF_TRISTATE1, 0x9e, 0); // disable AGC_TUN
        tda1004x_write_byte(i2c, tda_state, TDA10046H_CONF_TRISTATE2, 0xe1); // tristate setup
        tda1004x_write_byte(i2c, tda_state, TDA10046H_GPIO_OUT_SEL, 0xcc); // GPIO output config
        tda1004x_write_mask(i2c, tda_state, TDA10046H_GPIO_SELECT, 8, 8); // GPIO select
        tda10046h_set_bandwidth(i2c, tda_state, BANDWIDTH_8_MHZ); // default bandwidth 8 MHz

        // done
        return 0;
}



static int tda1004x_encode_fec(int fec)
{
	// convert known FEC values
	switch (fec) {
	case FEC_1_2:
		return 0;
	case FEC_2_3:
		return 1;
	case FEC_3_4:
		return 2;
	case FEC_5_6:
		return 3;
	case FEC_7_8:
		return 4;
	}

	// unsupported
	return -EINVAL;
}

static int tda1004x_decode_fec(int tdafec)
{
	// convert known FEC values
	switch (tdafec) {
	case 0:
		return FEC_1_2;
	case 1:
		return FEC_2_3;
	case 2:
		return FEC_3_4;
	case 3:
		return FEC_5_6;
	case 4:
		return FEC_7_8;
	}

	// unsupported
	return -1;
}

static int tda1004x_set_frequency(struct dvb_i2c_bus *i2c,
			   struct tda1004x_state *tda_state,
			   struct dvb_frontend_parameters *fe_params)
{
	u8 tuner_buf[4];
	struct i2c_msg tuner_msg = {.addr=0, .flags=0, .buf=tuner_buf, .len=sizeof(tuner_buf) };
        int tuner_frequency = 0;
        u8 band, cp, filter;
	int counter, counter2;

	dprintk("%s\n", __FUNCTION__);

	// setup the frequency buffer
        switch (tda_state->tuner_type) {
        case TUNER_TYPE_TD1344:

		// setup tuner buffer
                // ((Fif+((1000000/6)/2)) + Finput)/(1000000/6)
		tuner_frequency =
                        (((fe_params->frequency / 1000) * 6) + 217502) / 1000;
		tuner_buf[0] = tuner_frequency >> 8;
		tuner_buf[1] = tuner_frequency & 0xff;
		tuner_buf[2] = 0x88;
		if (fe_params->frequency < 550000000) {
			tuner_buf[3] = 0xab;
		} else {
			tuner_buf[3] = 0xeb;
		}

		// tune it
		tda1004x_enable_tuner_i2c(i2c, tda_state);
		tuner_msg.addr = tda_state->tuner_address;
		tuner_msg.len = 4;
		i2c->xfer(i2c, &tuner_msg, 1);

		// wait for it to finish
		tuner_msg.len = 1;
		tuner_msg.flags = I2C_M_RD;
		counter = 0;
		counter2 = 0;
		while (counter++ < 100) {
			if (i2c->xfer(i2c, &tuner_msg, 1) == 1) {
				if (tuner_buf[0] & 0x40) {
					counter2++;
				} else {
					counter2 = 0;
				}
			}

			if (counter2 > 10) {
				break;
			}
		}
		tda1004x_disable_tuner_i2c(i2c, tda_state);
		break;

        case TUNER_TYPE_TD1316:
		// determine charge pump
		tuner_frequency = fe_params->frequency + 36130000;
		if (tuner_frequency < 87000000) {
			return -EINVAL;
		} else if (tuner_frequency < 130000000) {
                        cp = 3;
		} else if (tuner_frequency < 160000000) {
			cp = 5;
		} else if (tuner_frequency < 200000000) {
			cp = 6;
		} else if (tuner_frequency < 290000000) {
			cp = 3;
		} else if (tuner_frequency < 420000000) {
			cp = 5;
		} else if (tuner_frequency < 480000000) {
			cp = 6;
		} else if (tuner_frequency < 620000000) {
			cp = 3;
		} else if (tuner_frequency < 830000000) {
			cp = 5;
		} else if (tuner_frequency < 895000000) {
			cp = 7;
		} else {
			return -EINVAL;
		}

		// determine band
		if (fe_params->frequency < 49000000) {
                        return -EINVAL;
		} else if (fe_params->frequency < 159000000) {
                        band = 1;
		} else if (fe_params->frequency < 444000000) {
			band = 2;
		} else if (fe_params->frequency < 861000000) {
			band = 4;
		} else {
			return -EINVAL;
		}

		// work out filter
		switch (fe_params->u.ofdm.bandwidth) {
		case BANDWIDTH_6_MHZ:
                        filter = 0;
                        break;

                case BANDWIDTH_7_MHZ:
			filter = 0;
			break;

		case BANDWIDTH_8_MHZ:
			filter = 1;
			break;

		default:
			return -EINVAL;
		}

                // calculate divisor
                // ((36130000+((1000000/6)/2)) + Finput)/(1000000/6)
		tuner_frequency =
                        (((fe_params->frequency / 1000) * 6) + 217280) / 1000;

                // setup tuner buffer
		tuner_buf[0] = tuner_frequency >> 8;
		tuner_buf[1] = tuner_frequency & 0xff;
		tuner_buf[2] = 0xca;
		tuner_buf[3] = (cp << 5) | (filter << 3) | band;

		// tune it
                if (tda_state->fe_type == FE_TYPE_TDA10046H) {
                        // setup auto offset
                        tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 0x10, 0x10);
                        tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x80, 0);
                        tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF2, 0xC0, 0);

                        // disable agc_conf[2]
                        tda1004x_write_mask(i2c, tda_state, TDA10046H_AGC_CONF, 4, 0);
                }
		tda1004x_enable_tuner_i2c(i2c, tda_state);
		tuner_msg.addr = tda_state->tuner_address;
		tuner_msg.len = 4;
                if (i2c->xfer(i2c, &tuner_msg, 1) != 1) {
			return -EIO;
		}
		dvb_delay(1);
		tda1004x_disable_tuner_i2c(i2c, tda_state);
                if (tda_state->fe_type == FE_TYPE_TDA10046H)
                        tda1004x_write_mask(i2c, tda_state, TDA10046H_AGC_CONF, 4, 4);
		break;

	default:
		return -EINVAL;
	}

	dprintk("%s: success\n", __FUNCTION__);

	// done
	return 0;
}

static int tda1004x_set_fe(struct dvb_i2c_bus *i2c,
	 	           struct tda1004x_state *tda_state,
		           struct dvb_frontend_parameters *fe_params)
{
	int tmp;
        int inversion;

	dprintk("%s\n", __FUNCTION__);

	// set frequency
        if ((tmp = tda1004x_set_frequency(i2c, tda_state, fe_params)) < 0)
		return tmp;

        // hardcoded to use auto as much as possible
        fe_params->u.ofdm.code_rate_HP = FEC_AUTO;
        fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;
        fe_params->u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;

	// Set standard params.. or put them to auto
	if ((fe_params->u.ofdm.code_rate_HP == FEC_AUTO) ||
	    (fe_params->u.ofdm.code_rate_LP == FEC_AUTO) ||
	    (fe_params->u.ofdm.constellation == QAM_AUTO) ||
	    (fe_params->u.ofdm.hierarchy_information == HIERARCHY_AUTO)) {
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 1, 1);	// enable auto
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x03, 0);	// turn off constellation bits
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x60, 0);	// turn off hierarchy bits
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF2, 0x3f, 0);	// turn off FEC bits
	} else {
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 1, 0);	// disable auto

		// set HP FEC
		tmp = tda1004x_encode_fec(fe_params->u.ofdm.code_rate_HP);
		if (tmp < 0) return tmp;
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF2, 7, tmp);

		// set LP FEC
		if (fe_params->u.ofdm.code_rate_LP != FEC_NONE) {
			tmp = tda1004x_encode_fec(fe_params->u.ofdm.code_rate_LP);
			if (tmp < 0) return tmp;
			tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF2, 0x38, tmp << 3);
		}

		// set constellation
		switch (fe_params->u.ofdm.constellation) {
		case QPSK:
			tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 3, 0);
			break;

		case QAM_16:
			tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 3, 1);
			break;

		case QAM_64:
			tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 3, 2);
			break;

		default:
			return -EINVAL;
		}

		// set hierarchy
		switch (fe_params->u.ofdm.hierarchy_information) {
		case HIERARCHY_NONE:
			tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x60, 0 << 5);
			break;

		case HIERARCHY_1:
			tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x60, 1 << 5);
			break;

		case HIERARCHY_2:
			tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x60, 2 << 5);
			break;

		case HIERARCHY_4:
			tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x60, 3 << 5);
			break;

		default:
			return -EINVAL;
		}
	}

        // set bandwidth
        switch(tda_state->fe_type) {
        case FE_TYPE_TDA10045H:
                tda10045h_set_bandwidth(i2c, tda_state, fe_params->u.ofdm.bandwidth);
                break;

        case FE_TYPE_TDA10046H:
                tda10046h_set_bandwidth(i2c, tda_state, fe_params->u.ofdm.bandwidth);
                break;
        }

        // need to invert the inversion for TT TDA10046H
        inversion = fe_params->inversion;
        if (tda_state->fe_type == FE_TYPE_TDA10046H) {
                inversion = inversion ? INVERSION_OFF : INVERSION_ON;
        }

	// set inversion
        switch (inversion) {
	case INVERSION_OFF:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC1, 0x20, 0);
		break;

	case INVERSION_ON:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC1, 0x20, 0x20);
		break;

	default:
		return -EINVAL;
	}

	// set guard interval
	switch (fe_params->u.ofdm.guard_interval) {
	case GUARD_INTERVAL_1_32:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 2, 0);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x0c, 0 << 2);
		break;

	case GUARD_INTERVAL_1_16:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 2, 0);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x0c, 1 << 2);
		break;

	case GUARD_INTERVAL_1_8:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 2, 0);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x0c, 2 << 2);
		break;

	case GUARD_INTERVAL_1_4:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 2, 0);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x0c, 3 << 2);
		break;

	case GUARD_INTERVAL_AUTO:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 2, 2);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x0c, 0 << 2);
		break;

	default:
		return -EINVAL;
	}

	// set transmission mode
	switch (fe_params->u.ofdm.transmission_mode) {
	case TRANSMISSION_MODE_2K:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 4, 0);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x10, 0 << 4);
		break;

	case TRANSMISSION_MODE_8K:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 4, 0);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x10, 1 << 4);
		break;

	case TRANSMISSION_MODE_AUTO:
		tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 4, 4);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_IN_CONF1, 0x10, 0);
		break;

	default:
		return -EINVAL;
	}

        // start the lock
        switch(tda_state->fe_type) {
        case FE_TYPE_TDA10045H:
	tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 8, 8);
	tda1004x_write_mask(i2c, tda_state, TDA1004X_CONFC4, 8, 0);
	dvb_delay(10);
                break;

        case FE_TYPE_TDA10046H:
                tda1004x_write_mask(i2c, tda_state, TDA1004X_AUTO, 0x40, 0x40);
                dvb_delay(10);
                break;
        }

	// done
	return 0;
}


static int tda1004x_get_fe(struct dvb_i2c_bus *i2c, struct tda1004x_state* tda_state, struct dvb_frontend_parameters *fe_params)
{

	dprintk("%s\n", __FUNCTION__);

	// inversion status
	fe_params->inversion = INVERSION_OFF;
	if (tda1004x_read_byte(i2c, tda_state, TDA1004X_CONFC1) & 0x20) {
		fe_params->inversion = INVERSION_ON;
	}

        // need to invert the inversion for TT TDA10046H
        if (tda_state->fe_type == FE_TYPE_TDA10046H) {
                fe_params->inversion = fe_params->inversion ? INVERSION_OFF : INVERSION_ON;
        }

	// bandwidth
        switch(tda_state->fe_type) {
        case FE_TYPE_TDA10045H:
                switch (tda1004x_read_byte(i2c, tda_state, TDA10045H_WREF_LSB)) {
	case 0x14:
		fe_params->u.ofdm.bandwidth = BANDWIDTH_8_MHZ;
		break;
	case 0xdb:
		fe_params->u.ofdm.bandwidth = BANDWIDTH_7_MHZ;
		break;
	case 0x4f:
		fe_params->u.ofdm.bandwidth = BANDWIDTH_6_MHZ;
		break;
	}
                break;

        case FE_TYPE_TDA10046H:
                switch (tda1004x_read_byte(i2c, tda_state, TDA10046H_TIME_WREF1)) {
                case 0x60:
                        fe_params->u.ofdm.bandwidth = BANDWIDTH_8_MHZ;
                        break;
                case 0x6e:
                        fe_params->u.ofdm.bandwidth = BANDWIDTH_7_MHZ;
                        break;
                case 0x80:
                        fe_params->u.ofdm.bandwidth = BANDWIDTH_6_MHZ;
                        break;
                }
                break;
        }

	// FEC
	fe_params->u.ofdm.code_rate_HP =
	    tda1004x_decode_fec(tda1004x_read_byte(i2c, tda_state, TDA1004X_OUT_CONF2) & 7);
	fe_params->u.ofdm.code_rate_LP =
	    tda1004x_decode_fec((tda1004x_read_byte(i2c, tda_state, TDA1004X_OUT_CONF2) >> 3) & 7);

	// constellation
	switch (tda1004x_read_byte(i2c, tda_state, TDA1004X_OUT_CONF1) & 3) {
	case 0:
		fe_params->u.ofdm.constellation = QPSK;
		break;
	case 1:
		fe_params->u.ofdm.constellation = QAM_16;
		break;
	case 2:
		fe_params->u.ofdm.constellation = QAM_64;
		break;
	}

	// transmission mode
	fe_params->u.ofdm.transmission_mode = TRANSMISSION_MODE_2K;
	if (tda1004x_read_byte(i2c, tda_state, TDA1004X_OUT_CONF1) & 0x10) {
		fe_params->u.ofdm.transmission_mode = TRANSMISSION_MODE_8K;
	}

	// guard interval
	switch ((tda1004x_read_byte(i2c, tda_state, TDA1004X_OUT_CONF1) & 0x0c) >> 2) {
	case 0:
		fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_1_32;
		break;
	case 1:
		fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_1_16;
		break;
	case 2:
		fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_1_4;
		break;
	}

	// hierarchy
	switch ((tda1004x_read_byte(i2c, tda_state, TDA1004X_OUT_CONF1) & 0x60) >> 5) {
	case 0:
		fe_params->u.ofdm.hierarchy_information = HIERARCHY_NONE;
		break;
	case 1:
		fe_params->u.ofdm.hierarchy_information = HIERARCHY_1;
		break;
	case 2:
		fe_params->u.ofdm.hierarchy_information = HIERARCHY_2;
		break;
	case 3:
		fe_params->u.ofdm.hierarchy_information = HIERARCHY_4;
		break;
	}

	// done
	return 0;
}


static int tda1004x_read_status(struct dvb_i2c_bus *i2c, struct tda1004x_state* tda_state, fe_status_t * fe_status)
{
	int status;
        int cber;
        int vber;

	dprintk("%s\n", __FUNCTION__);

	// read status
	status = tda1004x_read_byte(i2c, tda_state, TDA1004X_STATUS_CD);
	if (status == -1) {
		return -EIO;
	}

        // decode
	*fe_status = 0;
        if (status & 4) *fe_status |= FE_HAS_SIGNAL;
        if (status & 2) *fe_status |= FE_HAS_CARRIER;
        if (status & 8) *fe_status |= FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;

        // if we don't already have VITERBI (i.e. not LOCKED), see if the viterbi
        // is getting anything valid
        if (!(*fe_status & FE_HAS_VITERBI)) {
                // read the CBER
                cber = tda1004x_read_byte(i2c, tda_state, TDA1004X_CBER_LSB);
                if (cber == -1) return -EIO;
                status = tda1004x_read_byte(i2c, tda_state, TDA1004X_CBER_MSB);
                if (status == -1) return -EIO;
                cber |= (status << 8);
                tda1004x_read_byte(i2c, tda_state, TDA1004X_CBER_RESET);

                if (cber != 65535) {
                        *fe_status |= FE_HAS_VITERBI;
                }
        }

        // if we DO have some valid VITERBI output, but don't already have SYNC
        // bytes (i.e. not LOCKED), see if the RS decoder is getting anything valid.
        if ((*fe_status & FE_HAS_VITERBI) && (!(*fe_status & FE_HAS_SYNC))) {
                // read the VBER
                vber = tda1004x_read_byte(i2c, tda_state, TDA1004X_VBER_LSB);
                if (vber == -1) return -EIO;
                status = tda1004x_read_byte(i2c, tda_state, TDA1004X_VBER_MID);
                if (status == -1) return -EIO;
                vber |= (status << 8);
                status = tda1004x_read_byte(i2c, tda_state, TDA1004X_VBER_MSB);
                if (status == -1) return -EIO;
                vber |= ((status << 16) & 0x0f);
                tda1004x_read_byte(i2c, tda_state, TDA1004X_CVBER_LUT);

                // if RS has passed some valid TS packets, then we must be
                // getting some SYNC bytes
                if (vber < 16632) {
                        *fe_status |= FE_HAS_SYNC;
                }
        }

	// success
	dprintk("%s: fe_status=0x%x\n", __FUNCTION__, *fe_status);
	return 0;
}

static int tda1004x_read_signal_strength(struct dvb_i2c_bus *i2c, struct tda1004x_state* tda_state, u16 * signal)
{
	int tmp;
        int reg = 0;

	dprintk("%s\n", __FUNCTION__);

        // determine the register to use
        switch(tda_state->fe_type) {
        case FE_TYPE_TDA10045H:
                reg = TDA10045H_S_AGC;
                break;

        case FE_TYPE_TDA10046H:
                reg = TDA10046H_AGC_IF_LEVEL;
                break;
        }

	// read it
        tmp = tda1004x_read_byte(i2c, tda_state, reg);
	if (tmp < 0)
		return -EIO;

	// done
	*signal = (tmp << 8) | tmp;
	dprintk("%s: signal=0x%x\n", __FUNCTION__, *signal);
	return 0;
}


static int tda1004x_read_snr(struct dvb_i2c_bus *i2c, struct tda1004x_state* tda_state, u16 * snr)
{
	int tmp;

	dprintk("%s\n", __FUNCTION__);

	// read it
	tmp = tda1004x_read_byte(i2c, tda_state, TDA1004X_SNR);
	if (tmp < 0)
		return -EIO;
        if (tmp) {
                tmp = 255 - tmp;
        }

        // done
	*snr = ((tmp << 8) | tmp);
	dprintk("%s: snr=0x%x\n", __FUNCTION__, *snr);
	return 0;
}

static int tda1004x_read_ucblocks(struct dvb_i2c_bus *i2c, struct tda1004x_state* tda_state, u32* ucblocks)
{
	int tmp;
	int tmp2;
	int counter;

	dprintk("%s\n", __FUNCTION__);

	// read the UCBLOCKS and reset
	counter = 0;
	tmp = tda1004x_read_byte(i2c, tda_state, TDA1004X_UNCOR);
	if (tmp < 0)
		return -EIO;
        tmp &= 0x7f;
	while (counter++ < 5) {
		tda1004x_write_mask(i2c, tda_state, TDA1004X_UNCOR, 0x80, 0);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_UNCOR, 0x80, 0);
		tda1004x_write_mask(i2c, tda_state, TDA1004X_UNCOR, 0x80, 0);

		tmp2 = tda1004x_read_byte(i2c, tda_state, TDA1004X_UNCOR);
		if (tmp2 < 0)
			return -EIO;
		tmp2 &= 0x7f;
		if ((tmp2 < tmp) || (tmp2 == 0))
			break;
	}

	// done
	if (tmp != 0x7f) {
		*ucblocks = tmp;
	} else {
		*ucblocks = 0xffffffff;
	}
	dprintk("%s: ucblocks=0x%x\n", __FUNCTION__, *ucblocks);
	return 0;
}

static int tda1004x_read_ber(struct dvb_i2c_bus *i2c, struct tda1004x_state* tda_state, u32* ber)
{
        int tmp;

	dprintk("%s\n", __FUNCTION__);

	// read it in
        tmp = tda1004x_read_byte(i2c, tda_state, TDA1004X_CBER_LSB);
        if (tmp < 0) return -EIO;
        *ber = tmp << 1;
        tmp = tda1004x_read_byte(i2c, tda_state, TDA1004X_CBER_MSB);
        if (tmp < 0) return -EIO;
        *ber |= (tmp << 9);
        tda1004x_read_byte(i2c, tda_state, TDA1004X_CBER_RESET);

	// done
	dprintk("%s: ber=0x%x\n", __FUNCTION__, *ber);
	return 0;
}


static int tda1004x_ioctl(struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
	int status = 0;
	struct dvb_i2c_bus *i2c = fe->i2c;
	struct tda1004x_state *tda_state = (struct tda1004x_state *) &(fe->data);

	dprintk("%s: cmd=0x%x\n", __FUNCTION__, cmd);

	switch (cmd) {
	case FE_GET_INFO:
                switch(tda_state->fe_type) {
                case FE_TYPE_TDA10045H:
        		memcpy(arg, &tda10045h_info, sizeof(struct dvb_frontend_info));
                        break;

                case FE_TYPE_TDA10046H:
                        memcpy(arg, &tda10046h_info, sizeof(struct dvb_frontend_info));
                        break;
                }
		break;

	case FE_READ_STATUS:
		return tda1004x_read_status(i2c, tda_state, (fe_status_t *) arg);

	case FE_READ_BER:
		return tda1004x_read_ber(i2c, tda_state, (u32 *) arg);

	case FE_READ_SIGNAL_STRENGTH:
		return tda1004x_read_signal_strength(i2c, tda_state, (u16 *) arg);

	case FE_READ_SNR:
		return tda1004x_read_snr(i2c, tda_state, (u16 *) arg);

	case FE_READ_UNCORRECTED_BLOCKS:
		return tda1004x_read_ucblocks(i2c, tda_state, (u32 *) arg);

	case FE_SET_FRONTEND:
		return tda1004x_set_fe(i2c, tda_state, (struct dvb_frontend_parameters*) arg);

	case FE_GET_FRONTEND:
		return tda1004x_get_fe(i2c, tda_state, (struct dvb_frontend_parameters*) arg);

	case FE_INIT:
		// don't bother reinitialising
		if (tda_state->initialised)
			return 0;

		// OK, perform initialisation
                switch(tda_state->fe_type) {
                case FE_TYPE_TDA10045H:
                        status = tda10045h_init(i2c, tda_state);
                        break;

                case FE_TYPE_TDA10046H:
                        status = tda10046h_init(i2c, tda_state);
                        break;
                }
		if (status == 0)
			tda_state->initialised = 1;
		return status;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}


static int tda1004x_attach(struct dvb_i2c_bus *i2c, void **data)
{
        int tda1004x_address = -1;
	int tuner_address = -1;
        int fe_type = -1;
        int tuner_type = -1;
	struct tda1004x_state tda_state;
	struct i2c_msg tuner_msg = {.addr=0, .flags=0, .buf=0, .len=0 };
        static u8 td1344_init[] = { 0x0b, 0xf5, 0x88, 0xab };
        static u8 td1316_init[] = { 0x0b, 0xf5, 0x85, 0xab };
        static u8 td1316_init_tda10046h[] = { 0x0b, 0xf5, 0x80, 0xab };
        int status;

	dprintk("%s\n", __FUNCTION__);

        // probe for tda10045h
        if (tda1004x_address == -1) {
                tda_state.tda1004x_address = 0x08;
	if (tda1004x_read_byte(i2c, &tda_state, TDA1004X_CHIPID) == 0x25) {
                        tda1004x_address = 0x08;
                        fe_type = FE_TYPE_TDA10045H;
                printk("tda1004x: Detected Philips TDA10045H.\n");
        }
        }

        // probe for tda10046h
        if (tda1004x_address == -1) {
                tda_state.tda1004x_address = 0x08;
                if (tda1004x_read_byte(i2c, &tda_state, TDA1004X_CHIPID) == 0x46) {
                        tda1004x_address = 0x08;
                        fe_type = FE_TYPE_TDA10046H;
                        printk("tda1004x: Detected Philips TDA10046H.\n");
                }
        }

        // did we find a frontend?
        if (tda1004x_address == -1) {
		return -ENODEV;
        }

        // enable access to the tuner
	tda1004x_enable_tuner_i2c(i2c, &tda_state);

        // check for a TD1344 first
        if (tuner_address == -1) {
                tuner_msg.addr = 0x61;
	tuner_msg.buf = td1344_init;
	tuner_msg.len = sizeof(td1344_init);
	if (i2c->xfer(i2c, &tuner_msg, 1) == 1) {
                dvb_delay(1);
                        tuner_address = 0x61;
                        tuner_type = TUNER_TYPE_TD1344;
                        printk("tda1004x: Detected Philips TD1344 tuner.\n");
                }
        }

        // OK, try a TD1316 on address 0x63
        if (tuner_address == -1) {
                tuner_msg.addr = 0x63;
                tuner_msg.buf = td1316_init;
                tuner_msg.len = sizeof(td1316_init);
                if (i2c->xfer(i2c, &tuner_msg, 1) == 1) {
                        dvb_delay(1);
                        tuner_address = 0x63;
                        tuner_type = TUNER_TYPE_TD1316;
                        printk("tda1004x: Detected Philips TD1316 tuner.\n");
                }
        }

        // OK, TD1316 again, on address 0x60 (TDA10046H)
        if (tuner_address == -1) {
                tuner_msg.addr = 0x60;
                tuner_msg.buf = td1316_init_tda10046h;
                tuner_msg.len = sizeof(td1316_init_tda10046h);
                if (i2c->xfer(i2c, &tuner_msg, 1) == 1) {
                        dvb_delay(1);
                        tuner_address = 0x60;
                        tuner_type = TUNER_TYPE_TD1316;
                        printk("tda1004x: Detected Philips TD1316 tuner.\n");
		}
	}
	tda1004x_disable_tuner_i2c(i2c, &tda_state);

	// did we find a tuner?
	if (tuner_address == -1) {
		printk("tda1004x: Detected, but with unknown tuner.\n");
		return -ENODEV;
	}

        // create state
        tda_state.tda1004x_address = tda1004x_address;
        tda_state.fe_type = fe_type;
	tda_state.tuner_address = tuner_address;
        tda_state.tuner_type = tuner_type;
	tda_state.initialised = 0;

        // upload firmware
        if ((status = tda1004x_fwupload(i2c, &tda_state)) != 0) return status;

	// register
        switch(tda_state.fe_type) {
        case FE_TYPE_TDA10045H:
        	return dvb_register_frontend(tda1004x_ioctl, i2c, (void *)(*((u32*) &tda_state)), &tda10045h_info);

        case FE_TYPE_TDA10046H:
                return dvb_register_frontend(tda1004x_ioctl, i2c, (void *)(*((u32*) &tda_state)), &tda10046h_info);
        }

        // should not get here
        return -EINVAL;
}


static
void tda1004x_detach(struct dvb_i2c_bus *i2c, void *data)
{
	dprintk("%s\n", __FUNCTION__);

	dvb_unregister_frontend(tda1004x_ioctl, i2c);
}


static
int __init init_tda1004x(void)
{
	return dvb_register_i2c_device(THIS_MODULE, tda1004x_attach, tda1004x_detach);
}


static
void __exit exit_tda1004x(void)
{
	dvb_unregister_i2c_device(tda1004x_attach);
}

module_init(init_tda1004x);
module_exit(exit_tda1004x);

MODULE_DESCRIPTION("Philips TDA10045H & TDA10046H DVB-T Frontend");
MODULE_AUTHOR("Andrew de Quincey & Robert Schlabbach");
MODULE_LICENSE("GPL");

MODULE_PARM(tda1004x_debug, "i");
MODULE_PARM_DESC(tda1004x_debug, "enable verbose debug messages");

MODULE_PARM(tda1004x_firmware, "s");
MODULE_PARM_DESC(tda1004x_firmware, "Where to find the firmware file");
