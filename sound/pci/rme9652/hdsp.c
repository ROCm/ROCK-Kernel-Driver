/*
 *   ALSA driver for RME Hammerfall DSP audio interface(s)
 *
 *      Copyright (c) 2002  Paul Davis
 *                          Marcus Andersson
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

#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/asoundef.h>
#include <sound/rawmidi.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#include <asm/byteorder.h>
#include <asm/current.h>
#include <asm/io.h>

#include "multiface_firmware.dat"
#include "digiface_firmware.dat"

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int precise_ptr[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = 0 }; /* Enable precise pointer */
static int line_outs_monitor[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = 0}; /* Send all inputs/playback to line outs */
static int force_firmware[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = 0}; /* Force firmware reload */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for RME Hammerfall DSP interface.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for RME Hammerfall DSP interface.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable/disable specific Hammerfall DSP soundcards.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(precise_ptr, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(precise_ptr, "Enable precise pointer (doesn't work reliably).");
MODULE_PARM_SYNTAX(precise_ptr, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);
MODULE_PARM(line_outs_monitor,"1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(line_outs_monitor, "Send all input and playback streams to line outs by default.");
MODULE_PARM_SYNTAX(line_outs_monitor, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);
MODULE_PARM(force_firmware,"1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(force_firmware, "Force a reload of the I/O box firmware");
MODULE_PARM_SYNTAX(force_firmware, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);
MODULE_AUTHOR("Paul Davis <pbd@op.net>");
MODULE_DESCRIPTION("RME Hammerfall DSP");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{RME,Hammerfall-DSP}}");

typedef enum {
	Digiface,
	Multiface
} HDSP_Type;

#define HDSP_MAX_CHANNELS        26
#define DIGIFACE_SS_CHANNELS     26
#define DIGIFACE_DS_CHANNELS     14
#define MULTIFACE_SS_CHANNELS    18
#define MULTIFACE_DS_CHANNELS    14

/* Write registers. These are defined as byte-offsets from the iobase value.
 */
#define HDSP_resetPointer               0
#define HDSP_outputBufferAddress	32
#define HDSP_inputBufferAddress		36
#define HDSP_controlRegister		64
#define HDSP_interruptConfirmation	96
#define HDSP_outputEnable	  	128
#define HDSP_jtagReg  			256
#define HDSP_midiDataOut0  		352
#define HDSP_midiDataOut1  		356
#define HDSP_fifoData  			368
#define HDSP_inputEnable	 	384

/* Read registers. These are defined as byte-offsets from the iobase value
 */

#define HDSP_statusRegister    0
#define HDSP_timecode        128
#define HDSP_status2Register 192
#define HDSP_midiDataOut0    352
#define HDSP_midiDataOut1    356
#define HDSP_midiDataIn0     360
#define HDSP_midiDataIn1     364
#define HDSP_midiStatusOut0  384
#define HDSP_midiStatusOut1  388
#define HDSP_midiStatusIn0   392
#define HDSP_midiStatusIn1   396
#define HDSP_fifoStatus      400

/* the meters are regular i/o-mapped registers, but offset
   considerably from the rest. the peak registers are reset
   when read; the least-significant 4 bits are full-scale counters; 
   the actual peak value is in the most-significant 24 bits.
*/

#define HDSP_playbackPeakLevel  4096  /* 26 * 32 bit values */
#define HDSP_inputPeakLevel     4224  /* 26 * 32 bit values */
#define HDSP_outputPeakLevel    4100  /* 26 * 32 bit values */
#define HDSP_playbackRmsLevel   4612  /* 26 * 64 bit values */
#define HDSP_inputRmsLevel      4884  /* 26 * 64 bit values */

#define HDSP_IO_EXTENT     5192

/* jtag register bits */

#define HDSP_TMS                0x01
#define HDSP_TCK                0x02
#define HDSP_TDI                0x04
#define HDSP_JTAG               0x08
#define HDSP_PWDN               0x10
#define HDSP_PROGRAM	        0x020
#define HDSP_CONFIG_MODE_0	0x040
#define HDSP_CONFIG_MODE_1	0x080
#define HDSP_VERSION_BIT	0x100
#define HDSP_BIGENDIAN_MODE     0x200
#define HDSP_RD_MULTIPLE        0x400

#define HDSP_S_PROGRAM     	(HDSP_PROGRAM|HDSP_CONFIG_MODE_0)
#define HDSP_S_LOAD		(HDSP_PROGRAM|HDSP_CONFIG_MODE_1)

/* Control Register bits */

#define HDSP_Start                (1<<0)  // start engine
#define HDSP_Latency0             (1<<1)  // buffer size = 2^n where n is defined by Latency{2,1,0}
#define HDSP_Latency1             (1<<2)  // [ see above ]
#define HDSP_Latency2             (1<<3)  // ] see above ]
#define HDSP_ClockModeMaster      (1<<4)  // 1=Master, 0=Slave/Autosync
#define HDSP_AudioInterruptEnable (1<<5)  // what do you think ?
#define HDSP_Frequency0           (1<<6)  // 0=44.1kHz/88.2kHz 1=48kHz/96kHz
#define HDSP_Frequency1           (1<<7)  // 0=32kHz/64kHz
#define HDSP_DoubleSpeed          (1<<8)  // 0=normal speed, 1=double speed
#define HDSP_SPDIFProfessional    (1<<9)  // 0=consumer, 1=professional
#define HDSP_SPDIFEmphasis        (1<<10) // 0=none, 1=on
#define HDSP_SPDIFNonAudio        (1<<11) // 0=off, 1=on
#define HDSP_SPDIFOpticalOut      (1<<12) // 1=use 1st ADAT connector for SPDIF, 0=do not
#define HDSP_SyncRef2             (1<<13) 
#define HDSP_SPDIFInputSelect0    (1<<14) 
#define HDSP_SPDIFInputSelect1    (1<<15) 
#define HDSP_SyncRef0             (1<<16) 
#define HDSP_SyncRef1             (1<<17) 
#define HDSP_Midi0InterruptEnable (1<<22)
#define HDSP_Midi1InterruptEnable (1<<23)
#define HDSP_LineOut              (1<<24)

#define HDSP_LatencyMask    (HDSP_Latency0|HDSP_Latency1|HDSP_Latency2)
#define HDSP_FrequencyMask  (HDSP_Frequency0|HDSP_Frequency1|HDSP_DoubleSpeed)

#define HDSP_SPDIFInputMask    (HDSP_SPDIFInputSelect0|HDSP_SPDIFInputSelect1)
#define HDSP_SPDIFInputADAT1    0
#define HDSP_SPDIFInputCoaxial (HDSP_SPDIFInputSelect1)
#define HDSP_SPDIFInputCDROM   (HDSP_SPDIFInputSelect0|HDSP_SPDIFInputSelect1)

#define HDSP_SyncRefMask        (HDSP_SyncRef0|HDSP_SyncRef1|HDSP_SyncRef2)
#define HDSP_SyncRef_ADAT1      0
#define HDSP_SyncRef_ADAT2      (HDSP_SyncRef0)
#define HDSP_SyncRef_ADAT3      (HDSP_SyncRef1)
#define HDSP_SyncRef_SPDIF      (HDSP_SyncRef0|HDSP_SyncRef1)
#define HDSP_SyncRef_WORD       (HDSP_SyncRef2)
#define HDSP_SyncRef_ADAT_SYNC  (HDSP_SyncRef0|HDSP_SyncRef2)

/* Preferred sync source choices - used by "sync_pref" control switch */

#define HDSP_SYNC_FROM_SELF      0
#define HDSP_SYNC_FROM_WORD      1
#define HDSP_SYNC_FROM_ADAT_SYNC 2
#define HDSP_SYNC_FROM_SPDIF     3
#define HDSP_SYNC_FROM_ADAT1     4
#define HDSP_SYNC_FROM_ADAT2     5
#define HDSP_SYNC_FROM_ADAT3     6

/* Possible sources of S/PDIF input */

#define HDSP_SPDIFIN_OPTICAL 0	/* optical  (ADAT1) */
#define HDSP_SPDIFIN_COAXIAL 1	/* coaxial  (RCA) */
#define HDSP_SPDIFIN_INTERN  2	/* internal (CDROM) */

#define HDSP_Frequency32KHz    HDSP_Frequency0
#define HDSP_Frequency44_1KHz  HDSP_Frequency1
#define HDSP_Frequency48KHz   (HDSP_Frequency1|HDSP_Frequency0)
#define HDSP_Frequency64KHz   (HDSP_DoubleSpeed|HDSP_Frequency0)
#define HDSP_Frequency88_2KHz (HDSP_DoubleSpeed|HDSP_Frequency1)
#define HDSP_Frequency96KHz   (HDSP_DoubleSpeed|HDSP_Frequency1|HDSP_Frequency0)

#define hdsp_encode_latency(x)       (((x)<<1) & HDSP_LatencyMask)
#define hdsp_decode_latency(x)       (((x) & HDSP_LatencyMask)>>1)

#define hdsp_encode_spdif_in(x) (((x)&0x3)<<14)
#define hdsp_decode_spdif_in(x) (((x)>>14)&0x3)

/* Status Register bits */

#define HDSP_audioIRQPending    (1<<0)
#define HDSP_Lock2              (1<<1)
#define HDSP_Lock1              (1<<2)
#define HDSP_Lock0              (1<<3)
#define HDSP_SPDIFSync          (1<<4)
#define HDSP_TimecodeLock       (1<<5)
#define HDSP_BufferPositionMask 0x000FFC0 /* Bit 6..15 : h/w buffer pointer */
#define HDSP_Sync2              (1<<16)
#define HDSP_Sync1              (1<<17)
#define HDSP_Sync0              (1<<18)
#define HDSP_DoubleSpeedStatus  (1<<19)
#define HDSP_ConfigError        (1<<20)
#define HDSP_DllError           (1<<21)
#define HDSP_spdifFrequency0    (1<<22)
#define HDSP_spdifFrequency1    (1<<23)
#define HDSP_spdifFrequency2    (1<<24)
#define HDSP_SPDIFErrorFlag     (1<<25)
#define HDSP_BufferID           (1<<26)
#define HDSP_TimecodeSync       (1<<27)
#define HDSP_CIN                (1<<28)
#define HDSP_midi0IRQPending    (1<<30) /* notice the gap at bit 29 */
#define HDSP_midi1IRQPending    (1<<31)

#define HDSP_spdifFrequencyMask    (HDSP_spdifFrequency0|HDSP_spdifFrequency1|HDSP_spdifFrequency2)

#define HDSP_spdifFrequency32KHz   (HDSP_spdifFrequency0|HDSP_spdifFrequency1|HDSP_spdifFrequency2)
#define HDSP_spdifFrequency44_1KHz (HDSP_spdifFrequency2|HDSP_spdifFrequency1)
#define HDSP_spdifFrequency48KHz   (HDSP_spdifFrequency0|HDSP_spdifFrequency2)

#define HDSP_spdifFrequency64KHz    0
#define HDSP_spdifFrequency88_2KHz (HDSP_spdifFrequency2)
#define HDSP_spdifFrequency96KHz   (HDSP_spdifFrequency0|HDSP_spdifFrequency1)

/* Status2 Register bits */

#define HDSP_version0     (1<<0)
#define HDSP_version1     (1<<1)
#define HDSP_version2     (1<<2)
#define HDSP_wc_lock      (1<<3)
#define HDSP_wc_sync      (1<<4)
#define HDSP_inp_freq0    (1<<5)
#define HDSP_inp_freq1    (1<<6)
#define HDSP_inp_freq2    (1<<7)
#define HDSP_SelSyncRef0  (1<<8)
#define HDSP_SelSyncRef1  (1<<9)
#define HDSP_SelSyncRef2  (1<<10)

#define HDSP_wc_valid (HDSP_wc_lock|HDSP_wc_sync)

#define HDSP_systemFrequencyMask (HDSP_inp_freq0|HDSP_inp_freq1|HDSP_inp_freq2)
#define HDSP_systemFrequency32   (HDSP_inp_freq0)
#define HDSP_systemFrequency44_1 (HDSP_inp_freq1)
#define HDSP_systemFrequency48   (HDSP_inp_freq0|HDSP_inp_freq1)
#define HDSP_systemFrequency64   (HDSP_inp_freq2)
#define HDSP_systemFrequency88_2 (HDSP_inp_freq0|HDSP_inp_freq2)
#define HDSP_systemFrequency96   (HDSP_inp_freq1|HDSP_inp_freq2)

#define HDSP_SelSyncRefMask        (HDSP_SelSyncRef0|HDSP_SelSyncRef1|HDSP_SelSyncRef2)
#define HDSP_SelSyncRef_ADAT1      0
#define HDSP_SelSyncRef_ADAT2      (HDSP_SelSyncRef0)
#define HDSP_SelSyncRef_ADAT3      (HDSP_SelSyncRef1)
#define HDSP_SelSyncRef_SPDIF      (HDSP_SelSyncRef0|HDSP_SelSyncRef1)
#define HDSP_SelSyncRef_WORD       (HDSP_SelSyncRef2)
#define HDSP_SelSyncRef_ADAT_SYNC  (HDSP_SelSyncRef0|HDSP_SelSyncRef2)

/* FIFO wait times, defined in terms of loops on readl() */

#define HDSP_LONG_WAIT	 40000
#define HDSP_SHORT_WAIT  100

/* Computing addresses for adjusting gains */

#define INPUT_TO_OUTPUT_KEY(in,out)     ((64 * (out)) + (in))
#define PLAYBACK_TO_OUTPUT_KEY(chn,out) ((64 * (out)) + 32 + (chn))
#define UNITY_GAIN                       32768
#define MINUS_INFINITY_GAIN              0

#ifndef PCI_VENDOR_ID_XILINX
#define PCI_VENDOR_ID_XILINX		0x10ee
#endif
#ifndef PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP
#define PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP 0x3fc5
#endif

/* the size of a substream (1 mono data stream) */

#define HDSP_CHANNEL_BUFFER_SAMPLES  (16*1024)
#define HDSP_CHANNEL_BUFFER_BYTES    (4*HDSP_CHANNEL_BUFFER_SAMPLES)

/* the size of the area we need to allocate for DMA transfers. the
   size is the same regardless of the number of channels - the 
   Multiface still uses the same memory area.

   Note that we allocate 1 more channel than is apparently needed
   because the h/w seems to write 1 byte beyond the end of the last
   page. Sigh.
*/

#define HDSP_DMA_AREA_BYTES ((HDSP_MAX_CHANNELS+1) * HDSP_CHANNEL_BUFFER_BYTES)
#define HDSP_DMA_AREA_KILOBYTES (HDSP_DMA_AREA_BYTES/1024)

#define HDSP_MATRIX_MIXER_SIZE 2048

typedef struct _hdsp      hdsp_t;
typedef struct _hdsp_midi hdsp_midi_t;

struct _hdsp_midi {
    hdsp_t                  *hdsp;
    int                      id;
    snd_rawmidi_t           *rmidi;
    snd_rawmidi_substream_t *input;
    snd_rawmidi_substream_t *output;
    char                     istimer; /* timer in use */
    struct timer_list	     timer;
    spinlock_t               lock;
};

struct _hdsp {
	spinlock_t lock;
	snd_pcm_substream_t *capture_substream;
	snd_pcm_substream_t *playback_substream;
        hdsp_midi_t midi[2];
	int precise_ptr;
	u32 control_register;	         /* cached value */
	u32 creg_spdif;
	u32 creg_spdif_stream;
	char *card_name;		 /* digiface/multiface */
        HDSP_Type type;                  /* ditto, but for code use */
	size_t period_bytes;		 /* guess what this is */
	unsigned char ds_channels;
	unsigned char ss_channels;	 /* different for multiface/digiface */
	void *capture_buffer_unaligned;	 /* original buffer addresses */
	void *playback_buffer_unaligned; /* original buffer addresses */
	unsigned char *capture_buffer;	 /* suitably aligned address */
	unsigned char *playback_buffer;	 /* suitably aligned address */
	dma_addr_t capture_buffer_addr;
	dma_addr_t playback_buffer_addr;
	pid_t capture_pid;
	pid_t playback_pid;
	int running;
        int passthru;                   /* non-zero if doing pass-thru */
	int last_spdif_sample_rate;	/* so that we can catch externally ... */
	int last_adat_sample_rate;	/* ... induced rate changes            */
        char *channel_map;
	int dev;
	int irq;
	unsigned long port;
	struct resource *res_port;
        unsigned long iobase;
	snd_card_t *card;
	snd_pcm_t *pcm;
	struct pci_dev *pci;
	snd_kcontrol_t *spdif_ctl;
        unsigned short mixer_matrix[HDSP_MATRIX_MIXER_SIZE];
};

/* These tables map the ALSA channels 1..N to the channels that we
   need to use in order to find the relevant channel buffer. RME
   refer to this kind of mapping as between "the ADAT channel and
   the DMA channel." We index it using the logical audio channel,
   and the value is the DMA channel (i.e. channel buffer number)
   where the data for that channel can be read/written from/to.
*/

static char channel_map_df_ss[HDSP_MAX_CHANNELS] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
	18, 19, 20, 21, 22, 23, 24, 25
};

static char channel_map_mf_ss[HDSP_MAX_CHANNELS] = { /* Multiface */
	/* ADAT 0 */
	0, 1, 2, 3, 4, 5, 6, 7, 
	/* ADAT 2 */
	16, 17, 18, 19, 20, 21, 22, 23, 
	/* SPDIF */
	24, 25,
	-1, -1, -1, -1, -1, -1, -1, -1, 
};

static char channel_map_ds[HDSP_MAX_CHANNELS] = {
	/* ADAT channels are remapped */
	1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23,
	/* channels 12 and 13 are S/PDIF */
	24, 25,
	/* others don't exist */
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

#define HDSP_PREALLOCATE_MEMORY	/* via module snd-hdsp_mem */

#ifdef HDSP_PREALLOCATE_MEMORY
extern void *snd_hammerfall_get_buffer(struct pci_dev *, dma_addr_t *dmaaddr);
extern void snd_hammerfall_free_buffer(struct pci_dev *, void *ptr);
#endif

static struct pci_device_id snd_hdsp_ids[] __devinitdata = {
	{
		.vendor	   = PCI_VENDOR_ID_XILINX,
		.device	   = PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP, 
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	}, /* RME Hammerfall-DSP */
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, snd_hdsp_ids);

static inline void hdsp_write(hdsp_t *hdsp, int reg, int val)
{
	writel(val, hdsp->iobase + reg);
}

static inline unsigned int hdsp_read(hdsp_t *hdsp, int reg)
{
	return readl (hdsp->iobase + reg);
}

static inline unsigned long long hdsp_read64 (hdsp_t *hdsp, int reg)
{
	unsigned long long val;
	val = hdsp_read(hdsp, reg);
	val = (val<<32)|hdsp_read(hdsp, reg + 4);

	return val;
}

static inline int hdsp_check_for_iobox (hdsp_t *hdsp)
{
	if (hdsp_read (hdsp, HDSP_statusRegister) & HDSP_ConfigError) {
		snd_printk ("Hammerfall-DSP: no Digiface or Multiface connected!\n");
		return -EIO;
	}
	return 0;
}

static inline int hdsp_fifo_wait(hdsp_t *hdsp, int count, int timeout)
{    
	int i;

	/* the fifoStatus registers reports on how many words
	   are available in the command FIFO.
	*/
	
	for (i = 0; i < timeout; i++)
		if ((int)(hdsp_read (hdsp, HDSP_fifoStatus) & 0xff) <= count)
			return 0;

	snd_printk ("wait for FIFO status <= %d failed after %d iterations\n",
		    count, timeout);
	return -1;
}

static inline int hdsp_read_gain (hdsp_t *hdsp, unsigned int addr)
{
	if (addr >= HDSP_MATRIX_MIXER_SIZE) {
		return 0;
	}
	return hdsp->mixer_matrix[addr];
}

static inline int hdsp_write_gain(hdsp_t *hdsp, unsigned int addr, unsigned short data)
{
	unsigned int ad;

	if (addr >= HDSP_MATRIX_MIXER_SIZE)
		return -1;

	ad = data + addr * 65536;

	if (hdsp_fifo_wait(hdsp, 127, HDSP_LONG_WAIT)) {
		return -1;
	}
	hdsp_write (hdsp, HDSP_fifoData, ad);
	hdsp->mixer_matrix[addr] = data;

	return 0;
}

static inline int snd_hdsp_use_is_exclusive(hdsp_t *hdsp)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&hdsp->lock, flags);
	if ((hdsp->playback_pid != hdsp->capture_pid) &&
	    (hdsp->playback_pid >= 0) && (hdsp->capture_pid >= 0)) {
		ret = 0;
	}
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return ret;
}

