
/*
 *  linux/drivers/sound/dmasound/dmasound_awacs.c
 *
 *  PowerMac `AWACS' and `Burgundy' DMA Sound Driver
 *
 *  See linux/drivers/sound/dmasound/dmasound_core.c for copyright and credits
 */


#include <linux/module.h>
#include <linux/config.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/soundcard.h>
#include <linux/adb.h>
#include <linux/nvram.h>
#include <linux/vt_kern.h>
#ifdef CONFIG_ADB_CUDA
#include <linux/cuda.h>
#endif
#ifdef CONFIG_ADB_PMU
#include <linux/pmu.h>
#endif

#include <asm/uaccess.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/dbdma.h>
#include <asm/feature.h>
#include <asm/irq.h>

#include "awacs_defs.h"
#include "dmasound.h"


/*
 * Interrupt numbers and addresses, obtained from the device tree.
 */
static int awacs_irq, awacs_tx_irq, awacs_rx_irq;
static volatile struct awacs_regs *awacs;
static volatile struct dbdma_regs *awacs_txdma, *awacs_rxdma;
static int awacs_rate_index;
static int awacs_subframe;
static int awacs_spkr_vol;
static struct device_node* awacs_node;

static char awacs_name[64];
static int awacs_revision;
int awacs_is_screamer = 0;
int awacs_device_id = 0;
int awacs_has_iic = 0;
#define AWACS_BURGUNDY	100		/* fake revision # for burgundy */

/*
 * Space for the DBDMA command blocks.
 */
static void *awacs_tx_cmd_space;
static volatile struct dbdma_cmd *awacs_tx_cmds;

static void *awacs_rx_cmd_space;
static volatile struct dbdma_cmd *awacs_rx_cmds;

/*
 * Cached values of AWACS registers (we can't read them).
 * Except on the burgundy. XXX
 */
int awacs_reg[8];

#define HAS_16BIT_TABLES
#undef HAS_8BIT_TABLES

/*
 * Stuff for outputting a beep.  The values range from -327 to +327
 * so we can multiply by an amplitude in the range 0..100 to get a
 * signed short value to put in the output buffer.
 */
static short beep_wform[256] = {
	0,	40,	79,	117,	153,	187,	218,	245,
	269,	288,	304,	316,	323,	327,	327,	324,
	318,	310,	299,	288,	275,	262,	249,	236,
	224,	213,	204,	196,	190,	186,	183,	182,
	182,	183,	186,	189,	192,	196,	200,	203,
	206,	208,	209,	209,	209,	207,	204,	201,
	197,	193,	188,	183,	179,	174,	170,	166,
	163,	161,	160,	159,	159,	160,	161,	162,
	164,	166,	168,	169,	171,	171,	171,	170,
	169,	167,	163,	159,	155,	150,	144,	139,
	133,	128,	122,	117,	113,	110,	107,	105,
	103,	103,	103,	103,	104,	104,	105,	105,
	105,	103,	101,	97,	92,	86,	78,	68,
	58,	45,	32,	18,	3,	-11,	-26,	-41,
	-55,	-68,	-79,	-88,	-95,	-100,	-102,	-102,
	-99,	-93,	-85,	-75,	-62,	-48,	-33,	-16,
	0,	16,	33,	48,	62,	75,	85,	93,
	99,	102,	102,	100,	95,	88,	79,	68,
	55,	41,	26,	11,	-3,	-18,	-32,	-45,
	-58,	-68,	-78,	-86,	-92,	-97,	-101,	-103,
	-105,	-105,	-105,	-104,	-104,	-103,	-103,	-103,
	-103,	-105,	-107,	-110,	-113,	-117,	-122,	-128,
	-133,	-139,	-144,	-150,	-155,	-159,	-163,	-167,
	-169,	-170,	-171,	-171,	-171,	-169,	-168,	-166,
	-164,	-162,	-161,	-160,	-159,	-159,	-160,	-161,
	-163,	-166,	-170,	-174,	-179,	-183,	-188,	-193,
	-197,	-201,	-204,	-207,	-209,	-209,	-209,	-208,
	-206,	-203,	-200,	-196,	-192,	-189,	-186,	-183,
	-182,	-182,	-183,	-186,	-190,	-196,	-204,	-213,
	-224,	-236,	-249,	-262,	-275,	-288,	-299,	-310,
	-318,	-324,	-327,	-327,	-323,	-316,	-304,	-288,
	-269,	-245,	-218,	-187,	-153,	-117,	-79,	-40,
};

#define BEEP_SRATE	22050	/* 22050 Hz sample rate */
#define BEEP_BUFLEN	512
#define BEEP_VOLUME	15	/* 0 - 100 */

static int beep_volume = BEEP_VOLUME;
static int beep_playing = 0;
static int awacs_beep_state = 0;
static short *beep_buf;
static volatile struct dbdma_cmd *beep_dbdma_cmd;
static void (*orig_mksound)(unsigned int, unsigned int);
static int is_pbook_3400;
static unsigned char *latch_base;
static int is_pbook_G3;
static unsigned char *macio_base;

/* Burgundy functions */
static void awacs_burgundy_wcw(unsigned addr,unsigned newval);
static unsigned awacs_burgundy_rcw(unsigned addr);
static void awacs_burgundy_write_volume(unsigned address, int volume);
static int awacs_burgundy_read_volume(unsigned address);
static void awacs_burgundy_write_mvolume(unsigned address, int volume);
static int awacs_burgundy_read_mvolume(unsigned address);

#ifdef CONFIG_PMAC_PBOOK
/*
 * Stuff for restoring after a sleep.
 */
static int awacs_sleep_notify(struct pmu_sleep_notifier *self, int when);
struct pmu_sleep_notifier awacs_sleep_notifier = {
	awacs_sleep_notify, SLEEP_LEVEL_SOUND,
};
#endif /* CONFIG_PMAC_PBOOK */

static int expand_bal;	/* Balance factor for expanding (not volume!) */
static int expand_data;	/* Data for expanding */


/*** Translations ************************************************************/


/* ++TeSche: radically changed for new expanding purposes...
 *
 * These two routines now deal with copying/expanding/translating the samples
 * from user space into our buffer at the right frequency. They take care about
 * how much data there's actually to read, how much buffer space there is and
 * to convert samples into the right frequency/encoding. They will only work on
 * complete samples so it may happen they leave some bytes in the input stream
 * if the user didn't write a multiple of the current sample size. They both
 * return the number of bytes they've used from both streams so you may detect
 * such a situation. Luckily all programs should be able to cope with that.
 *
 * I think I've optimized anything as far as one can do in plain C, all
 * variables should fit in registers and the loops are really short. There's
 * one loop for every possible situation. Writing a more generalized and thus
 * parameterized loop would only produce slower code. Feel free to optimize
 * this in assembler if you like. :)
 *
 * I think these routines belong here because they're not yet really hardware
 * independent, especially the fact that the Falcon can play 16bit samples
 * only in stereo is hardcoded in both of them!
 *
 * ++geert: split in even more functions (one per format)
 */

