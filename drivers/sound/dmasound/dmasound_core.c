
/*
 *  linux/drivers/sound/dmasound/dmasound_core.c
 *
 *
 *  OSS/Free compatible Atari TT/Falcon and Amiga DMA sound driver for
 *  Linux/m68k
 *  Extended to support Power Macintosh for Linux/ppc by Paul Mackerras
 *
 *  (c) 1995 by Michael Schlueter & Michael Marte
 *
 *  Michael Schlueter (michael@duck.syd.de) did the basic structure of the VFS
 *  interface and the u-law to signed byte conversion.
 *
 *  Michael Marte (marte@informatik.uni-muenchen.de) did the sound queue,
 *  /dev/mixer, /dev/sndstat and complemented the VFS interface. He would like
 *  to thank:
 *    - Michael Schlueter for initial ideas and documentation on the MFP and
 *	the DMA sound hardware.
 *    - Therapy? for their CD 'Troublegum' which really made me rock.
 *
 *  /dev/sndstat is based on code by Hannu Savolainen, the author of the
 *  VoxWare family of drivers.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 *
 *  History:
 *
 *	1995/8/25	First release
 *
 *	1995/9/02	Roman Hodek:
 *			  - Fixed atari_stram_alloc() call, the timer
 *			    programming and several race conditions
 *	1995/9/14	Roman Hodek:
 *			  - After some discussion with Michael Schlueter,
 *			    revised the interrupt disabling
 *			  - Slightly speeded up U8->S8 translation by using
 *			    long operations where possible
 *			  - Added 4:3 interpolation for /dev/audio
 *
 *	1995/9/20	Torsten Scherer:
 *			  - Fixed a bug in sq_write and changed /dev/audio
 *			    converting to play at 12517Hz instead of 6258Hz.
 *
 *	1995/9/23	Torsten Scherer:
 *			  - Changed sq_interrupt() and sq_play() to pre-program
 *			    the DMA for another frame while there's still one
 *			    running. This allows the IRQ response to be
 *			    arbitrarily delayed and playing will still continue.
 *
 *	1995/10/14	Guenther Kelleter, Torsten Scherer:
 *			  - Better support for Falcon audio (the Falcon doesn't
 *			    raise an IRQ at the end of a frame, but at the
 *			    beginning instead!). uses 'if (codec_dma)' in lots
 *			    of places to simply switch between Falcon and TT
 *			    code.
 *
 *	1995/11/06	Torsten Scherer:
 *			  - Started introducing a hardware abstraction scheme
 *			    (may perhaps also serve for Amigas?)
 *			  - Can now play samples at almost all frequencies by
 *			    means of a more generalized expand routine
 *			  - Takes a good deal of care to cut data only at
 *			    sample sizes
 *			  - Buffer size is now a kernel runtime option
 *			  - Implemented fsync() & several minor improvements
 *			Guenther Kelleter:
 *			  - Useful hints and bug fixes
 *			  - Cross-checked it for Falcons
 *
 *	1996/3/9	Geert Uytterhoeven:
 *			  - Support added for Amiga, A-law, 16-bit little
 *			    endian.
 *			  - Unification to drivers/sound/dmasound.c.
 *
 *	1996/4/6	Martin Mitchell:
 *			  - Updated to 1.3 kernel.
 *
 *	1996/6/13       Topi Kanerva:
 *			  - Fixed things that were broken (mainly the amiga
 *			    14-bit routines)
 *			  - /dev/sndstat shows now the real hardware frequency
 *			  - The lowpass filter is disabled by default now
 *
 *	1996/9/25	Geert Uytterhoeven:
 *			  - Modularization
 *
 *	1998/6/10	Andreas Schwab:
 *			  - Converted to use sound_core
 *
 *	1999/12/28	Richard Zidlicky:
 *			  - Added support for Q40
 *
 *	2000/2/27	Geert Uytterhoeven:
 *			  - Clean up and split the code into 4 parts:
 *			      o dmasound_core: machine-independent code
 *			      o dmasound_atari: Atari TT and Falcon support
 *			      o dmasound_awacs: Apple PowerMac support
 *			      o dmasound_paula: Amiga support
 *
 *	2000/3/25	Geert Uytterhoeven:
 *			  - Integration of dmasound_q40
 *			  - Small clean ups
 */


#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/sound.h>
#include <linux/init.h>
#include <linux/soundcard.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

#include "dmasound.h"


    /*
     *  Declarations
     */

int dmasound_catchRadius = 0;
static unsigned int numWriteBufs = 4;
static unsigned int writeBufSize = 32;	/* in KB! */
#ifdef HAS_RECORD
static unsigned int numReadBufs = 4;
static unsigned int readBufSize = 32;	/* in KB! */
#endif

MODULE_PARM(dmasound_catchRadius, "i");
MODULE_PARM(numWriteBufs, "i");
MODULE_PARM(writeBufSize, "i");
MODULE_PARM(numReadBufs, "i");
MODULE_PARM(readBufSize, "i");

#ifdef MODULE
static int sq_unit = -1;
static int mixer_unit = -1;
static int state_unit = -1;
static int irq_installed = 0;
#endif /* MODULE */


    /*
     *  Conversion tables
     */

#ifdef HAS_8BIT_TABLES
/* 8 bit mu-law */