static inline int hdsp_system_sample_rate (hdsp_t *hdsp)
{
	unsigned int status2 = hdsp_read(hdsp, HDSP_status2Register);
	unsigned int rate_bits = status2 & HDSP_systemFrequencyMask;

	switch (rate_bits) {
	case HDSP_systemFrequency32:   return 32000;
	case HDSP_systemFrequency44_1: return 44100;
	case HDSP_systemFrequency48:   return 48000;
	case HDSP_systemFrequency64:   return 64000;
	case HDSP_systemFrequency88_2: return 88200;
	case HDSP_systemFrequency96:   return 96000;
	default:                       return 0;
	}
}

static inline int hdsp_spdif_sample_rate(hdsp_t *hdsp)
{
	unsigned int status = hdsp_read(hdsp, HDSP_statusRegister);
	unsigned int rate_bits = (status & HDSP_spdifFrequencyMask);

	if (status & HDSP_SPDIFErrorFlag) {
		return 0;
	}

	switch (rate_bits) {
	case HDSP_spdifFrequency32KHz: return 32000;
	case HDSP_spdifFrequency44_1KHz: return 44100;
	case HDSP_spdifFrequency48KHz: return 48000;
	case HDSP_spdifFrequency64KHz: return 64000;
	case HDSP_spdifFrequency88_2KHz: return 88200;
	case HDSP_spdifFrequency96KHz: return 96000;
	default:
		snd_printk ("unknown frequency status; bits = 0x%x, status = 0x%x", rate_bits, status);
		return 0;
	}
}

static inline void hdsp_compute_period_size(hdsp_t *hdsp)
{
	hdsp->period_bytes = 1 << ((hdsp_decode_latency(hdsp->control_register) + 8));
}

static snd_pcm_uframes_t hdsp_hw_pointer(hdsp_t *hdsp)
{
	int position;

	position = hdsp_read(hdsp, HDSP_statusRegister);

	if (!hdsp->precise_ptr) {
		return (position & HDSP_BufferID) ? (hdsp->period_bytes / 4) : 0;
	}

	position &= HDSP_BufferPositionMask;
	position /= 4;
	position -= 32;
	position &= (HDSP_CHANNEL_BUFFER_SAMPLES-1);
	return position;
}

static inline void hdsp_reset_hw_pointer(hdsp_t *hdsp)
{
#if 0
	/* reset the hw pointer to zero. We do this by writing to 8
	   registers, each of which is a 32bit wide register, and set
	   them all to zero. 
	*/

	for (i = 0; i < 8; ++i) {
		hdsp_write(hdsp, i, 0);
		udelay(10);
	}
#endif
}

static inline void hdsp_start_audio(hdsp_t *s)
{
	s->control_register |= (HDSP_AudioInterruptEnable | HDSP_Start);
	hdsp_write(s, HDSP_controlRegister, s->control_register);
}

static inline void hdsp_stop_audio(hdsp_t *s)
{
	s->control_register &= ~(HDSP_Start | HDSP_AudioInterruptEnable);
	hdsp_write(s, HDSP_controlRegister, s->control_register);
}

static inline void hdsp_silence_playback(hdsp_t *hdsp)
{
	memset(hdsp->playback_buffer, 0, HDSP_DMA_AREA_BYTES);
}

static int hdsp_set_interrupt_interval(hdsp_t *s, unsigned int frames)
{
	int n;

	spin_lock_irq(&s->lock);

	frames >>= 7;
	n = 0;
	while (frames) {
		n++;
		frames >>= 1;
	}

	s->control_register &= ~HDSP_LatencyMask;
	s->control_register |= hdsp_encode_latency(n);

	hdsp_write(s, HDSP_controlRegister, s->control_register);

	hdsp_compute_period_size(s);

	spin_unlock_irq(&s->lock);

	return 0;
}