static ssize_t pmac_ct_law(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ct_s8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft);
static ssize_t pmac_ct_u8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft);
static ssize_t pmac_ct_s16(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ct_u16(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ctx_law(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t pmac_ctx_s8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ctx_u8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ctx_s16(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t pmac_ctx_u16(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t pmac_ct_s16_read(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ct_u16_read(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);


/*** Low level stuff *********************************************************/


static void PMacOpen(void);
static void PMacRelease(void);
static void *PMacAlloc(unsigned int size, int flags);
static void PMacFree(void *ptr, unsigned int size);
static int PMacIrqInit(void);
#ifdef MODULE
static void PMacIrqCleanup(void);
#endif
static void PMacSilence(void);
static void PMacInit(void);
static int PMacSetFormat(int format);
static int PMacSetVolume(int volume);
static void PMacPlay(void);
static void PMacRecord(void);
static void pmac_awacs_tx_intr(int irq, void *devid, struct pt_regs *regs);
static void pmac_awacs_rx_intr(int irq, void *devid, struct pt_regs *regs);
static void pmac_awacs_intr(int irq, void *devid, struct pt_regs *regs);
static void awacs_write(int val);
static int awacs_get_volume(int reg, int lshift);
static int awacs_volume_setter(int volume, int n, int mute, int lshift);
static void awacs_mksound(unsigned int hz, unsigned int ticks);
static void awacs_nosound(unsigned long xx);


/*** Mid level stuff **********************************************************/


static int PMacMixerIoctl(u_int cmd, u_long arg);
static void PMacWriteSqSetup(void);
static void PMacReadSqSetup(void);
static void PMacAbortRead(void);


/*** Translations ************************************************************/


static ssize_t pmac_ct_law(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	short *table = dmasound.soft.format == AFMT_MU_LAW
		? dmasound_ulaw2dma16 : dmasound_alaw2dma16;
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = table[data];
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = table[data];
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t pmac_ct_s8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = data << 8;
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = data << 8;
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t pmac_ct_u8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = (data ^ 0x80) << 8;
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = (data ^ 0x80) << 8;
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t pmac_ct_s16(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int stereo = dmasound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min(userCount, frameLeft);
	if (!stereo) {
		short *up = (short *) userPtr;
		while (count > 0) {
			short data;
			if (get_user(data, up++))
				return -EFAULT;
			*fp++ = data;
			*fp++ = data;
			count--;
		}
	} else {
		if (copy_from_user(fp, userPtr, count * 4))
			return -EFAULT;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}

static ssize_t pmac_ct_u16(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int mask = (dmasound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	int stereo = dmasound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];
	short *up = (short *) userPtr;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		int data;
		if (get_user(data, up++))
			return -EFAULT;
		data ^= mask;
		*fp++ = data;
		if (stereo) {
			if (get_user(data, up++))
				return -EFAULT;
			data ^= mask;
		}
		*fp++ = data;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}


static ssize_t pmac_ctx_law(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	unsigned short *table = (unsigned short *)
		(dmasound.soft.format == AFMT_MU_LAW
		 ? dmasound_ulaw2dma16 : dmasound_alaw2dma16);
	unsigned int data = expand_data;
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int utotal, ftotal;
	int stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = table[c];
			if (stereo) {
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = (data << 16) + table[c];
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}


static ssize_t pmac_ctx_s8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int stereo = dmasound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = c << 8;
			if (stereo) {
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = (data << 16) + (c << 8);
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}


static ssize_t pmac_ctx_u8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int stereo = dmasound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = (c ^ 0x80) << 8;
			if (stereo) {
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = (data << 16) + ((c ^ 0x80) << 8);
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}


static ssize_t pmac_ctx_s16(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	unsigned short *up = (unsigned short *) userPtr;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int stereo = dmasound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		unsigned short c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(data, up++))
				return -EFAULT;
			if (stereo) {
				if (get_user(c, up++))
					return -EFAULT;
				data = (data << 16) + c;
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 4: utotal * 2;
}


static ssize_t pmac_ctx_u16(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	int mask = (dmasound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	unsigned short *up = (unsigned short *) userPtr;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int stereo = dmasound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		unsigned short c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(data, up++))
				return -EFAULT;
			data ^= mask;
			if (stereo) {
				if (get_user(c, up++))
					return -EFAULT;
				data = (data << 16) + (c ^ mask);
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 4: utotal * 2;
}

static ssize_t pmac_ct_s8_read(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;

		val = *p++;
		data = val >> 8;
		if (put_user(data, (u_char *)userPtr++))
			return -EFAULT;
		if (stereo) {
			val = *p;
			data = val >> 8;
			if (put_user(data, (u_char *)userPtr++))
				return -EFAULT;
		}
		p++;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t pmac_ct_u8_read(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;

		val = *p++;
		data = (val >> 8) ^ 0x80;
		if (put_user(data, (u_char *)userPtr++))
			return -EFAULT;
		if (stereo) {
			val = *p;
			data = (val >> 8) ^ 0x80;
			if (put_user(data, (u_char *)userPtr++))
				return -EFAULT;
		}
		p++;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t pmac_ct_s16_read(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int stereo = dmasound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min(userCount, frameLeft);
	if (!stereo) {
		short *up = (short *) userPtr;
		while (count > 0) {
			short data;
			data = *fp;
			if (put_user(data, up++))
				return -EFAULT;
			fp+=2;
			count--;
		}
	} else {
		if (copy_to_user((u_char *)userPtr, fp, count * 4))
			return -EFAULT;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}

static ssize_t pmac_ct_u16_read(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int mask = (dmasound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	int stereo = dmasound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];
	short *up = (short *) userPtr;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		int data;

		data = *fp++;
		data ^= mask;
		if (put_user(data, up++))
			return -EFAULT;
		if (stereo) {
			data = *fp;
			data ^= mask;
			if (put_user(data, up++))
				return -EFAULT;
		}
		fp++;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}


static TRANS transAwacsNormal = {
	ct_ulaw:	pmac_ct_law,
	ct_alaw:	pmac_ct_law,
	ct_s8:		pmac_ct_s8,
	ct_u8:		pmac_ct_u8,
	ct_s16be:	pmac_ct_s16,
	ct_u16be:	pmac_ct_u16,
	ct_s16le:	pmac_ct_s16,
	ct_u16le:	pmac_ct_u16,
};

static TRANS transAwacsExpand = {
	ct_ulaw:	pmac_ctx_law,
	ct_alaw:	pmac_ctx_law,
	ct_s8:		pmac_ctx_s8,
	ct_u8:		pmac_ctx_u8,
	ct_s16be:	pmac_ctx_s16,
	ct_u16be:	pmac_ctx_u16,
	ct_s16le:	pmac_ctx_s16,
	ct_u16le:	pmac_ctx_u16,
};

static TRANS transAwacsNormalRead = {
	ct_s8:		pmac_ct_s8_read,
	ct_u8:		pmac_ct_u8_read,
	ct_s16be:	pmac_ct_s16_read,
	ct_u16be:	pmac_ct_u16_read,
	ct_s16le:	pmac_ct_s16_read,
	ct_u16le:	pmac_ct_u16_read,
};

/*** Low level stuff *********************************************************/



/*
 * PCI PowerMac, with AWACS and DBDMA.
 */

static void PMacOpen(void)
{
	MOD_INC_USE_COUNT;
}

static void PMacRelease(void)
{
	MOD_DEC_USE_COUNT;
}

static void *PMacAlloc(unsigned int size, int flags)
{
	return kmalloc(size, flags);
}

static void PMacFree(void *ptr, unsigned int size)
{
	kfree(ptr);
}

static int __init PMacIrqInit(void)
{
	if (request_irq(awacs_irq, pmac_awacs_intr, 0, "AWACS", 0)
	    || request_irq(awacs_tx_irq, pmac_awacs_tx_intr, 0, "AWACS out", 0)
	    || request_irq(awacs_rx_irq, pmac_awacs_rx_intr, 0, "AWACS in", 0))
		return 0;
	return 1;
}

#ifdef MODULE
static void PMacIrqCleanup(void)
{
	/* turn off output dma */
	out_le32(&awacs_txdma->control, RUN<<16);
	/* disable interrupts from awacs interface */
	out_le32(&awacs->control, in_le32(&awacs->control) & 0xfff);
#ifdef CONFIG_PMAC_PBOOK
	if (is_pbook_G3) {
		feature_clear(awacs_node, FEATURE_Sound_power);
		feature_clear(awacs_node, FEATURE_Sound_CLK_enable);
	}
#endif
	free_irq(awacs_irq, 0);
	free_irq(awacs_tx_irq, 0);
	free_irq(awacs_rx_irq, 0);
	kfree(awacs_tx_cmd_space);
	if (awacs_rx_cmd_space)
		kfree(awacs_rx_cmd_space);
	if (beep_buf)
		kfree(beep_buf);
	kd_mksound = orig_mksound;
#ifdef CONFIG_PMAC_PBOOK
	pmu_unregister_sleep_notifier(&awacs_sleep_notifier);
#endif
}
#endif /* MODULE */

static void PMacSilence(void)
{
	/* turn off output dma */
	out_le32(&awacs_txdma->control, RUN<<16);
}

static int awacs_freqs[8] = {
	44100, 29400, 22050, 17640, 14700, 11025, 8820, 7350
};
static int awacs_freqs_ok[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };

static void PMacInit(void)
{
	int i, tolerance;

	switch (dmasound.soft.format) {
	case AFMT_S16_LE:
	case AFMT_U16_LE:
		dmasound.hard.format = AFMT_S16_LE;
		break;
	default:
		dmasound.hard.format = AFMT_S16_BE;
		break;
	}
	dmasound.hard.stereo = 1;
	dmasound.hard.size = 16;

	/*
	 * If we have a sample rate which is within catchRadius percent
	 * of the requested value, we don't have to expand the samples.
	 * Otherwise choose the next higher rate.
	 * N.B.: burgundy awacs (iMac and later) only works at 44100 Hz.
	 */
	i = 8;
	do {
		tolerance = catchRadius * awacs_freqs[--i] / 100;
		if (awacs_freqs_ok[i]
		    && dmasound.soft.speed <= awacs_freqs[i] + tolerance)
			break;
	} while (i > 0);
	if (dmasound.soft.speed >= awacs_freqs[i] - tolerance)
		dmasound.trans_write = &transAwacsNormal;
	else
		dmasound.trans_write = &transAwacsExpand;
	dmasound.trans_read = &transAwacsNormalRead;
	dmasound.hard.speed = awacs_freqs[i];
	awacs_rate_index = i;

	/* XXX disable error interrupt on burgundy for now */
	out_le32(&awacs->control, MASK_IEPC | (i << 8) | 0x11
		 | (awacs_revision < AWACS_BURGUNDY? MASK_IEE: 0));
	awacs_reg[1] = (awacs_reg[1] & ~MASK_SAMPLERATE) | (i << 3);
	awacs_write(awacs_reg[1] | MASK_ADDR1);
	out_le32(&awacs->byteswap, dmasound.hard.format != AFMT_S16_BE);

	/* We really want to execute a DMA stop command, after the AWACS
	 * is initialized.
	 * For reasons I don't understand, it stops the hissing noise
	 * common to many PowerBook G3 systems (like mine :-).
	 */
	out_le32(&awacs_txdma->control, (RUN|WAKE|FLUSH|PAUSE) << 16);
	st_le16(&beep_dbdma_cmd->command, DBDMA_STOP);
	out_le32(&awacs_txdma->cmdptr, virt_to_bus(beep_dbdma_cmd));
	out_le32(&awacs_txdma->control, RUN | (RUN << 16));

	expand_bal = -dmasound.soft.speed;
}

static int PMacSetFormat(int format)
{
	int size;

	switch (format) {
	case AFMT_QUERY:
		return dmasound.soft.format;
	case AFMT_MU_LAW:
	case AFMT_A_LAW:
	case AFMT_U8:
	case AFMT_S8:
		size = 8;
		break;
	case AFMT_S16_BE:
	case AFMT_U16_BE:
	case AFMT_S16_LE:
	case AFMT_U16_LE:
		size = 16;
		break;
	default: /* :-) */
		printk(KERN_ERR "dmasound: unknown format 0x%x, using AFMT_U8\n",
		       format);
		size = 8;
		format = AFMT_U8;
	}

	dmasound.soft.format = format;
	dmasound.soft.size = size;
	if (dmasound.minDev == SND_DEV_DSP) {
		dmasound.dsp.format = format;
		dmasound.dsp.size = size;
	}

	PMacInit();

	return format;
}

#define AWACS_VOLUME_TO_MASK(x)	(15 - ((((x) - 1) * 15) / 99))
#define AWACS_MASK_TO_VOLUME(y)	(100 - ((y) * 99 / 15))

static int awacs_get_volume(int reg, int lshift)
{
	int volume;

	volume = AWACS_MASK_TO_VOLUME((reg >> lshift) & 0xf);
	volume |= AWACS_MASK_TO_VOLUME(reg & 0xf) << 8;
	return volume;
}

static int awacs_volume_setter(int volume, int n, int mute, int lshift)
{
	int r1, rn;

	if (mute && volume == 0) {
		r1 = awacs_reg[1] | mute;
	} else {
		r1 = awacs_reg[1] & ~mute;
		rn = awacs_reg[n] & ~(0xf | (0xf << lshift));
		rn |= ((AWACS_VOLUME_TO_MASK(volume & 0xff) & 0xf) << lshift);
		rn |= AWACS_VOLUME_TO_MASK((volume >> 8) & 0xff) & 0xf;
		awacs_reg[n] = rn;
		awacs_write((n << 12) | rn);
		volume = awacs_get_volume(rn, lshift);
	}
	if (r1 != awacs_reg[1]) {
		awacs_reg[1] = r1;
		awacs_write(r1 | MASK_ADDR1);
	}
	return volume;
}

static int PMacSetVolume(int volume)
{
	return awacs_volume_setter(volume, 2, MASK_AMUTE, 6);
}

static void PMacPlay(void)
{
	volatile struct dbdma_cmd *cp;
	int i, count;
	unsigned long flags;

	save_flags(flags); cli();
	if (awacs_beep_state) {
		/* sound takes precedence over beeps */
		out_le32(&awacs_txdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
		out_le32(&awacs->control,
			 (in_le32(&awacs->control) & ~0x1f00)
			 | (awacs_rate_index << 8));
		out_le32(&awacs->byteswap, dmasound.hard.format != AFMT_S16_BE);
		out_le32(&awacs_txdma->cmdptr, virt_to_bus(&(awacs_tx_cmds[(write_sq.front+write_sq.active) % write_sq.max_count])));

		beep_playing = 0;
		awacs_beep_state = 0;
	}
	i = write_sq.front + write_sq.active;
	if (i >= write_sq.max_count)
		i -= write_sq.max_count;
	while (write_sq.active < 2 && write_sq.active < write_sq.count) {
		count = (write_sq.count == write_sq.active + 1)?write_sq.rear_size:write_sq.block_size;
		if (count < write_sq.block_size && !write_sq.syncing)
			/* last block not yet filled, and we're not syncing. */
			break;
		cp = &awacs_tx_cmds[i];
		st_le16(&cp->req_count, count);
		st_le16(&cp->xfer_status, 0);
		if (++i >= write_sq.max_count)
			i = 0;
		out_le16(&awacs_tx_cmds[i].command, DBDMA_STOP);
		out_le16(&cp->command, OUTPUT_MORE + INTR_ALWAYS);
		if (write_sq.active == 0)
			out_le32(&awacs_txdma->cmdptr, virt_to_bus(cp));
		out_le32(&awacs_txdma->control, ((RUN|WAKE) << 16) + (RUN|WAKE));
		++write_sq.active;
	}
	restore_flags(flags);
}


static void PMacRecord(void)
{
	unsigned long flags;

	if (read_sq.active)
		return;

	save_flags(flags); cli();

	/* This is all we have to do......Just start it up.
	*/
	out_le32(&awacs_rxdma->control, ((RUN|WAKE) << 16) + (RUN|WAKE));
	read_sq.active = 1;

	restore_flags(flags);
}


static void
pmac_awacs_tx_intr(int irq, void *devid, struct pt_regs *regs)
{
	int i = write_sq.front;
	int stat;
	volatile struct dbdma_cmd *cp;

	while (write_sq.active > 0) {
		cp = &awacs_tx_cmds[i];
		stat = ld_le16(&cp->xfer_status);
		if ((stat & ACTIVE) == 0)
			break;	/* this frame is still going */
		--write_sq.count;
		--write_sq.active;
		if (++i >= write_sq.max_count)
			i = 0;
	}
	if (i != write_sq.front)
		WAKE_UP(write_sq.action_queue);
	write_sq.front = i;

	PMacPlay();

	if (!write_sq.active)
		WAKE_UP(write_sq.sync_queue);
}


static void
pmac_awacs_rx_intr(int irq, void *devid, struct pt_regs *regs)
{

	/* For some reason on my PowerBook G3, I get one interrupt
	 * when the interrupt vector is installed (like something is
	 * pending).  This happens before the dbdma is initialize by
	 * us, so I just check the command pointer and if it is zero,
	 * just blow it off.
	 */
	if (in_le32(&awacs_rxdma->cmdptr) == 0)
		return;

	/* We also want to blow 'em off when shutting down.
	*/
	if (read_sq.active == 0)
		return;

	/* Check multiple buffers in case we were held off from
	 * interrupt processing for a long time.  Geeze, I really hope
	 * this doesn't happen.
	 */
	while (awacs_rx_cmds[read_sq.rear].xfer_status) {

		/* Clear status and move on to next buffer.
		*/
		awacs_rx_cmds[read_sq.rear].xfer_status = 0;
		read_sq.rear++;

		/* Wrap the buffer ring.
		*/
		if (read_sq.rear >= read_sq.max_active)
			read_sq.rear = 0;

		/* If we have caught up to the front buffer, bump it.
		 * This will cause weird (but not fatal) results if the
		 * read loop is currently using this buffer.  The user is
		 * behind in this case anyway, so weird things are going
		 * to happen.
		 */
		if (read_sq.rear == read_sq.front) {
			read_sq.front++;
			if (read_sq.front >= read_sq.max_active)
				read_sq.front = 0;
		}
	}

	WAKE_UP(read_sq.action_queue);
}


static void
pmac_awacs_intr(int irq, void *devid, struct pt_regs *regs)
{
	int ctrl = in_le32(&awacs->control);

	if (ctrl & MASK_PORTCHG) {
		/* do something when headphone is plugged/unplugged? */
	}
	if (ctrl & MASK_CNTLERR) {
		int err = (in_le32(&awacs->codec_stat) & MASK_ERRCODE) >> 16;
		if (err != 0 && awacs_revision < AWACS_BURGUNDY)
			printk(KERN_ERR "AWACS: error %x\n", err);
	}
	/* Writing 1s to the CNTLERR and PORTCHG bits clears them... */
	out_le32(&awacs->control, ctrl);
}

static void
awacs_write(int val)
{
	if (awacs_revision >= AWACS_BURGUNDY)
		return;
	while (in_le32(&awacs->codec_ctrl) & MASK_NEWECMD)
		;	/* XXX should have timeout */
	out_le32(&awacs->codec_ctrl, val | (awacs_subframe << 22));
}

static void awacs_nosound(unsigned long xx)
{
	unsigned long flags;

	save_flags(flags); cli();
	if (beep_playing) {
		st_le16(&beep_dbdma_cmd->command, DBDMA_STOP);
		out_le32(&awacs_txdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
		out_le32(&awacs->control,
			 (in_le32(&awacs->control) & ~0x1f00)
			 | (awacs_rate_index << 8));
		out_le32(&awacs->byteswap, dmasound.hard.format != AFMT_S16_BE);
		beep_playing = 0;
	}
	restore_flags(flags);
}

static struct timer_list beep_timer = {
	function: awacs_nosound
};

static void awacs_mksound(unsigned int hz, unsigned int ticks)
{
	unsigned long flags;
	int beep_speed = 0;
	int srate;
	int period, ncycles, nsamples;
	int i, j, f;
	short *p;
	static int beep_hz_cache;
	static int beep_nsamples_cache;
	static int beep_volume_cache;

	for (i = 0; i < 8 && awacs_freqs[i] >= BEEP_SRATE; ++i)
		if (awacs_freqs_ok[i])
			beep_speed = i;
	srate = awacs_freqs[beep_speed];

	if (hz <= srate / BEEP_BUFLEN || hz > srate / 2) {
#if 1
		/* this is a hack for broken X server code */
		hz = 750;
		ticks = 12;
#else
		/* cancel beep currently playing */
		awacs_nosound(0);
		return;
#endif
	}
	save_flags(flags); cli();
	del_timer(&beep_timer);
	if (ticks) {
		beep_timer.expires = jiffies + ticks;
		add_timer(&beep_timer);
	}
	if (beep_playing || write_sq.active || beep_buf == NULL) {
		restore_flags(flags);
		return;		/* too hard, sorry :-( */
	}
	beep_playing = 1;
	st_le16(&beep_dbdma_cmd->command, OUTPUT_MORE + BR_ALWAYS);
	restore_flags(flags);

	if (hz == beep_hz_cache && beep_volume == beep_volume_cache) {
		nsamples = beep_nsamples_cache;
	} else {
		period = srate * 256 / hz;	/* fixed point */
		ncycles = BEEP_BUFLEN * 256 / period;
		nsamples = (period * ncycles) >> 8;
		f = ncycles * 65536 / nsamples;
		j = 0;
		p = beep_buf;
		for (i = 0; i < nsamples; ++i, p += 2) {
			p[0] = p[1] = beep_wform[j >> 8] * beep_volume;
			j = (j + f) & 0xffff;
		}
		beep_hz_cache = hz;
		beep_volume_cache = beep_volume;
		beep_nsamples_cache = nsamples;
	}

	st_le16(&beep_dbdma_cmd->req_count, nsamples*4);
	st_le16(&beep_dbdma_cmd->xfer_status, 0);
	st_le32(&beep_dbdma_cmd->cmd_dep, virt_to_bus(beep_dbdma_cmd));
	st_le32(&beep_dbdma_cmd->phy_addr, virt_to_bus(beep_buf));
	awacs_beep_state = 1;

	save_flags(flags); cli();
	if (beep_playing) {	/* i.e. haven't been terminated already */
		out_le32(&awacs_txdma->control, (RUN|WAKE|FLUSH|PAUSE) << 16);
		out_le32(&awacs->control,
			 (in_le32(&awacs->control) & ~0x1f00)
			 | (beep_speed << 8));
		out_le32(&awacs->byteswap, 0);
		out_le32(&awacs_txdma->cmdptr, virt_to_bus(beep_dbdma_cmd));
		out_le32(&awacs_txdma->control, RUN | (RUN << 16));
	}
	restore_flags(flags);
}

#ifdef CONFIG_PMAC_PBOOK
/*
 * Save state when going to sleep, restore it afterwards.
 */
static int awacs_sleep_notify(struct pmu_sleep_notifier *self, int when)
{
	switch (when) {
	case PBOOK_SLEEP_NOW:
		/* XXX we should stop any dma in progress when going to sleep
		   and restart it when we wake. */
		PMacSilence();
		disable_irq(awacs_irq);
		disable_irq(awacs_tx_irq);
		if (is_pbook_G3) {
			feature_clear(awacs_node, FEATURE_Sound_CLK_enable);
			feature_clear(awacs_node, FEATURE_Sound_power);
		}
		break;
	case PBOOK_WAKE:
		/* There is still a problem on wake. Sound seems to work fine
		   if I launch mpg123 and resumes fine if mpg123 was playing,
		   but the console beep is dead until I do something with the
		   mixer. Probably yet another timing issue */
		if (!feature_test(awacs_node, FEATURE_Sound_CLK_enable)
		    || !feature_test(awacs_node, FEATURE_Sound_power)) {
			/* these aren't present on the 3400 AFAIK -- paulus */
			feature_set(awacs_node, FEATURE_Sound_CLK_enable);
			feature_set(awacs_node, FEATURE_Sound_power);
			mdelay(1000);
		}
		out_le32(&awacs->control, MASK_IEPC
			 | (awacs_rate_index << 8) | 0x11
			 | (awacs_revision < AWACS_BURGUNDY? MASK_IEE: 0));
		awacs_write(awacs_reg[0] | MASK_ADDR0);
		awacs_write(awacs_reg[1] | MASK_ADDR1);
		awacs_write(awacs_reg[2] | MASK_ADDR2);
		awacs_write(awacs_reg[4] | MASK_ADDR4);
		if (awacs_is_screamer) {
			awacs_write(awacs_reg[5] + MASK_ADDR5);
			awacs_write(awacs_reg[6] + MASK_ADDR6);
			awacs_write(awacs_reg[7] + MASK_ADDR7);
		}
		out_le32(&awacs->byteswap, dmasound.hard.format != AFMT_S16_BE);
		enable_irq(awacs_irq);
		enable_irq(awacs_tx_irq);
		if (awacs_revision == 3) {
			mdelay(100);
			awacs_write(0x6000);
			mdelay(2);
			awacs_write(awacs_reg[1] | MASK_ADDR1);
		}
		/* enable CD sound input */
		if (macio_base && is_pbook_G3) {
			out_8(macio_base + 0x37, 3);
		} else if (is_pbook_3400) {
			feature_set(awacs_node, FEATURE_IOBUS_enable);
			udelay(10);
			in_8(latch_base + 0x190);
		}
		/* Resume pending sounds. */
		PMacPlay();
	}
	return PBOOK_SLEEP_OK;
}
#endif /* CONFIG_PMAC_PBOOK */


/* All the burgundy functions: */

/* Waits for busy flag to clear */
inline static void
awacs_burgundy_busy_wait(void)
{
	while (in_le32(&awacs->codec_ctrl) & MASK_NEWECMD)
		;
}

inline static void
awacs_burgundy_extend_wait(void)
{
	while (!(in_le32(&awacs->codec_stat) & MASK_EXTEND))
		;
	while (in_le32(&awacs->codec_stat) & MASK_EXTEND)
		;
}

static void
awacs_burgundy_wcw(unsigned addr, unsigned val)
{
	out_le32(&awacs->codec_ctrl, addr + 0x200c00 + (val & 0xff));
	awacs_burgundy_busy_wait();
	out_le32(&awacs->codec_ctrl, addr + 0x200d00 +((val>>8) & 0xff));
	awacs_burgundy_busy_wait();
	out_le32(&awacs->codec_ctrl, addr + 0x200e00 +((val>>16) & 0xff));
	awacs_burgundy_busy_wait();
	out_le32(&awacs->codec_ctrl, addr + 0x200f00 +((val>>24) & 0xff));
	awacs_burgundy_busy_wait();
}

static unsigned
awacs_burgundy_rcw(unsigned addr)
{
	unsigned val = 0;
	unsigned long flags;

	/* should have timeouts here */
	save_flags(flags); cli();

	out_le32(&awacs->codec_ctrl, addr + 0x100000);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += (in_le32(&awacs->codec_stat) >> 4) & 0xff;

	out_le32(&awacs->codec_ctrl, addr + 0x100100);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += ((in_le32(&awacs->codec_stat)>>4) & 0xff) <<8;

	out_le32(&awacs->codec_ctrl, addr + 0x100200);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += ((in_le32(&awacs->codec_stat)>>4) & 0xff) <<16;

	out_le32(&awacs->codec_ctrl, addr + 0x100300);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += ((in_le32(&awacs->codec_stat)>>4) & 0xff) <<24;

	restore_flags(flags);

	return val;
}


static void
awacs_burgundy_wcb(unsigned addr, unsigned val)
{
	out_le32(&awacs->codec_ctrl, addr + 0x300000 + (val & 0xff));
	awacs_burgundy_busy_wait();
}

static unsigned
awacs_burgundy_rcb(unsigned addr)
{
	unsigned val = 0;
	unsigned long flags;

	/* should have timeouts here */
	save_flags(flags); cli();

	out_le32(&awacs->codec_ctrl, addr + 0x100000);
	awacs_burgundy_busy_wait();
	awacs_burgundy_extend_wait();
	val += (in_le32(&awacs->codec_stat) >> 4) & 0xff;

	restore_flags(flags);

	return val;
}

static int
awacs_burgundy_check(void)
{
	/* Checks to see the chip is alive and kicking */
	int error = in_le32(&awacs->codec_ctrl) & MASK_ERRCODE;

	return error == 0xf0000;
}

static int
awacs_burgundy_init(void)
{
	if (awacs_burgundy_check()) {
		printk(KERN_WARNING "AWACS: disabled by MacOS :-(\n");
		return 1;
	}

	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_OUTPUTENABLES,
			   DEF_BURGUNDY_OUTPUTENABLES);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
			   DEF_BURGUNDY_MORE_OUTPUTENABLES);
	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_OUTPUTSELECTS,
			   DEF_BURGUNDY_OUTPUTSELECTS);

	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_INPSEL21,
			   DEF_BURGUNDY_INPSEL21);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_INPSEL3,
			   DEF_BURGUNDY_INPSEL3);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_GAINCD,
			   DEF_BURGUNDY_GAINCD);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_GAINLINE,
			   DEF_BURGUNDY_GAINLINE);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_GAINMIC,
			   DEF_BURGUNDY_GAINMIC);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_GAINMODEM,
			   DEF_BURGUNDY_GAINMODEM);

	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_ATTENSPEAKER,
			   DEF_BURGUNDY_ATTENSPEAKER);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_ATTENLINEOUT,
			   DEF_BURGUNDY_ATTENLINEOUT);
	awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_ATTENHP,
			   DEF_BURGUNDY_ATTENHP);

	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_MASTER_VOLUME,
			   DEF_BURGUNDY_MASTER_VOLUME);
	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_VOLCD,
			   DEF_BURGUNDY_VOLCD);
	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_VOLLINE,
			   DEF_BURGUNDY_VOLLINE);
	awacs_burgundy_wcw(MASK_ADDR_BURGUNDY_VOLMIC,
			   DEF_BURGUNDY_VOLMIC);
	return 0;
}