char dmasound_ulaw2dma8[] = {
	-126,	-122,	-118,	-114,	-110,	-106,	-102,	-98,
	-94,	-90,	-86,	-82,	-78,	-74,	-70,	-66,
	-63,	-61,	-59,	-57,	-55,	-53,	-51,	-49,
	-47,	-45,	-43,	-41,	-39,	-37,	-35,	-33,
	-31,	-30,	-29,	-28,	-27,	-26,	-25,	-24,
	-23,	-22,	-21,	-20,	-19,	-18,	-17,	-16,
	-16,	-15,	-15,	-14,	-14,	-13,	-13,	-12,
	-12,	-11,	-11,	-10,	-10,	-9,	-9,	-8,
	-8,	-8,	-7,	-7,	-7,	-7,	-6,	-6,
	-6,	-6,	-5,	-5,	-5,	-5,	-4,	-4,
	-4,	-4,	-4,	-4,	-3,	-3,	-3,	-3,
	-3,	-3,	-3,	-3,	-2,	-2,	-2,	-2,
	-2,	-2,	-2,	-2,	-2,	-2,	-2,	-2,
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	0,
	125,	121,	117,	113,	109,	105,	101,	97,
	93,	89,	85,	81,	77,	73,	69,	65,
	62,	60,	58,	56,	54,	52,	50,	48,
	46,	44,	42,	40,	38,	36,	34,	32,
	30,	29,	28,	27,	26,	25,	24,	23,
	22,	21,	20,	19,	18,	17,	16,	15,
	15,	14,	14,	13,	13,	12,	12,	11,
	11,	10,	10,	9,	9,	8,	8,	7,
	7,	7,	6,	6,	6,	6,	5,	5,
	5,	5,	4,	4,	4,	4,	3,	3,
	3,	3,	3,	3,	2,	2,	2,	2,
	2,	2,	2,	2,	1,	1,	1,	1,
	1,	1,	1,	1,	1,	1,	1,	1,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0
};

/* 8 bit A-law */

char dmasound_alaw2dma8[] = {
	-22,	-21,	-24,	-23,	-18,	-17,	-20,	-19,
	-30,	-29,	-32,	-31,	-26,	-25,	-28,	-27,
	-11,	-11,	-12,	-12,	-9,	-9,	-10,	-10,
	-15,	-15,	-16,	-16,	-13,	-13,	-14,	-14,
	-86,	-82,	-94,	-90,	-70,	-66,	-78,	-74,
	-118,	-114,	-126,	-122,	-102,	-98,	-110,	-106,
	-43,	-41,	-47,	-45,	-35,	-33,	-39,	-37,
	-59,	-57,	-63,	-61,	-51,	-49,	-55,	-53,
	-2,	-2,	-2,	-2,	-2,	-2,	-2,	-2,
	-2,	-2,	-2,	-2,	-2,	-2,	-2,	-2,
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	-6,	-6,	-6,	-6,	-5,	-5,	-5,	-5,
	-8,	-8,	-8,	-8,	-7,	-7,	-7,	-7,
	-3,	-3,	-3,	-3,	-3,	-3,	-3,	-3,
	-4,	-4,	-4,	-4,	-4,	-4,	-4,	-4,
	21,	20,	23,	22,	17,	16,	19,	18,
	29,	28,	31,	30,	25,	24,	27,	26,
	10,	10,	11,	11,	8,	8,	9,	9,
	14,	14,	15,	15,	12,	12,	13,	13,
	86,	82,	94,	90,	70,	66,	78,	74,
	118,	114,	126,	122,	102,	98,	110,	106,
	43,	41,	47,	45,	35,	33,	39,	37,
	59,	57,	63,	61,	51,	49,	55,	53,
	1,	1,	1,	1,	1,	1,	1,	1,
	1,	1,	1,	1,	1,	1,	1,	1,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	5,	5,	5,	5,	4,	4,	4,	4,
	7,	7,	7,	7,	6,	6,	6,	6,
	2,	2,	2,	2,	2,	2,	2,	2,
	3,	3,	3,	3,	3,	3,	3,	3
};
#endif /* HAS_8BIT_TABLES */

#ifdef HAS_16BIT_TABLES

/* 16 bit mu-law */

short dmasound_ulaw2dma16[] = {
	-32124,	-31100,	-30076,	-29052,	-28028,	-27004,	-25980,	-24956,
	-23932,	-22908,	-21884,	-20860,	-19836,	-18812,	-17788,	-16764,
	-15996,	-15484,	-14972,	-14460,	-13948,	-13436,	-12924,	-12412,
	-11900,	-11388,	-10876,	-10364,	-9852,	-9340,	-8828,	-8316,
	-7932,	-7676,	-7420,	-7164,	-6908,	-6652,	-6396,	-6140,
	-5884,	-5628,	-5372,	-5116,	-4860,	-4604,	-4348,	-4092,
	-3900,	-3772,	-3644,	-3516,	-3388,	-3260,	-3132,	-3004,
	-2876,	-2748,	-2620,	-2492,	-2364,	-2236,	-2108,	-1980,
	-1884,	-1820,	-1756,	-1692,	-1628,	-1564,	-1500,	-1436,
	-1372,	-1308,	-1244,	-1180,	-1116,	-1052,	-988,	-924,
	-876,	-844,	-812,	-780,	-748,	-716,	-684,	-652,
	-620,	-588,	-556,	-524,	-492,	-460,	-428,	-396,
	-372,	-356,	-340,	-324,	-308,	-292,	-276,	-260,
	-244,	-228,	-212,	-196,	-180,	-164,	-148,	-132,
	-120,	-112,	-104,	-96,	-88,	-80,	-72,	-64,
	-56,	-48,	-40,	-32,	-24,	-16,	-8,	0,
	32124,	31100,	30076,	29052,	28028,	27004,	25980,	24956,
	23932,	22908,	21884,	20860,	19836,	18812,	17788,	16764,
	15996,	15484,	14972,	14460,	13948,	13436,	12924,	12412,
	11900,	11388,	10876,	10364,	9852,	9340,	8828,	8316,
	7932,	7676,	7420,	7164,	6908,	6652,	6396,	6140,
	5884,	5628,	5372,	5116,	4860,	4604,	4348,	4092,
	3900,	3772,	3644,	3516,	3388,	3260,	3132,	3004,
	2876,	2748,	2620,	2492,	2364,	2236,	2108,	1980,
	1884,	1820,	1756,	1692,	1628,	1564,	1500,	1436,
	1372,	1308,	1244,	1180,	1116,	1052,	988,	924,
	876,	844,	812,	780,	748,	716,	684,	652,
	620,	588,	556,	524,	492,	460,	428,	396,
	372,	356,	340,	324,	308,	292,	276,	260,
	244,	228,	212,	196,	180,	164,	148,	132,
	120,	112,	104,	96,	88,	80,	72,	64,
	56,	48,	40,	32,	24,	16,	8,	0,
};