static int hdsp_set_rate(hdsp_t *hdsp, int rate)
{
	int reject_if_open = 0;
	int current_rate;

	if (!(hdsp->control_register & HDSP_ClockModeMaster)) {
		snd_printk ("device is not running as a clock master: cannot set sample rate.\n");
		return -1;
	}

	/* Changing from a "single speed" to a "double speed" rate is
	   not allowed if any substreams are open. This is because
	   such a change causes a shift in the location of 
	   the DMA buffers and a reduction in the number of available
	   buffers. 

	   Note that a similar but essentially insoluble problem
	   exists for externally-driven rate changes. All we can do
	   is to flag rate changes in the read/write routines.
	 */

	spin_lock_irq(&hdsp->lock);
	current_rate = hdsp_system_sample_rate(hdsp);

	switch (rate) {
	case 32000:
		if (current_rate > 48000) {
			reject_if_open = 1;
		}
		rate = HDSP_Frequency32KHz;
		break;
	case 44100:
		if (current_rate > 48000) {
			reject_if_open = 1;
		}
		rate = HDSP_Frequency44_1KHz;
		break;
	case 48000:
		if (current_rate > 48000) {
			reject_if_open = 1;
		}
		rate = HDSP_Frequency48KHz;
		break;
	case 64000:
		if (current_rate < 48000) {
			reject_if_open = 1;
		}
		rate = HDSP_Frequency64KHz;
		break;
	case 88200:
		if (current_rate < 48000) {
			reject_if_open = 1;
		}
		rate = HDSP_Frequency88_2KHz;
		break;
	case 96000:
		if (current_rate < 48000) {
			reject_if_open = 1;
		}
		rate = HDSP_Frequency96KHz;
		break;
	default:
		spin_unlock_irq(&hdsp->lock);
		return -EINVAL;
	}

	if (reject_if_open && (hdsp->capture_pid >= 0 || hdsp->playback_pid >= 0)) {
		snd_printk ("cannot change between single- and double-speed mode (capture PID = %d, playback PID = %d)\n",
			    hdsp->capture_pid,
			    hdsp->playback_pid);
		spin_unlock_irq(&hdsp->lock);
		return -EBUSY;
	}

	hdsp->control_register &= ~HDSP_FrequencyMask;
	hdsp->control_register |= rate;
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);

	if (rate > 48000) {
		hdsp->channel_map = channel_map_ds;
	} else {
		switch (hdsp->type) {
		case Multiface:
			hdsp->channel_map = channel_map_mf_ss;
			break;
		case Digiface:
			hdsp->channel_map = channel_map_df_ss;
			break;
		}
	}

	spin_unlock_irq(&hdsp->lock);
	return 0;
}

static void hdsp_set_thru(hdsp_t *hdsp, int channel, int enable)
{

	hdsp->passthru = 0;

	if (channel < 0) {

		int i;

		/* set thru for all channels */

		if (enable) {
			for (i = 0; i < 26; i++) {
				hdsp_write_gain (hdsp, INPUT_TO_OUTPUT_KEY(i,i), UNITY_GAIN);
			}
		} else {
			for (i = 0; i < 26; i++) {
				hdsp_write_gain (hdsp, INPUT_TO_OUTPUT_KEY(i,i), MINUS_INFINITY_GAIN);
			}
		}

	} else {
		int mapped_channel;

		snd_assert(channel < HDSP_MAX_CHANNELS, return);

		mapped_channel = hdsp->channel_map[channel];

		snd_assert(mapped_channel > -1, return);

		if (enable) {
			hdsp_write_gain (hdsp, INPUT_TO_OUTPUT_KEY(mapped_channel,mapped_channel), UNITY_GAIN);
		} else {
			hdsp_write_gain (hdsp, INPUT_TO_OUTPUT_KEY(mapped_channel,mapped_channel), MINUS_INFINITY_GAIN);
		}
	}
}

static int hdsp_set_passthru(hdsp_t *hdsp, int onoff)
{
	if (onoff) {
		hdsp_set_thru(hdsp, -1, 1);
		hdsp_reset_hw_pointer(hdsp);
		hdsp_silence_playback(hdsp);

		/* we don't want interrupts, so do a
		   custom version of hdsp_start_audio().
		*/

		hdsp->control_register |= (HDSP_Start|HDSP_AudioInterruptEnable|hdsp_encode_latency(7));

		hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
		hdsp->passthru = 1;
	} else {
		hdsp_set_thru(hdsp, -1, 0);
		hdsp_stop_audio(hdsp);		
		hdsp->passthru = 0;
	}

	return 0;
}

/*----------------------------------------------------------------------------
   MIDI
  ----------------------------------------------------------------------------*/

static inline unsigned char snd_hdsp_midi_read_byte (hdsp_t *hdsp, int id)
{
	/* the hardware already does the relevant bit-mask with 0xff */
	if (id) {
		return hdsp_read(hdsp, HDSP_midiDataIn1);
	} else {
		return hdsp_read(hdsp, HDSP_midiDataIn0);
	}
}

static inline void snd_hdsp_midi_write_byte (hdsp_t *hdsp, int id, int val)
{
	/* the hardware already does the relevant bit-mask with 0xff */
	if (id) {
		return hdsp_write(hdsp, HDSP_midiDataOut1, val);
	} else {
		return hdsp_write(hdsp, HDSP_midiDataOut0, val);
	}
}

static inline int snd_hdsp_midi_input_available (hdsp_t *hdsp, int id)
{
	if (id) {
		return (hdsp_read(hdsp, HDSP_midiStatusIn1) & 0xff);
	} else {
		return (hdsp_read(hdsp, HDSP_midiStatusIn0) & 0xff);
	}
}

static inline int snd_hdsp_midi_output_possible (hdsp_t *hdsp, int id)
{
	int fifo_bytes_used;

	if (id) {
		fifo_bytes_used = hdsp_read(hdsp, HDSP_midiStatusOut1) & 0xff;
	} else {
		fifo_bytes_used = hdsp_read(hdsp, HDSP_midiStatusOut0) & 0xff;
	}

	if (fifo_bytes_used < 128) {
		return  128 - fifo_bytes_used;
	} else {
		return 0;
	}
}

static inline void snd_hdsp_flush_midi_input (hdsp_t *hdsp, int id)
{
	while (snd_hdsp_midi_input_available (hdsp, id)) {
		snd_hdsp_midi_read_byte (hdsp, id);
	}
}

static int snd_hdsp_midi_output_write (hdsp_midi_t *hmidi)
{
	unsigned long flags;
	int n_pending;
	int to_write;
	int i;
	unsigned char buf[128];

	/* Output is not interrupt driven */
		
	spin_lock_irqsave (&hmidi->lock, flags);

	if (hmidi->output) {
		if (!snd_rawmidi_transmit_empty (hmidi->output)) {
			if ((n_pending = snd_hdsp_midi_output_possible (hmidi->hdsp, hmidi->id)) > 0) {
				if (n_pending > (int)sizeof (buf))
					n_pending = sizeof (buf);
				
				if ((to_write = snd_rawmidi_transmit (hmidi->output, buf, n_pending)) > 0) {
					for (i = 0; i < to_write; ++i) 
						snd_hdsp_midi_write_byte (hmidi->hdsp, hmidi->id, buf[i]);
				}
			}
		}
	}

	spin_unlock_irqrestore (&hmidi->lock, flags);
	return 0;
}

static int snd_hdsp_midi_input_read (hdsp_midi_t *hmidi)
{
	unsigned char buf[128]; /* this buffer is designed to match the MIDI input FIFO size */
	unsigned long flags;
	int n_pending;
	int i;

	spin_lock_irqsave (&hmidi->lock, flags);

	if ((n_pending = snd_hdsp_midi_input_available (hmidi->hdsp, hmidi->id)) > 0) {
		if (hmidi->input) {
			if (n_pending > (int)sizeof (buf)) {
				n_pending = sizeof (buf);
			}
			for (i = 0; i < n_pending; ++i) {
				buf[i] = snd_hdsp_midi_read_byte (hmidi->hdsp, hmidi->id);
			}
			if (n_pending) {
				snd_rawmidi_receive (hmidi->input, buf, n_pending);
			}
		} else {
			/* flush the MIDI input FIFO */
			while (--n_pending) {
				snd_hdsp_midi_read_byte (hmidi->hdsp, hmidi->id);
			}
		}
	} 
	spin_unlock_irqrestore (&hmidi->lock, flags);
	return snd_hdsp_midi_output_write (hmidi);
}

static void snd_hdsp_midi_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	hdsp_t *hdsp;
	hdsp_midi_t *hmidi;
	unsigned long flags;

	hmidi = (hdsp_midi_t *) substream->rmidi->private_data;
	hdsp = hmidi->hdsp;
	spin_lock_irqsave (&hdsp->lock, flags);
	if (up) {
		snd_hdsp_flush_midi_input (hdsp, hmidi->id);
		if (hmidi->id) 
			hdsp->control_register |= HDSP_Midi1InterruptEnable;
		else 
			hdsp->control_register |= HDSP_Midi0InterruptEnable;
	} else {
		if (hmidi->id) 
			hdsp->control_register &= ~HDSP_Midi1InterruptEnable;
		else 
			hdsp->control_register &= ~HDSP_Midi0InterruptEnable;
	}

	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	spin_unlock_irqrestore (&hdsp->lock, flags);
}

static void snd_hdsp_midi_output_timer(unsigned long data)
{
	hdsp_midi_t *hmidi = (hdsp_midi_t *) data;
	unsigned long flags;
	
	snd_hdsp_midi_output_write(hmidi);
	spin_lock_irqsave (&hmidi->lock, flags);

	/* this does not bump hmidi->istimer, because the
	   kernel automatically removed the timer when it
	   expired, and we are now adding it back, thus
	   leaving istimer wherever it was set before.  
	*/

	if (hmidi->istimer) {
		hmidi->timer.expires = 1 + jiffies;
		add_timer(&hmidi->timer);
	}

	spin_unlock_irqrestore (&hmidi->lock, flags);
}

static void snd_hdsp_midi_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	hdsp_midi_t *hmidi;
	unsigned long flags;

	hmidi = (hdsp_midi_t *) substream->rmidi->private_data;
	spin_lock_irqsave (&hmidi->lock, flags);
	if (up) {
		if (!hmidi->istimer) {
			init_timer(&hmidi->timer);
			hmidi->timer.function = snd_hdsp_midi_output_timer;
			hmidi->timer.data = (unsigned long) hmidi;
			hmidi->timer.expires = 1 + jiffies;
			add_timer(&hmidi->timer);
			hmidi->istimer++;
		}
	} else {
		if (hmidi->istimer && --hmidi->istimer <= 0) {
			del_timer (&hmidi->timer);
		}
	}
	spin_unlock_irqrestore (&hmidi->lock, flags);
	if (up)
		snd_hdsp_midi_output_write(hmidi);
}

static int snd_hdsp_midi_input_open(snd_rawmidi_substream_t * substream)
{
	hdsp_midi_t *hmidi;
	unsigned long flags;

	hmidi = (hdsp_midi_t *) substream->rmidi->private_data;
	spin_lock_irqsave (&hmidi->lock, flags);
	snd_hdsp_flush_midi_input (hmidi->hdsp, hmidi->id);
	hmidi->input = substream;
	spin_unlock_irqrestore (&hmidi->lock, flags);

	return 0;
}

static int snd_hdsp_midi_output_open(snd_rawmidi_substream_t * substream)
{
	hdsp_midi_t *hmidi;
	unsigned long flags;

	hmidi = (hdsp_midi_t *) substream->rmidi->private_data;
	spin_lock_irqsave (&hmidi->lock, flags);
	hmidi->output = substream;
	spin_unlock_irqrestore (&hmidi->lock, flags);

	return 0;
}

static int snd_hdsp_midi_input_close(snd_rawmidi_substream_t * substream)
{
	hdsp_midi_t *hmidi;
	unsigned long flags;

	snd_hdsp_midi_input_trigger (substream, 0);

	hmidi = (hdsp_midi_t *) substream->rmidi->private_data;
	spin_lock_irqsave (&hmidi->lock, flags);
	hmidi->input = NULL;
	spin_unlock_irqrestore (&hmidi->lock, flags);

	return 0;
}

static int snd_hdsp_midi_output_close(snd_rawmidi_substream_t * substream)
{
	hdsp_midi_t *hmidi;
	unsigned long flags;

	snd_hdsp_midi_output_trigger (substream, 0);

	hmidi = (hdsp_midi_t *) substream->rmidi->private_data;
	spin_lock_irqsave (&hmidi->lock, flags);
	hmidi->output = NULL;
	spin_unlock_irqrestore (&hmidi->lock, flags);

	return 0;
}

snd_rawmidi_ops_t snd_hdsp_midi_output =
{
	.open =		snd_hdsp_midi_output_open,
	.close =	snd_hdsp_midi_output_close,
	.trigger =	snd_hdsp_midi_output_trigger,
};

snd_rawmidi_ops_t snd_hdsp_midi_input =
{
	.open =		snd_hdsp_midi_input_open,
	.close =	snd_hdsp_midi_input_close,
	.trigger =	snd_hdsp_midi_input_trigger,
};

