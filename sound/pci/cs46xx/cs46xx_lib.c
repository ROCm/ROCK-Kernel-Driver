/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Abramo Bagnara <abramo@alsa-project.org>
 *                   Cirrus Logic, Inc.
 *  Routines for control of Cirrus Logic CS461x chips
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    We need a DSP code to support multichannel outputs and S/PDIF.
 *    Unfortunately, it seems that Cirrus Logic, Inc. is not willing
 *    to provide us sufficient information about the DSP processor,
 *    so we can't update the driver.
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define __NO_VERSION__
#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/cs46xx.h>
#ifndef LINUX_2_2
#include <linux/gameport.h>
#endif

#define chip_t cs46xx_t

/*
 *  constants
 */

#define CS46XX_BA0_SIZE		0x1000
#define CS46XX_BA1_DATA0_SIZE	0x3000
#define CS46XX_BA1_DATA1_SIZE	0x3800
#define CS46XX_BA1_PRG_SIZE	0x7000
#define CS46XX_BA1_REG_SIZE	0x0100


#define CS46XX_PERIOD_SIZE 2048
#define CS46XX_FRAGS 2
#define CS46XX_BUFFER_SIZE CS46XX_PERIOD_SIZE * CS46XX_FRAGS

extern snd_pcm_ops_t snd_cs46xx_playback_ops;
extern snd_pcm_ops_t snd_cs46xx_playback_indirect_ops;
extern snd_pcm_ops_t snd_cs46xx_capture_ops;
extern snd_pcm_ops_t snd_cs46xx_capture_indirect_ops;


/*
 *  common I/O routines
 */

static inline void snd_cs46xx_poke(cs46xx_t *chip, unsigned long reg, unsigned int val)
{
	unsigned int bank = reg >> 16;
	unsigned int offset = reg & 0xffff;
	writel(val, chip->region.idx[bank+1].remap_addr + offset);
}

static inline unsigned int snd_cs46xx_peek(cs46xx_t *chip, unsigned long reg)
{
	unsigned int bank = reg >> 16;
	unsigned int offset = reg & 0xffff;
	return readl(chip->region.idx[bank+1].remap_addr + offset);
}

static inline void snd_cs46xx_pokeBA0(cs46xx_t *chip, unsigned long offset, unsigned int val)
{
	writel(val, chip->region.name.ba0.remap_addr + offset);
}

static inline unsigned int snd_cs46xx_peekBA0(cs46xx_t *chip, unsigned long offset)
{
	return readl(chip->region.name.ba0.remap_addr + offset);
}


static unsigned short snd_cs46xx_codec_read(cs46xx_t *chip,
					    unsigned short reg)
{
	int count;
	unsigned short result;

	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97 
	 *  3. Write ACCTL = Control Register = 460h for initiating the write
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 17h
	 *  5. if DCV not cleared, break and return error
	 *  6. Read ACSTS = Status Register = 464h, check VSTS bit
	 */

	snd_cs46xx_peekBA0(chip, BA0_ACSDA);

	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  set CRW - Read command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
	 */

	snd_cs46xx_pokeBA0(chip, BA0_ACCAD, reg);
	snd_cs46xx_pokeBA0(chip, BA0_ACCDA, 0);
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_DCV | ACCTL_CRW |
					     ACCTL_VFRM | ACCTL_ESYN |
					     ACCTL_RSTN);


	/*
	 *  Wait for the read to occur.
	 */
	for (count = 0; count < 1000; count++) {
		/*
		 *  First, we want to wait for a short time.
	 	 */
		udelay(10);
		/*
		 *  Now, check to see if the read has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 17h
		 */
		if (!(snd_cs46xx_peekBA0(chip, BA0_ACCTL) & ACCTL_DCV))
			goto ok1;
	}

	snd_printk("AC'97 read problem (ACCTL_DCV), reg = 0x%x\n", reg);
	result = 0xffff;
	goto end;
	
 ok1:
	/*
	 *  Wait for the valid status bit to go active.
	 */
	for (count = 0; count < 100; count++) {
		/*
		 *  Read the AC97 status register.
		 *  ACSTS = Status Register = 464h
		 *  VSTS - Valid Status
		 */
		if (snd_cs46xx_peekBA0(chip, BA0_ACSTS) & ACSTS_VSTS)
			goto ok2;
		udelay(10);
	}
	
	snd_printk("AC'97 read problem (ACSTS_VSTS), reg = 0x%x\n", reg);
	result = 0xffff;
	goto end;

 ok2:
	/*
	 *  Read the data returned from the AC97 register.
	 *  ACSDA = Status Data Register = 474h
	 */
#if 0
	printk("e) reg = 0x%x, val = 0x%x, BA0_ACCAD = 0x%x\n", reg,
			snd_cs46xx_peekBA0(chip, BA0_ACSDA),
			snd_cs46xx_peekBA0(chip, BA0_ACCAD));
#endif
	result = snd_cs46xx_peekBA0(chip, BA0_ACSDA);
 end:
	return result;
}

static unsigned short snd_cs46xx_ac97_read(ac97_t * ac97,
					    unsigned short reg)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, ac97->private_data, return -ENXIO);
	unsigned short val;
	chip->active_ctrl(chip, 1);
	val = snd_cs46xx_codec_read(chip, reg);
	chip->active_ctrl(chip, -1);
	return val;
}


static void snd_cs46xx_codec_write(cs46xx_t *chip,
				   unsigned short reg,
				   unsigned short val)
{
	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97
	 *  3. Write ACCTL = Control Register = 460h for initiating the write
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 07h
	 *  5. if DCV not cleared, break and return error
	 */
	int count;

	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  reset CRW - Write command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
         */
	snd_cs46xx_pokeBA0(chip, BA0_ACCAD, reg);
	snd_cs46xx_pokeBA0(chip, BA0_ACCDA, val);
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_DCV | ACCTL_VFRM |
				             ACCTL_ESYN | ACCTL_RSTN);
	for (count = 0; count < 4000; count++) {
		/*
		 *  First, we want to wait for a short time.
		 */
		udelay(10);
		/*
		 *  Now, check to see if the write has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 07h
		 */
		if (!(snd_cs46xx_peekBA0(chip, BA0_ACCTL) & ACCTL_DCV)) {
			return;
		}
	}
	snd_printk("AC'97 write problem, reg = 0x%x, val = 0x%x\n", reg, val);
}

static void snd_cs46xx_ac97_write(ac97_t *ac97,
				   unsigned short reg,
				   unsigned short val)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, ac97->private_data, return);
	int val2 = 0;

	chip->active_ctrl(chip, 1);
	if (reg == AC97_CD)
		val2 = snd_cs46xx_codec_read(chip, AC97_CD);

	snd_cs46xx_codec_write(chip, reg, val);

	
	/*
	 *	Adjust power if the mixer is selected/deselected according
	 *	to the CD.
	 *
	 *	IF the CD is a valid input source (mixer or direct) AND
	 *		the CD is not muted THEN power is needed
	 *
	 *	We do two things. When record select changes the input to
	 *	add/remove the CD we adjust the power count if the CD is
	 *	unmuted.
	 *
	 *	When the CD mute changes we adjust the power level if the
	 *	CD was a valid input.
	 *
	 *      We also check for CD volume != 0, as the CD mute isn't
	 *      normally tweaked from userspace.
	 */
	 
	/* CD mute change ? */
	
	if (reg == AC97_CD) {
		/* Mute bit change ? */
		if ((val2^val)&0x8000 || ((val2 == 0x1f1f || val == 0x1f1f) && val2 != val)) {
			/* Mute on */
			if(val&0x8000 || val == 0x1f1f)
				chip->amplifier_ctrl(chip, -1);
			else /* Mute off power on */
				chip->amplifier_ctrl(chip, 1);
		}
	}

	chip->active_ctrl(chip, -1);
}


/*
 *  Chip initialization
 */

int snd_cs46xx_download(cs46xx_t *chip,
			u32 *src,
                        unsigned long offset,
                        unsigned long len)
{
	unsigned long dst;
	unsigned int bank = offset >> 16;
	offset = offset & 0xffff;

	snd_assert(!(offset & 3) && !(len & 3), return -EINVAL);
	dst = chip->region.idx[bank+1].remap_addr + offset;
	len /= sizeof(u32);

	/* writel already converts 32-bit value to right endianess */
	while (len-- > 0) {
		writel(*src++, dst);
		dst += sizeof(u32);
	}
	return 0;
}

/* 3*1024 parameter, 3.5*1024 sample, 2*3.5*1024 code */
#define BA1_DWORD_SIZE		(13 * 1024 + 512)
#define BA1_MEMORY_COUNT	3

struct BA1struct {
	struct {
		unsigned long offset;
		unsigned long size;
	} memory[BA1_MEMORY_COUNT];
	u32 map[BA1_DWORD_SIZE];
};

static
#include "cs46xx_image.h"

int snd_cs46xx_download_image(cs46xx_t *chip)
{
	int idx, err;
	unsigned long offset = 0;

	for (idx = 0; idx < BA1_MEMORY_COUNT; idx++) {
		if ((err = snd_cs46xx_download(chip,
					       &BA1Struct.map[offset],
					       BA1Struct.memory[idx].offset,
					       BA1Struct.memory[idx].size)) < 0)
			return err;
		offset += BA1Struct.memory[idx].size >> 2;
	}	
	return 0;
}

/*
 *  Chip reset
 */

