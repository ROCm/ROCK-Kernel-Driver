#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <media/audiochip.h>
#include <media/id.h>

/* Chips:
   TDA9885 (PAL, NTSC)
   TDA9886 (PAL, SECAM, NTSC)
   TDA9887 (PAL, SECAM, NTSC, FM Radio)

   found on:
   - Pinnacle PCTV (Jul.2002 Version with MT2032, bttv)
      TDA9887 (world), TDA9885 (USA)
      Note: OP2 of tda988x must be set to 1, else MT2032 is disabled!
   - KNC One TV-Station RDS (saa7134)
*/


/* Addresses to scan */
static unsigned short normal_i2c[] = {
	0x86 >>1,
	0x96 >>1,
	I2C_CLIENT_END,
};
static unsigned short normal_i2c_range[] = {I2C_CLIENT_END,I2C_CLIENT_END};
I2C_CLIENT_INSMOD;

/* insmod options */
static unsigned int debug = 0;
MODULE_PARM(debug,"i");
MODULE_LICENSE("GPL");

/* ---------------------------------------------------------------------- */

#define UNSET       (-1U)
#define PREFIX      "tda9885/6/7: "
#define dprintk     if (debug) printk

struct tda9887 {
	struct i2c_client  client;
	v4l2_std_id        std;
	unsigned int       radio;
	unsigned int       pinnacle_id;
	unsigned int       using_v4l2;
};

struct tvnorm {
	v4l2_std_id       std;
	char              *name;
	unsigned char     b;
	unsigned char     c;
	unsigned char     e;
};

static struct i2c_driver driver;
static struct i2c_client client_template;

/* ---------------------------------------------------------------------- */

//
// TDA defines
//

//// first reg (b)
#define cVideoTrapBypassOFF     0x00    // bit b0
#define cVideoTrapBypassON      0x01    // bit b0

#define cAutoMuteFmInactive     0x00    // bit b1
#define cAutoMuteFmActive       0x02    // bit b1

#define cIntercarrier           0x00    // bit b2
#define cQSS                    0x04    // bit b2

#define cPositiveAmTV           0x00    // bit b3:4
#define cFmRadio                0x08    // bit b3:4
#define cNegativeFmTV           0x10    // bit b3:4


#define cForcedMuteAudioON      0x20    // bit b5
#define cForcedMuteAudioOFF     0x00    // bit b5

#define cOutputPort1Active      0x00    // bit b6
#define cOutputPort1Inactive    0x40    // bit b6

#define cOutputPort2Active      0x00    // bit b7
#define cOutputPort2Inactive    0x80    // bit b7


//// second reg (c)
#define cDeemphasisOFF          0x00    // bit c5
#define cDeemphasisON           0x20    // bit c5

#define cDeemphasis75           0x00    // bit c6
#define cDeemphasis50           0x40    // bit c6

#define cAudioGain0             0x00    // bit c7
#define cAudioGain6             0x80    // bit c7


//// third reg (e)
#define cAudioIF_4_5             0x00    // bit e0:1
#define cAudioIF_5_5             0x01    // bit e0:1
#define cAudioIF_6_0             0x02    // bit e0:1
#define cAudioIF_6_5             0x03    // bit e0:1


#define cVideoIF_58_75           0x00    // bit e2:4
#define cVideoIF_45_75           0x04    // bit e2:4
#define cVideoIF_38_90           0x08    // bit e2:4
#define cVideoIF_38_00           0x0C    // bit e2:4
#define cVideoIF_33_90           0x10    // bit e2:4
#define cVideoIF_33_40           0x14    // bit e2:4
#define cRadioIF_45_75           0x18    // bit e2:4
#define cRadioIF_38_90           0x1C    // bit e2:4


#define cTunerGainNormal         0x00    // bit e5
#define cTunerGainLow            0x20    // bit e5

#define cGating_18               0x00    // bit e6
#define cGating_36               0x40    // bit e6

#define cAgcOutON                0x80    // bit e7
#define cAgcOutOFF               0x00    // bit e7

/* ---------------------------------------------------------------------- */