static int __devinit snd_hdsp_create_midi (snd_card_t *card, hdsp_t *hdsp, int id)
{
	char buf[32];

	hdsp->midi[id].id = id;
	hdsp->midi[id].rmidi = NULL;
	hdsp->midi[id].input = NULL;
	hdsp->midi[id].output = NULL;
	hdsp->midi[id].hdsp = hdsp;
	hdsp->midi[id].istimer = 0;
	spin_lock_init (&hdsp->midi[id].lock);

	sprintf (buf, "%s MIDI %d", card->shortname, id+1);
	if (snd_rawmidi_new (card, buf, id, 1, 1, &hdsp->midi[id].rmidi) < 0) {
		return -1;
	}

	sprintf (hdsp->midi[id].rmidi->name, "%s MIDI %d", card->id, id+1);
	hdsp->midi[id].rmidi->private_data = &hdsp->midi[id];

	snd_rawmidi_set_ops (hdsp->midi[id].rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_hdsp_midi_output);
	snd_rawmidi_set_ops (hdsp->midi[id].rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_hdsp_midi_input);

	hdsp->midi[id].rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
		SNDRV_RAWMIDI_INFO_INPUT |
		SNDRV_RAWMIDI_INFO_DUPLEX;

	return 0;
}

/*-----------------------------------------------------------------------------
  Control Interface
  ----------------------------------------------------------------------------*/

static u32 snd_hdsp_convert_from_aes(snd_aes_iec958_t *aes)
{
	u32 val = 0;
	val |= (aes->status[0] & IEC958_AES0_PROFESSIONAL) ? HDSP_SPDIFProfessional : 0;
	val |= (aes->status[0] & IEC958_AES0_NONAUDIO) ? HDSP_SPDIFNonAudio : 0;
	if (val & HDSP_SPDIFProfessional)
		val |= (aes->status[0] & IEC958_AES0_PRO_EMPHASIS_5015) ? HDSP_SPDIFEmphasis : 0;
	else
		val |= (aes->status[0] & IEC958_AES0_CON_EMPHASIS_5015) ? HDSP_SPDIFEmphasis : 0;
	return val;
}

static void snd_hdsp_convert_to_aes(snd_aes_iec958_t *aes, u32 val)
{
	aes->status[0] = ((val & HDSP_SPDIFProfessional) ? IEC958_AES0_PROFESSIONAL : 0) |
			 ((val & HDSP_SPDIFNonAudio) ? IEC958_AES0_NONAUDIO : 0);
	if (val & HDSP_SPDIFProfessional)
		aes->status[0] |= (val & HDSP_SPDIFEmphasis) ? IEC958_AES0_PRO_EMPHASIS_5015 : 0;
	else
		aes->status[0] |= (val & HDSP_SPDIFEmphasis) ? IEC958_AES0_CON_EMPHASIS_5015 : 0;
}

static int snd_hdsp_control_spdif_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_hdsp_control_spdif_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	
	snd_hdsp_convert_to_aes(&ucontrol->value.iec958, hdsp->creg_spdif);
	return 0;
}

static int snd_hdsp_control_spdif_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	u32 val;
	
	val = snd_hdsp_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irqsave(&hdsp->lock, flags);
	change = val != hdsp->creg_spdif;
	hdsp->creg_spdif = val;
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return change;
}

static int snd_hdsp_control_spdif_stream_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_hdsp_control_spdif_stream_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	
	snd_hdsp_convert_to_aes(&ucontrol->value.iec958, hdsp->creg_spdif_stream);
	return 0;
}

static int snd_hdsp_control_spdif_stream_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	u32 val;
	
	val = snd_hdsp_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irqsave(&hdsp->lock, flags);
	change = val != hdsp->creg_spdif_stream;
	hdsp->creg_spdif_stream = val;
	hdsp->control_register &= ~(HDSP_SPDIFProfessional | HDSP_SPDIFNonAudio | HDSP_SPDIFEmphasis);
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register |= val);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return change;
}

static int snd_hdsp_control_spdif_mask_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_hdsp_control_spdif_mask_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ucontrol->value.iec958.status[0] = kcontrol->private_value;
	return 0;
}

#define HDSP_SPDIF_IN(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_spdif_in, \
  .get = snd_hdsp_get_spdif_in, .put = snd_hdsp_put_spdif_in }

static unsigned int hdsp_spdif_in(hdsp_t *hdsp)
{
	return hdsp_decode_spdif_in(hdsp->control_register & HDSP_SPDIFInputMask);
}

static int hdsp_set_spdif_input(hdsp_t *hdsp, int in)
{
	hdsp->control_register &= ~HDSP_SPDIFInputMask;
	hdsp->control_register |= hdsp_encode_spdif_in(in);
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_info_spdif_in(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[3] = {"ADAT1", "Coaxial", "Internal"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_spdif_in(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&hdsp->lock, flags);
	ucontrol->value.enumerated.item[0] = hdsp_spdif_in(hdsp);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return 0;
}

static int snd_hdsp_put_spdif_in(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned int val;
	
	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0] % 3;
	spin_lock_irqsave(&hdsp->lock, flags);
	change = val != hdsp_spdif_in(hdsp);
	if (change)
		hdsp_set_spdif_input(hdsp, val);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return change;
}

#define HDSP_SPDIF_OUT(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_spdif_out, \
  .get = snd_hdsp_get_spdif_out, .put = snd_hdsp_put_spdif_out }

static int hdsp_spdif_out(hdsp_t *hdsp)
{
	return (hdsp->control_register & HDSP_SPDIFOpticalOut) ? 1 : 0;
}

static int hdsp_set_spdif_output(hdsp_t *hdsp, int out)
{
	if (out) {
		hdsp->control_register |= HDSP_SPDIFOpticalOut;
	} else {
		hdsp->control_register &= ~HDSP_SPDIFOpticalOut;
	}
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_info_spdif_out(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_hdsp_get_spdif_out(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&hdsp->lock, flags);
	ucontrol->value.integer.value[0] = hdsp_spdif_out(hdsp);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return 0;
}

static int snd_hdsp_put_spdif_out(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned int val;
	
	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irqsave(&hdsp->lock, flags);
	change = (int)val != hdsp_spdif_out(hdsp);
	hdsp_set_spdif_output(hdsp, val);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return change;
}

#define HDSP_SYNC_PREF(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_sync_pref, \
  .get = snd_hdsp_get_sync_pref, .put = snd_hdsp_put_sync_pref }

static int hdsp_sync_pref(hdsp_t *hdsp)
{
	/* Notice that this looks at the requested sync source,
	   not the one actually in use.
	*/
	
	if (hdsp->control_register & HDSP_ClockModeMaster) {
		return HDSP_SYNC_FROM_SELF;
	}

	switch (hdsp->control_register & HDSP_SyncRefMask) {
	case HDSP_SyncRef_ADAT1:
		return HDSP_SYNC_FROM_ADAT1;
	case HDSP_SyncRef_ADAT2:
		return HDSP_SYNC_FROM_ADAT2;
	case HDSP_SyncRef_ADAT3:
		return HDSP_SYNC_FROM_ADAT3;
	case HDSP_SyncRef_SPDIF:
		return HDSP_SYNC_FROM_SPDIF;
	case HDSP_SyncRef_WORD:
		return HDSP_SYNC_FROM_WORD;
	case HDSP_SyncRef_ADAT_SYNC:
		return HDSP_SYNC_FROM_ADAT_SYNC;
	default:
		return HDSP_SYNC_FROM_SELF;
	}
	return 0;
}

static int hdsp_set_sync_pref(hdsp_t *hdsp, int pref)
{
	hdsp->control_register &= ~HDSP_SyncRefMask;
	switch (pref) {
	case HDSP_SYNC_FROM_ADAT1:
		hdsp->control_register &= ~HDSP_ClockModeMaster;
		hdsp->control_register &= ~HDSP_SyncRefMask; /* clear SyncRef bits */
		break;
	case HDSP_SYNC_FROM_ADAT2:
		hdsp->control_register &= ~HDSP_ClockModeMaster;
		hdsp->control_register |= HDSP_SyncRef_ADAT2;
		break;
	case HDSP_SYNC_FROM_ADAT3:
		hdsp->control_register &= ~HDSP_ClockModeMaster;
		hdsp->control_register |= HDSP_SyncRef_ADAT3;
		break;
	case HDSP_SYNC_FROM_SPDIF:
		hdsp->control_register &= ~HDSP_ClockModeMaster;
		hdsp->control_register |= HDSP_SyncRef_SPDIF;
		break;
	case HDSP_SYNC_FROM_WORD:
		hdsp->control_register &= ~HDSP_ClockModeMaster;
		hdsp->control_register |= HDSP_SyncRef_WORD;
		break;
	case HDSP_SYNC_FROM_ADAT_SYNC:
		hdsp->control_register &= ~HDSP_ClockModeMaster;
		hdsp->control_register |= HDSP_SyncRef_ADAT_SYNC;
		break;
	case HDSP_SYNC_FROM_SELF:
		hdsp->control_register |= HDSP_ClockModeMaster;
		break;
	default:
		return -1;
	}
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_info_sync_pref(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[] = {"Internal", "Word", "ADAT Sync", "IEC958", "ADAT1", "ADAT2", "ADAT3" };
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = (hdsp->type == Digiface) ? 7 : 6;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_hdsp_get_sync_pref(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&hdsp->lock, flags);
	ucontrol->value.enumerated.item[0] = hdsp_sync_pref(hdsp);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return 0;
}

static int snd_hdsp_put_sync_pref(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change, max;
	unsigned int val;
	
	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	max = hdsp->ss_channels == (hdsp->type == Digiface) ? 7 : 6;
	val = ucontrol->value.enumerated.item[0] % max;
	spin_lock_irqsave(&hdsp->lock, flags);
	change = (int)val != hdsp_sync_pref(hdsp);
	hdsp_set_sync_pref(hdsp, val);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return change;
}

#define HDSP_PASSTHRU(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_passthru, \
  .put = snd_hdsp_put_passthru, \
  .get = snd_hdsp_get_passthru }

static int snd_hdsp_info_passthru(snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_hdsp_get_passthru(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&hdsp->lock, flags);
	ucontrol->value.integer.value[0] = hdsp->passthru;
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return 0;
}

static int snd_hdsp_put_passthru(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned int val;
	int err = 0;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;

	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irqsave(&hdsp->lock, flags);
	change = (ucontrol->value.integer.value[0] != hdsp->passthru);
	if (change)
		err = hdsp_set_passthru(hdsp, val);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return err ? err : change;
}

#define HDSP_LINE_OUT(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_line_out, \
  .get = snd_hdsp_get_line_out, .put = snd_hdsp_put_line_out }

static int hdsp_line_out(hdsp_t *hdsp)
{
	return (hdsp->control_register & HDSP_LineOut) ? 1 : 0;
}

static int hdsp_set_line_output(hdsp_t *hdsp, int out)
{
	if (out) {
		hdsp->control_register |= HDSP_LineOut;
	} else {
		hdsp->control_register &= ~HDSP_LineOut;
	}
	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	return 0;
}

static int snd_hdsp_info_line_out(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_hdsp_get_line_out(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&hdsp->lock, flags);
	ucontrol->value.integer.value[0] = hdsp_line_out(hdsp);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return 0;
}

static int snd_hdsp_put_line_out(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned int val;
	
	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irqsave(&hdsp->lock, flags);
	change = (int)val != hdsp_line_out(hdsp);
	hdsp_set_line_output(hdsp, val);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return change;
}

#define HDSP_MIXER(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, .name = xname, .index = xindex, \
  .info = snd_hdsp_info_mixer, \
  .get = snd_hdsp_get_mixer, .put = snd_hdsp_put_mixer }

static int snd_hdsp_info_mixer(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 3;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 65536;
	uinfo->value.integer.step = 1;
	return 0;
}

static int snd_hdsp_get_mixer(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int source;
	int destination;
	int addr;

	source = ucontrol->value.integer.value[0];
	destination = ucontrol->value.integer.value[1];

	if (source > 25) {
		addr = PLAYBACK_TO_OUTPUT_KEY(source-26,destination);
	} else {
		addr = INPUT_TO_OUTPUT_KEY(source, destination);
	}
	
	spin_lock_irqsave(&hdsp->lock, flags);
	ucontrol->value.integer.value[0] = hdsp_read_gain (hdsp, addr);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return 0;
}

static int snd_hdsp_put_mixer(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	int source;
	int destination;
	int gain;
	int addr;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;

	source = ucontrol->value.integer.value[0];
	destination = ucontrol->value.integer.value[1];

	if (source > 25) {
		addr = PLAYBACK_TO_OUTPUT_KEY(source-26, destination);
	} else {
		addr = INPUT_TO_OUTPUT_KEY(source, destination);
	}

	gain = ucontrol->value.integer.value[2];

	spin_lock_irqsave(&hdsp->lock, flags);
	change = gain != hdsp_read_gain(hdsp, addr);
	if (change)
		hdsp_write_gain(hdsp, addr, gain);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return change;
}