/* 16 bit A-law */

short dmasound_alaw2dma16[] = {
	-5504,	-5248,	-6016,	-5760,	-4480,	-4224,	-4992,	-4736,
	-7552,	-7296,	-8064,	-7808,	-6528,	-6272,	-7040,	-6784,
	-2752,	-2624,	-3008,	-2880,	-2240,	-2112,	-2496,	-2368,
	-3776,	-3648,	-4032,	-3904,	-3264,	-3136,	-3520,	-3392,
	-22016,	-20992,	-24064,	-23040,	-17920,	-16896,	-19968,	-18944,
	-30208,	-29184,	-32256,	-31232,	-26112,	-25088,	-28160,	-27136,
	-11008,	-10496,	-12032,	-11520,	-8960,	-8448,	-9984,	-9472,
	-15104,	-14592,	-16128,	-15616,	-13056,	-12544,	-14080,	-13568,
	-344,	-328,	-376,	-360,	-280,	-264,	-312,	-296,
	-472,	-456,	-504,	-488,	-408,	-392,	-440,	-424,
	-88,	-72,	-120,	-104,	-24,	-8,	-56,	-40,
	-216,	-200,	-248,	-232,	-152,	-136,	-184,	-168,
	-1376,	-1312,	-1504,	-1440,	-1120,	-1056,	-1248,	-1184,
	-1888,	-1824,	-2016,	-1952,	-1632,	-1568,	-1760,	-1696,
	-688,	-656,	-752,	-720,	-560,	-528,	-624,	-592,
	-944,	-912,	-1008,	-976,	-816,	-784,	-880,	-848,
	5504,	5248,	6016,	5760,	4480,	4224,	4992,	4736,
	7552,	7296,	8064,	7808,	6528,	6272,	7040,	6784,
	2752,	2624,	3008,	2880,	2240,	2112,	2496,	2368,
	3776,	3648,	4032,	3904,	3264,	3136,	3520,	3392,
	22016,	20992,	24064,	23040,	17920,	16896,	19968,	18944,
	30208,	29184,	32256,	31232,	26112,	25088,	28160,	27136,
	11008,	10496,	12032,	11520,	8960,	8448,	9984,	9472,
	15104,	14592,	16128,	15616,	13056,	12544,	14080,	13568,
	344,	328,	376,	360,	280,	264,	312,	296,
	472,	456,	504,	488,	408,	392,	440,	424,
	88,	72,	120,	104,	24,	8,	56,	40,
	216,	200,	248,	232,	152,	136,	184,	168,
	1376,	1312,	1504,	1440,	1120,	1056,	1248,	1184,
	1888,	1824,	2016,	1952,	1632,	1568,	1760,	1696,
	688,	656,	752,	720,	560,	528,	624,	592,
	944,	912,	1008,	976,	816,	784,	880,	848,
};
#endif /* HAS_16BIT_TABLES */


#ifdef HAS_14BIT_TABLES

    /*
     *  Unused for now. Where are the MSB parts anyway??
     */

/* 14 bit mu-law (LSB) */

char dmasound_ulaw2dma14l[] = {
	33,	33,	33,	33,	33,	33,	33,	33,
	33,	33,	33,	33,	33,	33,	33,	33,
	33,	33,	33,	33,	33,	33,	33,	33,
	33,	33,	33,	33,	33,	33,	33,	33,
	1,	1,	1,	1,	1,	1,	1,	1,
	1,	1,	1,	1,	1,	1,	1,	1,
	49,	17,	49,	17,	49,	17,	49,	17,
	49,	17,	49,	17,	49,	17,	49,	17,
	41,	57,	9,	25,	41,	57,	9,	25,
	41,	57,	9,	25,	41,	57,	9,	25,
	37,	45,	53,	61,	5,	13,	21,	29,
	37,	45,	53,	61,	5,	13,	21,	29,
	35,	39,	43,	47,	51,	55,	59,	63,
	3,	7,	11,	15,	19,	23,	27,	31,
	34,	36,	38,	40,	42,	44,	46,	48,
	50,	52,	54,	56,	58,	60,	62,	0,
	31,	31,	31,	31,	31,	31,	31,	31,
	31,	31,	31,	31,	31,	31,	31,	31,
	31,	31,	31,	31,	31,	31,	31,	31,
	31,	31,	31,	31,	31,	31,	31,	31,
	63,	63,	63,	63,	63,	63,	63,	63,
	63,	63,	63,	63,	63,	63,	63,	63,
	15,	47,	15,	47,	15,	47,	15,	47,
	15,	47,	15,	47,	15,	47,	15,	47,
	23,	7,	55,	39,	23,	7,	55,	39,
	23,	7,	55,	39,	23,	7,	55,	39,
	27,	19,	11,	3,	59,	51,	43,	35,
	27,	19,	11,	3,	59,	51,	43,	35,
	29,	25,	21,	17,	13,	9,	5,	1,
	61,	57,	53,	49,	45,	41,	37,	33,
	30,	28,	26,	24,	22,	20,	18,	16,
	14,	12,	10,	8,	6,	4,	2,	0
};

/* 14 bit A-law (LSB) */

