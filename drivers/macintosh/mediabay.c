/*
 * Driver for the media bay on the PowerBook 3400 and 2400.
 *
 * Copyright (C) 1998 Paul Mackerras.
 *
 * Various evolutions by Benjamin Herrenschmidt & Henry Worth
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/hdreg.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <asm/prom.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/feature.h>
#include <asm/mediabay.h>
#include <asm/init.h>
#include <linux/adb.h>
#include <linux/pmu.h>

#ifdef CONFIG_PMAC_PBOOK
static int mb_notify_sleep(struct pmu_sleep_notifier *self, int when);
static struct pmu_sleep_notifier mb_sleep_notifier = {
	mb_notify_sleep,
	SLEEP_LEVEL_MEDIABAY,
};
#endif

#undef MB_USE_INTERRUPTS
#undef MB_DEBUG
#define MB_IGNORE_SIGNALS

#ifdef MB_DEBUG
#define MBDBG(fmt, arg...)	printk(KERN_INFO fmt , ## arg)
#else
#define MBDBG(fmt, arg...)	do { } while (0)
#endif

struct media_bay_hw {
	unsigned char	b0;
	unsigned char	contents;
	unsigned char	b2;
	unsigned char	b3;
};

struct media_bay_info {
	volatile struct media_bay_hw*	addr;
	volatile u8*			extint_gpio;
	int				content_id;
	int				state;
	int				last_value;
	int				value_count;
	int				timer;
	struct device_node*		dev_node;
	int				pismo;	/* New PowerBook3,1 */
	int				gpio_cache;
#ifdef CONFIG_BLK_DEV_IDE
	unsigned long			cd_base;
	int 				cd_index;
	int				cd_irq;
	int				cd_retry;
#endif
};

#define MAX_BAYS	2

static volatile struct media_bay_info media_bays[MAX_BAYS];
int media_bay_count = 0;

inline int mb_content(volatile struct media_bay_info *bay)
{
        if (bay->pismo) {
                unsigned char new_gpio = in_8(bay->extint_gpio + 0xe) & 2;
                if (new_gpio) {
                	bay->gpio_cache = new_gpio;
                        return MB_NO;
                } else if (bay->gpio_cache != new_gpio) {
                        /* make sure content bits are set */
                        feature_set(bay->dev_node, FEATURE_Mediabay_content);
                        udelay(5);
                        bay->gpio_cache = new_gpio;
                }
                return (in_le32((unsigned*)bay->addr) >> 4) & 0xf;
        } else {
                int cont = (in_8(&bay->addr->contents) >> 4) & 7;
                return (cont == 7) ? MB_NO : cont;
        }
}

#ifdef CONFIG_BLK_DEV_IDE
/* check the busy bit in the media-bay ide interface
   (assumes the media-bay contains an ide device) */
//#define MB_IDE_READY(i)	((inb(media_bays[i].cd_base + 0x70) & 0xc0) == 0x40)
#define MB_IDE_READY(i)	((inb(media_bays[i].cd_base + 0x70) & 0x80) == 0)
#endif

/* Note: All delays are not in milliseconds and converted to HZ relative
 * values by the macro below
 */
#define MS_TO_HZ(ms)	((ms * HZ) / 1000)

/*
 * Consider the media-bay ID value stable if it is the same for
 * this number of milliseconds
 */
#define MB_STABLE_DELAY	40

/* Wait after powering up the media bay this delay in ms
 * timeout bumped for some powerbooks
 */
#define MB_POWER_DELAY	200

/*
 * Hold the media-bay reset signal true for this many ticks
 * after a device is inserted before releasing it.
 */
#define MB_RESET_DELAY	40

/*
 * Wait this long after the reset signal is released and before doing
 * further operations. After this delay, the IDE reset signal is released
 * too for an IDE device
 */
#define MB_SETUP_DELAY	100

/*
 * Wait this many ticks after an IDE device (e.g. CD-ROM) is inserted
 * (or until the device is ready) before waiting for busy bit to disappear
 */
#define MB_IDE_WAIT	1000

/*
 * Timeout waiting for busy bit of an IDE device to go down
 */
#define MB_IDE_TIMEOUT	5000