/* The simple mixer control(s) provide gain control for the
   basic 1:1 mappings of playback streams to output
   streams. 
*/

#define HDSP_PLAYBACK_MIXER \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE | \
		 SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_playback_mixer, \
  .get = snd_hdsp_get_playback_mixer, .put = snd_hdsp_put_playback_mixer }

static int snd_hdsp_info_playback_mixer(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 65536;
	uinfo->value.integer.step = 1;
	return 0;
}

static int snd_hdsp_get_playback_mixer(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr;
	int channel;
	int mapped_channel;

	channel = ucontrol->id.index - 1;

        snd_assert(channel >= 0 || channel < HDSP_MAX_CHANNELS, return -EINVAL);
        
	if ((mapped_channel = hdsp->channel_map[channel]) < 0) {
		return -EINVAL;
	}

	addr = PLAYBACK_TO_OUTPUT_KEY(mapped_channel, mapped_channel);

	spin_lock_irqsave(&hdsp->lock, flags);
	ucontrol->value.integer.value[0] = hdsp_read_gain (hdsp, addr);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return 0;
}

static int snd_hdsp_put_playback_mixer(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	int addr;
	int channel;
	int mapped_channel;
	int gain;

	if (!snd_hdsp_use_is_exclusive(hdsp))
		return -EBUSY;
	
	channel = ucontrol->id.index - 1;

        snd_assert(channel >= 0 || channel < HDSP_MAX_CHANNELS, return -EINVAL);
        
	if ((mapped_channel = hdsp->channel_map[channel]) < 0) {
		return -EINVAL;
	}

	addr = PLAYBACK_TO_OUTPUT_KEY(mapped_channel, mapped_channel);
	gain = ucontrol->value.integer.value[0];


	spin_lock_irqsave(&hdsp->lock, flags);
	change = gain != hdsp_read_gain(hdsp, addr);
	if (change)
		hdsp_write_gain(hdsp, addr, gain);
	spin_unlock_irqrestore(&hdsp->lock, flags);
	return change;
}

#define HDSP_PEAK_PLAYBACK \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_peak_playback, \
  .get = snd_hdsp_get_peak_playback \
}

static int snd_hdsp_info_peak_playback(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	return 0;
}

static int snd_hdsp_get_peak_playback(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned int peakval = hdsp_read (hdsp, HDSP_playbackPeakLevel + (4 * (ucontrol->id.index-1)));
	ucontrol->value.integer.value[0] = peakval & 0xffffff00;  /* peak */
	ucontrol->value.integer.value[1] = peakval & 0xf;         /* overs */
	return 0;
}

#define HDSP_PEAK_INPUT \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_peak_input, \
  .get = snd_hdsp_get_peak_input \
}

static int snd_hdsp_info_peak_input(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	return 0;
}

static int snd_hdsp_get_peak_input(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned int peakval = hdsp_read (hdsp, HDSP_inputPeakLevel + (4 * (ucontrol->id.index-1)));
	ucontrol->value.integer.value[0] = peakval & 0xffffff00;  /* peak */
	ucontrol->value.integer.value[1] = peakval & 0xf;         /* overs */
	return 0;
}

#define HDSP_PEAK_OUTPUT \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_peak_output, \
  .get = snd_hdsp_get_peak_output \
}

static int snd_hdsp_info_peak_output(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	return 0;
}

static int snd_hdsp_get_peak_output(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	unsigned int peakval = hdsp_read (hdsp, HDSP_outputPeakLevel + (4 * (ucontrol->id.index-1)));
	ucontrol->value.integer.value[0] = peakval & 0xffffff00;  /* peak */
	ucontrol->value.integer.value[1] = peakval & 0xf;         /* overs */
	return 0;
}

#define HDSP_RMS_INPUT \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_rms_input, \
  .get = snd_hdsp_get_rms_input \
}

static int snd_hdsp_info_rms_input(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER64;
	uinfo->count = 1;
	return 0;
}

static int snd_hdsp_get_rms_input(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer64.value[0] = hdsp_read64 (hdsp, HDSP_inputRmsLevel + (8 * (ucontrol->id.index-1)));
	return 0;
}

#define HDSP_RMS_PLAYBACK \
{ .iface = SNDRV_CTL_ELEM_IFACE_PCM, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_hdsp_info_rms_playback, \
  .get = snd_hdsp_get_rms_playback \
}

static int snd_hdsp_info_rms_playback(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER64;
	uinfo->count = 1;
	return 0;
}

static int snd_hdsp_get_rms_playback(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	hdsp_t *hdsp = _snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer64.value[0] = hdsp_read64 (hdsp, HDSP_playbackRmsLevel + (8 * (ucontrol->id.index-1)));
	return 0;
}

static snd_kcontrol_new_t snd_hdsp_controls[] = {
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =		snd_hdsp_control_spdif_info,
	.get =		snd_hdsp_control_spdif_get,
	.put =		snd_hdsp_control_spdif_put,
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	.info =		snd_hdsp_control_spdif_stream_info,
	.get =		snd_hdsp_control_spdif_stream_get,
	.put =		snd_hdsp_control_spdif_stream_put,
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	.info =		snd_hdsp_control_spdif_mask_info,
	.get =		snd_hdsp_control_spdif_mask_get,
	.private_value = IEC958_AES0_NONAUDIO |
			IEC958_AES0_PROFESSIONAL |
			IEC958_AES0_CON_EMPHASIS,	                                                                                      
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
	.info =		snd_hdsp_control_spdif_mask_info,
	.get =		snd_hdsp_control_spdif_mask_get,
	.private_value = IEC958_AES0_NONAUDIO |
			IEC958_AES0_PROFESSIONAL |
			IEC958_AES0_PRO_EMPHASIS,
},
HDSP_MIXER("Mixer", 0),
HDSP_SPDIF_IN("IEC958 Input Connector", 0),
HDSP_SPDIF_OUT("IEC958 Output also on ADAT1", 0),
HDSP_SYNC_PREF("Preferred Sync Source", 0),
HDSP_PASSTHRU("Passthru", 0),
HDSP_LINE_OUT("Line Out", 0),
};

#define HDSP_CONTROLS (sizeof(snd_hdsp_controls)/sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_hdsp_playback_mixer = HDSP_PLAYBACK_MIXER;
static snd_kcontrol_new_t snd_hdsp_input_peak = HDSP_PEAK_INPUT;
static snd_kcontrol_new_t snd_hdsp_output_peak = HDSP_PEAK_OUTPUT;
static snd_kcontrol_new_t snd_hdsp_playback_peak = HDSP_PEAK_PLAYBACK;
static snd_kcontrol_new_t snd_hdsp_input_rms = HDSP_RMS_INPUT;
static snd_kcontrol_new_t snd_hdsp_playback_rms = HDSP_RMS_PLAYBACK;

int snd_hdsp_create_controls(snd_card_t *card, hdsp_t *hdsp)
{
	unsigned int idx, limit;
	int err;
	snd_kcontrol_t *kctl;

	for (idx = 0; idx < HDSP_CONTROLS; idx++) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_hdsp_controls[idx], hdsp))) < 0)
			return err;
		if (idx == 1)	/* IEC958 (S/PDIF) Stream */
			hdsp->spdif_ctl = kctl;
	}

	if (hdsp->type == Digiface) {
		limit = DIGIFACE_SS_CHANNELS;
	} else {
		limit = MULTIFACE_SS_CHANNELS;
	}

	/* The index values are one greater than the channel ID so that alsamixer
	   will display them correctly. We want to use the index for fast lookup
	   of the relevant channel, but if we use it at all, most ALSA software
	   does the wrong thing with it ...
	*/

	snd_hdsp_playback_mixer.name = "Chn";
	snd_hdsp_input_peak.name = "Input Peak";
	snd_hdsp_output_peak.name = "Output Peak";
	snd_hdsp_playback_peak.name = "Playback Peak";
	snd_hdsp_playback_rms.name = "Playback RMS";
	snd_hdsp_input_rms.name = "Input RMS";

	for (idx = 0; idx < limit; ++idx) {
		snd_hdsp_playback_mixer.index = idx+1;
		if ((err = snd_ctl_add (card, kctl = snd_ctl_new1(&snd_hdsp_playback_mixer, hdsp)))) {
			return err;
		}
		snd_hdsp_input_peak.index = idx+1;
		if ((err = snd_ctl_add (card, kctl = snd_ctl_new1(&snd_hdsp_input_peak, hdsp)))) {
			return err;
		}
		snd_hdsp_output_peak.index = idx+1;
		if ((err = snd_ctl_add (card, kctl = snd_ctl_new1(&snd_hdsp_output_peak, hdsp)))) {
			return err;
		}
		snd_hdsp_playback_peak.index = idx+1;
		if ((err = snd_ctl_add (card, kctl = snd_ctl_new1(&snd_hdsp_playback_peak, hdsp)))) {
			return err;
		}
		snd_hdsp_playback_rms.index = idx+1;
		if ((err = snd_ctl_add (card, kctl = snd_ctl_new1(&snd_hdsp_playback_rms, hdsp)))) {
			return err;
		}
		snd_hdsp_input_rms.index = idx+1;
		if ((err = snd_ctl_add (card, kctl = snd_ctl_new1(&snd_hdsp_input_rms, hdsp)))) {
			return err;
		}
	}

	return 0;
}

/*------------------------------------------------------------
   /proc interface 
 ------------------------------------------------------------*/

