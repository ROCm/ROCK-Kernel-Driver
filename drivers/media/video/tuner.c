#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev.h>
#include <linux/init.h>

#include "tuner.h"
#include "audiochip.h"

/* Addresses to scan */
static unsigned short normal_i2c[] = {I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {0x60,0x6f,I2C_CLIENT_END};
static unsigned short probe[2]        = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short probe_range[2]  = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore[2]       = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore_range[2] = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short force[2]        = { I2C_CLIENT_END, I2C_CLIENT_END };
static struct i2c_client_address_data addr_data = {
	normal_i2c, normal_i2c_range, 
	probe, probe_range, 
	ignore, ignore_range, 
	force
};

static int debug =  0; /* insmod parameter */
static int type  = -1; /* insmod parameter */

static int addr  =  0;
static int this_adap;

#define dprintk     if (debug) printk

MODULE_PARM(debug,"i");
MODULE_PARM(type,"i");
MODULE_PARM(addr,"i");

struct tuner
{
	int type;            /* chip type */
	int freq;            /* keep track of the current settings */
	int std;

	int radio;
	int mode;            /* PAL(0)/SECAM(1) mode (PHILIPS_SECAM only) */
};

static struct i2c_driver driver;
static struct i2c_client client_template;

/* ---------------------------------------------------------------------- */

struct tunertype 
{
	char *name;
	unsigned char Vendor;
	unsigned char Type;
  
	unsigned short thresh1; /* frequency Range for UHF,VHF-L, VHF_H */   
	unsigned short thresh2;  
	unsigned char VHF_L;
	unsigned char VHF_H;
	unsigned char UHF;
	unsigned char config; 
	unsigned short IFPCoff;
	unsigned char mode; /* mode change value (tested PHILIPS_SECAM only) */
			/* 0x01 -> ??? no change ??? */
			/* 0x02 -> PAL BDGHI / SECAM L */
			/* 0x04 -> ??? PAL others / SECAM others ??? */
	int capability;
};

/*
 *	The floats in the tuner struct are computed at compile time
 *	by gcc and cast back to integers. Thus we don't violate the
 *	"no float in kernel" rule.
 */
static struct tunertype tuners[] = {
        { "Temic PAL", TEMIC, PAL,
	  16*140.25,16*463.25,0x02,0x04,0x01,0x8e,623},
	{ "Philips PAL_I", Philips, PAL_I,
	  16*140.25,16*463.25,0xa0,0x90,0x30,0x8e,623},
	{ "Philips NTSC", Philips, NTSC,
	  16*157.25,16*451.25,0xA0,0x90,0x30,0x8e,732},
	{ "Philips SECAM", Philips, SECAM,
	  16*168.25,16*447.25,0xA7,0x97,0x37,0x8e,623,0x02},
	{ "NoTuner", NoTuner, NOTUNER,
	  0,0,0x00,0x00,0x00,0x00,0x00,000},
	{ "Philips PAL", Philips, PAL,
	  16*168.25,16*447.25,0xA0,0x90,0x30,0x8e,623},
	{ "Temic NTSC", TEMIC, NTSC,
	  16*157.25,16*463.25,0x02,0x04,0x01,0x8e,732},
	{ "Temic PAL_I", TEMIC, PAL_I,
	  16*170.00,16*450.00,0x02,0x04,0x01,0x8e,623},
 	{ "Temic 4036 FY5 NTSC", TEMIC, NTSC,
	  16*157.25,16*463.25,0xa0,0x90,0x30,0x8e,732},
        { "Alps HSBH1", TEMIC, NTSC,
	  16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},
        { "Alps TSBE1",TEMIC,PAL,
	  16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},
        { "Alps TSBB5", Alps, PAL_I, /* tested (UK UHF) with Modtec MM205 */
	  16*133.25,16*351.25,0x01,0x02,0x08,0x8e,632},
        { "Alps TSBE5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
	  16*133.25,16*351.25,0x01,0x02,0x08,0x8e,622},
        { "Alps TSBC5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
	  16*133.25,16*351.25,0x01,0x02,0x08,0x8e,608},
	{ "Temic 4006FH5", TEMIC, PAL_I,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623}, 
  	{ "Alps TSCH6",Alps,NTSC,
  	  16*137.25,16*385.25,0x14,0x12,0x11,0x8e,732},
};
#define TUNERS (sizeof(tuners)/sizeof(struct tunertype))

/* ---------------------------------------------------------------------- */

static int tuner_getstatus(struct i2c_client *c)
{
	unsigned char byte;

	if (1 != i2c_master_recv(c,&byte,1))
		return 0;
	return byte;
}

#define TUNER_POR       0x80
#define TUNER_FL        0x40
#define TUNER_MODE      0x38
#define TUNER_AFC       0x07

#define TUNER_STEREO    0x10 /* radio mode */
#define TUNER_SIGNAL    0x07 /* radio mode */

static int tuner_signal(struct i2c_client *c)
{
	return (tuner_getstatus(c) & TUNER_SIGNAL)<<13;
}

static int tuner_stereo(struct i2c_client *c)
{
	return (tuner_getstatus (c) & TUNER_STEREO);
}


static int tuner_islocked (struct i2c_client *c)
{
        return (tuner_getstatus (c) & TUNER_FL);
}

static int tuner_afcstatus (struct i2c_client *c)
{
        return (tuner_getstatus (c) & TUNER_AFC) - 2;
}

#if 0 /* unused */
static int tuner_mode (struct i2c_client *c)
{
        return (tuner_getstatus (c) & TUNER_MODE) >> 3;
}
#endif

static void set_tv_freq(struct i2c_client *c, int freq)
{
	u8 config;
	u16 div;
	struct tunertype *tun;
	struct tuner *t = c->data;
        unsigned char buffer[4];
	int rc;

	if (t->type == -1) {
		printk("tuner: tuner type not set\n");
		return;
	}

	tun=&tuners[t->type];
	if (freq < tun->thresh1) 
		config = tun->VHF_L;
	else if (freq < tun->thresh2) 
		config = tun->VHF_H;
	else
		config = tun->UHF;

#if 1   // Fix colorstandard mode change
	if (t->type == TUNER_PHILIPS_SECAM && t->mode)
		config |= tun->mode;
	else
		config &= ~tun->mode;
#else
		config &= ~tun->mode;
#endif

	div=freq + tun->IFPCoff;

    /*
     * Philips FI1216MK2 remark from specification :
     * for channel selection involving band switching, and to ensure
     * smooth tuning to the desired channel without causing
     * unnecessary charge pump action, it is recommended to consider
     * the difference between wanted channel frequency and the
     * current channel frequency.  Unnecessary charge pump action
     * will result in very low tuning voltage which may drive the
     * oscillator to extreme conditions.
     */
    /*
     * Progfou: specification says to send config data before
     * frequency in case (wanted frequency < current frequency).
     */

	if (t->type == TUNER_PHILIPS_SECAM && freq < t->freq) {
		buffer[0] = tun->config;
		buffer[1] = config;
		buffer[2] = (div>>8) & 0x7f;
		buffer[3] = div      & 0xff;
	} else {
		buffer[0] = (div>>8) & 0x7f;
		buffer[1] = div      & 0xff;
		buffer[2] = tun->config;
		buffer[3] = config;
	}

        if (4 != (rc = i2c_master_send(c,buffer,4)))
                printk("tuner: i2c i/o error: rc == %d (should be 4)\n",rc);

}

static void set_radio_freq(struct i2c_client *c, int freq)
{
	u8 config;
	u16 div;
	struct tunertype *tun;
	struct tuner *t = (struct tuner*)c->data;
        unsigned char buffer[4];
	int rc;

	if (t->type == -1) {
		printk("tuner: tuner type not set\n");
		return;
	}

	tun=&tuners[t->type];
	config = 0xa4 /* 0xa5 */; /* bit 0 is AFC (set) vs. RF-Signal (clear) */
	div=freq + (int)(16*10.7);
  	div&=0x7fff;

        buffer[0] = (div>>8) & 0x7f;
        buffer[1] = div      & 0xff;
        buffer[2] = tun->config;
        buffer[3] = config;
        if (4 != (rc = i2c_master_send(c,buffer,4)))
                printk("tuner: i2c i/o error: rc == %d (should be 4)\n",rc);

	if (debug) {
		current->state   = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/10);
		
		if (tuner_islocked (c))
			printk ("tuner: PLL locked\n");
		else
			printk ("tuner: PLL not locked\n");

		if (config & 1) {
			printk ("tuner: AFC: %d\n", tuner_afcstatus(c));
		} else {
			printk ("tuner: Signal: %d\n", tuner_signal(c));
		}
	}
}
/* ---------------------------------------------------------------------- */


