/*
 * Audio Command Interface (ACI) driver (sound/aci.c)
 *
 * ACI is a protocol used to communicate with the microcontroller on
 * some sound cards produced by miro, e.g. the miroSOUND PCM12 and
 * PCM20. The ACI has been developed for miro by Norberto Pellicci
 * <pellicci@home.com>. Special thanks to both him and miro for
 * providing the ACI specification.
 *
 * The main function of the ACI is to control the mixer and to get a
 * product identification. On the PCM20, ACI also controls the radio
 * tuner on this card, this is supported in the Video for Linux 
 * radio-miropcm20 driver.
 * 
 * This Voxware ACI driver currently only supports the ACI functions
 * on the miroSOUND PCM12 and PCM20 card. Support for miro sound cards 
 * with additional ACI functions can easily be added later.
 *
 * / NOTE / When compiling as a module, make sure to load the module 
 * after loading the mad16 module. The initialisation code expects the
 * MAD16 default mixer to be already available.
 *
 * Revision history:
 *
 *   1995-11-10  Markus Kuhn <mskuhn@cip.informatik.uni-erlangen.de>
 *        First version written.
 *   1995-12-31  Markus Kuhn
 *        Second revision, general code cleanup.
 *   1996-05-16	 Hannu Savolainen
 *	  Integrated with other parts of the driver.
 *   1996-05-28  Markus Kuhn
 *        Initialize CS4231A mixer, make ACI first mixer,
 *        use new private mixer API for solo mode.
 *   1998-08-18  Ruurd Reitsma <R.A.Reitsma@wbmt.tudelft.nl>
 *	  Small modification to export ACI functions and 
 *	  complete modularisation.
 */

/*
 * Some driver specific information and features:
 *
 * This mixer driver identifies itself to applications as "ACI" in
 * mixer_info.id as retrieved by ioctl(fd, SOUND_MIXER_INFO, &mixer_info).
 *
 * Proprietary mixer features that go beyond the standard OSS mixer
 * interface are:
 * 
 * Full duplex solo configuration:
 *
 *   int solo_mode;
 *   ioctl(fd, SOUND_MIXER_PRIVATE1, &solo_mode);
 *
 *   solo_mode = 0: deactivate solo mode (default)
 *   solo_mode > 0: activate solo mode
 *                  With activated solo mode, the PCM input can not any
 *                  longer hear the signals produced by the PCM output.
 *                  Activating solo mode is important in duplex mode in order
 *                  to avoid feedback distortions.
 *   solo_mode < 0: do not change solo mode (just retrieve the status)
 *
 *   When the ioctl() returns 0, solo_mode contains the previous
 *   status (0 = deactivated, 1 = activated). If solo mode is not
 *   implemented on this card, ioctl() returns -1 and sets errno to
 *   EINVAL.
 *
 */

#include <linux/init.h>
#include <linux/module.h> 

#include "sound_config.h"

#undef  DEBUG		/* if defined, produce a verbose report via syslog */

int aci_port = 0x354;	/* as determined by bit 4 in the OPTi 929 MC4 register */
unsigned char aci_idcode[2] = {0, 0};	/* manufacturer and product ID */
unsigned char aci_version = 0;		/* ACI firmware version	*/
int aci_solo;		/* status bit of the card that can't be		*
			 * checked with ACI versions prior to 0xb0	*/

static int aci_present = 0;

#ifdef MODULE                  /* Whether the aci mixer is to be reset.    */
int aci_reset = 0;             /* Default: don't reset if the driver is a  */
MODULE_PARM(aci_reset,"i");
#else                          /* module; use "insmod aci.o aci_reset=1" */
int aci_reset = 1;             /* to override.                             */
#endif


#define COMMAND_REGISTER    (aci_port)
#define STATUS_REGISTER     (aci_port + 1)
#define BUSY_REGISTER       (aci_port + 2)