static void snd_cs46xx_reset(cs46xx_t *chip)
{
	int idx;

	/*
	 *  Write the reset bit of the SP control register.
	 */
	snd_cs46xx_poke(chip, BA1_SPCR, SPCR_RSTSP);

	/*
	 *  Write the control register.
	 */
	snd_cs46xx_poke(chip, BA1_SPCR, SPCR_DRQEN);

	/*
	 *  Clear the trap registers.
	 */
	for (idx = 0; idx < 8; idx++) {
		snd_cs46xx_poke(chip, BA1_DREG, DREG_REGID_TRAP_SELECT + idx);
		snd_cs46xx_poke(chip, BA1_TWPR, 0xFFFF);
	}
	snd_cs46xx_poke(chip, BA1_DREG, 0);

	/*
	 *  Set the frame timer to reflect the number of cycles per frame.
	 */
	snd_cs46xx_poke(chip, BA1_FRMT, 0xadf);
}

static void snd_cs46xx_clear_serial_FIFOs(cs46xx_t *chip)
{
	int idx, loop, powerdown = 0;
	unsigned int tmp;

	/*
	 *  See if the devices are powered down.  If so, we must power them up first
	 *  or they will not respond.
	 */
	tmp = snd_cs46xx_peekBA0(chip, BA0_CLKCR1);
	if (!(tmp & CLKCR1_SWCE)) {
		snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp | CLKCR1_SWCE);
		powerdown = 1;
	}

	/*
	 *  We want to clear out the serial port FIFOs so we don't end up playing
	 *  whatever random garbage happens to be in them.  We fill the sample FIFOS
	 *  with zero (silence).
         */
	snd_cs46xx_pokeBA0(chip, BA0_SERBWP, 0);

	/*
	 *  Fill all 256 sample FIFO locations.
	 */
	for (idx = 0; idx < 256; idx++) {
		/*
		 *  Make sure the previous FIFO write operation has completed.
		 */
		for (loop = 0; loop < 5; loop++) {
			udelay(50);
			if (!(snd_cs46xx_peekBA0(chip, BA0_SERBST) & SERBST_WBSY))
				break;
		}
		if (snd_cs46xx_peekBA0(chip, BA0_SERBST) & SERBST_WBSY) {
			if (powerdown)
				snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp);
		}
		/*
		 *  Write the serial port FIFO index.
		 */
		snd_cs46xx_pokeBA0(chip, BA0_SERBAD, idx);
		/*
		 *  Tell the serial port to load the new value into the FIFO location.
		 */
		snd_cs46xx_pokeBA0(chip, BA0_SERBCM, SERBCM_WRC);
	}
	/*
	 *  Now, if we powered up the devices, then power them back down again.
	 *  This is kinda ugly, but should never happen.
	 */
	if (powerdown)
		snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp);
}

static void snd_cs46xx_proc_start(cs46xx_t *chip)
{
	int cnt;

	/*
	 *  Set the frame timer to reflect the number of cycles per frame.
	 */
	snd_cs46xx_poke(chip, BA1_FRMT, 0xadf);
	/*
	 *  Turn on the run, run at frame, and DMA enable bits in the local copy of
	 *  the SP control register.
	 */
	snd_cs46xx_poke(chip, BA1_SPCR, SPCR_RUN | SPCR_RUNFR | SPCR_DRQEN);
	/*
	 *  Wait until the run at frame bit resets itself in the SP control
	 *  register.
	 */
	for (cnt = 0; cnt < 25; cnt++) {
		udelay(50);
		if (!(snd_cs46xx_peek(chip, BA1_SPCR) & SPCR_RUNFR))
			break;
	}

	if (snd_cs46xx_peek(chip, BA1_SPCR) & SPCR_RUNFR)
		snd_printk("SPCR_RUNFR never reset\n");
}

static void snd_cs46xx_proc_stop(cs46xx_t *chip)
{
	/*
	 *  Turn off the run, run at frame, and DMA enable bits in the local copy of
	 *  the SP control register.
	 */
	snd_cs46xx_poke(chip, BA1_SPCR, 0);
}

/*
 *  Sample rate routines
 */

#define GOF_PER_SEC 200

static void snd_cs46xx_set_play_sample_rate(cs46xx_t *chip, unsigned int rate)
{
	unsigned long flags;
	unsigned int tmp1, tmp2;
	unsigned int phiIncr;
	unsigned int correctionPerGOF, correctionPerSec;

	/*
	 *  Compute the values used to drive the actual sample rate conversion.
	 *  The following formulas are being computed, using inline assembly
	 *  since we need to use 64 bit arithmetic to compute the values:
	 *
	 *  phiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF = floor((Fs,in * 2^26 - Fs,out * phiIncr) /
         *                                   GOF_PER_SEC)
         *  ulCorrectionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -M
         *                       GOF_PER_SEC * correctionPerGOF
	 *
	 *  i.e.
	 *
	 *  phiIncr:other = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF:correctionPerSec =
	 *      dividend:remainder(ulOther / GOF_PER_SEC)
	 */
	tmp1 = rate << 16;
	phiIncr = tmp1 / 48000;
	tmp1 -= phiIncr * 48000;
	tmp1 <<= 10;
	phiIncr <<= 10;
	tmp2 = tmp1 / 48000;
	phiIncr += tmp2;
	tmp1 -= tmp2 * 48000;
	correctionPerGOF = tmp1 / GOF_PER_SEC;
	tmp1 -= correctionPerGOF * GOF_PER_SEC;
	correctionPerSec = tmp1;

	/*
	 *  Fill in the SampleRateConverter control block.
	 */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs46xx_poke(chip, BA1_PSRC,
	  ((correctionPerSec << 16) & 0xFFFF0000) | (correctionPerGOF & 0xFFFF));
	snd_cs46xx_poke(chip, BA1_PPI, phiIncr);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_cs46xx_set_capture_sample_rate(cs46xx_t *chip, unsigned int rate)
{
	unsigned long flags;
	unsigned int phiIncr, coeffIncr, tmp1, tmp2;
	unsigned int correctionPerGOF, correctionPerSec, initialDelay;
	unsigned int frameGroupLength, cnt;

	/*
	 *  We can only decimate by up to a factor of 1/9th the hardware rate.
	 *  Correct the value if an attempt is made to stray outside that limit.
	 */
	if ((rate * 9) < 48000)
		rate = 48000 / 9;

	/*
	 *  We can not capture at at rate greater than the Input Rate (48000).
	 *  Return an error if an attempt is made to stray outside that limit.
	 */
	if (rate > 48000)
		rate = 48000;

	/*
	 *  Compute the values used to drive the actual sample rate conversion.
	 *  The following formulas are being computed, using inline assembly
	 *  since we need to use 64 bit arithmetic to compute the values:
	 *
	 *     coeffIncr = -floor((Fs,out * 2^23) / Fs,in)
	 *     phiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *     correctionPerGOF = floor((Fs,in * 2^26 - Fs,out * phiIncr) /
	 *                                GOF_PER_SEC)
	 *     correctionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -
	 *                          GOF_PER_SEC * correctionPerGOF
	 *     initialDelay = ceil((24 * Fs,in) / Fs,out)
	 *
	 * i.e.
	 *
	 *     coeffIncr = neg(dividend((Fs,out * 2^23) / Fs,in))
	 *     phiIncr:ulOther = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *     correctionPerGOF:correctionPerSec =
	 * 	    dividend:remainder(ulOther / GOF_PER_SEC)
	 *     initialDelay = dividend(((24 * Fs,in) + Fs,out - 1) / Fs,out)
	 */

	tmp1 = rate << 16;
	coeffIncr = tmp1 / 48000;
	tmp1 -= coeffIncr * 48000;
	tmp1 <<= 7;
	coeffIncr <<= 7;
	coeffIncr += tmp1 / 48000;
	coeffIncr ^= 0xFFFFFFFF;
	coeffIncr++;
	tmp1 = 48000 << 16;
	phiIncr = tmp1 / rate;
	tmp1 -= phiIncr * rate;
	tmp1 <<= 10;
	phiIncr <<= 10;
	tmp2 = tmp1 / rate;
	phiIncr += tmp2;
	tmp1 -= tmp2 * rate;
	correctionPerGOF = tmp1 / GOF_PER_SEC;
	tmp1 -= correctionPerGOF * GOF_PER_SEC;
	correctionPerSec = tmp1;
	initialDelay = ((48000 * 24) + rate - 1) / rate;

	/*
	 *  Fill in the VariDecimate control block.
	 */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs46xx_poke(chip, BA1_CSRC,
		((correctionPerSec << 16) & 0xFFFF0000) | (correctionPerGOF & 0xFFFF));
	snd_cs46xx_poke(chip, BA1_CCI, coeffIncr);
	snd_cs46xx_poke(chip, BA1_CD,
		(((BA1_VARIDEC_BUF_1 + (initialDelay << 2)) << 16) & 0xFFFF0000) | 0x80);
	snd_cs46xx_poke(chip, BA1_CPI, phiIncr);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	/*
	 *  Figure out the frame group length for the write back task.  Basically,
	 *  this is just the factors of 24000 (2^6*3*5^3) that are not present in
	 *  the output sample rate.
	 */
	frameGroupLength = 1;
	for (cnt = 2; cnt <= 64; cnt *= 2) {
		if (((rate / cnt) * cnt) != rate)
			frameGroupLength *= 2;
	}
	if (((rate / 3) * 3) != rate) {
		frameGroupLength *= 3;
	}
	for (cnt = 5; cnt <= 125; cnt *= 5) {
		if (((rate / cnt) * cnt) != rate) 
			frameGroupLength *= 5;
        }

	/*
	 * Fill in the WriteBack control block.
	 */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs46xx_poke(chip, BA1_CFG1, frameGroupLength);
	snd_cs46xx_poke(chip, BA1_CFG2, (0x00800000 | frameGroupLength));
	snd_cs46xx_poke(chip, BA1_CCST, 0x0000FFFF);
	snd_cs46xx_poke(chip, BA1_CSPB, ((65536 * rate) / 24000));
	snd_cs46xx_poke(chip, (BA1_CSPB + 4), 0x0000FFFF);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/*
 *  PCM part
 */

static int snd_cs46xx_playback_transfer(snd_pcm_substream_t *substream, 
					snd_pcm_uframes_t frames)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_sframes_t diff = runtime->control->appl_ptr - chip->play.appl_ptr;
	if (diff) {
		if (diff < -(snd_pcm_sframes_t) (runtime->boundary / 2))
			diff += runtime->boundary;
		chip->play.sw_ready += diff << chip->play.shift;
	}
	chip->play.sw_ready += frames << chip->play.shift;
	chip->play.appl_ptr = runtime->control->appl_ptr + frames;
	while (chip->play.hw_ready < CS46XX_BUFFER_SIZE && 
	       chip->play.sw_ready > 0) {
		size_t hw_to_end = CS46XX_BUFFER_SIZE - chip->play.hw_data;
		size_t sw_to_end = chip->play.sw_bufsize - chip->play.sw_data;
		size_t bytes = CS46XX_BUFFER_SIZE - chip->play.hw_ready;
		if (chip->play.sw_ready < bytes)
			bytes = chip->play.sw_ready;
		if (hw_to_end < bytes)
			bytes = hw_to_end;
		if (sw_to_end < bytes)
			bytes = sw_to_end;
		memcpy(chip->play.hw_area + chip->play.hw_data,
		       runtime->dma_area + chip->play.sw_data,
		       bytes);
		chip->play.hw_data += bytes;
		if (chip->play.hw_data == CS46XX_BUFFER_SIZE)
			chip->play.hw_data = 0;
		chip->play.sw_data += bytes;
		if (chip->play.sw_data == chip->play.sw_bufsize)
			chip->play.sw_data = 0;
		chip->play.hw_ready += bytes;
		chip->play.sw_ready -= bytes;
	}
	return 0;
}

static int snd_cs46xx_capture_transfer(snd_pcm_substream_t *substream, 
				       snd_pcm_uframes_t frames)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_sframes_t diff = runtime->control->appl_ptr - chip->capt.appl_ptr;
	if (diff) {
		if (diff < -(snd_pcm_sframes_t) (runtime->boundary / 2))
			diff += runtime->boundary;
		chip->capt.sw_ready -= diff << chip->capt.shift;
	}
	chip->capt.sw_ready -= frames << chip->capt.shift;
	chip->capt.appl_ptr = runtime->control->appl_ptr + frames;
	while (chip->capt.hw_ready > 0 && 
	       chip->capt.sw_ready < chip->capt.sw_bufsize) {
		size_t hw_to_end = CS46XX_BUFFER_SIZE - chip->capt.hw_data;
		size_t sw_to_end = chip->capt.sw_bufsize - chip->capt.sw_data;
		size_t bytes = chip->capt.sw_bufsize - chip->capt.sw_ready;
		if (chip->capt.hw_ready < bytes)
			bytes = chip->capt.hw_ready;
		if (hw_to_end < bytes)
			bytes = hw_to_end;
		if (sw_to_end < bytes)
			bytes = sw_to_end;
		memcpy(runtime->dma_area + chip->capt.sw_data,
		       chip->capt.hw_area + chip->capt.hw_data,
		       bytes);
		chip->capt.hw_data += bytes;
		if (chip->capt.hw_data == CS46XX_BUFFER_SIZE)
			chip->capt.hw_data = 0;
		chip->capt.sw_data += bytes;
		if (chip->capt.sw_data == chip->capt.sw_bufsize)
			chip->capt.sw_data = 0;
		chip->capt.hw_ready -= bytes;
		chip->capt.sw_ready += bytes;
	}
	return 0;
}