/*
 * Max retries of the full power up/down sequence for an IDE device
 */
#define MAX_CD_RETRIES	3

/*
 * States of a media bay
 */
enum {
	mb_empty = 0,		/* Idle */
	mb_powering_up,		/* power bit set, waiting MB_POWER_DELAY */
	mb_enabling_bay,	/* enable bits set, waiting MB_RESET_DELAY */
	mb_resetting,		/* reset bit unset, waiting MB_SETUP_DELAY */
	mb_ide_resetting,	/* IDE reset bit unser, waiting MB_IDE_WAIT */
	mb_ide_waiting,		/* Waiting for BUSY bit to go away until MB_IDE_TIMEOUT */
	mb_up,			/* Media bay full */
	mb_powering_down	/* Powering down (avoid too fast down/up) */
};

static void poll_media_bay(int which);
static void set_media_bay(int which, int id);
static void set_mb_power(int which, int onoff);
static void media_bay_step(int i);
static int media_bay_task(void *);

#ifdef MB_USE_INTERRUPTS
static void media_bay_intr(int irq, void *devid, struct pt_regs *regs);
#endif

/*
 * It seems that the bit for the media-bay interrupt in the IRQ_LEVEL
 * register is always set when there is something in the media bay.
 * This causes problems for the interrupt code if we attach an interrupt
 * handler to the media-bay interrupt, because it tends to go into
 * an infinite loop calling the media bay interrupt handler.
 * Therefore we do it all by polling the media bay once each tick.
 */