static int tuner_attach(struct i2c_adapter *adap, int addr,
			unsigned short flags, int kind)
{
	struct tuner *t;
	struct i2c_client *client;

	if (this_adap > 0)
		return -1;
	this_adap++;
	
        client_template.adapter = adap;
        client_template.addr = addr;

        printk("tuner: chip found @ 0x%x\n",addr);

        if (NULL == (client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL)))
                return -ENOMEM;
        memcpy(client,&client_template,sizeof(struct i2c_client));
        client->data = t = kmalloc(sizeof(struct tuner),GFP_KERNEL);
        if (NULL == t) {
                kfree(client);
                return -ENOMEM;
        }
        memset(t,0,sizeof(struct tuner));
	if (type >= 0 && type < TUNERS) {
		t->type = type;
		strncpy(client->name, tuners[t->type].name, sizeof(client->name));
	} else {
		t->type = -1;
	}
        i2c_attach_client(client);
	MOD_INC_USE_COUNT;

	return 0;
}

static int tuner_probe(struct i2c_adapter *adap)
{
	if (0 != addr) {
		normal_i2c_range[0] = addr;
		normal_i2c_range[1] = addr;
	}
	this_adap = 0;
	if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return i2c_probe(adap, &addr_data, tuner_attach);
	return 0;
}