static void
awacs_burgundy_write_volume(unsigned address, int volume)
{
	int hardvolume,lvolume,rvolume;

	lvolume = (volume & 0xff) ? (volume & 0xff) + 155 : 0;
	rvolume = ((volume >>8)&0xff) ? ((volume >> 8)&0xff ) + 155 : 0;

	hardvolume = lvolume + (rvolume << 16);

	awacs_burgundy_wcw(address, hardvolume);
}

static int
awacs_burgundy_read_volume(unsigned address)
{
	int softvolume,wvolume;

	wvolume = awacs_burgundy_rcw(address);

	softvolume = (wvolume & 0xff) - 155;
	softvolume += (((wvolume >> 16) & 0xff) - 155)<<8;

	return softvolume > 0 ? softvolume : 0;
}




static int
awacs_burgundy_read_mvolume(unsigned address)
{
	int lvolume,rvolume,wvolume;

	wvolume = awacs_burgundy_rcw(address);

	wvolume &= 0xffff;

	rvolume = (wvolume & 0xff) - 155;
	lvolume = ((wvolume & 0xff00)>>8) - 155;

	return lvolume + (rvolume << 8);
}


static void
awacs_burgundy_write_mvolume(unsigned address, int volume)
{
	int lvolume,rvolume,hardvolume;

	lvolume = (volume &0xff) ? (volume & 0xff) + 155 :0;
	rvolume = ((volume >>8) & 0xff) ? (volume >> 8) + 155 :0;

	hardvolume = lvolume + (rvolume << 8);
	hardvolume += (hardvolume << 16);

	awacs_burgundy_wcw(address, hardvolume);
}

