
/*
 *  linux/drivers/sound/dmasound/dmasound.h
 *
 *
 *  Minor numbers for the sound driver.
 *
 *  Unfortunately Creative called the codec chip of SB as a DSP. For this
 *  reason the /dev/dsp is reserved for digitized audio use. There is a
 *  device for true DSP processors but it will be called something else.
 *  In v3.0 it's /dev/sndproc but this could be a temporary solution.
 */


#include <linux/config.h>


#define SND_NDEVS	256	/* Number of supported devices */
#define SND_DEV_CTL	0	/* Control port /dev/mixer */
#define SND_DEV_SEQ	1	/* Sequencer output /dev/sequencer (FM
				   synthesizer and MIDI output) */
#define SND_DEV_MIDIN	2	/* Raw midi access */
#define SND_DEV_DSP	3	/* Digitized voice /dev/dsp */
#define SND_DEV_AUDIO	4	/* Sparc compatible /dev/audio */
#define SND_DEV_DSP16	5	/* Like /dev/dsp but 16 bits/sample */
#define SND_DEV_STATUS	6	/* /dev/sndstat */
/* #7 not in use now. Was in 2.4. Free for use after v3.0. */
#define SND_DEV_SEQ2	8	/* /dev/sequencer, level 2 interface */
#define SND_DEV_SNDPROC 9	/* /dev/sndproc for programmable devices */
#define SND_DEV_PSS	SND_DEV_SNDPROC

#define DSP_DEFAULT_SPEED	8000

#define ON		1
#define OFF		0

#define MAX_AUDIO_DEV	5
#define MAX_MIXER_DEV	2
#define MAX_SYNTH_DEV	3
#define MAX_MIDI_DEV	6
#define MAX_TIMER_DEV	3


#define MAX_CATCH_RADIUS	10
#define MIN_BUFFERS		4
#define MIN_BUFSIZE		4	/* in KB */
#define MAX_BUFSIZE		128	/* Limit for Amiga in KB */


#define min(x, y)	((x) < (y) ? (x) : (y))
#define le2be16(x)	(((x)<<8 & 0xff00) | ((x)>>8 & 0x00ff))
#define le2be16dbl(x)	(((x)<<8 & 0xff00ff00) | ((x)>>8 & 0x00ff00ff))

#define IOCTL_IN(arg, ret) \
	do { int error = get_user(ret, (int *)(arg)); \
		if (error) return error; \
	} while (0)
#define IOCTL_OUT(arg, ret)	ioctl_return((int *)(arg), ret)

static inline int ioctl_return(int *addr, int value)
{
	return value < 0 ? value : put_user(value, addr);
}


    /*
     *  Configuration
     */

#undef HAS_8BIT_TABLES
#undef HAS_14BIT_TABLES
#undef HAS_16BIT_TABLES
#undef HAS_RECORD

#if defined(CONFIG_DMASOUND_ATARI) || defined(CONFIG_DMASOUND_ATARI_MODULE) ||\
    defined(CONFIG_DMASOUND_PAULA) || defined(CONFIG_DMASOUND_PAULA_MODULE) ||\
    defined(CONFIG_DMASOUND_Q40) || defined(CONFIG_DMASOUND_Q40_MODULE)
#define HAS_8BIT_TABLES
#endif
#if defined(CONFIG_DMASOUND_AWACS) || defined(CONFIG_DMASOUND_AWACS_MODULE)
#define HAS_16BIT_TABLES
#define HAS_RECORD
#endif


    /*
     *  Initialization
     */

extern int dmasound_init(void);
#ifdef MODULE
extern void dmasound_deinit(void);
#else
#define dmasound_deinit()	do { } while (0)
#endif


    /*
     *  Machine definitions
     */