/*
 * Wait until the ACI microcontroller has set the READYFLAG in the
 * Busy/IRQ Source Register to 0. This is required to avoid
 * overrunning the sound card microcontroller. We do a busy wait here,
 * because the microcontroller is not supposed to signal a busy
 * condition for more than a few clock cycles. In case of a time-out,
 * this function returns -1.
 *
 * This busy wait code normally requires less than 15 loops and
 * practically always less than 100 loops on my i486/DX2 66 MHz.
 *
 * Warning: Waiting on the general status flag after reseting the MUTE
 * function can take a VERY long time, because the PCM12 does some kind
 * of fade-in effect. For this reason, access to the MUTE function has
 * not been implemented at all.
 */

static int busy_wait(void)
{
	long timeout;

	for (timeout = 0; timeout < 10000000L; timeout++)
		if ((inb_p(BUSY_REGISTER) & 1) == 0)
			return 0;

#ifdef DEBUG
	printk("ACI: READYFLAG timed out.\n");
#endif

	return -1;
}


/*
 * Read the GENERAL STATUS register.
 */

static int read_general_status(void)
{
	unsigned long flags;
	int status;

	save_flags(flags);
	cli();
	
	if (busy_wait()) {
		restore_flags(flags);
		return -1;
	}
	
	status = (unsigned) inb_p(STATUS_REGISTER);
	restore_flags(flags);
	return status;
}


/*
 * The four ACI command types (implied, write, read and indexed) can
 * be sent to the microcontroller using the following four functions.
 * If a problem occurred, they return -1.
 */

int aci_implied_cmd(unsigned char opcode)
{
	unsigned long flags;

#ifdef DEBUG
	printk("ACI: aci_implied_cmd(0x%02x)\n", opcode);
#endif

	save_flags(flags);
	cli();
  
  	if (read_general_status() < 0 || busy_wait()) {
		restore_flags(flags);
		return -1;
	}
	
	outb_p(opcode, COMMAND_REGISTER);

	restore_flags(flags);
	return 0;
}


int aci_write_cmd(unsigned char opcode, unsigned char parameter)
{
	unsigned long flags;
	int status;

#ifdef DEBUG
	printk("ACI: aci_write_cmd(0x%02x, 0x%02x)\n", opcode, parameter);
#endif

	save_flags(flags);
	cli();
	
	if (read_general_status() < 0 || busy_wait()) {
		restore_flags(flags);
		return -1;
	}

	outb_p(opcode, COMMAND_REGISTER);
	if (busy_wait()) {
		restore_flags(flags);
		return -1;
	}

	outb_p(parameter, COMMAND_REGISTER);

	if ((status = read_general_status()) < 0) {
		restore_flags(flags);
		return -1;
	}

	/* polarity of the INVALID flag depends on ACI version */
	if ((aci_version <  0xb0 && (status & 0x40) != 0) ||
	  (aci_version >= 0xb0 && (status & 0x40) == 0)) {
	  	restore_flags(flags);
		printk("ACI: invalid write command 0x%02x, 0x%02x.\n",
			opcode, parameter);
		return -1;
	}

	restore_flags(flags);
	return 0;
}

/*
 * This write command send 2 parameters instead of one.
 * Only used in PCM20 radio frequency tuning control
 */

int aci_write_cmd_d(unsigned char opcode, unsigned char parameter, unsigned char parameter2)
{
	unsigned long flags;
	int status;

#ifdef DEBUG
	printk("ACI: aci_write_cmd_d(0x%02x, 0x%02x)\n", opcode, parameter, parameter2);
#endif

	save_flags(flags);
	cli();
	
	if (read_general_status() < 0 || busy_wait()) {
		restore_flags(flags);
		return -1;
	}

	outb_p(opcode, COMMAND_REGISTER);
	if (busy_wait()) {
		restore_flags(flags);
		return -1;
	}

	outb_p(parameter, COMMAND_REGISTER);
	if (busy_wait()) {
		restore_flags(flags);
		return -1;
	}
	
	outb_p(parameter2, COMMAND_REGISTER);
	
	if ((status = read_general_status()) < 0) {
		restore_flags(flags);
		return -1;
	}
	
	/* polarity of the INVALID flag depends on ACI version */
	if ((aci_version <  0xb0 && (status & 0x40) != 0) ||
	  (aci_version >= 0xb0 && (status & 0x40) == 0)) {
		restore_flags(flags);
#if 0	/* Frequency tuning works, but the INVALID flag is set ??? */
		printk("ACI: invalid write (double) command 0x%02x, 0x%02x, 0x%02x.\n",
			opcode, parameter, parameter2);
#endif
		return -1;
  	}
	
	restore_flags(flags);
	return 0;
}

