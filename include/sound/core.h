#ifndef __SOUND_CORE_H
#define __SOUND_CORE_H

/*
 *  Main header file for the ALSA driver
 *  Copyright (c) 1994-2001 by Jaroslav Kysela <perex@suse.cz>
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

#include <linux/sched.h>		/* wake_up() */
#include <asm/semaphore.h>		/* struct semaphore */

/* Typedef's */
typedef struct timeval snd_timestamp_t;
typedef struct sndrv_interval snd_interval_t;
typedef enum sndrv_card_type snd_card_type;
typedef struct sndrv_xferi snd_xferi_t;
typedef struct sndrv_xfern snd_xfern_t;
typedef struct sndrv_xferv snd_xferv_t;

/* forward declarations */
#ifdef CONFIG_PCI
struct pci_dev;
#endif
#ifdef CONFIG_SBUS
struct sbus_dev;
#endif

/* device allocation stuff */

#define SNDRV_DEV_TYPE_RANGE_SIZE		0x1000

typedef enum {
	SNDRV_DEV_TOPLEVEL =		(0*SNDRV_DEV_TYPE_RANGE_SIZE),
	SNDRV_DEV_LOWLEVEL_PRE,
	SNDRV_DEV_LOWLEVEL_NORMAL =	(1*SNDRV_DEV_TYPE_RANGE_SIZE),
	SNDRV_DEV_PCM,
	SNDRV_DEV_RAWMIDI,
	SNDRV_DEV_TIMER,
	SNDRV_DEV_SEQUENCER,
	SNDRV_DEV_HWDEP,
	SNDRV_DEV_LOWLEVEL =		(2*SNDRV_DEV_TYPE_RANGE_SIZE)
} snd_device_type_t;

typedef enum {
	SNDRV_DEV_BUILD = 0,
	SNDRV_DEV_REGISTERED = 1
} snd_device_state_t;

typedef enum {
	SNDRV_DEV_CMD_PRE = 0,
	SNDRV_DEV_CMD_NORMAL = 1,
	SNDRV_DEV_CMD_POST = 2
} snd_device_cmd_t;

typedef struct _snd_card snd_card_t;
typedef struct _snd_device snd_device_t;

typedef int (snd_dev_free_t)(snd_device_t *device);
typedef int (snd_dev_register_t)(snd_device_t *device);
typedef int (snd_dev_unregister_t)(snd_device_t *device);

typedef struct {
	snd_dev_free_t *dev_free;
	snd_dev_register_t *dev_register;
	snd_dev_unregister_t *dev_unregister;
} snd_device_ops_t;

struct _snd_device {
	struct list_head list;		/* list of registered devices */
	snd_card_t *card;		/* card which holds this device */
	snd_device_state_t state;	/* state of the device */
	snd_device_type_t type;		/* device type */
	void *device_data;		/* device structure */
	snd_device_ops_t *ops;		/* operations */
};

#define snd_device(n) list_entry(n, snd_device_t, list)

/* various typedefs */

typedef struct snd_info_entry snd_info_entry_t;
typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_pcm_str snd_pcm_str_t;
typedef struct _snd_pcm_substream snd_pcm_substream_t;
typedef struct _snd_mixer snd_kmixer_t;
typedef struct _snd_rawmidi snd_rawmidi_t;
typedef struct _snd_ctl_file snd_ctl_file_t;
typedef struct _snd_kcontrol snd_kcontrol_t;
typedef struct _snd_timer snd_timer_t;
typedef struct _snd_timer_instance snd_timer_instance_t;
typedef struct _snd_hwdep snd_hwdep_t;
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
typedef struct _snd_oss_mixer snd_mixer_oss_t;
#endif

/* main structure for soundcard */

struct _snd_card {
	int number;			/* number of soundcard (index to snd_cards) */

	char id[16];			/* id string of this card */
	char driver[16];		/* driver name */
	char shortname[32];		/* short name of this soundcard */
	char longname[80];		/* name of this soundcard */
	char mixername[80];		/* mixer name */
	char components[80];		/* card components delimited with space */

	struct module *module;		/* top-level module */

	void *private_data;		/* private data for soundcard */
	void (*private_free) (snd_card_t *card); /* callback for freeing of private data */

	struct list_head devices;	/* devices */

	unsigned int last_numid;	/* last used numeric ID */
	rwlock_t control_rwlock;	/* control list lock */
	rwlock_t control_owner_lock;	/* control list lock */
	int controls_count;		/* count of all controls */
	struct list_head controls;	/* all controls for this card */
	struct list_head ctl_files;	/* active control files */