char dmasound_alaw2dma14l[] = {
	32,	32,	32,	32,	32,	32,	32,	32,
	32,	32,	32,	32,	32,	32,	32,	32,
	16,	48,	16,	48,	16,	48,	16,	48,
	16,	48,	16,	48,	16,	48,	16,	48,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	42,	46,	34,	38,	58,	62,	50,	54,
	10,	14,	2,	6,	26,	30,	18,	22,
	42,	46,	34,	38,	58,	62,	50,	54,
	10,	14,	2,	6,	26,	30,	18,	22,
	40,	56,	8,	24,	40,	56,	8,	24,
	40,	56,	8,	24,	40,	56,	8,	24,
	20,	28,	4,	12,	52,	60,	36,	44,
	20,	28,	4,	12,	52,	60,	36,	44,
	32,	32,	32,	32,	32,	32,	32,	32,
	32,	32,	32,	32,	32,	32,	32,	32,
	48,	16,	48,	16,	48,	16,	48,	16,
	48,	16,	48,	16,	48,	16,	48,	16,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	22,	18,	30,	26,	6,	2,	14,	10,
	54,	50,	62,	58,	38,	34,	46,	42,
	22,	18,	30,	26,	6,	2,	14,	10,
	54,	50,	62,	58,	38,	34,	46,	42,
	24,	8,	56,	40,	24,	8,	56,	40,
	24,	8,	56,	40,	24,	8,	56,	40,
	44,	36,	60,	52,	12,	4,	28,	20,
	44,	36,	60,	52,	12,	4,	28,	20
};
#endif /* HAS_14BIT_TABLES */


    /*
     *  Common stuff
     */

static long long sound_lseek(struct file *file, long long offset, int orig)
{
	return -ESPIPE;
}


    /*
     *  Mid level stuff
     */

struct sound_settings dmasound;

static inline void sound_silence(void)
{
	/* update hardware settings one more */
	dmasound.mach.init();

	dmasound.mach.silence();
}

static inline void sound_init(void)
{
	dmasound.mach.init();
}

static inline int sound_set_format(int format)
{
	return dmasound.mach.setFormat(format);
}

static int sound_set_speed(int speed)
{
	if (speed < 0)
		return dmasound.soft.speed;

	dmasound.soft.speed = speed;
	dmasound.mach.init();
	if (dmasound.minDev == SND_DEV_DSP)
		dmasound.dsp.speed = dmasound.soft.speed;

	return dmasound.soft.speed;
}

static int sound_set_stereo(int stereo)
{
	if (stereo < 0)
		return dmasound.soft.stereo;

	stereo = !!stereo;    /* should be 0 or 1 now */

	dmasound.soft.stereo = stereo;
	if (dmasound.minDev == SND_DEV_DSP)
		dmasound.dsp.stereo = stereo;
	dmasound.mach.init();

	return stereo;
}

static ssize_t sound_copy_translate(TRANS *trans, const u_char *userPtr,
				    size_t userCount, u_char frame[],
				    ssize_t *frameUsed, ssize_t frameLeft)
{
	ssize_t (*ct_func)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);

	switch (dmasound.soft.format) {
	    case AFMT_MU_LAW:
		ct_func = trans->ct_ulaw;
		break;
	    case AFMT_A_LAW:
		ct_func = trans->ct_alaw;
		break;
	    case AFMT_S8:
		ct_func = trans->ct_s8;
		break;
	    case AFMT_U8:
		ct_func = trans->ct_u8;
		break;
	    case AFMT_S16_BE:
		ct_func = trans->ct_s16be;
		break;
	    case AFMT_U16_BE:
		ct_func = trans->ct_u16be;
		break;
	    case AFMT_S16_LE:
		ct_func = trans->ct_s16le;
		break;
	    case AFMT_U16_LE:
		ct_func = trans->ct_u16le;
		break;
	    default:
		return 0;
	}
	return ct_func(userPtr, userCount, frame, frameUsed, frameLeft);
}


    /*
     *  /dev/mixer abstraction
     */

static struct {
    int busy;
    int modify_counter;
} mixer;

static int mixer_open(struct inode *inode, struct file *file)
{
	dmasound.mach.open();
	mixer.busy = 1;
	return 0;
}

static int mixer_release(struct inode *inode, struct file *file)
{
	lock_kernel();
	mixer.busy = 0;
	dmasound.mach.release();
	unlock_kernel();
	return 0;
}
static int mixer_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg)
{
	if (_SIOC_DIR(cmd) & _SIOC_WRITE)
	    mixer.modify_counter++;
	switch (cmd) {
	    case OSS_GETVERSION:
		return IOCTL_OUT(arg, SOUND_VERSION);
	    case SOUND_MIXER_INFO:
		{
		    mixer_info info;
		    strncpy(info.id, dmasound.mach.name2, sizeof(info.id));
		    strncpy(info.name, dmasound.mach.name2, sizeof(info.name));
		    info.name[sizeof(info.name)-1] = 0;
		    info.modify_counter = mixer.modify_counter;
		    if (copy_to_user((int *)arg, &info, sizeof(info)))
			    return -EFAULT;
		    return 0;
		}
	}
	if (dmasound.mach.mixer_ioctl)
	    return dmasound.mach.mixer_ioctl(cmd, arg);
	return -EINVAL;
}

static struct file_operations mixer_fops =
{
	owner:		THIS_MODULE,
	llseek:		sound_lseek,
	ioctl:		mixer_ioctl,
	open:		mixer_open,
	release:	mixer_release,
};

static void __init mixer_init(void)
{
#ifndef MODULE
	int mixer_unit;
#endif
	mixer_unit = register_sound_mixer(&mixer_fops, -1);
	if (mixer_unit < 0)
		return;

	mixer.busy = 0;
	dmasound.treble = 0;
	dmasound.bass = 0;
	if (dmasound.mach.mixer_init)
	    dmasound.mach.mixer_init();
}


    /*
     *  Sound queue stuff, the heart of the driver
     */

struct sound_queue dmasound_write_sq;
#ifdef HAS_RECORD
struct sound_queue dmasound_read_sq;
#endif