int aci_read_cmd(unsigned char opcode, int length, unsigned char *parameter)
{
	unsigned long flags;
	int i = 0;
	
	save_flags(flags);
	cli();
 
 	if (read_general_status() < 0) {
		restore_flags(flags);
		return -1;
	}
	while (i < length) {
		if (busy_wait()) {
			restore_flags(flags);
			return -1;
		}
			
		outb_p(opcode, COMMAND_REGISTER);
		if (busy_wait()) {
			restore_flags(flags);
			return -1;
		}
			
		parameter[i++] = inb_p(STATUS_REGISTER);
#ifdef DEBUG
		if (i == 1)
			printk("ACI: aci_read_cmd(0x%02x, %d) = 0x%02x\n",
				opcode, length, parameter[i-1]);
		else
			printk("ACI: aci_read_cmd cont.: 0x%02x\n", parameter[i-1]);
#endif
	}

	restore_flags(flags);
	return 0;
}


int aci_indexed_cmd(unsigned char opcode, unsigned char index,
		       unsigned char *parameter)
{
	unsigned long flags;

	save_flags(flags);
	cli();
  
	if (read_general_status() < 0 || busy_wait()) {
	  	restore_flags(flags);
		return -1;
	}
	
	outb_p(opcode, COMMAND_REGISTER);
	if (busy_wait()) {
		restore_flags(flags);
		return -1;
	}
	
	outb_p(index, COMMAND_REGISTER);
	if (busy_wait()) {
		restore_flags(flags);
		return -1;
	}
	
	*parameter = inb_p(STATUS_REGISTER);
#ifdef DEBUG
	printk("ACI: aci_indexed_cmd(0x%02x, 0x%02x) = 0x%02x\n", opcode, index,
		*parameter);
#endif

	restore_flags(flags);
	return 0;
}


/*
 * The following macro SCALE can be used to scale one integer volume
 * value into another one using only integer arithmetic. If the input
 * value x is in the range 0 <= x <= xmax, then the result will be in
 * the range 0 <= SCALE(xmax,ymax,x) <= ymax.
 *
 * This macro has for all xmax, ymax > 0 and all 0 <= x <= xmax the
 * following nice properties:
 *
 * - SCALE(xmax,ymax,xmax) = ymax
 * - SCALE(xmax,ymax,0) = 0
 * - SCALE(xmax,ymax,SCALE(ymax,xmax,SCALE(xmax,ymax,x))) = SCALE(xmax,ymax,x)
 *
 * In addition, the rounding error is minimal and nicely distributed.
 * The proofs are left as an exercise to the reader.
 */

#define SCALE(xmax,ymax,x) (((x)*(ymax)+(xmax)/2)/(xmax))


static int getvolume(caddr_t arg,
		     unsigned char left_index, unsigned char right_index)
{
	int vol;
	unsigned char buf;

	/* left channel */
	if (aci_indexed_cmd(0xf0, left_index, &buf))
		return -EIO;
	vol = SCALE(0x20, 100, buf < 0x20 ? 0x20-buf : 0);
	
	/* right channel */
	if (aci_indexed_cmd(0xf0, right_index, &buf))
		return -EIO;
	vol |= SCALE(0x20, 100, buf < 0x20 ? 0x20-buf : 0) << 8;

	return (*(int *) arg = vol);
}