void __pmac
media_bay_init(void)
{
	struct device_node *np;
	int		n,i;
	
	for (i=0; i<MAX_BAYS; i++) {
		memset((char *)&media_bays[i], 0, sizeof(struct media_bay_info));
		media_bays[i].content_id	= -1;
#ifdef CONFIG_BLK_DEV_IDE
		media_bays[i].cd_index		= -1;
#endif
	}
	
	np = find_devices("media-bay");
	n = 0;
	while(np && (n<MAX_BAYS)) {
		if (np->n_addrs == 0)
			continue;
		media_bays[n].addr = (volatile struct media_bay_hw *)
			ioremap(np->addrs[0].address, sizeof(struct media_bay_hw));

		media_bays[n].pismo = device_is_compatible(np, "keylargo-media-bay");
		if (media_bays[n].pismo) {
			if (!np->parent || strcmp(np->parent->name, "mac-io")) {
				printk(KERN_ERR "Pismo media-bay has no mac-io parent !\n");
				continue;
			}
			media_bays[n].extint_gpio = ioremap(np->parent->addrs[0].address
				+ 0x58, 0x10);
		}

#ifdef MB_USE_INTERRUPTS
		if (np->n_intrs == 0) {
			printk(KERN_ERR "media bay %d has no irq\n",n);
			continue;
		}

		if (request_irq(np->intrs[0].line, media_bay_intr, 0, "Media bay", (void *)n)) {
			printk(KERN_ERR "Couldn't get IRQ %d for media bay %d\n",
				np->intrs[0].line, n);
			continue;
		}
#endif	
		media_bay_count++;
	
		media_bays[n].dev_node		= np;

		/* Force an immediate detect */
		set_mb_power(n,0);
		mdelay(MB_POWER_DELAY);
		if(!media_bays[n].pismo)
			out_8(&media_bays[n].addr->contents, 0x70);
		mdelay(MB_STABLE_DELAY);
		media_bays[n].content_id = MB_NO;
		media_bays[n].last_value = mb_content(&media_bays[n]);
		media_bays[n].value_count = MS_TO_HZ(MB_STABLE_DELAY);
		media_bays[n].state = mb_empty;
		do {
			mdelay(1000/HZ);
			media_bay_step(n);
		} while((media_bays[n].state != mb_empty) &&
			(media_bays[n].state != mb_up));

		n++;
		np=np->next;
	}

	if (media_bay_count)
	{
		printk(KERN_INFO "Registered %d media-bay(s)\n", media_bay_count);

#ifdef CONFIG_PMAC_PBOOK
		pmu_register_sleep_notifier(&mb_sleep_notifier);
#endif /* CONFIG_PMAC_PBOOK */

		kernel_thread(media_bay_task, NULL,
			      CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	}
}

#ifdef MB_USE_INTERRUPTS
static void __pmac
media_bay_intr(int irq, void *devid, struct pt_regs *regs)
{
}
#endif

static void __pmac
set_mb_power(int which, int onoff)
{
	volatile struct media_bay_info*	mb = &media_bays[which];

	if (onoff) {
		feature_set(mb->dev_node, FEATURE_Mediabay_power);
		udelay(10);
		feature_set(mb->dev_node, FEATURE_Mediabay_reset);
		udelay(10);
		mb->state = mb_powering_up;
		MBDBG("mediabay%d: powering up\n", which);
	} else {
		feature_clear(mb->dev_node, FEATURE_Mediabay_floppy_enable);
		if (mb->pismo)
			feature_clear(mb->dev_node, FEATURE_IDE0_enable);
		else
			feature_clear(mb->dev_node, FEATURE_IDE1_enable);
		feature_clear(mb->dev_node, FEATURE_Mediabay_IDE_switch);
		feature_clear(mb->dev_node, FEATURE_Mediabay_PCI_enable);
		feature_clear(mb->dev_node, FEATURE_SWIM3_enable);
		feature_clear(mb->dev_node, FEATURE_Mediabay_power);
		mb->state = mb_powering_down;
		MBDBG("mediabay%d: powering down\n", which);
	}
	mb->timer = MS_TO_HZ(MB_POWER_DELAY);
}

static void __pmac
set_media_bay(int which, int id)
{
	volatile struct media_bay_info* bay;

	bay = &media_bays[which];
	
	switch (id) {
	case MB_CD:
		if (bay->pismo) {
			feature_set(bay->dev_node, FEATURE_Mediabay_IDE_switch);
			udelay(10);
			feature_set(bay->dev_node, FEATURE_IDE0_enable);
			udelay(10);
			feature_set(bay->dev_node, FEATURE_IDE0_reset);
		} else {
			feature_set(bay->dev_node, FEATURE_IDE1_enable);
			udelay(10);
			feature_set(bay->dev_node, FEATURE_IDE1_reset);
		}
		printk(KERN_INFO "media bay %d contains a CD-ROM drive\n", which);
		break;
	case MB_FD:
	case MB_FD1:
		feature_set(bay->dev_node, FEATURE_Mediabay_floppy_enable);
		feature_set(bay->dev_node, FEATURE_SWIM3_enable);
		printk(KERN_INFO "media bay %d contains a floppy disk drive\n", which);
		break;
	case MB_NO:
		break;
	default:
		printk(KERN_INFO "media bay %d contains an unknown device (%d)\n",
		       which, id);
		break;
	}
}

int __pmac
check_media_bay(struct device_node *which_bay, int what)
{
#ifdef CONFIG_BLK_DEV_IDE
	int	i;

	for (i=0; i<media_bay_count; i++)
		if (which_bay == media_bays[i].dev_node)
		{
			if ((what == media_bays[i].content_id) && media_bays[i].state == mb_up)
				return 0;
			media_bays[i].cd_index = -1;
			return -EINVAL;
		}
#endif /* CONFIG_BLK_DEV_IDE */
	return -ENODEV;
}

int __pmac
check_media_bay_by_base(unsigned long base, int what)
{
#ifdef CONFIG_BLK_DEV_IDE
	int	i;

	for (i=0; i<media_bay_count; i++)
		if (base == media_bays[i].cd_base)
		{
			if ((what == media_bays[i].content_id) && media_bays[i].state == mb_up)
				return 0;
			media_bays[i].cd_index = -1;
			return -EINVAL;
		} 
#endif
	
	return -ENODEV;
}

int __pmac
media_bay_set_ide_infos(struct device_node* which_bay, unsigned long base,
	int irq, int index)
{
#ifdef CONFIG_BLK_DEV_IDE
	int	i;

	for (i=0; i<media_bay_count; i++)
		if (which_bay == media_bays[i].dev_node)
		{
			int timeout = 5000;
			
 			media_bays[i].cd_base	= base;
			media_bays[i].cd_irq	= irq;

			if ((MB_CD != media_bays[i].content_id) || media_bays[i].state != mb_up)
				return 0;

			printk(KERN_DEBUG "Registered ide %d for media bay %d\n", index, i);
			do {
				if (MB_IDE_READY(i)) {
					media_bays[i].cd_index	= index;
					return 0;
				}
				mdelay(1);
			} while(--timeout);
			printk(KERN_DEBUG "Timeount waiting IDE in bay %d\n", i);
			return -ENODEV;
		} 
#endif
	
	return -ENODEV;
}

static void __pmac
media_bay_step(int i)
{
	volatile struct media_bay_info* bay = &media_bays[i];

	/* We don't poll when powering down */
	if (bay->state != mb_powering_down)
	    poll_media_bay(i);

	/* If timer expired or polling IDE busy, run state machine */
	if ((bay->state != mb_ide_waiting) && (bay->timer != 0) && ((--bay->timer) != 0))
	    return;

	switch(bay->state) {
	case mb_powering_up:
	    	set_media_bay(i, bay->last_value);
	    	bay->timer = MS_TO_HZ(MB_RESET_DELAY);
	    	bay->state = mb_enabling_bay;
		MBDBG("mediabay%d: enabling (kind:%d)\n", i, bay->content_id);
		break;
	case mb_enabling_bay:
	    	feature_clear(bay->dev_node, FEATURE_Mediabay_reset);
	    	bay->timer = MS_TO_HZ(MB_SETUP_DELAY);
	    	bay->state = mb_resetting;
		MBDBG("mediabay%d: waiting reset (kind:%d)\n", i, bay->content_id);
	    	break;
	    
	case mb_resetting:
		if (bay->content_id != MB_CD) {
			MBDBG("mediabay%d: bay is up (kind:%d)\n", i, bay->content_id);
			bay->state = mb_up;
			break;
	    	}
#ifdef CONFIG_BLK_DEV_IDE
		MBDBG("mediabay%d: waiting IDE reset (kind:%d)\n", i, bay->content_id);
		if (bay->pismo)
	    		feature_clear(bay->dev_node, FEATURE_IDE0_reset);
		else
	    		feature_clear(bay->dev_node, FEATURE_IDE1_reset);
	    	bay->timer = MS_TO_HZ(MB_IDE_WAIT);
	    	bay->state = mb_ide_resetting;
#else
		printk(KERN_DEBUG "media-bay %d is ide (not compiled in kernel)\n", i);
		set_mb_power(i, 0);
#endif // #ifdef CONFIG_BLK_DEV_IDE
	    	break;
	    
#ifdef CONFIG_BLK_DEV_IDE
	case mb_ide_resetting:
	    	bay->timer = MS_TO_HZ(MB_IDE_TIMEOUT);
	    	bay->state = mb_ide_waiting;
		MBDBG("mediabay%d: waiting IDE ready (kind:%d)\n", i, bay->content_id);
	    	break;
	    
	case mb_ide_waiting:
	    	if (bay->cd_base == 0) {
			bay->timer = 0;
			bay->state = mb_up;
			MBDBG("mediabay%d: up before IDE init\n", i);
			break;
	    	} else if (MB_IDE_READY(i)) {
			bay->timer = 0;
			bay->state = mb_up;
			if (bay->cd_index < 0) {
				pmu_suspend();
				bay->cd_index = ide_register(bay->cd_base, 0, bay->cd_irq);
				pmu_resume();
			}
			if (bay->cd_index == -1) {
				/* We eventually do a retry */
				bay->cd_retry++;
				printk("IDE register error\n");
				set_mb_power(i, 0);
			} else {
				printk(KERN_DEBUG "media-bay %d is ide %d\n", i, bay->cd_index);
				MBDBG("mediabay %d IDE ready\n", i);
			}
			break;
	    	}
	    	if (bay->timer == 0) {
			printk("\nIDE Timeout in bay %d !\n", i);
			MBDBG("mediabay%d: nIDE Timeout !\n", i);
			set_mb_power(i, 0);
	    	}
		break;
#endif // #ifdef CONFIG_BLK_DEV_IDE

	case mb_powering_down:
	    	bay->state = mb_empty;
#ifdef CONFIG_BLK_DEV_IDE
    	        if (bay->cd_index >= 0) {
			printk(KERN_DEBUG "Unregistering mb %d ide, index:%d\n", i,
			       bay->cd_index);
			ide_unregister(bay->cd_index);
			bay->cd_index = -1;
		}
	    	if (bay->cd_retry) {
			if (bay->cd_retry > MAX_CD_RETRIES) {
				/* Should add an error sound (sort of beep in dmasound) */
				printk("\nmedia-bay %d, IDE device badly inserted or unrecognised\n", i);
			} else {
				/* Force a new power down/up sequence */
				bay->content_id = MB_NO;
			}
	    	}
#endif	    
		MBDBG("mediabay%d: end of power down\n", i);
	    	break;
	}
}

/*
 * This procedure runs as a kernel thread to poll the media bay
 * once each tick and register and unregister the IDE interface
 * with the IDE driver.  It needs to be a thread because
 * ide_register can't be called from interrupt context.
 */
int __pmac
media_bay_task(void *x)
{
	int	i;

	strcpy(current->comm, "media-bay");
#ifdef MB_IGNORE_SIGNALS
	sigfillset(&current->blocked);
#endif

	for (;;) {
		for (i = 0; i < media_bay_count; ++i)
			media_bay_step(i);

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
		if (signal_pending(current))
			return 0;
	}
}

void __pmac
poll_media_bay(int which)
{
	volatile struct media_bay_info* bay = &media_bays[which];
	int id = mb_content(bay);

	if (id == bay->last_value) {
	    if (id != bay->content_id
	        && ++bay->value_count >= MS_TO_HZ(MB_STABLE_DELAY)) {
	        /* If the device type changes without going thru "MB_NO", we force
	           a pass by "MB_NO" to make sure things are properly reset */
	        if ((id != MB_NO) && (bay->content_id != MB_NO)) {
	            id = MB_NO;
		    MBDBG("mediabay%d: forcing MB_NO\n", which);
		}
		MBDBG("mediabay%d: switching to %d\n", which, id);
		set_mb_power(which, id != MB_NO);
		bay->content_id = id;
		if (id == MB_NO) {
#ifdef CONFIG_BLK_DEV_IDE
		    bay->cd_retry = 0;
#endif
		    printk(KERN_INFO "media bay %d is empty\n", which);
		}
 	    }
	} else {
		bay->last_value = id;
		bay->value_count = 0;
	}
}


#ifdef CONFIG_PMAC_PBOOK
/*
 * notify clients before sleep and reset bus afterwards
 */
int __pmac
mb_notify_sleep(struct pmu_sleep_notifier *self, int when)
{
	volatile struct media_bay_info* bay;
	int i;
	
	switch (when) {
	case PBOOK_SLEEP_REQUEST:
	case PBOOK_SLEEP_REJECT:
		break;
		
	case PBOOK_SLEEP_NOW:
		for (i=0; i<media_bay_count; i++) {
			bay = &media_bays[i];
			set_mb_power(i, 0);
			mdelay(10);
		}
		break;
	case PBOOK_WAKE:
		for (i=0; i<media_bay_count; i++) {
			bay = &media_bays[i];
			/* We re-enable the bay using it's previous content
			   only if it did not change. Note those bozo timings,
			   they seem to help the 3400 get it right.
			 */
			/* Force MB power to 0 */
			set_mb_power(i, 0);
			mdelay(MB_POWER_DELAY);
			if (!bay->pismo)
				out_8(&bay->addr->contents, 0x70);
			mdelay(MB_STABLE_DELAY);
			if (mb_content(bay) != bay->content_id)
				continue;
			set_mb_power(i, 1);
			bay->last_value = bay->content_id;
			bay->value_count = MS_TO_HZ(MB_STABLE_DELAY);
			bay->timer = MS_TO_HZ(MB_POWER_DELAY);
#ifdef CONFIG_BLK_DEV_IDE
			bay->cd_retry = 0;
#endif
			do {
				mdelay(1000/HZ);
				media_bay_step(i);
			} while((media_bays[i].state != mb_empty) &&
				(media_bays[i].state != mb_up));
		}
		break;
	}
	return PBOOK_SLEEP_OK;
}
#endif /* CONFIG_PMAC_PBOOK */