static snd_pcm_uframes_t snd_cs46xx_playback_direct_pointer(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr = snd_cs46xx_peek(chip, BA1_PBA) - chip->play.hw_addr;
	return ptr >> chip->play.shift;
}

static snd_pcm_uframes_t snd_cs46xx_playback_indirect_pointer(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr = snd_cs46xx_peek(chip, BA1_PBA) - chip->play.hw_addr;
	ssize_t bytes = ptr - chip->play.hw_io;
	if (bytes < 0)
		bytes += CS46XX_BUFFER_SIZE;
	chip->play.hw_io = ptr;
	chip->play.hw_ready -= bytes;
	chip->play.sw_io += bytes;
	if (chip->play.sw_io > chip->play.sw_bufsize)
		chip->play.sw_io -= chip->play.sw_bufsize;
	snd_cs46xx_playback_transfer(substream, 0);
	return chip->play.sw_io >> chip->play.shift;
}

static snd_pcm_uframes_t snd_cs46xx_capture_direct_pointer(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr = snd_cs46xx_peek(chip, BA1_CBA) - chip->capt.hw_addr;
	return ptr >> chip->capt.shift;
}

static snd_pcm_uframes_t snd_cs46xx_capture_indirect_pointer(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr = snd_cs46xx_peek(chip, BA1_CBA) - chip->capt.hw_addr;
	ssize_t bytes = ptr - chip->capt.hw_io;
	if (bytes < 0)
		bytes += CS46XX_BUFFER_SIZE;
	chip->capt.hw_io = ptr;
	chip->capt.hw_ready += bytes;
	chip->capt.sw_io += bytes;
	if (chip->capt.sw_io > chip->capt.sw_bufsize)
		chip->capt.sw_io -= chip->capt.sw_bufsize;
	snd_cs46xx_capture_transfer(substream, 0);
	return chip->capt.sw_io >> chip->capt.shift;
}

static int snd_cs46xx_playback_copy(snd_pcm_substream_t *substream,
				    int channel,
				    snd_pcm_uframes_t hwoff,
				    void *src,
				    snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	size_t hwoffb = hwoff << chip->play.shift;
	size_t bytes = frames << chip->play.shift;
	char *hwbuf = runtime->dma_area + hwoffb;
	if (copy_from_user(hwbuf, src, bytes))
		return -EFAULT;
	spin_lock_irq(&runtime->lock);
	snd_cs46xx_playback_transfer(substream, frames);
	spin_unlock_irq(&runtime->lock);
	return 0;
}
	
static int snd_cs46xx_capture_copy(snd_pcm_substream_t *substream,
				   int channel,
				   snd_pcm_uframes_t hwoff,
				   void *dst,
				   snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	size_t hwoffb = hwoff << chip->capt.shift;
	size_t bytes = frames << chip->capt.shift;
	char *hwbuf = runtime->dma_area + hwoffb;
	if (copy_to_user(dst, hwbuf, bytes))
		return -EFAULT;
	spin_lock_irq(&runtime->lock);
	snd_cs46xx_capture_transfer(substream, frames);
	spin_unlock_irq(&runtime->lock);
	return 0;
}
	
static int snd_cs46xx_playback_trigger(snd_pcm_substream_t * substream,
				       int cmd)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	unsigned int tmp;
	int result = 0;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (substream->runtime->periods != CS46XX_FRAGS)
			snd_cs46xx_playback_transfer(substream, 0);
		tmp = snd_cs46xx_peek(chip, BA1_PCTL);
		tmp &= 0x0000ffff;
		snd_cs46xx_poke(chip, BA1_PCTL, chip->play.ctl | tmp);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		tmp = snd_cs46xx_peek(chip, BA1_PCTL);
		tmp &= 0x0000ffff;
		snd_cs46xx_poke(chip, BA1_PCTL, tmp);
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

static int snd_cs46xx_capture_trigger(snd_pcm_substream_t * substream,
				      int cmd)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	unsigned int tmp;
	int result = 0;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		tmp = snd_cs46xx_peek(chip, BA1_CCTL);
		tmp &= 0xffff0000;
		snd_cs46xx_poke(chip, BA1_CCTL, chip->capt.ctl | tmp);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		tmp = snd_cs46xx_peek(chip, BA1_CCTL);
		tmp &= 0xffff0000;
		snd_cs46xx_poke(chip, BA1_CCTL, tmp);
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

static int snd_cs46xx_playback_hw_params(snd_pcm_substream_t * substream,
					 snd_pcm_hw_params_t * hw_params)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if (params_periods(hw_params) == CS46XX_FRAGS) {
		if (runtime->dma_area != chip->play.hw_area)
			snd_pcm_lib_free_pages(substream);
		runtime->dma_area = chip->play.hw_area;
		runtime->dma_addr = chip->play.hw_addr;
		runtime->dma_bytes = chip->play.hw_size;
		substream->ops = &snd_cs46xx_playback_ops;
	} else {
		if (runtime->dma_area == chip->play.hw_area) {
			runtime->dma_area = NULL;
			runtime->dma_addr = 0;
			runtime->dma_bytes = 0;
		}
		if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
			return err;
		substream->ops = &snd_cs46xx_playback_indirect_ops;
	}
	return 0;
}

static int snd_cs46xx_playback_hw_free(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (runtime->dma_area != chip->play.hw_area)
		snd_pcm_lib_free_pages(substream);
	runtime->dma_area = NULL;
	runtime->dma_addr = 0;
	runtime->dma_bytes = 0;
	return 0;
}