static int setvolume(caddr_t arg, 
		     unsigned char left_index, unsigned char right_index)
{
	int vol, ret;

	/* left channel */
	vol = *(int *)arg & 0xff;
	if (vol > 100)
		vol = 100;
	vol = SCALE(100, 0x20, vol);
	if (aci_write_cmd(left_index, 0x20 - vol))
		return -EIO;
	ret = SCALE(0x20, 100, vol);


	/* right channel */
	vol = (*(int *)arg >> 8) & 0xff;
	if (vol > 100)
		vol = 100;
	vol = SCALE(100, 0x20, vol);
	if (aci_write_cmd(right_index, 0x20 - vol))
		return -EIO;
	ret |= SCALE(0x20, 100, vol) << 8;
 
	return (*(int *) arg = ret);
}


static int
aci_mixer_ioctl (int dev, unsigned int cmd, caddr_t arg)
{
	int status, vol;
	unsigned char buf;

	/* handle solo mode control */
	if (cmd == SOUND_MIXER_PRIVATE1) {
		if (*(int *) arg >= 0) {
			aci_solo = !!*(int *) arg;
			if (aci_write_cmd(0xd2, aci_solo))
				return -EIO;
		} else if (aci_version >= 0xb0) {
			if ((status = read_general_status()) < 0)
				return -EIO;
			return (*(int *) arg = (status & 0x20) == 0);
		}
		
		return (*(int *) arg = aci_solo);
	}
	
	if (((cmd >> 8) & 0xff) == 'M') {
		if (cmd & SIOC_IN)
			/* read and write */
			switch (cmd & 0xff) {
				case SOUND_MIXER_VOLUME:
					return setvolume(arg, 0x01, 0x00);
				case SOUND_MIXER_CD:
					return setvolume(arg, 0x3c, 0x34);
				case SOUND_MIXER_MIC:
					return setvolume(arg, 0x38, 0x30);
				case SOUND_MIXER_LINE:
					return setvolume(arg, 0x39, 0x31);
				case SOUND_MIXER_SYNTH:
					return setvolume(arg, 0x3b, 0x33);
				case SOUND_MIXER_PCM:
					return setvolume(arg, 0x3a, 0x32);
				case SOUND_MIXER_LINE1:  /* AUX1 */
					return setvolume(arg, 0x3d, 0x35);
				case SOUND_MIXER_LINE2:  /* AUX2 */
					return setvolume(arg, 0x3e, 0x36);
				case SOUND_MIXER_IGAIN:  /* MIC pre-amp */
					vol = *(int *) arg & 0xff;
					if (vol > 100)
						vol = 100;
					vol = SCALE(100, 3, vol);
					if (aci_write_cmd(0x03, vol))
						return -EIO;
					vol = SCALE(3, 100, vol);
					return (*(int *) arg = vol | (vol << 8));
				case SOUND_MIXER_RECSRC:
					return (*(int *) arg = 0);
					break;
				default:
					return -EINVAL;
			}
		else
			/* only read */
			switch (cmd & 0xff) {
				case SOUND_MIXER_DEVMASK:
					return (*(int *) arg =
				 SOUND_MASK_VOLUME | SOUND_MASK_CD    |
				 SOUND_MASK_MIC    | SOUND_MASK_LINE  |
				 SOUND_MASK_SYNTH  | SOUND_MASK_PCM   |
#if 0
				 SOUND_MASK_IGAIN  |
#endif
				 SOUND_MASK_LINE1  | SOUND_MASK_LINE2);
				 	break;
				case SOUND_MIXER_STEREODEVS:
					return (*(int *) arg =
				 SOUND_MASK_VOLUME | SOUND_MASK_CD   |
				 SOUND_MASK_MIC    | SOUND_MASK_LINE |
				 SOUND_MASK_SYNTH  | SOUND_MASK_PCM  |
				 SOUND_MASK_LINE1  | SOUND_MASK_LINE2);
				 	break;
				case SOUND_MIXER_RECMASK:
					return (*(int *) arg = 0);
					break;
				case SOUND_MIXER_RECSRC:
					return (*(int *) arg = 0);
					break;
				case SOUND_MIXER_CAPS:
					return (*(int *) arg = 0);
					break;
				case SOUND_MIXER_VOLUME:
					return getvolume(arg, 0x04, 0x03);
				case SOUND_MIXER_CD:
					return getvolume(arg, 0x0a, 0x09);
				case SOUND_MIXER_MIC:
					return getvolume(arg, 0x06, 0x05);
				case SOUND_MIXER_LINE:
					return getvolume(arg, 0x08, 0x07);
				case SOUND_MIXER_SYNTH:
					return getvolume(arg, 0x0c, 0x0b);
				case SOUND_MIXER_PCM:
					return getvolume(arg, 0x0e, 0x0d);
				case SOUND_MIXER_LINE1:  /* AUX1 */
					return getvolume(arg, 0x11, 0x10);
				case SOUND_MIXER_LINE2:  /* AUX2 */
					return getvolume(arg, 0x13, 0x12);
				case SOUND_MIXER_IGAIN:  /* MIC pre-amp */
					if (aci_indexed_cmd(0xf0, 0x21, &buf))
						return -EIO;
					vol = SCALE(3, 100, buf <= 3 ? buf : 3);
					vol |= vol << 8;
					return (*(int *) arg = vol);
				default:
					return -EINVAL;
			}
	}
	
	return -EINVAL;
}