static void
snd_hdsp_proc_read(snd_info_entry_t *entry, snd_info_buffer_t *buffer)
{
	hdsp_t *hdsp = (hdsp_t *) entry->private_data;
	unsigned int status;
	unsigned int status2;
	char *requested_sync_ref;
	int x;

	if (hdsp_check_for_iobox (hdsp)) {
		return;
	}

	status = hdsp_read(hdsp, HDSP_statusRegister);
	status2 = hdsp_read(hdsp, HDSP_status2Register);

	snd_iprintf(buffer, "%s (Card #%d)\n", hdsp->card_name, hdsp->card->number + 1);
	snd_iprintf(buffer, "Buffers: capture %p playback %p\n",
		    hdsp->capture_buffer, hdsp->playback_buffer);
	snd_iprintf(buffer, "IRQ: %d Registers bus: 0x%lx VM: 0x%lx\n",
		    hdsp->irq, hdsp->port, hdsp->iobase);
	snd_iprintf(buffer, "Control register: 0x%x\n", hdsp->control_register);
	snd_iprintf(buffer, "Status register: 0x%x\n", status);
	snd_iprintf(buffer, "Status2 register: 0x%x\n", status2);
	snd_iprintf(buffer, "FIFO status: %d\n", hdsp_read(hdsp, HDSP_fifoStatus) & 0xff);

	snd_iprintf(buffer, "MIDI1 Output status: 0x%x\n", hdsp_read(hdsp, HDSP_midiStatusOut0));
	snd_iprintf(buffer, "MIDI1 Input status: 0x%x\n", hdsp_read(hdsp, HDSP_midiStatusIn0));
	snd_iprintf(buffer, "MIDI2 Output status: 0x%x\n", hdsp_read(hdsp, HDSP_midiStatusOut1));
	snd_iprintf(buffer, "MIDI2 Input status: 0x%x\n", hdsp_read(hdsp, HDSP_midiStatusIn1));

	snd_iprintf(buffer, "\n");

	x = 1 << (6 + hdsp_decode_latency(hdsp->control_register & HDSP_LatencyMask));

	snd_iprintf(buffer, "Latency: %d samples (2 periods of %lu bytes)\n", x, (unsigned long) hdsp->period_bytes);
	snd_iprintf(buffer, "Hardware pointer (frames): %ld\n", hdsp_hw_pointer(hdsp));
	snd_iprintf(buffer, "Passthru: %s\n", hdsp->passthru ? "yes" : "no");
	snd_iprintf(buffer, "Line out: %s\n", (hdsp->control_register & HDSP_LineOut) ? "on" : "off");

	snd_iprintf(buffer, "Firmware version: %d\n", (status2&HDSP_version0)|(status2&HDSP_version1)<<1|(status2&HDSP_version2)<<2);

	switch (hdsp_sync_pref (hdsp)) {
	case HDSP_SYNC_FROM_WORD:
		requested_sync_ref = "Word";
		break;
	case HDSP_SYNC_FROM_ADAT_SYNC:
		requested_sync_ref = "ADAT Sync";
		break;
	case HDSP_SYNC_FROM_SPDIF:
		requested_sync_ref = "SPDIF";
		break;
	case HDSP_SYNC_FROM_ADAT1:
		requested_sync_ref = "ADAT1";
		break;
	case HDSP_SYNC_FROM_ADAT2:
		requested_sync_ref = "ADAT2";
		break;
	case HDSP_SYNC_FROM_ADAT3:
		requested_sync_ref = "ADAT3";
		break;
	case HDSP_SYNC_FROM_SELF:
	default:
		requested_sync_ref = "Master";
		break;
	}

	if ((hdsp->control_register & HDSP_ClockModeMaster)) {
		snd_iprintf (buffer, "Sync reference: %s/Master (chosen)\n", requested_sync_ref);
	} else if (hdsp_system_sample_rate(hdsp) == 0) {
		snd_iprintf (buffer, "Sync reference: %s/Master (forced)\n", requested_sync_ref);
	} else {
		switch (status2 & HDSP_SelSyncRefMask) {
		case HDSP_SelSyncRef_ADAT1:
			snd_iprintf (buffer, "Sync reference: %s/ADAT1\n", requested_sync_ref);
			break;
		case HDSP_SelSyncRef_ADAT2:
			snd_iprintf (buffer, "Sync reference: %s/ADAT2\n", requested_sync_ref);
			break;
		case HDSP_SelSyncRef_ADAT3:
			snd_iprintf (buffer, "Sync reference: %s/ADAT3\n", requested_sync_ref);
			break;
		case HDSP_SelSyncRef_SPDIF:
			snd_iprintf (buffer, "Sync reference: %s/SPDIF\n", requested_sync_ref);
			break;
		case HDSP_SelSyncRef_WORD:
			snd_iprintf (buffer, "Sync reference: %s/WORD\n", requested_sync_ref);
			break;
		case HDSP_SelSyncRef_ADAT_SYNC:
			snd_iprintf (buffer, "Sync reference: %s/ADAT Sync\n", requested_sync_ref);
			break;
		default:
			snd_iprintf (buffer, "Sync reference: %s/Master (fallback)\n", requested_sync_ref);
			break;
		}
	}
	snd_iprintf (buffer, "Sample rate: %d\n", hdsp_system_sample_rate(hdsp));

	snd_iprintf(buffer, "\n");

	switch ((hdsp->control_register & HDSP_SPDIFInputMask) >> 14) {
	case HDSP_SPDIFIN_OPTICAL:
		snd_iprintf(buffer, "IEC958 input: ADAT1\n");
		break;
	case HDSP_SPDIFIN_COAXIAL:
		snd_iprintf(buffer, "IEC958 input: Coaxial\n");
		break;
	case HDSP_SPDIFIN_INTERN:
		snd_iprintf(buffer, "IEC958 input: Internal\n");
		break;
	default:
		snd_iprintf(buffer, "IEC958 input: ???\n");
		break;
	}
	
	if (hdsp->control_register & HDSP_SPDIFOpticalOut) {
		snd_iprintf(buffer, "IEC958 output: Coaxial & ADAT1\n");
	} else {
		snd_iprintf(buffer, "IEC958 output: Coaxial only\n");
	}

	if (hdsp->control_register & HDSP_SPDIFProfessional) {
		snd_iprintf(buffer, "IEC958 quality: Professional\n");
	} else {
		snd_iprintf(buffer, "IEC958 quality: Consumer\n");
	}

	if (hdsp->control_register & HDSP_SPDIFEmphasis) {
		snd_iprintf(buffer, "IEC958 emphasis: on\n");
	} else {
		snd_iprintf(buffer, "IEC958 emphasis: off\n");
	}

	if (hdsp->control_register & HDSP_SPDIFNonAudio) {
		snd_iprintf(buffer, "IEC958 NonAudio: on\n");
	} else {
		snd_iprintf(buffer, "IEC958 NonAudio: off\n");
	}

	snd_iprintf(buffer, "\n");

	if ((x = hdsp_spdif_sample_rate (hdsp)) != 0) {
		snd_iprintf (buffer, "IEC958 sample rate: %d\n", x);
	} else {
		snd_iprintf (buffer, "IEC958 sample rate: Error flag set\n");
	}

	snd_iprintf(buffer, "\n");

	/* Sync Check */
	x = status & HDSP_Sync0;
	if (status & HDSP_Lock0) {
		snd_iprintf(buffer, "ADAT1: %s\n", x ? "Sync" : "Lock");
	} else {
		snd_iprintf(buffer, "ADAT1: No Lock\n");
	}

	x = status & HDSP_Sync1;
	if (status & HDSP_Lock1) {
		snd_iprintf(buffer, "ADAT2: %s\n", x ? "Sync" : "Lock");
	} else {
		snd_iprintf(buffer, "ADAT2: No Lock\n");
	}

	if (hdsp->type == Digiface) {
		x = status & HDSP_Sync2;
		if (status & HDSP_Lock2) {
			snd_iprintf(buffer, "ADAT3: %s\n", x ? "Sync" : "Lock");
		} else {
			snd_iprintf(buffer, "ADAT3: No Lock\n");
		}
	}

	snd_iprintf(buffer, "\n");

#if 0
	for (x = 0; x < 26; x++) {
		unsigned int val = hdsp_read (hdsp, HDSP_inputPeakLevel + (4 * x));
		snd_iprintf (buffer, "%d: input peak = %d overs = %d\n", x, val&0xffffff00, val&0xf);
	}
#endif
}

static void __devinit snd_hdsp_proc_init(hdsp_t *hdsp)
{
	snd_info_entry_t *entry;

	if (! snd_card_proc_new(hdsp->card, "hdsp", &entry))
		snd_info_set_text_ops(entry, hdsp, snd_hdsp_proc_read);
}

static void snd_hdsp_free_buffers(hdsp_t *hdsp)
{
	if (hdsp->capture_buffer_unaligned) {
#ifndef HDSP_PREALLOCATE_MEMORY
		snd_free_pci_pages(hdsp->pci,
				   HDSP_DMA_AREA_BYTES,
				   hdsp->capture_buffer_unaligned,
				   hdsp->capture_buffer_addr);
#else
		snd_hammerfall_free_buffer(hdsp->pci, hdsp->capture_buffer_unaligned);
#endif
	}

	if (hdsp->playback_buffer_unaligned) {
#ifndef HDSP_PREALLOCATE_MEMORY
		snd_free_pci_pages(hdsp->pci,
				   HDSP_DMA_AREA_BYTES,
				   hdsp->playback_buffer_unaligned,
				   hdsp->playback_buffer_addr);
#else
		snd_hammerfall_free_buffer(hdsp->pci, hdsp->playback_buffer_unaligned);
#endif
	}
}

static int __devinit snd_hdsp_initialize_memory(hdsp_t *hdsp)
{
	void *pb, *cb;
	dma_addr_t pb_addr, cb_addr;
	unsigned long pb_bus, cb_bus;

#ifndef HDSP_PREALLOCATE_MEMORY
	cb = snd_malloc_pci_pages(hdsp->pci, HDSP_DMA_AREA_BYTES, &cb_addr);
	pb = snd_malloc_pci_pages(hdsp->pci, HDSP_DMA_AREA_BYTES, &pb_addr);
#else
	cb = snd_hammerfall_get_buffer(hdsp->pci, &cb_addr);
	pb = snd_hammerfall_get_buffer(hdsp->pci, &pb_addr);
#endif

	if (cb == 0 || pb == 0) {
		if (cb) {
#ifdef HDSP_PREALLOCATE_MEMORY
			snd_hammerfall_free_buffer(hdsp->pci, cb);
#else
			snd_free_pci_pages(hdsp->pci, HDSP_DMA_AREA_BYTES, cb, cb_addr);
#endif
		}
		if (pb) {
#ifdef HDSP_PREALLOCATE_MEMORY
			snd_hammerfall_free_buffer(hdsp->pci, pb);
#else
			snd_free_pci_pages(hdsp->pci, HDSP_DMA_AREA_BYTES, pb, pb_addr);
#endif
		}

		printk(KERN_ERR "%s: no buffers available\n", hdsp->card_name);
		return -ENOMEM;
	}

	/* save raw addresses for use when freeing memory later */

	hdsp->capture_buffer_unaligned = cb;
	hdsp->playback_buffer_unaligned = pb;
	hdsp->capture_buffer_addr = cb_addr;
	hdsp->playback_buffer_addr = pb_addr;

	/* Align to bus-space 64K boundary */

	cb_bus = (cb_addr + 0xFFFF) & ~0xFFFFl;
	pb_bus = (pb_addr + 0xFFFF) & ~0xFFFFl;

	/* Tell the card where it is */

	hdsp_write(hdsp, HDSP_inputBufferAddress, cb_bus);
	hdsp_write(hdsp, HDSP_outputBufferAddress, pb_bus);

	hdsp->capture_buffer = cb + (cb_bus - cb_addr);
	hdsp->playback_buffer = pb + (pb_bus - pb_addr);

	return 0;
}

static void snd_hdsp_set_defaults(hdsp_t *hdsp)
{
	unsigned int i;

	/* ASSUMPTION: hdsp->lock is either held, or
	   there is no need to hold it (e.g. during module
	   initalization).
	 */

	/* set defaults:

	   SPDIF Input via Coax 
	   Master clock mode
	   maximum latency (7 => 2^7 = 8192 samples, 64Kbyte buffer,
	                    which implies 2 4096 sample, 32Kbyte periods).
           Enable line out.			    
	 */

	hdsp->control_register = HDSP_ClockModeMaster | 
		                 HDSP_SPDIFInputCoaxial | 
		                 hdsp_encode_latency(7) | 
		                 HDSP_LineOut;

	hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register);
	hdsp_reset_hw_pointer(hdsp);
	hdsp_compute_period_size(hdsp);

	/* silence everything */
	
	for (i = 0; i < HDSP_MATRIX_MIXER_SIZE; ++i) {
		hdsp->mixer_matrix[i] = MINUS_INFINITY_GAIN;
	}

	for (i = 0; i < 2048; i++)
		hdsp_write_gain (hdsp, i, MINUS_INFINITY_GAIN);

	if (line_outs_monitor[hdsp->dev]) {
		
		snd_printk ("sending all inputs and playback streams to line outs.\n");

		/* route all inputs to the line outs for easy monitoring. send
		   odd numbered channels to right, even to left.
		*/
		
		for (i = 0; i < HDSP_MAX_CHANNELS; i++) {
			if (i & 1) { 
				hdsp_write_gain (hdsp, INPUT_TO_OUTPUT_KEY (i, 26), UNITY_GAIN);
				hdsp_write_gain (hdsp, PLAYBACK_TO_OUTPUT_KEY (i, 26), UNITY_GAIN);
			} else {
				hdsp_write_gain (hdsp, INPUT_TO_OUTPUT_KEY (i, 27), UNITY_GAIN);
				hdsp_write_gain (hdsp, PLAYBACK_TO_OUTPUT_KEY (i, 27), UNITY_GAIN);
			}
		}
	}

	hdsp->passthru = 0;

	/* set a default rate so that the channel map is set up.
	 */

	hdsp_set_rate(hdsp, 48000);
}

void snd_hdsp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	hdsp_t *hdsp = (hdsp_t *) dev_id;
	unsigned int status;
	int audio;
	int midi0;
	int midi1;
	unsigned int midi0status;
	unsigned int midi1status;

	status = hdsp_read(hdsp, HDSP_statusRegister);

	audio = status & HDSP_audioIRQPending;
	midi0 = status & HDSP_midi0IRQPending;
	midi1 = status & HDSP_midi1IRQPending;

	if (!audio && !midi0 && !midi1) {
		return;
	}

	hdsp_write(hdsp, HDSP_interruptConfirmation, 0);

	midi0status = hdsp_read (hdsp, HDSP_midiStatusIn0) & 0xff;
	midi1status = hdsp_read (hdsp, HDSP_midiStatusIn1) & 0xff;

	if (audio) {
		if (hdsp->capture_substream) {
			snd_pcm_period_elapsed(hdsp->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream);
		}
		
		if (hdsp->playback_substream) {
			snd_pcm_period_elapsed(hdsp->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream);
		}
	}

	/* note: snd_hdsp_midi_input_read() calls output_write() because
	   output is not interrupt-driven ...
	*/

	if (midi0status)
		snd_hdsp_midi_input_read (&hdsp->midi[0]);
	if (midi1status)
		snd_hdsp_midi_input_read (&hdsp->midi[1]);
}

static snd_pcm_uframes_t snd_hdsp_hw_pointer(snd_pcm_substream_t *substream)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	return hdsp_hw_pointer(hdsp);
}

static char *hdsp_channel_buffer_location(hdsp_t *hdsp,
					     int stream,
					     int channel)

{
	int mapped_channel;

        snd_assert(channel >= 0 || channel < HDSP_MAX_CHANNELS, return NULL);
        
	if ((mapped_channel = hdsp->channel_map[channel]) < 0) {
		return NULL;
	}
	
	if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		return hdsp->capture_buffer + (mapped_channel * HDSP_CHANNEL_BUFFER_BYTES);
	} else {
		return hdsp->playback_buffer + (mapped_channel * HDSP_CHANNEL_BUFFER_BYTES);
	}
}

