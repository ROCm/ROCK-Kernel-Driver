/* Miro PCM20 radio driver for Linux radio support
 * (c) 1998 Ruurd Reitsma <R.A.Reitsma@wbmt.tudelft.nl>
 * Thanks to Norberto Pellici for the ACI device interface specification
 * The API part is based on the radiotrack driver by M. Kirkwood
 * This driver relies on the aci mixer (drivers/sound/lowlevel/aci.c)
 * Look there for further info...
 */

#include <linux/module.h>		/* Modules 			*/
#include <linux/init.h>			/* Initdata			*/
#include <asm/uaccess.h>		/* copy to/from user		*/
#include <linux/videodev.h>		/* kernel radio structs		*/
#include "../../sound/miroaci.h"	/* ACI Control by acimixer      */

static int users = 0;

struct pcm20_device
{
	int port;
	int curvol;
	unsigned long curfreq;
	int muted;
};


/* local things */


static void pcm20_mute(struct pcm20_device *dev)
{

	dev->muted = 1;
	aci_write_cmd(0xa3,0x01);			

}

static int pcm20_setvol(struct pcm20_device *dev, int vol)
{

	if(vol == dev->curvol) {	/* requested volume = current */
		if (dev->muted) {	/* user is unmuting the card  */
			dev->muted = 0;
			aci_write_cmd(0xa3,0x00); 	/* enable card */
		}	
	
		return 0;
	}

	if(vol == 0) {			/* volume = 0 means mute the card */
		aci_write_cmd(0x3d, 0x20);
		aci_write_cmd(0x35, 0x20);
		return 0;
	}

	dev->muted = 0;
	aci_write_cmd(0x3d, 32-vol); 	/* Right Channel */
	aci_write_cmd(0x35, 32-vol);	/* Left Channel */
	dev->curvol = vol;

	return 0;
}

static int pcm20_setfreq(struct pcm20_device *dev, unsigned long freq)
{
	unsigned char freql;
	unsigned char freqh;

	freq = (freq * 10) / 16;
	freql = freq & 0xff;
	freqh = freq >> 8;	


	aci_write_cmd_d(0xa7, freql, freqh);	/*  Tune to frequency	*/

	return 0;
}

int pcm20_getsigstr(struct pcm20_device *dev)
{
	unsigned char buf;
	aci_indexed_cmd(0xf0, 0x32, &buf);
	if ((buf & 0x80) == 0x80)	
		return 0;
	return 1;		/* signal present		*/
}

static int pcm20_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct pcm20_device *pcm20=dev->priv;
	
	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability v;
			v.type=VID_TYPE_TUNER;
			strcpy(v.name, "Miro PCM20");
			v.channels=1;
			v.audios=1;
			/* No we don't do pictures */
			v.maxwidth=0;
			v.maxheight=0;
			v.minwidth=0;
			v.minheight=0;
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg,sizeof(v))!=0) 
				return -EFAULT;
			if(v.tuner)	/* Only 1 tuner */ 
				return -EINVAL;
			v.rangelow=(int)(87.5*16);
			v.rangehigh=(int)(108.0*16);
			v.flags=0;
			v.mode=VIDEO_MODE_AUTO;
			v.signal=0xFFFF*pcm20_getsigstr(pcm20);
			strcpy(v.name, "FM");
			if(copy_to_user(arg,&v, sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if(v.tuner!=0)
				return -EINVAL;
			/* Only 1 tuner so no setting needed ! */
			return 0;
		}
		case VIDIOCGFREQ:
			if(copy_to_user(arg, &pcm20->curfreq, sizeof(pcm20->curfreq)))
				return -EFAULT;
			return 0;
		case VIDIOCSFREQ:
			if(copy_from_user(&pcm20->curfreq, arg,sizeof(pcm20->curfreq)))
				return -EFAULT;
			pcm20_setfreq(pcm20, pcm20->curfreq);
			return 0;
		case VIDIOCGAUDIO:
		{	
			struct video_audio v;
			memset(&v,0, sizeof(v));
			v.flags|=VIDEO_AUDIO_MUTABLE|VIDEO_AUDIO_VOLUME;
			v.volume=pcm20->curvol * 2048;
			strcpy(v.name, "Radio");
			if(copy_to_user(arg,&v, sizeof(v)))
				return -EFAULT;
			return 0;			
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio v;
			if(copy_from_user(&v, arg, sizeof(v))) 
				return -EFAULT;	
			if(v.audio) 
				return -EINVAL;

			if(v.flags&VIDEO_AUDIO_MUTE) 
				pcm20_mute(pcm20);
			else
				pcm20_setvol(pcm20,v.volume/2048);	

			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}

static int pcm20_open(struct video_device *dev, int flags)
{
	if(users)
		return -EBUSY;
	users++;
	MOD_INC_USE_COUNT;
	return 0;
}

static void pcm20_close(struct video_device *dev)
{
	users--;
	MOD_DEC_USE_COUNT;
}

static struct pcm20_device pcm20_unit;

static struct video_device pcm20_radio=
{
	name:		"Miro PCM 20 radio",
	type:		VID_TYPE_TUNER,
	hardware:	VID_HARDWARE_RTRACK,
	open:		pcm20_open,
	close:		pcm20_close,
	ioctl:		pcm20_ioctl,
};

static int __init pcm20_init(void)
{

	pcm20_radio.priv=&pcm20_unit;
	
	if(video_register_device(&pcm20_radio, VFL_TYPE_RADIO)==-1)
		return -EINVAL;
		
	printk(KERN_INFO "Miro PCM20 radio card driver.\n");

 	/* mute card - prevents noisy bootups */

	/* this ensures that the volume is all the way down  */

	pcm20_unit.curvol = 0;

	return 0;
}

MODULE_AUTHOR("Ruurd Reitsma");
MODULE_DESCRIPTION("A driver for the Miro PCM20 radio card.");

EXPORT_NO_SYMBOLS;

static void __exit pcm20_cleanup(void)
{
	video_unregister_device(&pcm20_radio);
}

module_init(pcm20_init);
module_exit(pcm20_cleanup);