static int sq_allocate_buffers(struct sound_queue *sq, int num, int size)
{
	int i;

	if (sq->buffers)
		return 0;
	sq->numBufs = num;
	sq->bufSize = size;
	sq->buffers = kmalloc (num * sizeof(char *), GFP_KERNEL);
	if (!sq->buffers)
		return -ENOMEM;
	for (i = 0; i < num; i++) {
		sq->buffers[i] = dmasound.mach.dma_alloc(size, GFP_KERNEL);
		if (!sq->buffers[i]) {
			while (i--)
				dmasound.mach.dma_free(sq->buffers[i], size);
			kfree(sq->buffers);
			sq->buffers = 0;
			return -ENOMEM;
		}
	}
	return 0;
}

static void sq_release_buffers(struct sound_queue *sq)
{
	int i;

	if (sq->buffers) {
		if (sq != &write_sq && dmasound.mach.abort_read)
			dmasound.mach.abort_read();
		for (i = 0; i < sq->numBufs; i++)
			dmasound.mach.dma_free(sq->buffers[i], sq->bufSize);
		kfree(sq->buffers);
		sq->buffers = NULL;
	}
}

static void sq_setup(struct sound_queue *sq, int max_count, int max_active,
		     int block_size)
{
	void (*setup_func)(void);

	sq->max_count = max_count;
	sq->max_active = max_active;
	sq->block_size = block_size;

	sq->front = sq->count = sq->rear_size = 0;
	sq->syncing = 0;
	sq->active = 0;

	if (sq == &write_sq) {
	    sq->rear = -1;
	    setup_func = dmasound.mach.write_sq_setup;
	} else {
	    sq->rear = 0;
	    setup_func = dmasound.mach.read_sq_setup;
	}
	if (setup_func)
	    setup_func();
}

static inline void sq_play(void)
{
	dmasound.mach.play();
}

static ssize_t sq_write(struct file *file, const char *src, size_t uLeft,
			loff_t *ppos)
{
	ssize_t uWritten = 0;
	u_char *dest;
	ssize_t uUsed, bUsed, bLeft;

	/* ++TeSche: Is something like this necessary?
	 * Hey, that's an honest question! Or does any other part of the
	 * filesystem already checks this situation? I really don't know.
	 */
	if (uLeft == 0)
		return 0;

	/* The interrupt doesn't start to play the last, incomplete frame.
	 * Thus we can append to it without disabling the interrupts! (Note
	 * also that write_sq.rear isn't affected by the interrupt.)
	 */

	if (write_sq.count > 0 &&
	    (bLeft = write_sq.block_size-write_sq.rear_size) > 0) {
		dest = write_sq.buffers[write_sq.rear];
		bUsed = write_sq.rear_size;
		uUsed = sound_copy_translate(dmasound.trans_write, src, uLeft,
					     dest, &bUsed, bLeft);
		if (uUsed <= 0)
			return uUsed;
		src += uUsed;
		uWritten += uUsed;
		uLeft -= uUsed;
		write_sq.rear_size = bUsed;
	}

	do {
		while (write_sq.count == write_sq.max_active) {
			sq_play();
			if (write_sq.open_mode & O_NONBLOCK)
				return uWritten > 0 ? uWritten : -EAGAIN;
			SLEEP(write_sq.action_queue);
			if (signal_pending(current))
				return uWritten > 0 ? uWritten : -EINTR;
		}

		/* Here, we can avoid disabling the interrupt by first
		 * copying and translating the data, and then updating
		 * the write_sq variables. Until this is done, the interrupt
		 * won't see the new frame and we can work on it
		 * undisturbed.
		 */

		dest = write_sq.buffers[(write_sq.rear+1) % write_sq.max_count];
		bUsed = 0;
		bLeft = write_sq.block_size;
		uUsed = sound_copy_translate(dmasound.trans_write, src, uLeft,
					     dest, &bUsed, bLeft);
		if (uUsed <= 0)
			break;
		src += uUsed;
		uWritten += uUsed;
		uLeft -= uUsed;
		if (bUsed) {
			write_sq.rear = (write_sq.rear+1) % write_sq.max_count;
			write_sq.rear_size = bUsed;
			write_sq.count++;
		}
	} while (bUsed);   /* uUsed may have been 0 */

	sq_play();

	return uUsed < 0? uUsed: uWritten;
}

#ifdef HAS_RECORD
    /*
     *  Here is how the values are used for reading.
     *  The value 'active' simply indicates the DMA is running.  This is done
     *  so the driver semantics are DMA starts when the first read is posted.
     *  The value 'front' indicates the buffer we should next send to the user.
     *  The value 'rear' indicates the buffer the DMA is currently filling.
     *  When 'front' == 'rear' the buffer "ring" is empty (we always have an
     *  empty available).  The 'rear_size' is used to track partial offsets
     *  into the current buffer.  Right now, I just keep the DMA running.  If
     *  the reader can't keep up, the interrupt tosses the oldest buffer.  We
     *  could also shut down the DMA in this case.
     */

static ssize_t sq_read(struct file *file, char *dst, size_t uLeft,
		       loff_t *ppos)
{

	ssize_t	uRead, bLeft, bUsed, uUsed;

	if (uLeft == 0)
		return 0;

	if (!read_sq.active && dmasound.mach.record)
		dmasound.mach.record();	/* Kick off the record process. */

	uRead = 0;

	/* Move what the user requests, depending upon other options.
	*/
	while (uLeft > 0) {

		/* When front == rear, the DMA is not done yet.
		*/
		while (read_sq.front == read_sq.rear) {
			if (read_sq.open_mode & O_NONBLOCK) {
			       return uRead > 0 ? uRead : -EAGAIN;
			}
			SLEEP(read_sq.action_queue);
			if (signal_pending(current))
				return uRead > 0 ? uRead : -EINTR;
		}

		/* The amount we move is either what is left in the
		 * current buffer or what the user wants.
		 */
		bLeft = read_sq.block_size - read_sq.rear_size;
		bUsed = read_sq.rear_size;
		uUsed = sound_copy_translate(dmasound.trans_read, dst, uLeft,
					     read_sq.buffers[read_sq.front],
					     &bUsed, bLeft);
		if (uUsed <= 0)
			return uUsed;
		dst += uUsed;
		uRead += uUsed;
		uLeft -= uUsed;
		read_sq.rear_size += bUsed;
		if (read_sq.rear_size >= read_sq.block_size) {
			read_sq.rear_size = 0;
			read_sq.front++;
			if (read_sq.front >= read_sq.max_active)
				read_sq.front = 0;
		}
	}
	return uRead;
}
#endif /* HAS_RECORD */

