/*
 **********************************************************************
 *     hwaccess.h
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date		    Author	    Summary of changes
 *     ----		    ------	    ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#ifndef _HWACCESS_H
#define _HWACCESS_H

#include <linux/fs.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/pci.h>

#include "emu_wrapper.h"

#define EMUPAGESIZE     4096            /* don't change */
#define NUM_G           64              /* use all channels */
#define NUM_FXSENDS     4               /* don't change */
/* setting this to other than a power of two may break some applications */
#define MAXBUFSIZE	65536
#define MAXPAGES	8192 
#define BUFMAXPAGES     (MAXBUFSIZE / PAGE_SIZE)

#define FLAGS_AVAILABLE     0x0001
#define FLAGS_READY         0x0002

#define min(x,y) ((x) < (y)) ? (x) : (y)

struct memhandle
{
	dma_addr_t dma_handle;
	void *addr;
	u32 size;
};

#define DEBUG_LEVEL 2

#ifdef EMU10K1_DEBUG
# define DPD(level,x,y...) do {if(level <= DEBUG_LEVEL) printk( KERN_NOTICE "emu10k1: %s: %d: " x , __FILE__ , __LINE__ , y );} while(0)
# define DPF(level,x)   do {if(level <= DEBUG_LEVEL) printk( KERN_NOTICE "emu10k1: %s: %d: " x , __FILE__ , __LINE__ );} while(0)
#else
# define DPD(level,x,y...) do { } while (0) /* not debugging: nothing */
# define DPF(level,x) do { } while (0)
#endif /* EMU10K1_DEBUG */

#define ERROR() DPF(1,"error\n")

/* DATA STRUCTURES */

struct emu10k1_waveout
{
	u16 send_routing[3];

	u8 send_a[3];
	u8 send_b[3];
	u8 send_c[3];
	u8 send_d[3];
};

struct emu10k1_wavein
{
        struct wiinst *ac97;
        struct wiinst *mic;
        struct wiinst *fx;

        u8 recsrc;
        u32 fxwc;
};


struct emu10k1_card 
{
	struct list_head list;

	struct memhandle	virtualpagetable;
	struct memhandle	tankmem;
	struct memhandle	silentpage;

	spinlock_t		lock;

	u8			voicetable[NUM_G];
	u16			emupagetable[MAXPAGES];

	struct list_head	timers;
	unsigned		timer_delay;
	spinlock_t		timer_lock;

	struct pci_dev		*pci_dev;
	unsigned long           iobase;
	unsigned long		length;
	unsigned short		model;
	unsigned int irq; 

	int	audio_num;
	int	audio1_num;
	int	mixer_num;
	int	midi_num;

	struct emu10k1_waveout	waveout;
	struct emu10k1_wavein	wavein;
	struct emu10k1_mpuout	*mpuout;
	struct emu10k1_mpuin	*mpuin;

	u16			arrwVol[SOUND_MIXER_NRDEVICES + 1];
	/* array is used from the member 1 to save (-1) operation */
	u32			digmix[9 * 6 * 2];
	unsigned int		modcnt;
	struct semaphore	open_sem;
	mode_t			open_mode;
	wait_queue_head_t	open_wait;

	u32	    mpuacqcount;	  // Mpu acquire count
	u32	    has_toslink;	       // TOSLink detection

	u8 chiprev;                    /* Chip revision                */

	int isaps;
};

int emu10k1_addxmgr_alloc(u32, struct emu10k1_card *);
void emu10k1_addxmgr_free(struct emu10k1_card *, int);

#ifdef PRIVATE_PCM_VOLUME

#define MAX_PCM_CHANNELS NUM_G 
struct sblive_pcm_volume_rec {
	struct files_struct *files; // identification of the same thread
	u8 attn_l;		// attenuation for left channel
	u8 attn_r;		// attenuation for right channel
	u16 mixer;		// saved mixer value for return
	u8 channel_l;		// idx of left channel
	u8 channel_r;		// idx of right channel
	int opened;		// counter - locks element
};
extern struct sblive_pcm_volume_rec sblive_pcm_volume[];
extern u16 pcm_last_mixer;
#endif

#define TIMEOUT 		    16384

u32 srToPitch(u32);
u8 sumVolumeToAttenuation(u32);

extern struct list_head emu10k1_devs;

/* Hardware Abstraction Layer access functions */

void emu10k1_writefn0(struct emu10k1_card *, u32 , u32 );
u32 emu10k1_readfn0(struct emu10k1_card *, u32 );

void sblive_writeptr(struct emu10k1_card *, u32 , u32 , u32 );
void sblive_writeptr_tag(struct emu10k1_card *card, u32 channel, ...);
#define TAGLIST_END 0

u32 sblive_readptr(struct emu10k1_card *, u32 , u32 );

void emu10k1_irq_enable(struct emu10k1_card *, u32);
void emu10k1_irq_disable(struct emu10k1_card *, u32);
void emu10k1_set_stop_on_loop(struct emu10k1_card *, u32);
void emu10k1_clear_stop_on_loop(struct emu10k1_card *, u32);

/* AC97 Mixer access function */
int sblive_readac97(struct emu10k1_card *, u8, u16 *);
int sblive_writeac97(struct emu10k1_card *, u8, u16);
int sblive_rmwac97(struct emu10k1_card *, u8, u16, u16);

/* MPU access function*/
int emu10k1_mpu_write_data(struct emu10k1_card *, u8);
int emu10k1_mpu_read_data(struct emu10k1_card *, u8 *);
int emu10k1_mpu_reset(struct emu10k1_card *);
int emu10k1_mpu_acquire(struct emu10k1_card *);
int emu10k1_mpu_release(struct emu10k1_card *);

#endif  /* _HWACCESS_H */