	snd_info_entry_t *proc_root;	/* root for soundcard specific files */
	snd_info_entry_t *proc_id;	/* the card id */
	struct proc_dir_entry *proc_root_link;	/* number link to real id */

#ifdef CONFIG_PM
	int (*set_power_state) (snd_card_t *card, unsigned int state);
	void *power_state_private_data;
	unsigned int power_state;	/* power state */
	struct semaphore power_lock;	/* power lock */
	wait_queue_head_t power_sleep;
#endif

#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
	snd_mixer_oss_t *mixer_oss;
	int mixer_oss_change_count;
#endif
};

#ifdef CONFIG_PM
static inline void snd_power_lock(snd_card_t *card)
{
	down(&card->power_lock);
}

static inline void snd_power_unlock(snd_card_t *card)
{
	up(&card->power_lock);
}

void snd_power_wait(snd_card_t *card);

static inline unsigned int snd_power_get_state(snd_card_t *card)
{
	return card->power_state;
}

static inline void snd_power_change_state(snd_card_t *card, unsigned int state)
{
	card->power_state = state;
	wake_up(&card->power_sleep);
}
#else
#define snd_power_lock(card) do { ; } while (0)
#define snd_power_unlock(card) do { ; } while (0)
#define snd_power_wait(card) do { ; } while (0)
#define snd_power_get_state(card) SNDRV_CTL_POWER_D0
#define snd_power_change_state(card, state) do { ; } while (0)
#endif

/* device.c */

struct _snd_minor {
	struct list_head list;		/* list of all minors per card */
	int number;			/* minor number */
	int device;			/* device number */
	const char *comment;		/* for /proc/asound/devices */
	snd_info_entry_t *dev;		/* for /proc/asound/dev */
	struct file_operations *f_ops;	/* file operations */
};

typedef struct _snd_minor snd_minor_t;

/* sound.c */

extern int snd_ecards_limit;
extern int snd_device_mode;
extern int snd_device_gid;
extern int snd_device_uid;

void snd_request_card(int card);

int snd_register_device(int type, snd_card_t *card, int dev, snd_minor_t *reg, const char *name);
int snd_unregister_device(int type, snd_card_t *card, int dev);

#ifdef CONFIG_SND_OSSEMUL
int snd_register_oss_device(int type, snd_card_t *card, int dev, snd_minor_t *reg, const char *name);
int snd_unregister_oss_device(int type, snd_card_t *card, int dev);
#endif

int snd_minor_info_init(void);
int snd_minor_info_done(void);

/* sound_oss.c */

#ifdef CONFIG_SND_OSSEMUL

int snd_minor_info_oss_init(void);
int snd_minor_info_oss_done(void);

int snd_oss_init_module(void);
void snd_oss_cleanup_module(void);

#endif

/* memory.c */

#ifdef CONFIG_SND_DEBUG_MEMORY
void snd_memory_init(void);
void snd_memory_done(void);
int snd_memory_info_init(void);
int snd_memory_info_done(void);
void *snd_hidden_kmalloc(size_t size, int flags);
void snd_hidden_kfree(const void *obj);
void *snd_hidden_vmalloc(unsigned long size);
void snd_hidden_vfree(void *obj);
#define kmalloc(size, flags) snd_hidden_kmalloc(size, flags)
#define kfree(obj) snd_hidden_kfree(obj)
#define kfree_nocheck(obj) snd_wrapper_kfree(obj)
#define vmalloc(size) snd_hidden_vmalloc(size)
#define vfree(obj) snd_hidden_vfree(obj)
#else
#define kfree_nocheck(obj) kfree(obj)
#endif
void *snd_kcalloc(size_t size, int flags);
char *snd_kmalloc_strdup(const char *string, int flags);
void *snd_malloc_pages(unsigned long size, unsigned int dma_flags);
void *snd_malloc_pages_fallback(unsigned long size, unsigned int dma_flags, unsigned long *res_size);
void snd_free_pages(void *ptr, unsigned long size);
#ifdef CONFIG_PCI
void *snd_malloc_pci_pages(struct pci_dev *pci, unsigned long size, dma_addr_t *dma_addr);
void *snd_malloc_pci_pages_fallback(struct pci_dev *pci, unsigned long size, dma_addr_t *dma_addr, unsigned long *res_size);
void snd_free_pci_pages(struct pci_dev *pci, unsigned long size, void *ptr, dma_addr_t dma_addr);
#endif
#ifdef CONFIG_SBUS
void *snd_malloc_sbus_pages(struct sbus_dev *sdev, unsigned long size, dma_addr_t *dma_addr);
void *snd_malloc_sbus_pages_fallback(struct sbus_dev *sdev, unsigned long size, dma_addr_t *dma_addr, unsigned long *res_size);
void snd_free_sbus_pages(struct sbus_dev *sdev, unsigned long size, void *ptr, dma_addr_t dma_addr);
#endif
#ifdef CONFIG_ISA
#ifdef CONFIG_PCI
#define snd_malloc_isa_pages(size, dma_addr) snd_malloc_pci_pages(NULL, size, dma_addr)
#define snd_malloc_isa_pages_fallback(size, dma_addr, res_size) snd_malloc_pci_pages_fallback(NULL, size, dma_addr, res_size)
#define snd_free_isa_pages(size, ptr, dma_addr) snd_free_pci_pages(NULL, size, ptr, dma_addr)
#else /* !CONFIG_PCI */
void *snd_malloc_isa_pages(unsigned long size, dma_addr_t *dma_addr);
void *snd_malloc_isa_pages_fallback(unsigned long size, dma_addr_t *dma_addr, unsigned long *res_size);
#define snd_free_isa_pages(size, ptr, dma_addr) snd_free_pages(ptr, size)
#endif /* CONFIG_PCI */
#endif /* CONFIG_ISA */
int copy_to_user_fromio(void *dst, unsigned long src, size_t count);
int copy_from_user_toio(unsigned long dst, const void *src, size_t count);