static inline void sq_init_waitqueue(struct sound_queue *sq)
{
	init_waitqueue_head(&sq->action_queue);
	init_waitqueue_head(&sq->open_queue);
	init_waitqueue_head(&sq->sync_queue);
	sq->busy = 0;
}

static inline void sq_wake_up(struct sound_queue *sq, struct file *file,
			      mode_t mode)
{
	if (file->f_mode & mode) {
		sq->busy = 0;
		WAKE_UP(sq->open_queue);
	}
}

static int sq_open2(struct sound_queue *sq, struct file *file, mode_t mode,
		    int numbufs, int bufsize)
{
	int rc = 0;

	if (file->f_mode & mode) {
		if (sq->busy) {
			rc = -EBUSY;
			if (file->f_flags & O_NONBLOCK)
				return rc;
			rc = -EINTR;
			while (sq->busy) {
				SLEEP(sq->open_queue);
				if (signal_pending(current))
					return rc;
			}
			rc = 0;
		}
		sq->busy = 1; /* Let's play spot-the-race-condition */

		if (sq_allocate_buffers(sq, numbufs, bufsize)) {
			sq_wake_up(sq, file, mode);
			return rc;
		}

		sq_setup(sq, numbufs, numbufs, bufsize);
		sq->open_mode = file->f_mode;
	}
	return rc;
}

#define write_sq_init_waitqueue()	sq_init_waitqueue(&write_sq)
#define write_sq_wake_up(file)		sq_wake_up(&write_sq, file, FMODE_WRITE)
#define write_sq_release_buffers()	sq_release_buffers(&write_sq)
#define write_sq_open(file)	\
	sq_open2(&write_sq, file, FMODE_WRITE, numWriteBufs, writeBufSize << 10)

#ifdef HAS_RECORD
#define read_sq_init_waitqueue()	sq_init_waitqueue(&read_sq)
#define read_sq_wake_up(file)		sq_wake_up(&read_sq, file, FMODE_READ)
#define read_sq_release_buffers()	sq_release_buffers(&read_sq)
#define read_sq_open(file)	\
	sq_open2(&read_sq, file, FMODE_READ, numReadBufs, readBufSize << 10)
#else /* !HAS_RECORD */
#define read_sq_init_waitqueue()	do {} while (0)
#define read_sq_wake_up(file)		do {} while (0)
#define read_sq_release_buffers()	do {} while (0)
#define read_sq_open(file)		(0)
#endif /* !HAS_RECORD */

static int sq_open(struct inode *inode, struct file *file)
{
	int rc;

	dmasound.mach.open();
	if ((rc = write_sq_open(file)) || (rc = read_sq_open(file))) {
		dmasound.mach.release();
		return rc;
	}

	if (dmasound.mach.sq_open)
	    dmasound.mach.sq_open();
	dmasound.minDev = MINOR(inode->i_rdev) & 0x0f;
	dmasound.soft = dmasound.dsp;
	dmasound.hard = dmasound.dsp;
	sound_init();
	if ((MINOR(inode->i_rdev) & 0x0f) == SND_DEV_AUDIO) {
		sound_set_speed(8000);
		sound_set_stereo(0);
		sound_set_format(AFMT_MU_LAW);
	}

#if 0
	if (file->f_mode == FMODE_READ && dmasound.mach.record) {
		/* Start dma'ing straight away */
		dmasound.mach.record();
	}
#endif

	return 0;
}

static void sq_reset(void)
{
	sound_silence();
	write_sq.active = 0;
	write_sq.count = 0;
	write_sq.front = (write_sq.rear+1) % write_sq.max_count;
}

static int sq_fsync(struct file *filp, struct dentry *dentry)
{
	int rc = 0;

	write_sq.syncing = 1;
	sq_play();	/* there may be an incomplete frame waiting */

	while (write_sq.active) {
		SLEEP(write_sq.sync_queue);
		if (signal_pending(current)) {
			/* While waiting for audio output to drain, an
			 * interrupt occurred.  Stop audio output immediately
			 * and clear the queue. */
			sq_reset();
			rc = -EINTR;
			break;
		}
	}

	write_sq.syncing = 0;
	return rc;
}

static int sq_release(struct inode *inode, struct file *file)
{
	int rc = 0;

	lock_kernel();
	if (write_sq.busy)
		rc = sq_fsync(file, file->f_dentry);
	dmasound.soft = dmasound.dsp;
	dmasound.hard = dmasound.dsp;
	sound_silence();

	write_sq_release_buffers();
	read_sq_release_buffers();
	dmasound.mach.release();

	/* There is probably a DOS atack here. They change the mode flag. */
	/* XXX add check here */
	read_sq_wake_up(file);
	write_sq_wake_up(file);

	/* Wake up a process waiting for the queue being released.
	 * Note: There may be several processes waiting for a call
	 * to open() returning. */
	unlock_kernel();

	return rc;
}