static struct tvnorm tvnorms[] = {
	{
		.std   = V4L2_STD_PAL_BG,
		.name  = "PAL-BG",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  ),
		.e     = ( cAudioIF_5_5   |
			   cVideoIF_38_90 ),
	},{
		.std   = V4L2_STD_PAL_I,
		.name  = "PAL-I",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  ),
		.e     = ( cAudioIF_6_0   |
			   cVideoIF_38_90 ),
	},{
		.std   = V4L2_STD_PAL_DK,
		.name  = "PAL-DK",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  ),
		.e     = ( cAudioIF_6_5   |
			   cVideoIF_38_00 ),
	},{
		.std   = V4L2_STD_PAL_M | V4L2_STD_PAL_N,
		.name  = "PAL-M/N",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis75  ),
		.e     = ( cAudioIF_4_5   |
			   cVideoIF_45_75 ),
	},{
		.std   = V4L2_STD_SECAM_L,
		.name  = "SECAM-L",
		.b     = ( cPositiveAmTV  |
			   cQSS           ),
		.e     = ( cAudioIF_6_5   |
			   cVideoIF_38_90 ),
	},{
		.std   = V4L2_STD_SECAM_DK,
		.name  = "SECAM-DK",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  ),
		.e     = ( cAudioIF_6_5   |
			   cVideoIF_38_00 ),
	},{
		.std   = V4L2_STD_NTSC_M,
		.name  = "NTSC-M",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  ),
		.e     = ( cGating_36     |
			   cAudioIF_4_5   |
			   cVideoIF_45_75 ),
	},{
		.std   = V4L2_STD_NTSC_M_JP,
		.name  = "NTSC-JP",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  ),
		.e     = ( cGating_36     |
			   cAudioIF_4_5   |
			   cVideoIF_58_75 ),
	}
};

static struct tvnorm radio = {
	.name = "radio",
	.b    = ( cFmRadio       |
		  cQSS           ),
	.c    = ( cDeemphasisON  |
		  cDeemphasis50  ),
	.e    = ( cAudioIF_5_5   |
		  cRadioIF_38_90 ),
};

/* ---------------------------------------------------------------------- */

static void dump_read_message(unsigned char *buf)
{
	static char *afc[16] = {
		"- 12.5 kHz",
		"- 37.5 kHz",
		"- 62.5 kHz",
		"- 87.5 kHz",
		"-112.5 kHz",
		"-137.5 kHz",
		"-162.5 kHz",
		"-187.5 kHz [min]",
		"+187.5 kHz [max]",
		"+162.5 kHz",
		"+137.5 kHz",
		"+112.5 kHz",
		"+ 87.5 kHz",
		"+ 62.5 kHz",
		"+ 37.5 kHz",
		"+ 12.5 kHz",
	};
	printk(PREFIX "read: 0x%2x\n", buf[0]);
	printk("  after power on : %s\n", (buf[0] & 0x01) ? "yes" : "no");
	printk("  afc            : %s\n", afc[(buf[0] >> 1) & 0x0f]);
	printk("  afc window     : %s\n", (buf[0] & 0x40) ? "in" : "out");
	printk("  vfi level      : %s\n", (buf[0] & 0x80) ? "high" : "low");
}