/* init.c */

extern int snd_cards_count;
extern snd_card_t *snd_cards[SNDRV_CARDS];
extern rwlock_t snd_card_rwlock;
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
extern int (*snd_mixer_oss_notify_callback)(snd_card_t *card, int free_flag);
#endif

snd_card_t *snd_card_new(int idx, const char *id,
			 struct module *module, int extra_size);
int snd_card_free(snd_card_t *card);
int snd_card_register(snd_card_t *card);
int snd_card_info_init(void);
int snd_card_info_done(void);
int snd_component_add(snd_card_t *card, const char *component);

/* device.c */

int snd_device_new(snd_card_t *card, snd_device_type_t type,
		   void *device_data, snd_device_ops_t *ops);
int snd_device_register(snd_card_t *card, void *device_data);
int snd_device_register_all(snd_card_t *card);
int snd_device_free(snd_card_t *card, void *device_data);
int snd_device_free_all(snd_card_t *card, snd_device_cmd_t cmd);

/* isadma.c */

#define DMA_MODE_NO_ENABLE	0x0100

void snd_dma_program(unsigned long dma, unsigned long addr, unsigned int size, unsigned short mode);
void snd_dma_disable(unsigned long dma);
unsigned int snd_dma_residue(unsigned long dma);

/* misc.c */

int snd_task_name(struct task_struct *task, char *name, size_t size);
#ifdef CONFIG_SND_VERBOSE_PRINTK
void snd_verbose_printk(const char *file, int line, const char *format, ...);
#endif
#if defined(CONFIG_SND_DEBUG) && defined(CONFIG_SND_VERBOSE_PRINTK)
void snd_verbose_printd(const char *file, int line, const char *format, ...);
#endif
#if defined(CONFIG_SND_DEBUG) && !defined(CONFIG_SND_VERBOSE_PRINTK)
void snd_printd(const char *format, ...);
#endif

/* --- */

#ifdef CONFIG_SND_VERBOSE_PRINTK
#define snd_printk(fmt, args...) \
	snd_verbose_printk(__FILE__, __LINE__, fmt ,##args)
#else
#define snd_printk(fmt, args...) \
	printk(fmt ,##args)
#endif

#ifdef CONFIG_SND_DEBUG

#define __ASTRING__(x) #x

#ifdef CONFIG_SND_VERBOSE_PRINTK
#define snd_printd(fmt, args...) \
	snd_verbose_printd(__FILE__, __LINE__, fmt ,##args)
#endif
#define snd_assert(expr, args...) do {\
	if (!(expr)) {\
		snd_printk("BUG? (%s) (called from %p)\n", __ASTRING__(expr), __builtin_return_address(0));\
		args;\
	}\
} while (0)
#define snd_runtime_check(expr, args...) do {\
	if (!(expr)) {\
		snd_printk("ERROR (%s) (called from %p)\n", __ASTRING__(expr), __builtin_return_address(0));\
		args;\
	}\
} while (0)

#else /* !CONFIG_SND_DEBUG */

#define snd_printd(fmt, args...)	/* nothing */
#define snd_assert(expr, args...)	(void)(expr)
#define snd_runtime_check(expr, args...) do { if (!(expr)) { args; } } while (0)

#endif /* CONFIG_SND_DEBUG */

#ifdef CONFIG_SND_DEBUG_DETECT
#define snd_printdd(format, args...) snd_printk(format, ##args)
#else
#define snd_printdd(format, args...) /* nothing */
#endif

#define snd_BUG() snd_assert(0, )


#define snd_timestamp_now(tstamp) do_gettimeofday(tstamp)
#define snd_timestamp_zero(tstamp) do { (tstamp)->tv_sec = 0; (tstamp)->tv_usec = 0; } while (0)
#define snd_timestamp_null(tstamp) ((tstamp)->tv_sec == 0 && (tstamp)->tv_usec ==0)

#define SNDRV_OSS_VERSION         ((3<<16)|(8<<8)|(1<<4)|(0))	/* 3.8.1a */

#endif /* __SOUND_CORE_H */