static int sq_ioctl(struct inode *inode, struct file *file, u_int cmd,
		    u_long arg)
{
	int val;
	u_long fmt;
	int data;
	int size, nbufs;
	audio_buf_info info;

	switch (cmd) {
	case SNDCTL_DSP_RESET:
		sq_reset();
		return 0;
	case SNDCTL_DSP_POST:
	case SNDCTL_DSP_SYNC:
		return sq_fsync(file, file->f_dentry);

		/* ++TeSche: before changing any of these it's
		 * probably wise to wait until sound playing has
		 * settled down. */
	case SNDCTL_DSP_SPEED:
		sq_fsync(file, file->f_dentry);
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, sound_set_speed(data));
	case SNDCTL_DSP_STEREO:
		sq_fsync(file, file->f_dentry);
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, sound_set_stereo(data));
	case SOUND_PCM_WRITE_CHANNELS:
		sq_fsync(file, file->f_dentry);
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, sound_set_stereo(data-1)+1);
	case SNDCTL_DSP_SETFMT:
		sq_fsync(file, file->f_dentry);
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, sound_set_format(data));
	case SNDCTL_DSP_GETFMTS:
		fmt = 0;
		if (dmasound.trans_write) {
			if (dmasound.trans_write->ct_ulaw)
				fmt |= AFMT_MU_LAW;
			if (dmasound.trans_write->ct_alaw)
				fmt |= AFMT_A_LAW;
			if (dmasound.trans_write->ct_s8)
				fmt |= AFMT_S8;
			if (dmasound.trans_write->ct_u8)
				fmt |= AFMT_U8;
			if (dmasound.trans_write->ct_s16be)
				fmt |= AFMT_S16_BE;
			if (dmasound.trans_write->ct_u16be)
				fmt |= AFMT_U16_BE;
			if (dmasound.trans_write->ct_s16le)
				fmt |= AFMT_S16_LE;
			if (dmasound.trans_write->ct_u16le)
				fmt |= AFMT_U16_LE;
		}
		return IOCTL_OUT(arg, fmt);
	case SNDCTL_DSP_GETBLKSIZE:
		size = write_sq.block_size
			* dmasound.soft.size * (dmasound.soft.stereo + 1)
			/ (dmasound.hard.size * (dmasound.hard.stereo + 1));
		return IOCTL_OUT(arg, size);
	case SNDCTL_DSP_SUBDIVIDE:
		break;
	case SNDCTL_DSP_SETFRAGMENT:
		if (write_sq.count || write_sq.active || write_sq.syncing)
			return -EINVAL;
		IOCTL_IN(arg, size);
		nbufs = size >> 16;
		if (nbufs < 2 || nbufs > write_sq.numBufs)
			nbufs = write_sq.numBufs;
		size &= 0xffff;
		if (size >= 8 && size <= 29) {
			size = 1 << size;
			size *= dmasound.hard.size * (dmasound.hard.stereo + 1);
			size /= dmasound.soft.size * (dmasound.soft.stereo + 1);
			if (size > write_sq.bufSize)
				size = write_sq.bufSize;
		} else
			size = write_sq.bufSize;
		sq_setup(&write_sq, write_sq.numBufs, nbufs, size);
		return IOCTL_OUT(arg,write_sq.bufSize | write_sq.numBufs << 16);
	case SNDCTL_DSP_GETOSPACE:
		info.fragments = write_sq.max_active - write_sq.count;
		info.fragstotal = write_sq.max_active;
		info.fragsize = write_sq.block_size;
		info.bytes = info.fragments * info.fragsize;
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	case SNDCTL_DSP_GETCAPS:
		val = 1;        /* Revision level of this ioctl() */
		return IOCTL_OUT(arg,val);

	default:
		return mixer_ioctl(inode, file, cmd, arg);
	}
	return -EINVAL;
}

static struct file_operations sq_fops =
{
	owner:		THIS_MODULE,
	llseek:		sound_lseek,
	write:		sq_write,
	ioctl:		sq_ioctl,
	open:		sq_open,
	release:	sq_release,
#ifdef HAS_RECORD
	read:		sq_read,
#endif
};

static void __init sq_init(void)
{
#ifndef MODULE
	int sq_unit;
#endif
	sq_unit = register_sound_dsp(&sq_fops, -1);
	if (sq_unit < 0)
		return;

	write_sq_init_waitqueue();
	read_sq_init_waitqueue();

	/* whatever you like as startup mode for /dev/dsp,
	 * (/dev/audio hasn't got a startup mode). note that
	 * once changed a new open() will *not* restore these!
	 */
	dmasound.dsp.format = AFMT_U8;
	dmasound.dsp.stereo = 0;
	dmasound.dsp.size = 8;

	/* set minimum rate possible without expanding */
	dmasound.dsp.speed = dmasound.mach.min_dsp_speed;

	/* before the first open to /dev/dsp this wouldn't be set */
	dmasound.soft = dmasound.dsp;
	dmasound.hard = dmasound.dsp;

	sound_silence();
}


    /*
     *  /dev/sndstat
     */

static struct {
    int busy;
    char buf[512];	/* state.buf should not overflow! */
    int len, ptr;
} state;