static void dump_write_message(unsigned char *buf)
{
	static char *sound[4] = {
		"AM/TV",
		"FM/radio",
		"FM/TV",
		"FM/radio"
	};
	static char *adjust[32] = {
		"-16", "-15", "-14", "-13", "-12", "-11", "-10", "-9",
		"-8",  "-7",  "-6",  "-5",  "-4",  "-3",  "-2",  "-1",
		"0",   "+1",  "+2",  "+3",  "+4",  "+5",  "+6",  "+7",
		"+8",  "+9",  "+10", "+11", "+12", "+13", "+14", "+15"
	};
	static char *deemph[4] = {
		"no", "no", "75", "50"
	};
	static char *carrier[4] = {
		"4.5 MHz",
		"5.5 MHz",
		"6.0 MHz",
		"6.5 MHz / AM"
	};
	static char *vif[8] = {
		"58.75 MHz",
		"45.75 MHz",
		"38.9 MHz",
		"38.0 MHz",
		"33.9 MHz",
		"33.4 MHz",
		"45.75 MHz + pin13",
		"38.9 MHz + pin13",
	};
	static char *rif[4] = {
		"44 MHz",
		"52 MHz",
		"52 MHz",
		"44 MHz",
	};

	printk(PREFIX "write: byte B 0x%02x\n",buf[1]);
	printk("  B0   video mode      : %s\n",
	       (buf[1] & 0x01) ? "video trap" : "sound trap");
	printk("  B1   auto mute fm    : %s\n",
	       (buf[1] & 0x02) ? "yes" : "no");
	printk("  B2   carrier mode    : %s\n",
	       (buf[1] & 0x04) ? "QSS" : "Intercarrier");
	printk("  B3-4 tv sound/radio  : %s\n",
	       sound[(buf[1] & 0x18) >> 3]);
	printk("  B5   force mute audio: %s\n",
	       (buf[1] & 0x20) ? "yes" : "no");
	printk("  B6   output port 1   : %s\n",
	       (buf[1] & 0x40) ? "high" : "low");
	printk("  B7   output port 2   : %s\n",
	       (buf[1] & 0x80) ? "high" : "low");

	printk(PREFIX "write: byte C 0x%02x\n",buf[2]);
	printk("  C0-4 top adjustment  : %s dB\n", adjust[buf[2] & 0x1f]);
	printk("  C5-6 de-emphasis     : %s\n", deemph[(buf[2] & 0x60) >> 5]);
	printk("  C7   audio gain      : %s\n",
	       (buf[2] & 0x80) ? "-6" : "0");

	printk(PREFIX "write: byte E 0x%02x\n",buf[3]);
	printk("  E0-1 sound carrier   : %s\n",
	       carrier[(buf[3] & 0x03)]);
	printk("  E6   l pll ganting   : %s\n",
	       (buf[3] & 0x40) ? "36" : "13");

	if (buf[1] & 0x08) {
		/* radio */
		printk("  E2-4 video if        : %s\n",
		       rif[(buf[3] & 0x0c) >> 2]);
		printk("  E7   vif agc output  : %s\n",
		       (buf[3] & 0x80)
		       ? ((buf[3] & 0x10) ? "fm-agc radio" : "sif-agc radio")
		       : "fm radio carrier afc");
	} else {
		/* video */
		printk("  E2-4 video if        : %s\n",
		       vif[(buf[3] & 0x1c) >> 2]);
		printk("  E5   tuner gain      : %s\n",
		       (buf[3] & 0x80)
		       ? ((buf[3] & 0x20) ? "external" : "normal")
		       : ((buf[3] & 0x20) ? "minimum"  : "normal"));
		printk("  E7   vif agc output  : %s\n",
		       (buf[3] & 0x80)
		       ? ((buf[3] & 0x20)
			  ? "pin3 port, pin22 vif agc out"
			  : "pin22 port, pin3 vif acg ext in")
		       : "pin3+pin22 port");
	}
	printk("--\n");
}

/* ---------------------------------------------------------------------- */

static int tda9887_set_tvnorm(struct tda9887 *t, char *buf)
{
	struct tvnorm *norm = NULL;
	int i;

	if (t->radio) {
		norm = &radio;
	} else {
		for (i = 0; i < ARRAY_SIZE(tvnorms); i++) {
			if (tvnorms[i].std & t->std) {
				norm = tvnorms+i;
				break;
			}
		}
	}
	if (NULL == norm) {
		dprintk(PREFIX "Oops: no tvnorm entry found\n");
		return -1;
	}

	dprintk(PREFIX "configure for: %s\n",norm->name);
	buf[1] = norm->b;
	buf[2] = norm->c;
	buf[3] = norm->e;
	return 0;
}

static unsigned int port1  = 1;
static unsigned int port2  = 1;
static unsigned int qss    = UNSET;
static unsigned int adjust = 0x10;
MODULE_PARM(port1,"i");
MODULE_PARM(port2,"i");
MODULE_PARM(qss,"i");
MODULE_PARM(adjust,"i");