static int snd_cs46xx_playback_prepare(snd_pcm_substream_t * substream)
{
	unsigned int tmp;
	unsigned int pfie;
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	pfie = snd_cs46xx_peek(chip, BA1_PFIE);
	pfie &= ~0x0000f03f;

	chip->play.shift = 2;
	if (runtime->channels == 1) {
		chip->play.shift--;
		pfie |= 0x00002000;
	}
	if (snd_pcm_format_width(runtime->format) == 8) {
		chip->play.shift--;
		pfie |= 0x00001000;
	}
	if (snd_pcm_format_unsigned(runtime->format))
		pfie |= 0x00008000;
	if (snd_pcm_format_big_endian(runtime->format))
		pfie |= 0x00004000;

	
	chip->play.sw_bufsize = snd_pcm_lib_buffer_bytes(substream);
	chip->play.sw_data = chip->play.sw_io = chip->play.sw_ready = 0;
	chip->play.hw_data = chip->play.hw_io = chip->play.hw_ready = 0;
	chip->play.appl_ptr = 0;
	snd_cs46xx_poke(chip, BA1_PBA, chip->play.hw_addr);

	tmp = snd_cs46xx_peek(chip, BA1_PDTC);
	tmp &= ~0x000003ff;
	tmp |= (4 << chip->play.shift) - 1;
	snd_cs46xx_poke(chip, BA1_PDTC, tmp);

	snd_cs46xx_poke(chip, BA1_PFIE, pfie);

	snd_cs46xx_set_play_sample_rate(chip, runtime->rate);
	return 0;
}

static int snd_cs46xx_capture_hw_params(snd_pcm_substream_t * substream,
					snd_pcm_hw_params_t * hw_params)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if (runtime->periods == CS46XX_FRAGS) {
		if (runtime->dma_area != chip->capt.hw_area)
			snd_pcm_lib_free_pages(substream);
		runtime->dma_area = chip->capt.hw_area;
		runtime->dma_addr = chip->capt.hw_addr;
		runtime->dma_bytes = chip->capt.hw_size;
		substream->ops = &snd_cs46xx_capture_ops;
	} else {
		if (runtime->dma_area == chip->capt.hw_area) {
			runtime->dma_area = NULL;
			runtime->dma_addr = 0;
			runtime->dma_bytes = 0;
		}
		if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
			return err;
		substream->ops = &snd_cs46xx_capture_indirect_ops;
	}
	return 0;
}

static int snd_cs46xx_capture_hw_free(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (runtime->dma_area != chip->capt.hw_area)
		snd_pcm_lib_free_pages(substream);
	runtime->dma_area = NULL;
	runtime->dma_addr = 0;
	runtime->dma_bytes = 0;
	return 0;
}

static int snd_cs46xx_capture_prepare(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_cs46xx_poke(chip, BA1_CBA, chip->capt.hw_addr);
	chip->capt.shift = 2;
	chip->capt.sw_bufsize = snd_pcm_lib_buffer_bytes(substream);
	chip->capt.sw_data = chip->capt.sw_io = chip->capt.sw_ready = 0;
	chip->capt.hw_data = chip->capt.hw_io = chip->capt.hw_ready = 0;
	chip->capt.appl_ptr = 0;
	snd_cs46xx_set_capture_sample_rate(chip, runtime->rate);
	return 0;
}

static void snd_cs46xx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, dev_id, return);
	unsigned int status;

	/*
	 *  Read the Interrupt Status Register to clear the interrupt
	 */
	status = snd_cs46xx_peekBA0(chip, BA0_HISR);
	if ((status & 0x7fffffff) == 0) {
		snd_cs46xx_pokeBA0(chip, BA0_HICR, HICR_CHGM | HICR_IEV);
		return;
	}

	if ((status & HISR_VC0) && chip->pcm) {
		if (chip->play.substream)
			snd_pcm_period_elapsed(chip->play.substream);
	}
	if ((status & HISR_VC1) && chip->pcm) {
		if (chip->capt.substream)
			snd_pcm_period_elapsed(chip->capt.substream);
	}
	if ((status & HISR_MIDI) && chip->rmidi) {
		unsigned char c;
		
		spin_lock(&chip->reg_lock);
		while ((snd_cs46xx_peekBA0(chip, BA0_MIDSR) & MIDSR_RBE) == 0) {
			c = snd_cs46xx_peekBA0(chip, BA0_MIDRP);
			if ((chip->midcr & MIDCR_RIE) == 0)
				continue;
			snd_rawmidi_receive(chip->midi_input, &c, 1);
		}
		while ((snd_cs46xx_peekBA0(chip, BA0_MIDSR) & MIDSR_TBF) == 0) {
			if ((chip->midcr & MIDCR_TIE) == 0)
				break;
			if (snd_rawmidi_transmit(chip->midi_output, &c, 1) != 1) {
				chip->midcr &= ~MIDCR_TIE;
				snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
				break;
			}
			snd_cs46xx_pokeBA0(chip, BA0_MIDWP, c);
		}
		spin_unlock(&chip->reg_lock);
	}
	/*
	 *  EOI to the PCI part....reenables interrupts
	 */
	snd_cs46xx_pokeBA0(chip, BA0_HICR, HICR_CHGM | HICR_IEV);
}

static snd_pcm_hardware_t snd_cs46xx_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED | 
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_RESUME),
	formats:		(SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
				 SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_U16_BE),
	.rates			= SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 5500,
	.rate_max		= 48000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= (256 * 1024),
	.period_bytes_min	= CS46XX_PERIOD_SIZE,
	.period_bytes_max	= CS46XX_PERIOD_SIZE,
	.periods_min		= CS46XX_FRAGS,
	.periods_max		= 1024,
	.fifo_size		= 0,
};

static snd_pcm_hardware_t snd_cs46xx_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_RESUME),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 5500,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= (256 * 1024),
	.period_bytes_min	= CS46XX_PERIOD_SIZE,
	.period_bytes_max	= CS46XX_PERIOD_SIZE,
	.periods_min		= CS46XX_FRAGS,
	.periods_max		= 1024,
	.fifo_size		= 0,
};

static int snd_cs46xx_playback_open(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);

	if ((chip->play.hw_area = snd_malloc_pci_pages(chip->pci, chip->play.hw_size, &chip->play.hw_addr)) == NULL)
		return -ENOMEM;
	chip->play.substream = substream;
	substream->runtime->hw = snd_cs46xx_playback;
	if (chip->accept_valid)
		substream->runtime->hw.info |= SNDRV_PCM_INFO_MMAP_VALID;
	chip->active_ctrl(chip, 1);
	chip->amplifier_ctrl(chip, 1);
	return 0;
}

static int snd_cs46xx_capture_open(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);

	if ((chip->capt.hw_area = snd_malloc_pci_pages(chip->pci, chip->capt.hw_size, &chip->capt.hw_addr)) == NULL)
		return -ENOMEM;
	chip->capt.substream = substream;
	substream->runtime->hw = snd_cs46xx_capture;
	if (chip->accept_valid)
		substream->runtime->hw.info |= SNDRV_PCM_INFO_MMAP_VALID;
	chip->active_ctrl(chip, 1);
	chip->amplifier_ctrl(chip, 1);
	return 0;
}

static int snd_cs46xx_playback_close(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);

	chip->play.substream = NULL;
	snd_free_pci_pages(chip->pci, chip->play.hw_size, chip->play.hw_area, chip->play.hw_addr);
	chip->active_ctrl(chip, -1);
	chip->amplifier_ctrl(chip, -1);
	return 0;
}

static int snd_cs46xx_capture_close(snd_pcm_substream_t * substream)
{
	cs46xx_t *chip = snd_pcm_substream_chip(substream);

	chip->capt.substream = NULL;
	snd_free_pci_pages(chip->pci, chip->capt.hw_size, chip->capt.hw_area, chip->capt.hw_addr);
	chip->active_ctrl(chip, -1);
	chip->amplifier_ctrl(chip, -1);
	return 0;
}

snd_pcm_ops_t snd_cs46xx_playback_ops = {
	.open			= snd_cs46xx_playback_open,
	.close			= snd_cs46xx_playback_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params		= snd_cs46xx_playback_hw_params,
	.hw_free		= snd_cs46xx_playback_hw_free,
	.prepare		= snd_cs46xx_playback_prepare,
	.trigger		= snd_cs46xx_playback_trigger,
	.pointer		= snd_cs46xx_playback_direct_pointer,
};

snd_pcm_ops_t snd_cs46xx_playback_indirect_ops = {
	.open			= snd_cs46xx_playback_open,
	.close			= snd_cs46xx_playback_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params		= snd_cs46xx_playback_hw_params,
	.hw_free		= snd_cs46xx_playback_hw_free,
	.prepare		= snd_cs46xx_playback_prepare,
	.trigger		= snd_cs46xx_playback_trigger,
	.copy			= snd_cs46xx_playback_copy,
	.pointer		= snd_cs46xx_playback_indirect_pointer,
};

snd_pcm_ops_t snd_cs46xx_capture_ops = {
	.open			= snd_cs46xx_capture_open,
	.close			= snd_cs46xx_capture_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params		= snd_cs46xx_capture_hw_params,
	.hw_free		= snd_cs46xx_capture_hw_free,
	.prepare		= snd_cs46xx_capture_prepare,
	.trigger		= snd_cs46xx_capture_trigger,
	.pointer		= snd_cs46xx_capture_direct_pointer,
};

snd_pcm_ops_t snd_cs46xx_capture_indirect_ops = {
	.open			= snd_cs46xx_capture_open,
	.close			= snd_cs46xx_capture_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params		= snd_cs46xx_capture_hw_params,
	.hw_free		= snd_cs46xx_capture_hw_free,
	.prepare		= snd_cs46xx_capture_prepare,
	.trigger		= snd_cs46xx_capture_trigger,
	.copy			= snd_cs46xx_capture_copy,
	.pointer		= snd_cs46xx_capture_indirect_pointer,
};