typedef struct {
    const char *name;
    const char *name2;
    void (*open)(void);
    void (*release)(void);
    void *(*dma_alloc)(unsigned int, int);
    void (*dma_free)(void *, unsigned int);
    int (*irqinit)(void);
#ifdef MODULE
    void (*irqcleanup)(void);
#endif
    void (*init)(void);
    void (*silence)(void);
    int (*setFormat)(int);
    int (*setVolume)(int);
    int (*setBass)(int);
    int (*setTreble)(int);
    int (*setGain)(int);
    void (*play)(void);
    void (*record)(void);			/* optional */
    void (*mixer_init)(void);			/* optional */
    int (*mixer_ioctl)(u_int, u_long);		/* optional */
    void (*write_sq_setup)(void);		/* optional */
    void (*read_sq_setup)(void);		/* optional */
    void (*sq_open)(void);			/* optional */
    int (*state_info)(char *);			/* optional */
    void (*abort_read)(void);			/* optional */
    int min_dsp_speed;
} MACHINE;


    /*
     *  Low level stuff
     */

typedef struct {
    int format;		/* AFMT_* */
    int stereo;		/* 0 = mono, 1 = stereo */
    int size;		/* 8/16 bit*/
    int speed;		/* speed */
} SETTINGS;

typedef struct {
    ssize_t (*ct_ulaw)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_alaw)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_s8)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_u8)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_s16be)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_u16be)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_s16le)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_u16le)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
} TRANS;

struct sound_settings {
    MACHINE mach;	/* machine dependent things */
    SETTINGS hard;	/* hardware settings */
    SETTINGS soft;	/* software settings */
    SETTINGS dsp;	/* /dev/dsp default settings */
    TRANS *trans_write;	/* supported translations */
#ifdef HAS_RECORD
    TRANS *trans_read;	/* supported translations */
#endif
    int volume_left;	/* volume (range is machine dependent) */
    int volume_right;
    int bass;		/* tone (range is machine dependent) */
    int treble;
    int gain;
    int minDev;		/* minor device number currently open */
};

extern struct sound_settings dmasound;

extern char dmasound_ulaw2dma8[];
extern char dmasound_alaw2dma8[];
extern short dmasound_ulaw2dma16[];
extern short dmasound_alaw2dma16[];


    /*
     *  Mid level stuff
     */

static inline int dmasound_set_volume(int volume)
{
	return dmasound.mach.setVolume(volume);
}

static inline int dmasound_set_bass(int bass)
{
	return dmasound.mach.setBass ? dmasound.mach.setBass(bass) : 50;
}

static inline int dmasound_set_treble(int treble)
{
	return dmasound.mach.setTreble ? dmasound.mach.setTreble(treble) : 50;
}

static inline int dmasound_set_gain(int gain)
{
	return dmasound.mach.setGain ? dmasound.mach.setGain(gain) : 100;
}


    /*
     * Sound queue stuff, the heart of the driver
     */

struct sound_queue {
    /* buffers allocated for this queue */
    int numBufs;
    int bufSize;			/* in bytes */
    char **buffers;

    /* current parameters */
    int max_count;
    int block_size;			/* in bytes */
    int max_active;

    /* it shouldn't be necessary to declare any of these volatile */
    int front, rear, count;
    int rear_size;
    /*
     *	The use of the playing field depends on the hardware
     *
     *	Atari, PMac: The number of frames that are loaded/playing
     *
     *	Amiga: Bit 0 is set: a frame is loaded
     *	       Bit 1 is set: a frame is playing
     */
    int active;
    wait_queue_head_t action_queue, open_queue, sync_queue;
    int open_mode;
    int busy, syncing;
};

#define SLEEP(queue)		interruptible_sleep_on_timeout(&queue, HZ)
#define WAKE_UP(queue)		(wake_up_interruptible(&queue))

extern struct sound_queue dmasound_write_sq;
extern struct sound_queue dmasound_read_sq;

#define write_sq	dmasound_write_sq
#define read_sq		dmasound_read_sq

extern int dmasound_catchRadius;

#define catchRadius	dmasound_catchRadius