static int snd_hdsp_playback_copy(snd_pcm_substream_t *substream, int channel,
				  snd_pcm_uframes_t pos, void *src, snd_pcm_uframes_t count)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	char *channel_buf;

	snd_assert(pos + count <= HDSP_CHANNEL_BUFFER_BYTES / 4, return -EINVAL);

	channel_buf = hdsp_channel_buffer_location (hdsp, substream->pstr->stream, channel);
	snd_assert(channel_buf != NULL, return -EIO);
	if (copy_from_user(channel_buf + pos * 4, src, count * 4))
		return -EFAULT;
	return count;
}

static int snd_hdsp_capture_copy(snd_pcm_substream_t *substream, int channel,
				 snd_pcm_uframes_t pos, void *dst, snd_pcm_uframes_t count)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	char *channel_buf;

	snd_assert(pos + count <= HDSP_CHANNEL_BUFFER_BYTES / 4, return -EINVAL);

	channel_buf = hdsp_channel_buffer_location (hdsp, substream->pstr->stream, channel);
	snd_assert(channel_buf != NULL, return -EIO);
	if (copy_to_user(dst, channel_buf + pos * 4, count * 4))
		return -EFAULT;
	return count;
}

static int snd_hdsp_hw_silence(snd_pcm_substream_t *substream, int channel,
				  snd_pcm_uframes_t pos, snd_pcm_uframes_t count)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	char *channel_buf;

	channel_buf = hdsp_channel_buffer_location (hdsp, substream->pstr->stream, channel);
	snd_assert(channel_buf != NULL, return -EIO);
	memset(channel_buf + pos * 4, 0, count * 4);
	return count;
}

static int snd_hdsp_reset(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	snd_pcm_substream_t *other;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		other = hdsp->capture_substream;
	else
		other = hdsp->playback_substream;
	if (hdsp->running)
		runtime->status->hw_ptr = hdsp_hw_pointer(hdsp);
	else
		runtime->status->hw_ptr = 0;
	if (other) {
		snd_pcm_substream_t *s = substream;
		snd_pcm_runtime_t *oruntime = other->runtime;
		do {
			s = s->link_next;
			if (s == other) {
				oruntime->status->hw_ptr = runtime->status->hw_ptr;
				break;
			}
		} while (s != substream);
	}
	return 0;
}

static int snd_hdsp_hw_params(snd_pcm_substream_t *substream,
				 snd_pcm_hw_params_t *params)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	int err;
	pid_t this_pid;
	pid_t other_pid;

	if (hdsp_check_for_iobox (hdsp)) {
		return -EIO;
	}

	spin_lock_irq(&hdsp->lock);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		hdsp->control_register &= ~(HDSP_SPDIFProfessional | HDSP_SPDIFNonAudio | HDSP_SPDIFEmphasis);
		hdsp_write(hdsp, HDSP_controlRegister, hdsp->control_register |= hdsp->creg_spdif_stream);
		this_pid = hdsp->playback_pid;
		other_pid = hdsp->capture_pid;
	} else {
		this_pid = hdsp->capture_pid;
		other_pid = hdsp->playback_pid;
	}

	if ((other_pid > 0) && (this_pid != other_pid)) {

		/* The other stream is open, and not by the same
		   task as this one. Make sure that the parameters
		   that matter are the same.
		 */

		if ((int)params_rate(params) != hdsp_system_sample_rate(hdsp)) {
			spin_unlock_irq(&hdsp->lock);
			_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_RATE);
			return -EBUSY;
		}

		if (params_period_size(params) != hdsp->period_bytes / 4) {
			spin_unlock_irq(&hdsp->lock);
			_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
			return -EBUSY;
		}

		/* We're fine. */

		spin_unlock_irq(&hdsp->lock);
 		return 0;

	} else {
		spin_unlock_irq(&hdsp->lock);
	}

	/* how to make sure that the rate matches an externally-set one ?
	 */

	if ((err = hdsp_set_rate(hdsp, params_rate(params))) < 0) {
		_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_RATE);
		return err;
	}

	if ((err = hdsp_set_interrupt_interval(hdsp, params_period_size(params))) < 0) {
		_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
		return err;
	}

	return 0;
}

static int snd_hdsp_channel_info(snd_pcm_substream_t *substream,
				    snd_pcm_channel_info_t *info)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	int mapped_channel;

	snd_assert(info->channel < HDSP_MAX_CHANNELS, return -EINVAL);

	if ((mapped_channel = hdsp->channel_map[info->channel]) < 0) {
		return -EINVAL;
	}

	info->offset = mapped_channel * HDSP_CHANNEL_BUFFER_BYTES;
	info->first = 0;
	info->step = 32;
	return 0;
}

static int snd_hdsp_ioctl(snd_pcm_substream_t *substream,
			     unsigned int cmd, void *arg)
{
	switch (cmd) {
	case SNDRV_PCM_IOCTL1_RESET:
	{
		return snd_hdsp_reset(substream);
	}
	case SNDRV_PCM_IOCTL1_CHANNEL_INFO:
	{
		snd_pcm_channel_info_t *info = arg;
		return snd_hdsp_channel_info(substream, info);
	}
	default:
		break;
	}

	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_hdsp_trigger(snd_pcm_substream_t *substream, int cmd)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	snd_pcm_substream_t *other;
	int running;
	
	if (hdsp_check_for_iobox (hdsp)) {
		return -EIO;
	}

	spin_lock(&hdsp->lock);
	running = hdsp->running;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		running |= 1 << substream->stream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		running &= ~(1 << substream->stream);
		break;
	default:
		snd_BUG();
		spin_unlock(&hdsp->lock);
		return -EINVAL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		other = hdsp->capture_substream;
	else
		other = hdsp->playback_substream;

	if (other) {
		snd_pcm_substream_t *s = substream;
		do {
			s = s->link_next;
			if (s == other) {
				snd_pcm_trigger_done(s, substream);
				if (cmd == SNDRV_PCM_TRIGGER_START)
					running |= 1 << s->stream;
				else
					running &= ~(1 << s->stream);
				goto _ok;
			}
		} while (s != substream);
		if (cmd == SNDRV_PCM_TRIGGER_START) {
			if (!(running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) &&
			    substream->stream == SNDRV_PCM_STREAM_CAPTURE)
				hdsp_silence_playback(hdsp);
		} else {
			if (running &&
			    substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				hdsp_silence_playback(hdsp);
		}
	} else {
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
				hdsp_silence_playback(hdsp);
	}
 _ok:
	snd_pcm_trigger_done(substream, substream);
	if (!hdsp->running && running)
		hdsp_start_audio(hdsp);
	else if (hdsp->running && !running)
		hdsp_stop_audio(hdsp);
	hdsp->running = running;
	spin_unlock(&hdsp->lock);

	return 0;
}

static int snd_hdsp_prepare(snd_pcm_substream_t *substream)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	int result = 0;

	if (hdsp_check_for_iobox (hdsp)) {
		return -EIO;
	}

	spin_lock_irq(&hdsp->lock);
	if (!hdsp->running)
		hdsp_reset_hw_pointer(hdsp);
	spin_unlock_irq(&hdsp->lock);
	return result;
}

static snd_pcm_hardware_t snd_hdsp_playback_subinfo =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_DOUBLE),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		(SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_44100 | 
				 SNDRV_PCM_RATE_48000 | 
				 SNDRV_PCM_RATE_64000 | 
				 SNDRV_PCM_RATE_88200 | 
				 SNDRV_PCM_RATE_96000),
	.rate_min =		32000,
	.rate_max =		96000,
	.channels_min =		14,
	.channels_max =		HDSP_MAX_CHANNELS,
	.buffer_bytes_max =	HDSP_CHANNEL_BUFFER_BYTES * HDSP_MAX_CHANNELS,
	.period_bytes_min =	(64 * 4) *10,
	.period_bytes_max =	(8192 * 4) * HDSP_MAX_CHANNELS,
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

static snd_pcm_hardware_t snd_hdsp_capture_subinfo =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_SYNC_START),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		(SNDRV_PCM_RATE_32000 |
				 SNDRV_PCM_RATE_44100 | 
				 SNDRV_PCM_RATE_48000 | 
				 SNDRV_PCM_RATE_64000 | 
				 SNDRV_PCM_RATE_88200 | 
				 SNDRV_PCM_RATE_96000),
	.rate_min =		32000,
	.rate_max =		96000,
	.channels_min =		14,
	.channels_max =		HDSP_MAX_CHANNELS,
	.buffer_bytes_max =	HDSP_CHANNEL_BUFFER_BYTES * HDSP_MAX_CHANNELS,
	.period_bytes_min =	(64 * 4) * 10,
	.period_bytes_max =	(8192 * 4) * HDSP_MAX_CHANNELS,
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

static unsigned int period_sizes[] = { 64, 128, 256, 512, 1024, 2048, 4096, 8192 };

#define PERIOD_SIZES sizeof(period_sizes) / sizeof(period_sizes[0])

static snd_pcm_hw_constraint_list_t hw_constraints_period_sizes = {
	.count = PERIOD_SIZES,
	.list = period_sizes,
	.mask = 0
};

static int snd_hdsp_hw_rule_channels(snd_pcm_hw_params_t *params,
					snd_pcm_hw_rule_t *rule)
{
	hdsp_t *hdsp = rule->private;
	snd_interval_t *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	unsigned int list[2] = { hdsp->ds_channels, hdsp->ss_channels };
	return snd_interval_list(c, 2, list, 0);
}

static int snd_hdsp_hw_rule_channels_rate(snd_pcm_hw_params_t *params,
					     snd_pcm_hw_rule_t *rule)
{
	hdsp_t *hdsp = rule->private;
	snd_interval_t *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	snd_interval_t *r = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (r->min > 48000) {
		snd_interval_t t = {
			.min = hdsp->ds_channels,
			.max = hdsp->ds_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->max < 64000) {
		snd_interval_t t = {
			.min = hdsp->ss_channels,
			.max = hdsp->ss_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	}
	return 0;
}

static int snd_hdsp_hw_rule_rate_channels(snd_pcm_hw_params_t *params,
					     snd_pcm_hw_rule_t *rule)
{
	hdsp_t *hdsp = rule->private;
	snd_interval_t *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	snd_interval_t *r = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (c->min >= hdsp->ss_channels) {
		snd_interval_t t = {
			.min = 32000,
			.max = 48000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= hdsp->ds_channels) {
		snd_interval_t t = {
			.min = 64000,
			.max = 96000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	}
	return 0;
}

static int snd_hdsp_playback_open(snd_pcm_substream_t *substream)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	unsigned long flags;
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (hdsp_check_for_iobox (hdsp)) {
		return -EIO;
	}

	spin_lock_irqsave(&hdsp->lock, flags);

	snd_pcm_set_sync(substream);

        runtime->hw = snd_hdsp_playback_subinfo;
	runtime->dma_area = hdsp->playback_buffer;
	runtime->dma_bytes = HDSP_DMA_AREA_BYTES;

	if (hdsp->capture_substream == NULL) {
		hdsp_stop_audio(hdsp);
		hdsp_set_thru(hdsp, -1, 0);
	}

	hdsp->playback_pid = current->pid;
	hdsp->playback_substream = substream;

	spin_unlock_irqrestore(&hdsp->lock, flags);

	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, &hw_constraints_period_sizes);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_hdsp_hw_rule_channels, hdsp,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_hdsp_hw_rule_channels_rate, hdsp,
			     SNDRV_PCM_HW_PARAM_RATE, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
			     snd_hdsp_hw_rule_rate_channels, hdsp,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);

	hdsp->creg_spdif_stream = hdsp->creg_spdif;
	hdsp->spdif_ctl->access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(hdsp->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &hdsp->spdif_ctl->id);
	return 0;
}

static int snd_hdsp_playback_release(snd_pcm_substream_t *substream)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&hdsp->lock, flags);

	hdsp->playback_pid = -1;
	hdsp->playback_substream = NULL;

	spin_unlock_irqrestore(&hdsp->lock, flags);

	hdsp->spdif_ctl->access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(hdsp->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &hdsp->spdif_ctl->id);
	return 0;
}