static int tda9887_set_insmod(struct tda9887 *t, char *buf)
{
	if (port1)
		buf[1] |= cOutputPort1Inactive;
	if (port2)
		buf[1] |= cOutputPort2Inactive;
	if (UNSET != qss) {
		if (qss)
			buf[1] |= cQSS;
		else
			buf[1] &= ~cQSS;
	}

	if (adjust >= 0x00 && adjust < 0x20)
		buf[2] |= adjust;
	return 0;
}

/* ---------------------------------------------------------------------- */

static int tda9887_set_pinnacle(struct tda9887 *t, char *buf)
{
	unsigned int bCarrierMode = UNSET;

	if (t->std & V4L2_STD_PAL) {
		if ((1 == t->pinnacle_id) || (7 == t->pinnacle_id)) {
			bCarrierMode = cIntercarrier;
		} else {
			bCarrierMode = cQSS;
		}
	}
	if (t->std & V4L2_STD_NTSC) {
                if ((5 == t->pinnacle_id) || (6 == t->pinnacle_id)) {
			bCarrierMode = cIntercarrier;
		} else {
			bCarrierMode = cQSS;
                }
	}

	if (bCarrierMode != UNSET) {
		buf[1] &= ~0x04;
		buf[1] |= bCarrierMode;
	}
	return 0;
}

/* ---------------------------------------------------------------------- */

static char *pal = "-";
MODULE_PARM(pal,"s");
static char *secam = "-";
MODULE_PARM(secam,"s");

static int tda9887_fixup_std(struct tda9887 *t)
{
	/* get more precise norm info from insmod option */
	if ((t->std & V4L2_STD_PAL) == V4L2_STD_PAL) {
		switch (pal[0]) {
		case 'b':
		case 'B':
		case 'g':
		case 'G':
			dprintk(PREFIX "insmod fixup: PAL => PAL-BG\n");
			t->std = V4L2_STD_PAL_BG;
			break;
		case 'i':
		case 'I':
			dprintk(PREFIX "insmod fixup: PAL => PAL-I\n");
			t->std = V4L2_STD_PAL_I;
			break;
		case 'd':
		case 'D':
		case 'k':
		case 'K':
			dprintk(PREFIX "insmod fixup: PAL => PAL-DK\n");
			t->std = V4L2_STD_PAL_DK;
			break;
		}
	}
	if ((t->std & V4L2_STD_SECAM) == V4L2_STD_SECAM) {
		switch (secam[0]) {
		case 'd':
		case 'D':
		case 'k':
		case 'K':
			dprintk(PREFIX "insmod fixup: SECAM => SECAM-DK\n");
			t->std = V4L2_STD_SECAM_DK;
			break;
		case 'l':
		case 'L':
			dprintk(PREFIX "insmod fixup: SECAM => SECAM-L\n");
			t->std = V4L2_STD_SECAM_L;
			break;
		}
	}
	return 0;
}

static int tda9887_status(struct tda9887 *t)
{
	unsigned char buf[1];
	int rc;

	memset(buf,0,sizeof(buf));
        if (1 != (rc = i2c_master_recv(&t->client,buf,1)))
                printk(PREFIX "i2c i/o error: rc == %d (should be 1)\n",rc);
	dump_read_message(buf);
	return 0;
}

static int tda9887_configure(struct tda9887 *t)
{
	unsigned char buf[4];
	int rc;

	memset(buf,0,sizeof(buf));
	tda9887_set_tvnorm(t,buf);
	if (UNSET != t->pinnacle_id) {
		tda9887_set_pinnacle(t,buf);
	}
	tda9887_set_insmod(t,buf);

	dprintk(PREFIX "writing: b=0x%02x c=0x%02x e=0x%02x\n",
		buf[1],buf[2],buf[3]);
	if (debug > 1)
		dump_write_message(buf);

        if (4 != (rc = i2c_master_send(&t->client,buf,4)))
                printk(PREFIX "i2c i/o error: rc == %d (should be 4)\n",rc);

	if (debug > 2) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
		tda9887_status(t);
	}
	return 0;
}

/* ---------------------------------------------------------------------- */