static struct mixer_operations aci_mixer_operations =
{
	owner:	THIS_MODULE,
	id:	"ACI",
	name:	"ACI mixer",
	ioctl:	aci_mixer_ioctl
};

static unsigned char
mad_read (int port)
{
	outb (0xE3, 0xf8f); /* Write MAD16 password */
	return inb (port);  /* Read from port */
}


/*
 * Check, whether there actually is any ACI port operational and if
 * one was found, then initialize the ACI interface, reserve the I/O
 * addresses and attach the new mixer to the relevant VoxWare data
 * structures.
 *
 * Returns:  1   ACI mixer detected
 *           0   nothing there
 *
 * There is also an internal mixer in the codec (CS4231A or AD1845),
 * that deserves no purpose in an ACI based system which uses an
 * external ACI controlled stereo mixer. Make sure that this codec
 * mixer has the AUX1 input selected as the recording source, that the
 * input gain is set near maximum and that the other channels going
 * from the inputs to the codec output are muted.
 */

static int __init attach_aci(void)
{
	char *boardname = "unknown";
	int volume;

#define MC4_PORT	0xf90

	aci_port =
		(mad_read(MC4_PORT) & 0x10) ? 0x344 : 0x354;

	if (check_region(aci_port, 3)) {
#ifdef DEBUG
		printk("ACI: I/O area 0x%03x-0x%03x already used.\n",
			aci_port, aci_port+2);
#endif
		return 0;
	}
	
	if (aci_read_cmd(0xf2, 2, aci_idcode)) {
#ifdef DEBUG
		printk("ACI: Failed to read idcode.\n");
#endif
		return 0;
	}
	
	if (aci_read_cmd(0xf1, 1, &aci_version)) {
#ifdef DEBUG
		printk("ACI: Failed to read version.\n");
#endif
		return 0;
	}

	if (aci_idcode[0] == 0x6d) {
		/* It looks like a miro sound card. */
		switch (aci_idcode[1]) {
			case 0x41:
				boardname = "PCM1 pro / early PCM12";
				break;
			case 0x42:
				boardname = "PCM12";
				break;
			case 0x43:
				boardname = "PCM20";
				break;
			default:
				boardname = "unknown miro";
		}
	} else
#ifndef DEBUG
	return 0;
#endif
  
  	printk("<ACI %02x, id %02x %02x (%s)> at 0x%03x\n",
		aci_version, aci_idcode[0], aci_idcode[1], boardname, aci_port);

	if (aci_reset) {
		/* initialize ACI mixer */
		aci_implied_cmd(0xff);
		aci_solo = 0;
	}

	/* attach the mixer */
	request_region(aci_port, 3, "sound mixer (ACI)");
	if (num_mixers < MAX_MIXER_DEV) {
		if (num_mixers > 0 &&
		  !strncmp("MAD16 WSS", mixer_devs[num_mixers-1]->name, 9)) {
			/*
			 * The previously registered mixer device is the CS4231A which
			 * has no function on an ACI card. Make the ACI mixer the first
			 * of the two mixer devices.
			 */
			mixer_devs[num_mixers] = mixer_devs[num_mixers-1];
			mixer_devs[num_mixers-1] = &aci_mixer_operations;
			/*
			 * Initialize the CS4231A mixer with reasonable values. It is
			 * unlikely that the user ever will want to change these as all
			 * channels can be mixed via ACI.
			 */
			volume = 0x6464;
			mixer_devs[num_mixers]->ioctl(num_mixers,
				SOUND_MIXER_WRITE_PCM, (caddr_t) &volume);
			volume = 0x6464;
			mixer_devs[num_mixers]->ioctl(num_mixers,
				SOUND_MIXER_WRITE_IGAIN,   (caddr_t) &volume);
			volume = 0;
			mixer_devs[num_mixers]->ioctl(num_mixers,
				SOUND_MIXER_WRITE_SPEAKER, (caddr_t) &volume);
			volume = 0;
			mixer_devs[num_mixers]->ioctl(num_mixers,
				SOUND_MIXER_WRITE_MIC, (caddr_t) &volume);
			volume = 0;
			mixer_devs[num_mixers]->ioctl(num_mixers,
				SOUND_MIXER_WRITE_IMIX, (caddr_t) &volume);
			volume = 0;
			mixer_devs[num_mixers]->ioctl(num_mixers,
				SOUND_MIXER_WRITE_LINE1, (caddr_t) &volume);
			volume = 0;
			mixer_devs[num_mixers]->ioctl(num_mixers,
				SOUND_MIXER_WRITE_LINE2, (caddr_t) &volume);
			volume = 0;
			mixer_devs[num_mixers]->ioctl(num_mixers,
				SOUND_MIXER_WRITE_LINE3, (caddr_t) &volume);
			volume = SOUND_MASK_LINE1;
			mixer_devs[num_mixers]->ioctl(num_mixers,
				SOUND_MIXER_WRITE_RECSRC, (caddr_t) &volume);
			num_mixers++;
		} else
			mixer_devs[num_mixers++] = &aci_mixer_operations;
	}

	/* Just do something; otherwise the first write command fails, at
	 * least with my PCM20.
	 */
	aci_mixer_ioctl(num_mixers-1, SOUND_MIXER_READ_VOLUME, (caddr_t) &volume);
	
	if (aci_reset) {
		/* Initialize ACI mixer with reasonable power-up values */
		volume = 0x3232;
		aci_mixer_ioctl(num_mixers-1, SOUND_MIXER_WRITE_VOLUME, (caddr_t) &volume);
		volume = 0x3232;
		aci_mixer_ioctl(num_mixers-1, SOUND_MIXER_WRITE_SYNTH,  (caddr_t) &volume);
		volume = 0x3232;
		aci_mixer_ioctl(num_mixers-1, SOUND_MIXER_WRITE_PCM,    (caddr_t) &volume);
		volume = 0x3232;
		aci_mixer_ioctl(num_mixers-1, SOUND_MIXER_WRITE_LINE,   (caddr_t) &volume);
		volume = 0x3232;
		aci_mixer_ioctl(num_mixers-1, SOUND_MIXER_WRITE_MIC,    (caddr_t) &volume);
		volume = 0x3232;
		aci_mixer_ioctl(num_mixers-1, SOUND_MIXER_WRITE_CD,     (caddr_t) &volume);
		volume = 0x3232;
		aci_mixer_ioctl(num_mixers-1, SOUND_MIXER_WRITE_LINE1,  (caddr_t) &volume);
		volume = 0x3232;
		aci_mixer_ioctl(num_mixers-1, SOUND_MIXER_WRITE_LINE2,  (caddr_t) &volume);
	}

	aci_present = 1;

	return 1;
}

static void __exit unload_aci(void)
{
	if (aci_present)
		release_region(aci_port, 3);
}

module_init(attach_aci);
module_exit(unload_aci);