static int snd_hdsp_capture_open(snd_pcm_substream_t *substream)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	unsigned long flags;
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (hdsp_check_for_iobox (hdsp)) {
		return -EIO;
	}

	spin_lock_irqsave(&hdsp->lock, flags);

	snd_pcm_set_sync(substream);

	runtime->hw = snd_hdsp_capture_subinfo;
	runtime->dma_area = hdsp->capture_buffer;
	runtime->dma_bytes = HDSP_DMA_AREA_BYTES;

	if (hdsp->playback_substream == NULL) {
		hdsp_stop_audio(hdsp);
		hdsp_set_thru(hdsp, -1, 0);
	}

	hdsp->capture_pid = current->pid;
	hdsp->capture_substream = substream;

	spin_unlock_irqrestore(&hdsp->lock, flags);

	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, &hw_constraints_period_sizes);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_hdsp_hw_rule_channels, hdsp,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_hdsp_hw_rule_channels_rate, hdsp,
			     SNDRV_PCM_HW_PARAM_RATE, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
			     snd_hdsp_hw_rule_rate_channels, hdsp,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	return 0;
}

static int snd_hdsp_capture_release(snd_pcm_substream_t *substream)
{
	hdsp_t *hdsp = _snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&hdsp->lock, flags);

	hdsp->capture_pid = -1;
	hdsp->capture_substream = NULL;

	spin_unlock_irqrestore(&hdsp->lock, flags);
	return 0;
}

static snd_pcm_ops_t snd_hdsp_playback_ops = {
	.open =		snd_hdsp_playback_open,
	.close =	snd_hdsp_playback_release,
	.ioctl =	snd_hdsp_ioctl,
	.hw_params =	snd_hdsp_hw_params,
	.prepare =	snd_hdsp_prepare,
	.trigger =	snd_hdsp_trigger,
	.pointer =	snd_hdsp_hw_pointer,
	.copy =		snd_hdsp_playback_copy,
	.silence =	snd_hdsp_hw_silence,
};

static snd_pcm_ops_t snd_hdsp_capture_ops = {
	.open =		snd_hdsp_capture_open,
	.close =	snd_hdsp_capture_release,
	.ioctl =	snd_hdsp_ioctl,
	.hw_params =	snd_hdsp_hw_params,
	.prepare =	snd_hdsp_prepare,
	.trigger =	snd_hdsp_trigger,
	.pointer =	snd_hdsp_hw_pointer,
	.copy =		snd_hdsp_capture_copy,
};

static int __devinit snd_hdsp_create_pcm(snd_card_t *card,
					 hdsp_t *hdsp)
{
	snd_pcm_t *pcm;
	int err;

	if ((err = snd_pcm_new(card, hdsp->card_name, 0, 1, 1, &pcm)) < 0)
		return err;

	hdsp->pcm = pcm;
	pcm->private_data = hdsp;
	strcpy(pcm->name, hdsp->card_name);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_hdsp_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_hdsp_capture_ops);

	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;

	return 0;
}

static int __devinit snd_hdsp_initialize_firmware (hdsp_t *hdsp)
{
	int i;
	u32 *firmware_ptr;

	if (hdsp_check_for_iobox (hdsp)) {
		return -EIO;
	}

	if (hdsp_fifo_wait (hdsp, 0, 100)) {
		return -EIO;
	}
	
	/* enable all channels */

	for (i = 0; i < HDSP_MAX_CHANNELS; ++i) {
		hdsp_write (hdsp, HDSP_inputEnable + (4 * i), 1);
		hdsp_write (hdsp, HDSP_outputEnable + (4 * i), 1);
	}

	if (force_firmware[hdsp->dev] || (hdsp_read (hdsp, HDSP_statusRegister) & HDSP_DllError) != 0) {

		snd_printk ("loading firmware\n");

		hdsp_write (hdsp, HDSP_jtagReg, HDSP_PROGRAM);
		hdsp_write (hdsp, HDSP_fifoData, 0);
		if (hdsp_fifo_wait (hdsp, 0, HDSP_SHORT_WAIT) < 0) {
			snd_printk ("timeout waiting for firmware setup\n");
			return -EIO;
		}

		hdsp_write (hdsp, HDSP_jtagReg, HDSP_S_LOAD);
		hdsp_write (hdsp, HDSP_fifoData, 0);

		if (hdsp_fifo_wait (hdsp, 0, HDSP_SHORT_WAIT)) {
			hdsp->type = Multiface;
			hdsp_write (hdsp, HDSP_jtagReg, HDSP_VERSION_BIT);
			hdsp_write (hdsp, HDSP_jtagReg, HDSP_S_LOAD);
			hdsp_fifo_wait (hdsp, 0, HDSP_SHORT_WAIT);
		} else {
			hdsp->type = Digiface;
		} 

		hdsp_write (hdsp, HDSP_jtagReg, HDSP_S_PROGRAM);
		hdsp_write (hdsp, HDSP_fifoData, 0);
		
		if (hdsp_fifo_wait (hdsp, 0, HDSP_LONG_WAIT)) {
			snd_printk ("timeout waiting for download preparation\n");
			return -EIO;
		}
		
		hdsp_write (hdsp, HDSP_jtagReg, HDSP_S_LOAD);
		
		if (hdsp->type == Digiface) {
			firmware_ptr = (u32 *) digiface_firmware;
		} else {
			firmware_ptr = (u32 *) multiface_firmware;
		}
		
		for (i = 0; i < 24413; ++i) {
			hdsp_write(hdsp, HDSP_fifoData, firmware_ptr[i]);
			if (hdsp_fifo_wait (hdsp, 127, HDSP_LONG_WAIT)) {
				snd_printk ("timeout during firmware loading\n");
				return -EIO;
			}
		}
		
		if (hdsp_fifo_wait (hdsp, 0, HDSP_LONG_WAIT)) {
			snd_printk ("timeout at end of firmware loading\n");
			return -EIO;
		}

		hdsp_write (hdsp, HDSP_jtagReg, 0);
		snd_printk ("finished firmware loading\n");
		mdelay(3000);

	} else {

		/* firmware already loaded, but we need to know what type
		   of I/O box is connected.
		*/

		if (hdsp_read(hdsp, HDSP_status2Register) & HDSP_version1) {
			hdsp->type = Multiface;
		} else {
			hdsp->type = Digiface;
		}
	}

	if (hdsp->type == Digiface) {
		snd_printk ("I/O Box is a Digiface\n");
		hdsp->card_name = "RME Hammerfall DSP (Digiface)";
		hdsp->ss_channels = DIGIFACE_SS_CHANNELS;
		hdsp->ds_channels = DIGIFACE_DS_CHANNELS;
	} else {
		snd_printk ("I/O Box is a Multiface\n");
		hdsp->card_name = "RME Hammerfall DSP (Multiface)";
		hdsp->ss_channels = MULTIFACE_SS_CHANNELS;
		hdsp->ds_channels = MULTIFACE_DS_CHANNELS;
	}
	
	snd_hdsp_flush_midi_input (hdsp, 0);
	snd_hdsp_flush_midi_input (hdsp, 1);

#ifdef SNDRV_BIG_ENDIAN
	hdsp_write(hdsp, HDSP_jtagReg, HDSP_BIGENDIAN_MODE);
#endif

	return 0;
}

static int __devinit snd_hdsp_create(snd_card_t *card,
				     hdsp_t *hdsp,
				     int precise_ptr)
{
	struct pci_dev *pci = hdsp->pci;
	int err;
	unsigned short rev;

	hdsp->irq = -1;
	hdsp->midi[0].rmidi = 0;
	hdsp->midi[1].rmidi = 0;
	hdsp->midi[0].input = 0;
	hdsp->midi[1].input = 0;
	hdsp->midi[0].output = 0;
	hdsp->midi[1].output = 0;
	spin_lock_init(&hdsp->midi[0].lock);
	spin_lock_init(&hdsp->midi[1].lock);
	hdsp->iobase = 0;
	hdsp->res_port = 0;

	hdsp->card = card;
	
	spin_lock_init(&hdsp->lock);

	pci_read_config_word(hdsp->pci, PCI_CLASS_REVISION, &rev);
	strcpy(card->driver, "H-DSP");
	strcpy(card->mixername, "Xilinx FPGA");
	
	switch (rev & 0xff) {
	case 0xa:
	case 0xb:
	case 0x64:
		/* hdsp_initialize_firmware() will reset this */
		hdsp->card_name = "RME Hammerfall DSP";
		break;

	default:
		return -ENODEV;
	}

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	pci_set_master(hdsp->pci);

	hdsp->port = pci_resource_start(pci, 0);

	if ((hdsp->res_port = request_mem_region(hdsp->port, HDSP_IO_EXTENT, "hdsp")) == NULL) {
		snd_printk("unable to grab memory region 0x%lx-0x%lx\n", hdsp->port, hdsp->port + HDSP_IO_EXTENT - 1);
		return -EBUSY;
	}

	if ((hdsp->iobase = (unsigned long) ioremap_nocache(hdsp->port, HDSP_IO_EXTENT)) == 0) {
		snd_printk("unable to remap region 0x%lx-0x%lx\n", hdsp->port, hdsp->port + HDSP_IO_EXTENT - 1);
		return -EBUSY;
	}

	if (request_irq(pci->irq, snd_hdsp_interrupt, SA_INTERRUPT|SA_SHIRQ, "hdsp", (void *)hdsp)) {
		snd_printk("unable to use IRQ %d\n", pci->irq);
		return -EBUSY;
	}

	hdsp->irq = pci->irq;
	hdsp->precise_ptr = precise_ptr;

	if ((err = snd_hdsp_initialize_memory(hdsp)) < 0) {
		return err;
	}

	if ((err = snd_hdsp_initialize_firmware(hdsp)) < 0) {
		return err;
	}

	if ((err = snd_hdsp_create_pcm(card, hdsp)) < 0) {
		return err;
	}

	if ((err = snd_hdsp_create_midi(card, hdsp, 0)) < 0) {
		return err;
	}

	if ((err = snd_hdsp_create_midi(card, hdsp, 1)) < 0) {
		return err;
	}

	if ((err = snd_hdsp_create_controls(card, hdsp)) < 0) {
		return err;
	}

	snd_hdsp_proc_init(hdsp);

	hdsp->last_spdif_sample_rate = -1;
	hdsp->last_adat_sample_rate = -1;
	hdsp->playback_pid = -1;
	hdsp->capture_pid = -1;
	hdsp->capture_substream = NULL;
	hdsp->playback_substream = NULL;

	snd_hdsp_set_defaults(hdsp);

	return 0;
}

static int snd_hdsp_free(hdsp_t *hdsp)
{
	if (hdsp->res_port) {
		/* stop the audio, and cancel all interrupts */
		hdsp->control_register &= ~(HDSP_Start|HDSP_AudioInterruptEnable|HDSP_Midi0InterruptEnable|HDSP_Midi1InterruptEnable);
		hdsp_write (hdsp, HDSP_controlRegister, hdsp->control_register);
	}

	if (hdsp->irq >= 0)
		free_irq(hdsp->irq, (void *)hdsp);

	snd_hdsp_free_buffers(hdsp);
	
	if (hdsp->iobase)
		iounmap((void *) hdsp->iobase);

	if (hdsp->res_port) {
		release_resource(hdsp->res_port);
		kfree_nocheck(hdsp->res_port);
	}
		
	return 0;
}

static void snd_hdsp_card_free(snd_card_t *card)
{
	hdsp_t *hdsp = (hdsp_t *) card->private_data;

	if (hdsp)
		snd_hdsp_free(hdsp);
}

static int __devinit snd_hdsp_probe(struct pci_dev *pci,
				    const struct pci_device_id *pci_id)
{
	static int dev;
	hdsp_t *hdsp;
	snd_card_t *card;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	if (!(card = snd_card_new(index[dev], id[dev], THIS_MODULE, sizeof(hdsp_t))))
		return -ENOMEM;

	hdsp = (hdsp_t *) card->private_data;
	card->private_free = snd_hdsp_card_free;
	hdsp->dev = dev;
	hdsp->pci = pci;

	if ((err = snd_hdsp_create(card, hdsp, precise_ptr[dev])) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->shortname, "Hammerfall DSP");
	sprintf(card->longname, "%s at 0x%lx, irq %d", hdsp->card_name, 
		hdsp->port, hdsp->irq);
	
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_hdsp_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name	  = "RME Hammerfall DSP",
	.id_table = snd_hdsp_ids,
	.probe	  = snd_hdsp_probe,
	.remove	  = __devexit_p(snd_hdsp_remove),
};

static int __init alsa_card_hdsp_init(void)
{
	if (pci_module_init(&driver) < 0) {
#ifdef MODULE
		printk(KERN_ERR "RME Hammerfall-DSP: no cards found\n");
#endif
		return -ENODEV;
	}

	return 0;
}

static void __exit alsa_card_hdsp_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_hdsp_init)
module_exit(alsa_card_hdsp_exit)

#ifndef MODULE

/* format is: snd-hdsp=enable,index,id */

static int __init alsa_card_hdsp_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-hdsp=", alsa_card_hdsp_setup);

#endif /* ifndef MODULE */