/* End burgundy functions */





/* Turn on sound output, needed on G3 desktop powermacs */
static void
awacs_enable_amp(int spkr_vol)
{
	struct adb_request req;

	awacs_spkr_vol = spkr_vol;
	if (sys_ctrler != SYS_CTRLER_CUDA)
		return;

#ifdef CONFIG_ADB_CUDA
	/* turn on headphones */
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 4, 0);
	while (!req.complete) cuda_poll();
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 6, 0);
	while (!req.complete) cuda_poll();

	/* turn on speaker */
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 3, (100 - (spkr_vol & 0xff)) * 32 / 100);
	while (!req.complete) cuda_poll();
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
		     0x8a, 5, (100 - ((spkr_vol >> 8) & 0xff)) * 32 / 100);
	while (!req.complete) cuda_poll();

	cuda_request(&req, NULL, 5, CUDA_PACKET,
		     CUDA_GET_SET_IIC, 0x8a, 1, 0x29);
	while (!req.complete) cuda_poll();
#endif /* CONFIG_ADB_CUDA */
}


/*** Mid level stuff *********************************************************/


/*
 * /dev/mixer abstraction
 */

static int awacs_mixer_ioctl(u_int cmd, u_long arg)
{
	int data;

	switch (cmd) {
	case SOUND_MIXER_READ_DEVMASK:
		data = SOUND_MASK_VOLUME | SOUND_MASK_SPEAKER
			| SOUND_MASK_LINE | SOUND_MASK_MIC
			| SOUND_MASK_CD | SOUND_MASK_RECLEV
			| SOUND_MASK_ALTPCM
			| SOUND_MASK_MONITOR;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_RECMASK:
		data = SOUND_MASK_LINE | SOUND_MASK_MIC
			| SOUND_MASK_CD;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_RECSRC:
		data = 0;
		if (awacs_reg[0] & MASK_MUX_AUDIN)
			data |= SOUND_MASK_LINE;
		if (awacs_reg[0] & MASK_MUX_MIC)
			data |= SOUND_MASK_MIC;
		if (awacs_reg[0] & MASK_MUX_CD)
			data |= SOUND_MASK_CD;
		if (awacs_reg[1] & MASK_LOOPTHRU)
			data |= SOUND_MASK_MONITOR;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_RECSRC:
		IOCTL_IN(arg, data);
		data &= (SOUND_MASK_LINE
			 | SOUND_MASK_MIC | SOUND_MASK_CD
			 | SOUND_MASK_MONITOR);
		awacs_reg[0] &= ~(MASK_MUX_CD | MASK_MUX_MIC
				  | MASK_MUX_AUDIN);
		awacs_reg[1] &= ~MASK_LOOPTHRU;
		if (data & SOUND_MASK_LINE)
			awacs_reg[0] |= MASK_MUX_AUDIN;
		if (data & SOUND_MASK_MIC)
			awacs_reg[0] |= MASK_MUX_MIC;
		if (data & SOUND_MASK_CD)
			awacs_reg[0] |= MASK_MUX_CD;
		if (data & SOUND_MASK_MONITOR)
			awacs_reg[1] |= MASK_LOOPTHRU;
		awacs_write(awacs_reg[0] | MASK_ADDR0);
		awacs_write(awacs_reg[1] | MASK_ADDR1);
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_STEREODEVS:
		data = SOUND_MASK_VOLUME | SOUND_MASK_SPEAKER
			| SOUND_MASK_RECLEV;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_CAPS:
		return IOCTL_OUT(arg, 0);
	case SOUND_MIXER_READ_VOLUME:
		data = (awacs_reg[1] & MASK_AMUTE)? 0:
			awacs_get_volume(awacs_reg[2], 6);
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_VOLUME:
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, PMacSetVolume(data));
	case SOUND_MIXER_READ_SPEAKER:
		if (awacs_revision == 3
		    && sys_ctrler == SYS_CTRLER_CUDA)
			data = awacs_spkr_vol;
		else
			data = (awacs_reg[1] & MASK_CMUTE)? 0:
				awacs_get_volume(awacs_reg[4], 6);
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_SPEAKER:
		IOCTL_IN(arg, data);
		if (awacs_revision == 3
		    && sys_ctrler == SYS_CTRLER_CUDA)
			awacs_enable_amp(data);
		else
			data = awacs_volume_setter(data, 4, MASK_CMUTE, 6);
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_ALTPCM:	/* really bell volume */
		IOCTL_IN(arg, data);
		beep_volume = data & 0xff;
				/* fall through */
	case SOUND_MIXER_READ_ALTPCM:
		return IOCTL_OUT(arg, beep_volume);
	case SOUND_MIXER_WRITE_LINE:
		IOCTL_IN(arg, data);
		awacs_reg[0] &= ~MASK_MUX_AUDIN;
		if ((data & 0xff) >= 50)
			awacs_reg[0] |= MASK_MUX_AUDIN;
		awacs_write(MASK_ADDR0 | awacs_reg[0]);
				/* fall through */
	case SOUND_MIXER_READ_LINE:
		data = (awacs_reg[0] & MASK_MUX_AUDIN)? 100: 0;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_MIC:
		IOCTL_IN(arg, data);
		data &= 0xff;
		awacs_reg[0] &= ~(MASK_MUX_MIC | MASK_GAINLINE);
		if (data >= 25) {
			awacs_reg[0] |= MASK_MUX_MIC;
			if (data >= 75)
				awacs_reg[0] |= MASK_GAINLINE;
		}
		awacs_write(MASK_ADDR0 | awacs_reg[0]);
				/* fall through */
	case SOUND_MIXER_READ_MIC:
		data = (awacs_reg[0] & MASK_MUX_MIC)?
			(awacs_reg[0] & MASK_GAINLINE? 100: 50): 0;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_CD:
		IOCTL_IN(arg, data);
		awacs_reg[0] &= ~MASK_MUX_CD;
		if ((data & 0xff) >= 50)
			awacs_reg[0] |= MASK_MUX_CD;
		awacs_write(MASK_ADDR0 | awacs_reg[0]);
				/* fall through */
	case SOUND_MIXER_READ_CD:
		data = (awacs_reg[0] & MASK_MUX_CD)? 100: 0;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_RECLEV:
		IOCTL_IN(arg, data);
		data = awacs_volume_setter(data, 0, 0, 4);
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_RECLEV:
		data = awacs_get_volume(awacs_reg[0], 4);
		return IOCTL_OUT(arg, data);
	case MIXER_WRITE(SOUND_MIXER_MONITOR):
		IOCTL_IN(arg, data);
		awacs_reg[1] &= ~MASK_LOOPTHRU;
		if ((data & 0xff) >= 50)
			awacs_reg[1] |= MASK_LOOPTHRU;
		awacs_write(MASK_ADDR1 | awacs_reg[1]);
		/* fall through */
	case MIXER_READ(SOUND_MIXER_MONITOR):
		data = (awacs_reg[1] & MASK_LOOPTHRU)? 100: 0;
		return IOCTL_OUT(arg, data);
	}
	return -EINVAL;
}