static void snd_cs46xx_pcm_free(snd_pcm_t *pcm)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, pcm->private_data, return);
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int __devinit snd_cs46xx_pcm(cs46xx_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(chip->card, "CS46xx", device, 1, 1, &pcm)) < 0)
		return err;
	pcm->private_data = chip;
	pcm->private_free = snd_cs46xx_pcm_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cs46xx_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_cs46xx_capture_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "CS46xx");
	chip->pcm = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024, 256*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  Mixer routines
 */

static void snd_cs46xx_mixer_free_ac97(ac97_t *ac97)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, ac97->private_data, return);
	chip->ac97 = NULL;
	chip->eapd_switch = NULL;
}

static int snd_cs46xx_vol_info(snd_kcontrol_t *kcontrol, 
			       snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 32767;
	return 0;
}

static int snd_cs46xx_vol_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs46xx_t *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value;
	unsigned int val = snd_cs46xx_peek(chip, reg);
	ucontrol->value.integer.value[0] = 0xffff - (val >> 16);
	ucontrol->value.integer.value[1] = 0xffff - (val & 0xffff);
	return 0;
}

static int snd_cs46xx_vol_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cs46xx_t *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value;
	unsigned int val = ((0xffff - ucontrol->value.integer.value[0]) << 16 | 
			    (0xffff - ucontrol->value.integer.value[1]));
	unsigned int old = snd_cs46xx_peek(chip, reg);
	int change = (old != val);
	if (change)
		snd_cs46xx_poke(chip, reg, val);
	return change;
}

static snd_kcontrol_new_t snd_cs46xx_controls[] __devinitdata = {
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DAC Volume",
	.info = snd_cs46xx_vol_info,
	.get = snd_cs46xx_vol_get,
	.put = snd_cs46xx_vol_put,
	.private_value = BA1_PVOL,
},
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "ADC Volume",
	.info = snd_cs46xx_vol_info,
	.get = snd_cs46xx_vol_get,
	.put = snd_cs46xx_vol_put,
	.private_value = BA1_CVOL,
}};

int __devinit snd_cs46xx_mixer(cs46xx_t *chip)
{
	snd_card_t *card = chip->card;
	ac97_t ac97;
	snd_ctl_elem_id_t id;
	int err;
	int idx;

	memset(&ac97, 0, sizeof(ac97));
	ac97.write = snd_cs46xx_ac97_write;
	ac97.read = snd_cs46xx_ac97_read;
	ac97.private_data = chip;
	ac97.private_free = snd_cs46xx_mixer_free_ac97;

	snd_cs46xx_ac97_write(&ac97, AC97_MASTER, 0x8000);
	for (idx = 0; idx < 100; ++idx) {
		if (snd_cs46xx_ac97_read(&ac97, AC97_MASTER) == 0x8000)
			goto _ok;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/100);
	}
	return -ENXIO;

 _ok:
	if ((err = snd_ac97_mixer(card, &ac97, &chip->ac97)) < 0)
		return err;
	for (idx = 0; idx < sizeof(snd_cs46xx_controls) / 
		     sizeof(snd_cs46xx_controls[0]); idx++) {
		snd_kcontrol_t *kctl;
		kctl = snd_ctl_new1(&snd_cs46xx_controls[idx], chip);
		if ((err = snd_ctl_add(card, kctl)) < 0)
			return err;
	}

	/* get EAPD mixer switch (for voyetra hack) */
	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strcpy(id.name, "External Amplifier Power Down");
	chip->eapd_switch = snd_ctl_find_id(chip->card, &id);

	return 0;
}

/*
 *  RawMIDI interface
 */

static void snd_cs46xx_midi_reset(cs46xx_t *chip)
{
	snd_cs46xx_pokeBA0(chip, BA0_MIDCR, MIDCR_MRST);
	udelay(100);
	snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
}

static int snd_cs46xx_midi_input_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, substream->rmidi->private_data, return -ENXIO);

	chip->active_ctrl(chip, 1);
	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->uartm |= CS46XX_MODE_INPUT;
	chip->midcr |= MIDCR_RXE;
	chip->midi_input = substream;
	if (!(chip->uartm & CS46XX_MODE_OUTPUT)) {
		snd_cs46xx_midi_reset(chip);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cs46xx_midi_input_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->midcr &= ~(MIDCR_RXE | MIDCR_RIE);
	chip->midi_input = NULL;
	if (!(chip->uartm & CS46XX_MODE_OUTPUT)) {
		snd_cs46xx_midi_reset(chip);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	chip->uartm &= ~CS46XX_MODE_INPUT;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	chip->active_ctrl(chip, -1);
	return 0;
}

static int snd_cs46xx_midi_output_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, substream->rmidi->private_data, return -ENXIO);

	chip->active_ctrl(chip, 1);

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->uartm |= CS46XX_MODE_OUTPUT;
	chip->midcr |= MIDCR_TXE;
	chip->midi_output = substream;
	if (!(chip->uartm & CS46XX_MODE_INPUT)) {
		snd_cs46xx_midi_reset(chip);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cs46xx_midi_output_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&chip->reg_lock, flags);
	chip->midcr &= ~(MIDCR_TXE | MIDCR_TIE);
	chip->midi_output = NULL;
	if (!(chip->uartm & CS46XX_MODE_INPUT)) {
		snd_cs46xx_midi_reset(chip);
	} else {
		snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
	}
	chip->uartm &= ~CS46XX_MODE_OUTPUT;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	chip->active_ctrl(chip, -1);
	return 0;
}