static int tuner_detach(struct i2c_client *client)
{
	struct tuner *t = (struct tuner*)client->data;

	i2c_detach_client(client);
	kfree(t);
	kfree(client);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
tuner_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tuner *t = (struct tuner*)client->data;
        int   *iarg = (int*)arg;
#if 0
        __u16 *sarg = (__u16*)arg;
#endif

        switch (cmd) {

	/* --- configuration --- */
	case TUNER_SET_TYPE:
		if (t->type != -1)
			return 0;
		if (*iarg < 0 || *iarg >= TUNERS)
			return 0;
		t->type = *iarg;
		dprintk("tuner: type set to %d (%s)\n",
                        t->type,tuners[t->type].name);
		strncpy(client->name, tuners[t->type].name, sizeof(client->name));
		break;
	case AUDC_SET_RADIO:
		t->radio = 1;
		break;
		
	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOCSCHAN:
	{
		struct video_channel *vc = arg;
		
		t->radio = 0;
		if (t->type == TUNER_PHILIPS_SECAM) {
			t->mode = (vc->norm == VIDEO_MODE_SECAM) ? 1 : 0;
			set_tv_freq(client,t->freq);
		}
		return 0;
	}
	case VIDIOCSFREQ:
	{
		unsigned long *v = arg;

		t->freq = *v;
		if (t->radio) {
			dprintk("tuner: radio freq set to %d.%02d\n",
				(*iarg)/16,(*iarg)%16*100/16);
			set_radio_freq(client,t->freq);
		} else {
			dprintk("tuner: tv freq set to %d.%02d\n",
				(*iarg)/16,(*iarg)%16*100/16);
			set_tv_freq(client,t->freq);
		}
		return 0;
	}
	case VIDIOCGTUNER:
	{
		struct video_tuner *vt = arg;

		if (t->radio)
			vt->signal = tuner_signal(client);
		return 0;
	}
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;
		if (t->radio)
			va->mode = (tuner_stereo(client) ? VIDEO_SOUND_STEREO : VIDEO_SOUND_MONO);
		return 0;
	}
	
#if 0
	/* --- old, obsolete interface --- */
	case TUNER_SET_TVFREQ:
		dprintk("tuner: tv freq set to %d.%02d\n",
			(*iarg)/16,(*iarg)%16*100/16);
		set_tv_freq(client,*iarg);
		t->radio = 0;
		t->freq = *iarg;
		break;

	case TUNER_SET_RADIOFREQ:
		dprintk("tuner: radio freq set to %d.%02d\n",
			(*iarg)/16,(*iarg)%16*100/16);
		set_radio_freq(client,*iarg);
		t->radio = 1;
		t->freq = *iarg;
		break;
	case TUNER_SET_MODE:
		if (t->type != TUNER_PHILIPS_SECAM) {
			dprintk("tuner: trying to change mode for other than TUNER_PHILIPS_SECAM\n");
		} else {
			int mode=(*sarg==VIDEO_MODE_SECAM)?1:0;
			dprintk("tuner: mode set to %d\n", *sarg);
			t->mode = mode;
			set_tv_freq(client,t->freq);
		}
		break;
#endif
	default:
		/* nothing */
	}
	
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
        "i2c TV tuner driver",
        I2C_DRIVERID_TUNER,
        I2C_DF_NOTIFY,
        tuner_probe,
        tuner_detach,
        tuner_command,
};

static struct i2c_client client_template =
{
        "(unset)",		/* name       */
        -1,
        0,
        0,
        NULL,
        &driver
};

EXPORT_NO_SYMBOLS;

int tuner_init_module(void)
{
	i2c_add_driver(&driver);
	return 0;
}

void tuner_cleanup_module(void)
{
	i2c_del_driver(&driver);
}

module_init(tuner_init_module);
module_exit(tuner_cleanup_module);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