static int burgundy_mixer_ioctl(u_int cmd, u_long arg)
{
	int data;

	/* We are, we are, we are... Burgundy or better */
	switch(cmd) {
	case SOUND_MIXER_READ_DEVMASK:
		data = SOUND_MASK_VOLUME | SOUND_MASK_CD |
			SOUND_MASK_LINE | SOUND_MASK_MIC |
			SOUND_MASK_SPEAKER | SOUND_MASK_ALTPCM;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_RECMASK:
		data = SOUND_MASK_LINE | SOUND_MASK_MIC
			| SOUND_MASK_CD;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_RECSRC:
		data = 0;
		if (awacs_reg[0] & MASK_MUX_AUDIN)
			data |= SOUND_MASK_LINE;
		if (awacs_reg[0] & MASK_MUX_MIC)
			data |= SOUND_MASK_MIC;
		if (awacs_reg[0] & MASK_MUX_CD)
			data |= SOUND_MASK_CD;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_RECSRC:
		IOCTL_IN(arg, data);
		data &= (SOUND_MASK_LINE
			 | SOUND_MASK_MIC | SOUND_MASK_CD);
		awacs_reg[0] &= ~(MASK_MUX_CD | MASK_MUX_MIC
				  | MASK_MUX_AUDIN);
		if (data & SOUND_MASK_LINE)
			awacs_reg[0] |= MASK_MUX_AUDIN;
		if (data & SOUND_MASK_MIC)
			awacs_reg[0] |= MASK_MUX_MIC;
		if (data & SOUND_MASK_CD)
			awacs_reg[0] |= MASK_MUX_CD;
		awacs_write(awacs_reg[0] | MASK_ADDR0);
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_STEREODEVS:
		data = SOUND_MASK_VOLUME | SOUND_MASK_SPEAKER
			| SOUND_MASK_RECLEV | SOUND_MASK_CD
			| SOUND_MASK_LINE;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_CAPS:
		return IOCTL_OUT(arg, 0);
	case SOUND_MIXER_WRITE_VOLUME:
		IOCTL_IN(arg, data);
		awacs_burgundy_write_mvolume(MASK_ADDR_BURGUNDY_MASTER_VOLUME, data);
				/* Fall through */
	case SOUND_MIXER_READ_VOLUME:
		return IOCTL_OUT(arg, awacs_burgundy_read_mvolume(MASK_ADDR_BURGUNDY_MASTER_VOLUME));
	case SOUND_MIXER_WRITE_SPEAKER:
		IOCTL_IN(arg, data);

		if (!(data & 0xff)) {
			/* Mute the left speaker */
			awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
					   awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES) & ~0x2);
		} else {
			/* Unmute the left speaker */
			awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
					   awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES) | 0x2);
		}
		if (!(data & 0xff00)) {
			/* Mute the right speaker */
			awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
					   awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES) & ~0x4);
		} else {
			/* Unmute the right speaker */
			awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
					   awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES) | 0x4);
		}

		data = (((data&0xff)*16)/100 > 0xf ? 0xf :
			(((data&0xff)*16)/100)) + 
			((((data>>8)*16)/100 > 0xf ? 0xf :
			  ((((data>>8)*16)/100)))<<4);

		awacs_burgundy_wcb(MASK_ADDR_BURGUNDY_ATTENSPEAKER, ~data);
				/* Fall through */
	case SOUND_MIXER_READ_SPEAKER:
		data = awacs_burgundy_rcb(MASK_ADDR_BURGUNDY_ATTENSPEAKER);
		data = (((data & 0xf)*100)/16) + ((((data>>4)*100)/16)<<8);
		return IOCTL_OUT(arg, ~data);
	case SOUND_MIXER_WRITE_ALTPCM:	/* really bell volume */
		IOCTL_IN(arg, data);
		beep_volume = data & 0xff;
				/* fall through */
	case SOUND_MIXER_READ_ALTPCM:
		return IOCTL_OUT(arg, beep_volume);
	case SOUND_MIXER_WRITE_LINE:
		IOCTL_IN(arg, data);
		awacs_burgundy_write_volume(MASK_ADDR_BURGUNDY_VOLLINE, data);

				/* fall through */
	case SOUND_MIXER_READ_LINE:
		data = awacs_burgundy_read_volume(MASK_ADDR_BURGUNDY_VOLLINE);				
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_MIC:
		IOCTL_IN(arg, data);
				/* Mic is mono device */
		data = (data << 8) + (data << 24);
		awacs_burgundy_write_volume(MASK_ADDR_BURGUNDY_VOLMIC, data);
				/* fall through */
	case SOUND_MIXER_READ_MIC:
		data = awacs_burgundy_read_volume(MASK_ADDR_BURGUNDY_VOLMIC);				
		data <<= 24;
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_CD:
		IOCTL_IN(arg, data);
		awacs_burgundy_write_volume(MASK_ADDR_BURGUNDY_VOLCD, data);
				/* fall through */
	case SOUND_MIXER_READ_CD:
		data = awacs_burgundy_read_volume(MASK_ADDR_BURGUNDY_VOLCD);
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_WRITE_RECLEV:
		IOCTL_IN(arg, data);
		data = awacs_volume_setter(data, 0, 0, 4);
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_READ_RECLEV:
		data = awacs_get_volume(awacs_reg[0], 4);
		return IOCTL_OUT(arg, data);
	case SOUND_MIXER_OUTMASK:
		break;
	case SOUND_MIXER_OUTSRC:
		break;
	}
	return -EINVAL;
}