static void snd_cs46xx_midi_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, substream->rmidi->private_data, return);

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (up) {
		if ((chip->midcr & MIDCR_RIE) == 0) {
			chip->midcr |= MIDCR_RIE;
			snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	} else {
		if (chip->midcr & MIDCR_RIE) {
			chip->midcr &= ~MIDCR_RIE;
			snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_cs46xx_midi_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, substream->rmidi->private_data, return);
	unsigned char byte;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (up) {
		if ((chip->midcr & MIDCR_TIE) == 0) {
			chip->midcr |= MIDCR_TIE;
			/* fill UART FIFO buffer at first, and turn Tx interrupts only if necessary */
			while ((chip->midcr & MIDCR_TIE) &&
			       (snd_cs46xx_peekBA0(chip, BA0_MIDSR) & MIDSR_TBF) == 0) {
				if (snd_rawmidi_transmit(substream, &byte, 1) != 1) {
					chip->midcr &= ~MIDCR_TIE;
				} else {
					snd_cs46xx_pokeBA0(chip, BA0_MIDWP, byte);
				}
			}
			snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	} else {
		if (chip->midcr & MIDCR_TIE) {
			chip->midcr &= ~MIDCR_TIE;
			snd_cs46xx_pokeBA0(chip, BA0_MIDCR, chip->midcr);
		}
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static snd_rawmidi_ops_t snd_cs46xx_midi_output =
{
	.open           = snd_cs46xx_midi_output_open,
	.close          = snd_cs46xx_midi_output_close,
	.trigger        = snd_cs46xx_midi_output_trigger,
};

static snd_rawmidi_ops_t snd_cs46xx_midi_input =
{
	.open           = snd_cs46xx_midi_input_open,
	.close          = snd_cs46xx_midi_input_close,
	.trigger        = snd_cs46xx_midi_input_trigger,
};

int __devinit snd_cs46xx_midi(cs46xx_t *chip, int device, snd_rawmidi_t **rrawmidi)
{
	snd_rawmidi_t *rmidi;
	int err;

	if (rrawmidi)
		*rrawmidi = NULL;
	if ((err = snd_rawmidi_new(chip->card, "CS46XX", device, 1, 1, &rmidi)) < 0)
		return err;
	strcpy(rmidi->name, "CS46XX");
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_cs46xx_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_cs46xx_midi_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = chip;
	chip->rmidi = rmidi;
	if (rrawmidi)
		*rrawmidi = NULL;
	return 0;
}


/*
 * gameport interface
 */

#ifndef LINUX_2_2

typedef struct snd_cs46xx_gameport {
	struct gameport info;
	cs46xx_t *chip;
} cs46xx_gameport_t;

static void snd_cs46xx_gameport_trigger(struct gameport *gameport)
{
	cs46xx_gameport_t *gp = (cs46xx_gameport_t *)gameport;
	cs46xx_t *chip;
	snd_assert(gp, return);
	chip = snd_magic_cast(cs46xx_t, gp->chip, return);
	snd_cs46xx_pokeBA0(chip, BA0_JSPT, 0xFF);  //outb(gameport->io, 0xFF);
}

static unsigned char snd_cs46xx_gameport_read(struct gameport *gameport)
{
	cs46xx_gameport_t *gp = (cs46xx_gameport_t *)gameport;
	cs46xx_t *chip;
	snd_assert(gp, return 0);
	chip = snd_magic_cast(cs46xx_t, gp->chip, return 0);
	return snd_cs46xx_peekBA0(chip, BA0_JSPT); //inb(gameport->io);
}

static int snd_cs46xx_gameport_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	cs46xx_gameport_t *gp = (cs46xx_gameport_t *)gameport;
	cs46xx_t *chip;
	unsigned js1, js2, jst;
	
	snd_assert(gp, return 0);
	chip = snd_magic_cast(cs46xx_t, gp->chip, return 0);

	js1 = snd_cs46xx_peekBA0(chip, BA0_JSC1);
	js2 = snd_cs46xx_peekBA0(chip, BA0_JSC2);
	jst = snd_cs46xx_peekBA0(chip, BA0_JSPT);
	
	*buttons = (~jst >> 4) & 0x0F; 
	
	axes[0] = ((js1 & JSC1_Y1V_MASK) >> JSC1_Y1V_SHIFT) & 0xFFFF;
	axes[1] = ((js1 & JSC1_X1V_MASK) >> JSC1_X1V_SHIFT) & 0xFFFF;
	axes[2] = ((js2 & JSC2_Y2V_MASK) >> JSC2_Y2V_SHIFT) & 0xFFFF;
	axes[3] = ((js2 & JSC2_X2V_MASK) >> JSC2_X2V_SHIFT) & 0xFFFF;

	for(jst=0;jst<4;++jst)
		if(axes[jst]==0xFFFF) axes[jst] = -1;
	return 0;
}

static int snd_cs46xx_gameport_open(struct gameport *gameport, int mode)
{
	switch (mode) {
	case GAMEPORT_MODE_COOKED:
		return 0;
	case GAMEPORT_MODE_RAW:
		return 0;
	default:
		return -1;
	}
	return 0;
}

void __devinit snd_cs46xx_gameport(cs46xx_t *chip)
{
	cs46xx_gameport_t *gp;
	gp = kmalloc(sizeof(*gp), GFP_KERNEL);
	if (! gp) {
		snd_printk("cannot allocate gameport area\n");
		return;
	}
	memset(gp, 0, sizeof(*gp));
	gp->info.open = snd_cs46xx_gameport_open;
	gp->info.read = snd_cs46xx_gameport_read;
	gp->info.trigger = snd_cs46xx_gameport_trigger;
	gp->info.cooked_read = snd_cs46xx_gameport_cooked_read;
	gp->chip = chip;
	chip->gameport = gp;

	snd_cs46xx_pokeBA0(chip, BA0_JSIO, 0xFF); // ?
	snd_cs46xx_pokeBA0(chip, BA0_JSCTL, JSCTL_SP_MEDIUM_SLOW);
	gameport_register_port(&gp->info);
}

#else /* LINUX_2_2 */

void __devinit snd_cs46xx_gameport(cs46xx_t *chip)
{
}

#endif /* !LINUX_2_2 */

/*
 *  proc interface
 */

static long snd_cs46xx_io_read(snd_info_entry_t *entry, void *file_private_data,
			       struct file *file, char *buf, long count)
{
	long size;
	snd_cs46xx_region_t *region = (snd_cs46xx_region_t *)entry->private_data;
	
	size = count;
	if (file->f_pos + size > region->size)
		size = region->size - file->f_pos;
	if (size > 0) {
		char *tmp;
		long res;
		unsigned long virt;
		if ((tmp = kmalloc(size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		virt = region->remap_addr + file->f_pos;
		memcpy_fromio(tmp, virt, size);
		if (copy_to_user(buf, tmp, size))
			res = -EFAULT;
		else {
			res = size;
			file->f_pos += size;
		}
		kfree(tmp);
		return res;
	}
	return 0;
}

static struct snd_info_entry_ops snd_cs46xx_proc_io_ops = {
	.read = snd_cs46xx_io_read,
};

static int __devinit snd_cs46xx_proc_init(snd_card_t * card, cs46xx_t *chip)
{
	snd_info_entry_t *entry;
	int idx;
	
	for (idx = 0; idx < 5; idx++) {
		snd_cs46xx_region_t *region = &chip->region.idx[idx];
		entry = snd_info_create_card_entry(card, region->name, card->proc_root);
		if (entry) {
			entry->content = SNDRV_INFO_CONTENT_DATA;
			entry->private_data = chip;
			entry->c.ops = &snd_cs46xx_proc_io_ops;
			entry->size = region->size;
			entry->mode = S_IFREG | S_IRUSR;
			if (snd_info_register(entry) < 0) {
				snd_info_unregister(entry);
				entry = NULL;
			}
		}
		region->proc_entry = entry;
	}
	return 0;
}

static int snd_cs46xx_proc_done(cs46xx_t *chip)
{
	int idx;

	for (idx = 0; idx < 5; idx++) {
		snd_cs46xx_region_t *region = &chip->region.idx[idx];
		if (region->proc_entry) {
			snd_info_unregister((snd_info_entry_t *) region->proc_entry);
			region->proc_entry = NULL;
		}
	}
	return 0;
}

/*
 * stop the h/w
 */
static void snd_cs46xx_hw_stop(cs46xx_t *chip)
{
	unsigned int tmp;

	tmp = snd_cs46xx_peek(chip, BA1_PFIE);
	tmp &= ~0x0000f03f;
	tmp |=  0x00000010;
	snd_cs46xx_poke(chip, BA1_PFIE, tmp);	/* playback interrupt disable */

	tmp = snd_cs46xx_peek(chip, BA1_CIE);
	tmp &= ~0x0000003f;
	tmp |=  0x00000011;
	snd_cs46xx_poke(chip, BA1_CIE, tmp);	/* capture interrupt disable */

	/*
         *  Stop playback DMA.
	 */
	tmp = snd_cs46xx_peek(chip, BA1_PCTL);
	snd_cs46xx_poke(chip, BA1_PCTL, tmp & 0x0000ffff);

	/*
         *  Stop capture DMA.
	 */
	tmp = snd_cs46xx_peek(chip, BA1_CCTL);
	snd_cs46xx_poke(chip, BA1_CCTL, tmp & 0xffff0000);

	/*
         *  Reset the processor.
         */
	snd_cs46xx_reset(chip);

	snd_cs46xx_proc_stop(chip);

	/*
	 *  Power down the PLL.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, 0);

	/*
	 *  Turn off the Processor by turning off the software clock enable flag in 
	 *  the clock control register.
	 */
	tmp = snd_cs46xx_peekBA0(chip, BA0_CLKCR1) & ~CLKCR1_SWCE;
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp);
}


static int snd_cs46xx_free(cs46xx_t *chip)
{
	int idx;

	snd_assert(chip != NULL, return -EINVAL);

	if (chip->active_ctrl)
		chip->active_ctrl(chip, 1);

#ifndef LINUX_2_2
	if (chip->gameport) {
		gameport_unregister_port(&chip->gameport->info);
		kfree(chip->gameport);
	}
#endif
#ifdef CONFIG_PM
	if (chip->pm_dev)
		pm_unregister(chip->pm_dev);
#endif
	if (chip->amplifier_ctrl)
		chip->amplifier_ctrl(chip, -chip->amplifier); /* force to off */
	
	snd_cs46xx_proc_done(chip);

	if (chip->region.idx[0].resource)
		snd_cs46xx_hw_stop(chip);

	for (idx = 0; idx < 5; idx++) {
		snd_cs46xx_region_t *region = &chip->region.idx[idx];
		if (region->remap_addr)
			iounmap((void *) region->remap_addr);
		if (region->resource) {
			release_resource(region->resource);
			kfree_nocheck(region->resource);
		}
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);

	if (chip->active_ctrl)
		chip->active_ctrl(chip, -chip->amplifier);

	snd_magic_kfree(chip);
	return 0;
}

static int snd_cs46xx_dev_free(snd_device_t *device)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, device->device_data, return -ENXIO);
	return snd_cs46xx_free(chip);
}

/*
 *  initialize chip
 */

static int snd_cs46xx_chip_init(cs46xx_t *chip, int busywait)
{
	unsigned int tmp;
	int timeout;

	/* 
	 *  First, blast the clock control register to zero so that the PLL starts
         *  out in a known state, and blast the master serial port control register
         *  to zero so that the serial ports also start out in a known state.
         */
        snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, 0);
        snd_cs46xx_pokeBA0(chip, BA0_SERMC1, 0);

	/*
	 *  If we are in AC97 mode, then we must set the part to a host controlled
         *  AC-link.  Otherwise, we won't be able to bring up the link.
         */        
        snd_cs46xx_pokeBA0(chip, BA0_SERACC, SERACC_HSP | SERACC_CHIP_TYPE_1_03);	/* 1.03 codec */
        /* snd_cs46xx_pokeBA0(chip, BA0_SERACC, SERACC_HSP | SERACC_CHIP_TYPE_2_0); */ /* 2.00 codec */

        /*
         *  Drive the ARST# pin low for a minimum of 1uS (as defined in the AC97
         *  spec) and then drive it high.  This is done for non AC97 modes since
         *  there might be logic external to the CS461x that uses the ARST# line
         *  for a reset.
         */
        snd_cs46xx_pokeBA0(chip, BA0_ACCTL, 0);
        udelay(50);
        snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_RSTN);

	/*
	 *  The first thing we do here is to enable sync generation.  As soon
	 *  as we start receiving bit clock, we'll start producing the SYNC
	 *  signal.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_ESYN | ACCTL_RSTN);

	/*
	 *  Now wait for a short while to allow the AC97 part to start
	 *  generating bit clock (so we don't try to start the PLL without an
	 *  input clock).
	 */
	mdelay(1);

	/*
	 *  Set the serial port timing configuration, so that
	 *  the clock control circuit gets its clock from the correct place.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_SERMC1, SERMC1_PTC_AC97);

	/*
	 *  Write the selected clock control setup to the hardware.  Do not turn on
	 *  SWCE yet (if requested), so that the devices clocked by the output of
	 *  PLL are not clocked until the PLL is stable.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_PLLCC, PLLCC_LPF_1050_2780_KHZ | PLLCC_CDR_73_104_MHZ);
	snd_cs46xx_pokeBA0(chip, BA0_PLLM, 0x3a);
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR2, CLKCR2_PDIVS_8);

	/*
	 *  Power up the PLL.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, CLKCR1_PLLP);

	/*
         *  Wait until the PLL has stabilized.
	 */
	mdelay(1);

	/*
	 *  Turn on clocking of the core so that we can setup the serial ports.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, CLKCR1_PLLP | CLKCR1_SWCE);

	/*
	 *  Fill the serial port FIFOs with silence.
	 */
	snd_cs46xx_clear_serial_FIFOs(chip);

	/*
	 *  Set the serial port FIFO pointer to the first sample in the FIFO.
	 */
	/* snd_cs46xx_pokeBA0(chip, BA0_SERBSP, 0); */

	/*
	 *  Write the serial port configuration to the part.  The master
	 *  enable bit is not set until all other values have been written.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_SERC1, SERC1_SO1F_AC97 | SERC1_SO1EN);
	snd_cs46xx_pokeBA0(chip, BA0_SERC2, SERC2_SI1F_AC97 | SERC1_SO1EN);
	snd_cs46xx_pokeBA0(chip, BA0_SERMC1, SERMC1_PTC_AC97 | SERMC1_MSPE);

	/*
	 * Wait for the codec ready signal from the AC97 codec.
	 */
	timeout = 150;
	while (timeout-- > 0) {
		/*
		 *  Read the AC97 status register to see if we've seen a CODEC READY
		 *  signal from the AC97 codec.
		 */
		if (snd_cs46xx_peekBA0(chip, BA0_ACSTS) & ACSTS_CRDY)
			goto ok1;
		if (busywait)
			mdelay(10);
		else {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout((HZ+99)/100);
		}
	}


	snd_printk("create - never read codec ready from AC'97\n");
	snd_printk("it is not probably bug, try to use CS4236 driver\n");
	snd_cs46xx_free(chip);
	return -EIO;
 ok1:
	/*
	 *  Assert the vaid frame signal so that we can start sending commands
	 *  to the AC97 codec.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_ACCTL, ACCTL_VFRM | ACCTL_ESYN | ACCTL_RSTN);

	/*
	 *  Wait until we've sampled input slots 3 and 4 as valid, meaning that
	 *  the codec is pumping ADC data across the AC-link.
	 */
	timeout = 150;
	while (timeout-- > 0) {
		/*
		 *  Read the input slot valid register and see if input slots 3 and
		 *  4 are valid yet.
		 */
		if ((snd_cs46xx_peekBA0(chip, BA0_ACISV) & (ACISV_ISV3 | ACISV_ISV4)) == (ACISV_ISV3 | ACISV_ISV4))
			goto ok2;
		if (busywait)
			mdelay(10);
		else {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout((HZ+99)/100);
		}
	}

	snd_printk("create - never read ISV3 & ISV4 from AC'97\n");
	snd_cs46xx_free(chip);
	return -EIO;
 ok2:

	/*
	 *  Now, assert valid frame and the slot 3 and 4 valid bits.  This will
	 *  commense the transfer of digital audio data to the AC97 codec.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_ACOSV, ACOSV_SLV3 | ACOSV_SLV4);

	/*
	 *  Power down the DAC and ADC.  We will power them up (if) when we need
	 *  them.
	 */
	/* snd_cs46xx_pokeBA0(chip, BA0_AC97_POWERDOWN, 0x300); */

	/*
	 *  Turn off the Processor by turning off the software clock enable flag in 
	 *  the clock control register.
	 */
	/* tmp = snd_cs46xx_peekBA0(chip, BA0_CLKCR1) & ~CLKCR1_SWCE; */
	/* snd_cs46xx_pokeBA0(chip, BA0_CLKCR1, tmp); */

	/*
         *  Reset the processor.
         */
	snd_cs46xx_reset(chip);

	/*
         *  Download the image to the processor.
	 */
	if (snd_cs46xx_download_image(chip) < 0) {
		snd_printk("image download error\n");
		snd_cs46xx_free(chip);
		return -EIO;
	}

	/*
         *  Stop playback DMA.
	 */
	tmp = snd_cs46xx_peek(chip, BA1_PCTL);
	chip->play.ctl = tmp & 0xffff0000;
	snd_cs46xx_poke(chip, BA1_PCTL, tmp & 0x0000ffff);

	/*
         *  Stop capture DMA.
	 */
	tmp = snd_cs46xx_peek(chip, BA1_CCTL);
	chip->capt.ctl = tmp & 0x0000ffff;
	snd_cs46xx_poke(chip, BA1_CCTL, tmp & 0xffff0000);

	snd_cs46xx_set_play_sample_rate(chip, 8000);
	snd_cs46xx_set_capture_sample_rate(chip, 8000);

	snd_cs46xx_proc_start(chip);

	/*
	 *  Enable interrupts on the part.
	 */
	snd_cs46xx_pokeBA0(chip, BA0_HICR, HICR_IEV | HICR_CHGM);

	tmp = snd_cs46xx_peek(chip, BA1_PFIE);
	tmp &= ~0x0000f03f;
	snd_cs46xx_poke(chip, BA1_PFIE, tmp);	/* playback interrupt enable */

	tmp = snd_cs46xx_peek(chip, BA1_CIE);
	tmp &= ~0x0000003f;
	tmp |=  0x00000001;
	snd_cs46xx_poke(chip, BA1_CIE, tmp);	/* capture interrupt enable */
	
	/* set the attenuation to 0dB */ 
	snd_cs46xx_poke(chip, BA1_PVOL, 0x80008000);
	snd_cs46xx_poke(chip, BA1_CVOL, 0x80008000);

	return 0;
}


/*
 *	AMP control - null AMP
 */
 
static void amp_none(cs46xx_t *chip, int change)
{	
}

/*
 *	Crystal EAPD mode
 */
 
static void amp_voyetra(cs46xx_t *chip, int change)
{
	/* Manage the EAPD bit on the Crystal 4297 
	   and the Analog AD1885 */
	   
	int old = chip->amplifier;
	int oval, val;
	
	chip->amplifier += change;
	oval = snd_cs46xx_codec_read(chip, AC97_POWERDOWN);
	val = oval;
	if (chip->amplifier && !old) {
		/* Turn the EAPD amp on */
		val |= 0x8000;
	} else if (old && !chip->amplifier) {
		/* Turn the EAPD amp off */
		val &= ~0x8000;
	}
	if (val != oval) {
		snd_cs46xx_codec_write(chip, AC97_POWERDOWN, val);
		if (chip->eapd_switch)
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->eapd_switch->id);
	}
}