static int tda9887_attach(struct i2c_adapter *adap, int addr, int kind)
{
	struct tda9887 *t;

        client_template.adapter = adap;
        client_template.addr    = addr;

        printk(PREFIX "chip found @ 0x%x\n", addr<<1);

        if (NULL == (t = kmalloc(sizeof(*t), GFP_KERNEL)))
                return -ENOMEM;
	memset(t,0,sizeof(*t));
	t->client      = client_template;
	t->std         = 0;;
	t->pinnacle_id = UNSET;
        i2c_set_clientdata(&t->client, t);
        i2c_attach_client(&t->client);
        
	return 0;
}

static int tda9887_probe(struct i2c_adapter *adap)
{
#ifdef I2C_CLASS_TV_ANALOG
	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, tda9887_attach);
#else
	switch (adap->id) {
	case I2C_ALGO_BIT | I2C_HW_B_BT848:
	case I2C_ALGO_BIT | I2C_HW_B_RIVA:
	case I2C_ALGO_SAA7134:
		return i2c_probe(adap, &addr_data, tda9887_attach);
		break;
	}
#endif
	return 0;
}

static int tda9887_detach(struct i2c_client *client)
{
	struct tda9887 *t = i2c_get_clientdata(client);

	i2c_detach_client(client);
	kfree(t);
	return 0;
}

#define SWITCH_V4L2	if (!t->using_v4l2 && debug) \
		          printk(PREFIX "switching to v4l2\n"); \
	                  t->using_v4l2 = 1;
#define CHECK_V4L2	if (t->using_v4l2) { if (debug) \
			  printk(PREFIX "ignore v4l1 call\n"); \
		          return 0; }

static int
tda9887_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tda9887 *t = i2c_get_clientdata(client);

        switch (cmd) {

	/* --- configuration --- */
	case AUDC_SET_RADIO:
		t->radio = 1;
		tda9887_configure(t);
		break;
		
	case AUDC_CONFIG_PINNACLE:
	{
		int *i = arg;

		t->pinnacle_id = *i;
		tda9887_configure(t);
		break;
	}
	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOCSCHAN:
	{
		static const v4l2_std_id map[] = {
			[ VIDEO_MODE_PAL   ] = V4L2_STD_PAL,
			[ VIDEO_MODE_NTSC  ] = V4L2_STD_NTSC_M,
			[ VIDEO_MODE_SECAM ] = V4L2_STD_SECAM,
			[ 4 /* bttv */     ] = V4L2_STD_PAL_M,
			[ 5 /* bttv */     ] = V4L2_STD_PAL_N,
			[ 6 /* bttv */     ] = V4L2_STD_NTSC_M_JP,
		};
		struct video_channel *vc = arg;

		CHECK_V4L2;
		t->radio = 0;
		if (vc->norm < ARRAY_SIZE(map))
			t->std = map[vc->norm];
		tda9887_fixup_std(t);
		tda9887_configure(t);
		break;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;

		SWITCH_V4L2;
		t->radio = 0;
		t->std   = *id;
		tda9887_fixup_std(t);
		tda9887_configure(t);
		break;
	}
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		SWITCH_V4L2;
		if (V4L2_TUNER_ANALOG_TV == f->type) {
			if (t->radio == 0)
				return 0;
			t->radio = 0;
		}
		if (V4L2_TUNER_RADIO == f->type) {
			if (t->radio == 1)
				return 0;
			t->radio = 1;
		}
		tda9887_configure(t);
	}
	default:
		/* nothing */
		break;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
	.owner          = THIS_MODULE,
        .name           = "i2c tda9887 driver",
        .id             = -1, /* FIXME */
        .flags          = I2C_DF_NOTIFY,
        .attach_adapter = tda9887_probe,
        .detach_client  = tda9887_detach,
        .command        = tda9887_command,
};
static struct i2c_client client_template =
{
	I2C_DEVNAME("tda9887"),
	.flags     = I2C_CLIENT_ALLOW_USE,
        .driver    = &driver,
};

static int tda9887_init_module(void)
{
	i2c_add_driver(&driver);
	return 0;
}

static void tda9887_cleanup_module(void)
{
	i2c_del_driver(&driver);
}

module_init(tda9887_init_module);
module_exit(tda9887_cleanup_module);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