static int state_open(struct inode *inode, struct file *file)
{
	char *buffer = state.buf;
	int len = 0;

	if (state.busy)
		return -EBUSY;

	dmasound.mach.open();
	state.ptr = 0;
	state.busy = 1;

	len += sprintf(buffer+len, "%sDMA sound driver:\n", dmasound.mach.name);

	len += sprintf(buffer+len, "\tsound.format = 0x%x",
		       dmasound.soft.format);
	switch (dmasound.soft.format) {
	    case AFMT_MU_LAW:
		len += sprintf(buffer+len, " (mu-law)");
		break;
	    case AFMT_A_LAW:
		len += sprintf(buffer+len, " (A-law)");
		break;
	    case AFMT_U8:
		len += sprintf(buffer+len, " (unsigned 8 bit)");
		break;
	    case AFMT_S8:
		len += sprintf(buffer+len, " (signed 8 bit)");
		break;
	    case AFMT_S16_BE:
		len += sprintf(buffer+len, " (signed 16 bit big)");
		break;
	    case AFMT_U16_BE:
		len += sprintf(buffer+len, " (unsigned 16 bit big)");
		break;
	    case AFMT_S16_LE:
		len += sprintf(buffer+len, " (signed 16 bit little)");
		break;
	    case AFMT_U16_LE:
		len += sprintf(buffer+len, " (unsigned 16 bit little)");
		break;
	}
	len += sprintf(buffer+len, "\n");
	len += sprintf(buffer+len, "\tsound.speed = %dHz (phys. %dHz)\n",
		       dmasound.soft.speed, dmasound.hard.speed);
	len += sprintf(buffer+len, "\tsound.stereo = 0x%x (%s)\n",
		       dmasound.soft.stereo,
		       dmasound.soft.stereo ? "stereo" : "mono");
	if (dmasound.mach.state_info)
	    len += dmasound.mach.state_info(buffer);
	len += sprintf(buffer+len, "\tsq.block_size = %d sq.max_count = %d"
		       " sq.max_active = %d\n",
		       write_sq.block_size, write_sq.max_count,
		       write_sq.max_active);
	len += sprintf(buffer+len, "\tsq.count = %d sq.rear_size = %d\n",
		       write_sq.count, write_sq.rear_size);
	len += sprintf(buffer+len, "\tsq.active = %d sq.syncing = %d\n",
		       write_sq.active, write_sq.syncing);
	state.len = len;
	return 0;
}

static int state_release(struct inode *inode, struct file *file)
{
	lock_kernel();
	state.busy = 0;
	dmasound.mach.release();
	unlock_kernel();
	return 0;
}

static ssize_t state_read(struct file *file, char *buf, size_t count,
			  loff_t *ppos)
{
	int n = state.len - state.ptr;
	if (n > count)
		n = count;
	if (n <= 0)
		return 0;
	if (copy_to_user(buf, &state.buf[state.ptr], n))
		return -EFAULT;
	state.ptr += n;
	return n;
}

static struct file_operations state_fops = {
	owner:		THIS_MODULE,
	llseek:		sound_lseek,
	read:		state_read,
	open:		state_open,
	release:	state_release,
};

static void __init state_init(void)
{
#ifndef MODULE
	int state_unit;
#endif
	state_unit = register_sound_special(&state_fops, SND_DEV_STATUS);
	if (state_unit < 0)
		return;
	state.busy = 0;
}


    /*
     *  Config & Setup
     *
     *  This function is called by _one_ chipset-specific driver
     */

int __init dmasound_init(void)
{
#ifdef MODULE
	if (irq_installed)
		return -EBUSY;
#endif

	/* Set up sound queue, /dev/audio and /dev/dsp. */

	/* Set default settings. */
	sq_init();

	/* Set up /dev/sndstat. */
	state_init();

	/* Set up /dev/mixer. */
	mixer_init();

	if (!dmasound.mach.irqinit()) {
		printk(KERN_ERR "DMA sound driver: Interrupt initialization failed\n");
		return -ENODEV;
	}
#ifdef MODULE
	irq_installed = 1;
#endif

	printk(KERN_INFO "DMA sound driver installed, using %d buffers of %dk.\n",
	       numWriteBufs, writeBufSize);

	return 0;
}

#ifdef MODULE

void dmasound_deinit(void)
{
	if (irq_installed) {
		sound_silence();
		dmasound.mach.irqcleanup();
	}

	write_sq_release_buffers();
	read_sq_release_buffers();

	if (mixer_unit >= 0)
		unregister_sound_mixer(mixer_unit);
	if (state_unit >= 0)
		unregister_sound_special(state_unit);
	if (sq_unit >= 0)
		unregister_sound_dsp(sq_unit);
}

#else /* !MODULE */

static int __init dmasound_setup(char *str)
{
	int ints[6];

	str = get_options(str, ARRAY_SIZE(ints), ints);

	/* check the bootstrap parameter for "dmasound=" */

	switch (ints[0]) {
	case 3:
		if ((ints[3] < 0) || (ints[3] > MAX_CATCH_RADIUS))
			printk("dmasound_setup: illegal catch radius, using default = %d\n", catchRadius);
		else
			catchRadius = ints[3];
		/* fall through */
	case 2:
		if (ints[1] < MIN_BUFFERS)
			printk("dmasound_setup: illegal number of buffers, using default = %d\n", numWriteBufs);
		else
			numWriteBufs = ints[1];
		if (ints[2] < MIN_BUFSIZE || ints[2] > MAX_BUFSIZE)
			printk("dmasound_setup: illegal buffer size, using default = %dK\n", writeBufSize);
		else
			writeBufSize = ints[2];
		break;
	case 0:
		break;
	default:
		printk("dmasound_setup: illegal number of arguments\n");
		return 0;
	}
	return 1;
}

__setup("dmasound=", dmasound_setup);

#endif /* !MODULE */


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(dmasound);
EXPORT_SYMBOL(dmasound_init);
#ifdef MODULE
EXPORT_SYMBOL(dmasound_deinit);
#endif
EXPORT_SYMBOL(dmasound_write_sq);
#ifdef HAS_RECORD
EXPORT_SYMBOL(dmasound_read_sq);
#endif
EXPORT_SYMBOL(dmasound_catchRadius);
#ifdef HAS_8BIT_TABLES
EXPORT_SYMBOL(dmasound_ulaw2dma8);
EXPORT_SYMBOL(dmasound_alaw2dma8);
#endif
#ifdef HAS_16BIT_TABLES
EXPORT_SYMBOL(dmasound_ulaw2dma16);
EXPORT_SYMBOL(dmasound_alaw2dma16);
#endif
#ifdef HAS_14BIT_TABLES
EXPORT_SYMBOL(dmasound_ulaw2dma14l);
EXPORT_SYMBOL(dmasound_ulaw2dma14h);
EXPORT_SYMBOL(dmasound_alaw2dma14l);
EXPORT_SYMBOL(dmasound_alaw2dma14h);
#endif