/*
 *	Game Theatre XP card - EGPIO[2] is used to enable the external amp.
 */
 
static void amp_hercules(cs46xx_t *chip, int change)
{
	int old = chip->amplifier;

	chip->amplifier += change;
	if (chip->amplifier && !old) {
		snd_cs46xx_pokeBA0(chip, BA0_EGPIODR, 
				   EGPIODR_GPOE2);     /* enable EGPIO2 output */
		snd_cs46xx_pokeBA0(chip, BA0_EGPIOPTR, 
				   EGPIOPTR_GPPT2);   /* open-drain on output */
	} else if (old && !chip->amplifier) {
		snd_cs46xx_pokeBA0(chip, BA0_EGPIODR, 0); /* disable */
		snd_cs46xx_pokeBA0(chip, BA0_EGPIOPTR, 0); /* disable */
	}
}


#if 0
/*
 *	Untested
 */
 
static void amp_voyetra_4294(cs46xx_t *chip, int change)
{
	chip->amplifier += change;

	if (chip->amplifier) {
		/* Switch the GPIO pins 7 and 8 to open drain */
		snd_cs46xx_codec_write(chip, 0x4C,
				       snd_cs46xx_codec_read(chip, 0x4C) & 0xFE7F);
		snd_cs46xx_codec_write(chip, 0x4E,
				       snd_cs46xx_codec_read(chip, 0x4E) | 0x0180);
		/* Now wake the AMP (this might be backwards) */
		snd_cs46xx_codec_write(chip, 0x54,
				       snd_cs46xx_codec_read(chip, 0x54) & ~0x0180);
	} else {
		snd_cs46xx_codec_write(chip, 0x54,
				       snd_cs46xx_codec_read(chip, 0x54) | 0x0180);
	}
}
#endif


/*
 * piix4 pci ids
 */
#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL 0x8086
#endif /* PCI_VENDOR_ID_INTEL */

#ifndef PCI_DEVICE_ID_INTEL_82371AB_3
#define PCI_DEVICE_ID_INTEL_82371AB_3 0x7113
#endif /* PCI_DEVICE_ID_INTEL_82371AB_3 */

/*
 *	Handle the CLKRUN on a thinkpad. We must disable CLKRUN support
 *	whenever we need to beat on the chip.
 *
 *	The original idea and code for this hack comes from David Kaiser at
 *	Linuxcare. Perhaps one day Crystal will document their chips well
 *	enough to make them useful.
 */
 
static void clkrun_hack(cs46xx_t *chip, int change)
{
	u16 control;
	int old;
	
	if (chip->acpi_dev == NULL)
		return;

	old = chip->amplifier;
	chip->amplifier += change;
	
	/* Read ACPI port */	
	control = inw(chip->acpi_port + 0x10);

	/* Flip CLKRUN off while running */
	if (! chip->amplifier && old)
		outw(control | 0x2000, chip->acpi_port + 0x10);
	else if (chip->amplifier && ! old)
		outw(control & ~0x2000, chip->acpi_port + 0x10);
}

	
/*
 * detect intel piix4
 */