static int PMacMixerIoctl(u_int cmd, u_long arg)
{
	/* Different IOCTLS for burgundy*/
	if (awacs_revision >= AWACS_BURGUNDY)
		return burgundy_mixer_ioctl(cmd, arg);
	return awacs_mixer_ioctl(cmd, arg);
}


static void PMacWriteSqSetup(void)
{
	int i;
	volatile struct dbdma_cmd *cp;

	cp = awacs_tx_cmds;
	memset((void *)cp, 0, (write_sq.numBufs+1) * sizeof(struct dbdma_cmd));
	for (i = 0; i < write_sq.numBufs; ++i, ++cp) {
		st_le32(&cp->phy_addr, virt_to_bus(write_sq.buffers[i]));
	}
	st_le16(&cp->command, DBDMA_NOP + BR_ALWAYS);
	st_le32(&cp->cmd_dep, virt_to_bus(awacs_tx_cmds));
	out_le32(&awacs_txdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
	out_le32(&awacs_txdma->cmdptr, virt_to_bus(awacs_tx_cmds));
}

static void PMacReadSqSetup(void)
{
	int i;
	volatile struct dbdma_cmd *cp;

	cp = awacs_rx_cmds;
	memset((void *)cp, 0, (read_sq.numBufs+1) * sizeof(struct dbdma_cmd));

	/* Set dma buffers up in a loop */
	for (i = 0; i < read_sq.numBufs; i++,cp++) {
		st_le32(&cp->phy_addr, virt_to_bus(read_sq.buffers[i]));
		st_le16(&cp->command, INPUT_MORE + INTR_ALWAYS);
		st_le16(&cp->req_count, read_sq.block_size);
		st_le16(&cp->xfer_status, 0);
	}

	/* The next two lines make the thing loop around.
	*/
	st_le16(&cp->command, DBDMA_NOP + BR_ALWAYS);
	st_le32(&cp->cmd_dep, virt_to_bus(awacs_rx_cmds));

	/* Don't start until the first read is done.
	 * This will also abort any operations in progress if the DMA
	 * happens to be running (and it shouldn't).
	 */
	out_le32(&awacs_rxdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
	out_le32(&awacs_rxdma->cmdptr, virt_to_bus(awacs_rx_cmds));

}

static void PMacAbortRead(void)
{
	int i;
	volatile struct dbdma_cmd *cp;

	cp = awacs_rx_cmds;
	for (i = 0; i < read_sq.numBufs; i++,cp++)
		st_le16(&cp->command, DBDMA_STOP);
	/*
	 * We should probably wait for the thing to stop before we
	 * release the memory
	 */
}


/*** Machine definitions *****************************************************/


static MACHINE machPMac = {
	name:		awacs_name,
	name2:		"AWACS",
	open:		PMacOpen,
	release:	PMacRelease,
	dma_alloc:	PMacAlloc,
	dma_free:	PMacFree,
	irqinit:	PMacIrqInit,
#ifdef MODULE
	irqcleanup:	PMacIrqCleanup,
#endif /* MODULE */
	init:		PMacInit,
	silence:	PMacSilence,
	setFormat:	PMacSetFormat,
	setVolume:	PMacSetVolume,
	play:		PMacPlay,
	record:		PMacRecord,
	mixer_ioctl:	PMacMixerIoctl,
	write_sq_setup:	PMacWriteSqSetup,
	read_sq_setup:	PMacReadSqSetup,
	abort_read:	PMacAbortRead,
	min_dsp_speed:	8000
};


/*** Config & Setup **********************************************************/


int __init dmasound_awacs_init(void)
{
	struct device_node *np;

	if (_machine != _MACH_Pmac)
		return -ENODEV;

	awacs_subframe = 0;
	awacs_revision = 0;
	np = find_devices("awacs");
	if (np == 0) {
		/*
		 * powermac G3 models have a node called "davbus"
		 * with a child called "sound".
		 */
		struct device_node *sound;
		np = find_devices("davbus");
		sound = find_devices("sound");
		if (sound != 0 && sound->parent == np) {
			unsigned int *prop, l, i;
			prop = (unsigned int *)
				get_property(sound, "sub-frame", 0);
			if (prop != 0 && *prop >= 0 && *prop < 16)
				awacs_subframe = *prop;
			if (device_is_compatible(sound, "burgundy"))
				awacs_revision = AWACS_BURGUNDY;
			/* This should be verified on older screamers */
			if (device_is_compatible(sound, "screamer"))
				awacs_is_screamer = 1;
			prop = (unsigned int *)get_property(sound, "device-id", 0);
			if (prop != 0)
				awacs_device_id = *prop;
			awacs_has_iic = (find_devices("perch") != NULL);

			/* look for a property saying what sample rates
			   are available */
			for (i = 0; i < 8; ++i)
				awacs_freqs_ok[i] = 0;
			prop = (unsigned int *) get_property
				(sound, "sample-rates", &l);
			if (prop == 0)
				prop = (unsigned int *) get_property
					(sound, "output-frame-rates", &l);
			if (prop != 0) {
				for (l /= sizeof(int); l > 0; --l) {
					/* sometimes the rate is in the
					   high-order 16 bits (?) */
					unsigned int r = *prop++;
					if (r >= 0x10000)
						r >>= 16;
					for (i = 0; i < 8; ++i) {
						if (r == awacs_freqs[i]) {
							awacs_freqs_ok[i] = 1;
							break;
						}
					}
				}
			} else {
				/* assume just 44.1k is OK */
				awacs_freqs_ok[0] = 1;
			}
		}
	}
	if (np != NULL && np->n_addrs >= 3 && np->n_intrs >= 3) {
		int vol;
		dmasound.mach = machPMac;

		awacs = (volatile struct awacs_regs *)
			ioremap(np->addrs[0].address, 0x80);
		awacs_txdma = (volatile struct dbdma_regs *)
			ioremap(np->addrs[1].address, 0x100);
		awacs_rxdma = (volatile struct dbdma_regs *)
			ioremap(np->addrs[2].address, 0x100);

		awacs_irq = np->intrs[0].line;
		awacs_tx_irq = np->intrs[1].line;
		awacs_rx_irq = np->intrs[2].line;

		awacs_tx_cmd_space = kmalloc((write_sq.numBufs + 4) * sizeof(struct dbdma_cmd),
					     GFP_KERNEL);
		if (awacs_tx_cmd_space == NULL) {
			printk(KERN_ERR "DMA sound driver: Not enough buffer memory, driver disabled!\n");
			return -ENOMEM;
		}
		awacs_node = np;
#ifdef CONFIG_PMAC_PBOOK
		if (machine_is_compatible("PowerBook1,1")
		    || machine_is_compatible("AAPL,PowerBook1998")) {
			pmu_suspend();
			feature_set(np, FEATURE_Sound_CLK_enable);
			feature_set(np, FEATURE_Sound_power);
			/* Shorter delay will not work */
			mdelay(1000);
			pmu_resume();
		}
#endif
		awacs_tx_cmds = (volatile struct dbdma_cmd *)
			DBDMA_ALIGN(awacs_tx_cmd_space);


		awacs_rx_cmd_space = kmalloc((read_sq.numBufs + 4) * sizeof(struct dbdma_cmd),
					     GFP_KERNEL);
		if (awacs_rx_cmd_space == NULL) {
		  printk("DMA sound driver: No memory for input");
		}
		awacs_rx_cmds = (volatile struct dbdma_cmd *)
		  DBDMA_ALIGN(awacs_rx_cmd_space);



		awacs_reg[0] = MASK_MUX_CD;
		/* FIXME: Only machines with external SRS module need MASK_PAROUT */
		awacs_reg[1] = MASK_LOOPTHRU;
		if (awacs_has_iic || awacs_device_id == 0x5 || /*awacs_device_id == 0x8
			|| */awacs_device_id == 0xb)
			awacs_reg[1] |= MASK_PAROUT;
		/* get default volume from nvram */
		vol = (~nvram_read_byte(0x1308) & 7) << 1;
		awacs_reg[2] = vol + (vol << 6);
		awacs_reg[4] = vol + (vol << 6);
		awacs_reg[5] = 0;
		awacs_reg[6] = 0;
		awacs_reg[7] = 0;
		out_le32(&awacs->control, 0x11);
		awacs_write(awacs_reg[0] + MASK_ADDR0);
		awacs_write(awacs_reg[1] + MASK_ADDR1);
		awacs_write(awacs_reg[2] + MASK_ADDR2);
		awacs_write(awacs_reg[4] + MASK_ADDR4);
		if (awacs_is_screamer) {
			awacs_write(awacs_reg[5] + MASK_ADDR5);
			awacs_write(awacs_reg[6] + MASK_ADDR6);
			awacs_write(awacs_reg[7] + MASK_ADDR7);
		}

		/* Initialize recent versions of the awacs */
		if (awacs_revision == 0) {
			awacs_revision =
				(in_le32(&awacs->codec_stat) >> 12) & 0xf;
			if (awacs_revision == 3) {
				mdelay(100);
				awacs_write(0x6000);
				mdelay(2);
				awacs_write(awacs_reg[1] + MASK_ADDR1);
				awacs_enable_amp(100 * 0x101);
			}
		}
		if (awacs_revision >= AWACS_BURGUNDY)
			awacs_burgundy_init();

		/* Initialize beep stuff */
		beep_dbdma_cmd = awacs_tx_cmds + (write_sq.numBufs + 1);
		orig_mksound = kd_mksound;
		kd_mksound = awacs_mksound;
		beep_buf = (short *) kmalloc(BEEP_BUFLEN * 4, GFP_KERNEL);
		if (beep_buf == NULL)
			printk(KERN_WARNING "dmasound: no memory for "
			       "beep buffer\n");
#ifdef CONFIG_PMAC_PBOOK
		pmu_register_sleep_notifier(&awacs_sleep_notifier);
#endif /* CONFIG_PMAC_PBOOK */

		/* Powerbooks have odd ways of enabling inputs such as
		   an expansion-bay CD or sound from an internal modem
		   or a PC-card modem. */
		if (machine_is_compatible("AAPL,3400/2400")
			|| machine_is_compatible("AAPL,3500")) {
			is_pbook_3400 = 1;
			/*
			 * Enable CD and PC-card sound inputs.
			 * This is done by reading from address
			 * f301a000, + 0x10 to enable the expansion-bay
			 * CD sound input, + 0x80 to enable the PC-card
			 * sound input.  The 0x100 enables the SCSI bus
			 * terminator power.
			 */
			latch_base = (unsigned char *) ioremap
				(0xf301a000, 0x1000);
			in_8(latch_base + 0x190);
		} else if (machine_is_compatible("PowerBook1,1")
			   || machine_is_compatible("AAPL,PowerBook1998")) {
			struct device_node* mio;
			macio_base = 0;
			is_pbook_G3 = 1;
			for (mio = np->parent; mio; mio = mio->parent) {
				if (strcmp(mio->name, "mac-io") == 0
				    && mio->n_addrs > 0) {
					macio_base = (unsigned char *) ioremap
						(mio->addrs[0].address, 0x40);
					break;
				}
			}
			/*
			 * Enable CD sound input.
			 * The relevant bits for writing to this byte are 0x8f.
			 * I haven't found out what the 0x80 bit does.
			 * For the 0xf bits, writing 3 or 7 enables the CD
			 * input, any other value disables it.  Values
			 * 1, 3, 5, 7 enable the microphone.  Values 0, 2,
			 * 4, 6, 8 - f enable the input from the modem.
			 */
			if (macio_base)
				out_8(macio_base + 0x37, 3);
		}
		sprintf(awacs_name, "PowerMac (AWACS rev %d) ",
			awacs_revision);
		return dmasound_init();
	}
	return -ENODEV;
}

static void __exit dmasound_awacs_cleanup(void)
{
	dmasound_deinit();
}

module_init(dmasound_awacs_init);
module_exit(dmasound_awacs_cleanup);