static void clkrun_init(cs46xx_t *chip)
{
	u8 pp;

	chip->acpi_dev = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3, NULL);
	if (chip->acpi_dev == NULL)
		return;		/* Not a thinkpad thats for sure */

	/* Find the control port */		
	pci_read_config_byte(chip->acpi_dev, 0x41, &pp);
	chip->acpi_port = pp << 8;
}


/*
 * Card subid table
 */
 
struct cs_card_type
{
	u16 vendor;
	u16 id;
	char *name;
	void (*init)(cs46xx_t *);
	void (*amp)(cs46xx_t *, int);
	void (*active)(cs46xx_t *, int);
};

static struct cs_card_type __initdata cards[] = {
	{0x1489, 0x7001, "Genius Soundmaker 128 value", NULL, amp_none, NULL},
	{0x5053, 0x3357, "Voyetra", NULL, amp_voyetra, NULL},
	{0x1071, 0x6003, "Mitac MI6020/21", NULL, amp_voyetra, NULL},
	{0x14AF, 0x0050, "Hercules Game Theatre XP", NULL, amp_hercules, NULL},
	{0x1681, 0x0050, "Hercules Game Theatre XP", NULL, amp_hercules, NULL},
	{0x1681, 0x0051, "Hercules Game Theatre XP", NULL, amp_hercules, NULL},
	{0x1681, 0x0052, "Hercules Game Theatre XP", NULL, amp_hercules, NULL},
	{0x1681, 0x0053, "Hercules Game Theatre XP", NULL, amp_hercules, NULL},
	{0x1681, 0x0054, "Hercules Game Theatre XP", NULL, amp_hercules, NULL},
	/* Not sure if the 570 needs the clkrun hack */
	{PCI_VENDOR_ID_IBM, 0x0132, "Thinkpad 570", clkrun_init, NULL, clkrun_hack},
	{PCI_VENDOR_ID_IBM, 0x0153, "Thinkpad 600X/A20/T20", clkrun_init, NULL, clkrun_hack},
	{PCI_VENDOR_ID_IBM, 0x1010, "Thinkpad 600E (unsupported)", NULL, NULL, NULL},
	{0, 0, "Card without SSID set", NULL, NULL, NULL },
	{0, 0, NULL, NULL, NULL, NULL}
};


/*
 * APM support
 */
#ifdef CONFIG_PM
void snd_cs46xx_suspend(cs46xx_t *chip)
{
	snd_card_t *card = chip->card;

	snd_power_lock(card);
	if (card->power_state == SNDRV_CTL_POWER_D3hot)
		goto __skip;
	snd_pcm_suspend_all(chip->pcm);
	// chip->ac97_powerdown = snd_cs46xx_codec_read(chip, AC97_POWER_CONTROL);
	// chip->ac97_general_purpose = snd_cs46xx_codec_read(chip, BA0_AC97_GENERAL_PURPOSE);
	snd_cs46xx_hw_stop(chip);
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
      __skip:
      	snd_power_unlock(card);
}

void snd_cs46xx_resume(cs46xx_t *chip)
{
	snd_card_t *card = chip->card;
	int amp_saved;

	snd_power_lock(card);
	if (card->power_state == SNDRV_CTL_POWER_D0)
		goto __skip;

	pci_enable_device(chip->pci);
	amp_saved = chip->amplifier;
	chip->amplifier = 0;
	chip->active_ctrl(chip, 1); /* force to on */

	snd_cs46xx_chip_init(chip, 1);

#if 0
	snd_cs46xx_codec_write(chip, BA0_AC97_GENERAL_PURPOSE, 
			       chip->ac97_general_purpose);
	snd_cs46xx_codec_write(chip, AC97_POWER_CONTROL, 
			       chip->ac97_powerdown);
	mdelay(10);
	snd_cs46xx_codec_write(chip, BA0_AC97_POWERDOWN,
			       chip->ac97_powerdown);
	mdelay(5);
#endif

	snd_ac97_resume(chip->ac97);

	if (amp_saved)
		chip->amplifier_ctrl(chip, 1); /* try to turn on */
	if (! amp_saved) {
		chip->amplifier = 1;
		chip->active_ctrl(chip, -1);
	}
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
      __skip:
      	snd_power_unlock(card);
}

static int snd_cs46xx_set_power_state(snd_card_t *card, unsigned int power_state)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, card->power_state_private_data, return -ENXIO);

	switch (power_state) {
	case SNDRV_CTL_POWER_D0:
	case SNDRV_CTL_POWER_D1:
	case SNDRV_CTL_POWER_D2:
		snd_cs46xx_resume(chip);
		break;
	case SNDRV_CTL_POWER_D3hot:
	case SNDRV_CTL_POWER_D3cold:
		snd_cs46xx_suspend(chip);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
#endif /* CONFIG_PM */


/*
 */

int __devinit snd_cs46xx_create(snd_card_t * card,
		      struct pci_dev * pci,
		      int external_amp, int thinkpad,
		      cs46xx_t ** rchip)
{
	cs46xx_t *chip;
	int err, idx;
	snd_cs46xx_region_t *region;
	struct cs_card_type *cp;
	u16 ss_card, ss_vendor;
	static snd_device_ops_t ops = {
		.dev_free	= snd_cs46xx_dev_free,
	};
	
	*rchip = NULL;

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = snd_magic_kcalloc(cs46xx_t, 0, GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	spin_lock_init(&chip->reg_lock);
	chip->card = card;
	chip->pci = pci;
	chip->play.hw_size = PAGE_SIZE;
	chip->capt.hw_size = PAGE_SIZE;
	chip->irq = -1;
	chip->ba0_addr = pci_resource_start(pci, 0);
	chip->ba1_addr = pci_resource_start(pci, 1);
	if (chip->ba0_addr == 0 || chip->ba0_addr == ~0 ||
	    chip->ba1_addr == 0 || chip->ba1_addr == ~0) {
	    	snd_cs46xx_free(chip);
	    	snd_printk("wrong address(es) - ba0 = 0x%lx, ba1 = 0x%lx\n", chip->ba0_addr, chip->ba1_addr);
	    	return -ENOMEM;
	}

	region = &chip->region.name.ba0;
	strcpy(region->name, "CS46xx_BA0");
	region->base = chip->ba0_addr;
	region->size = CS46XX_BA0_SIZE;

	region = &chip->region.name.data0;
	strcpy(region->name, "CS46xx_BA1_data0");
	region->base = chip->ba1_addr + BA1_SP_DMEM0;
	region->size = CS46XX_BA1_DATA0_SIZE;

	region = &chip->region.name.data1;
	strcpy(region->name, "CS46xx_BA1_data1");
	region->base = chip->ba1_addr + BA1_SP_DMEM1;
	region->size = CS46XX_BA1_DATA1_SIZE;

	region = &chip->region.name.pmem;
	strcpy(region->name, "CS46xx_BA1_pmem");
	region->base = chip->ba1_addr + BA1_SP_PMEM;
	region->size = CS46XX_BA1_PRG_SIZE;

	region = &chip->region.name.reg;
	strcpy(region->name, "CS46xx_BA1_reg");
	region->base = chip->ba1_addr + BA1_SP_REG;
	region->size = CS46XX_BA1_REG_SIZE;

	/* set up amp and clkrun hack */
	pci_read_config_word(pci, PCI_SUBSYSTEM_VENDOR_ID, &ss_vendor);
	pci_read_config_word(pci, PCI_SUBSYSTEM_ID, &ss_card);

	for (cp = &cards[0]; cp->name; cp++) {
		if (cp->vendor == ss_vendor && cp->id == ss_card) {
			snd_printd("hack for %s enabled\n", cp->name);
			if (cp->init)
				cp->init(chip);
			chip->amplifier_ctrl = cp->amp;
			chip->active_ctrl = cp->active;
			break;
		}
	}

	if (external_amp) {
		snd_printk("Crystal EAPD support forced on.\n");
		chip->amplifier_ctrl = amp_voyetra;
	}

	if (thinkpad) {
		snd_printk("Activating CLKRUN hack for Thinkpad.\n");
		chip->active_ctrl = clkrun_hack;
		clkrun_init(chip);
	}
	
	if (chip->amplifier_ctrl == NULL)
		chip->amplifier_ctrl = amp_none;
	if (chip->active_ctrl == NULL)
		chip->active_ctrl = amp_none;

	chip->active_ctrl(chip, 1);

	pci_set_master(pci);

	for (idx = 0; idx < 5; idx++) {
		region = &chip->region.idx[idx];
		if ((region->resource = request_mem_region(region->base, region->size, region->name)) == NULL) {
			snd_cs46xx_free(chip);
			snd_printk("unable to request memory region 0x%lx-0x%lx\n", region->base, region->base + region->size - 1);
			return -EBUSY;
		}
		region->remap_addr = (unsigned long) ioremap_nocache(region->base, region->size);
		if (region->remap_addr == 0) {
			snd_cs46xx_free(chip);
			snd_printk("%s ioremap problem\n", region->name);
			return -ENOMEM;
		}
	}
	if (request_irq(pci->irq, snd_cs46xx_interrupt, SA_INTERRUPT|SA_SHIRQ, "CS46XX", (void *) chip)) {
		snd_cs46xx_free(chip);
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	chip->irq = pci->irq;

	err = snd_cs46xx_chip_init(chip, 0);
	if (err < 0) {
		snd_cs46xx_free(chip);
		return err;
	}

	snd_cs46xx_proc_init(card, chip);

#ifdef CONFIG_PM
	card->set_power_state = snd_cs46xx_set_power_state;
	card->power_state_private_data = chip;
#endif

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_cs46xx_free(chip);
		return err;
	}

	chip->active_ctrl(chip, -1);

	*rchip = chip;
	return 0;
}
