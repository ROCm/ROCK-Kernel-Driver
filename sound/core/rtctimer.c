/*
 *  RTC based high-frequency timer
 *
 *  Copyright (C) 2000 Takashi Iwai
 *	based on rtctimer.c by Steve Ratcliffe
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
 * 
 *================================================================
 * For enabling this timer, apply the patch file to your kernel.
 * The configure script checks the patch automatically.
 * The patches, rtc-xxx.dif, are found under utils/patches, where
 * xxx is the kernel version.
 *================================================================
 *
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/timer.h>
#include <sound/info.h>

#if defined(CONFIG_RTC) || defined(CONFIG_RTC_MODULE)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 2, 12)	/* FIXME: which 2.2.x kernel? */
#include <linux/rtc.h>
#else
#include <linux/mc146818rtc.h>
#endif

/* use tasklet for interrupt handling */
#define USE_TASKLET

#define RTC_FREQ	1024		/* default frequency */
#define NANO_SEC	1000000000L	/* 10^9 in sec */

/*
 * prototypes
 */
static int rtctimer_open(snd_timer_t *t);
static int rtctimer_close(snd_timer_t *t);
static int rtctimer_start(snd_timer_t *t);
static int rtctimer_stop(snd_timer_t *t);


/*
 * The harware depenant description for this timer.
 */
static struct _snd_timer_hardware rtc_hw = {
	flags:		SNDRV_TIMER_HW_FIRST|SNDRV_TIMER_HW_AUTO,
	ticks:		100000000L,		/* FIXME: XXX */
	open:		rtctimer_open,
	close:		rtctimer_close,
	start:		rtctimer_start,
	stop:		rtctimer_stop,
};

int rtctimer_freq = RTC_FREQ;		/* frequency */
static snd_timer_t *rtctimer;
static volatile int rtc_inc = 0;
static rtc_task_t rtc_task;

/* tasklet */
#ifdef USE_TASKLET
static struct tasklet_struct rtc_tq;
#endif

static int
rtctimer_open(snd_timer_t *t)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int
rtctimer_close(snd_timer_t *t)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
rtctimer_start(snd_timer_t *timer)
{
	rtc_task_t *rtc = timer->private_data;
	snd_assert(rtc != NULL, return -EINVAL);
	rtc_control(rtc, RTC_IRQP_SET, rtctimer_freq);
	rtc_control(rtc, RTC_PIE_ON, 0);
	rtc_inc = 0;
	return 0;
}

static int
rtctimer_stop(snd_timer_t *timer)
{
	rtc_task_t *rtc = timer->private_data;
	snd_assert(rtc != NULL, return -EINVAL);
	rtc_control(rtc, RTC_PIE_OFF, 0);
	return 0;
}

/*
 * interrupt
 */
static void rtctimer_interrupt(void *private_data)
{
	rtc_inc++;
#ifdef USE_TASKLET
	tasklet_hi_schedule(&rtc_tq);
#else
	snd_timer_interrupt((snd_timer_t*)private_data, rtc_inc);
	rtc_inc = 0;
#endif /* USE_TASKLET */
}

#ifdef USE_TASKLET
static void rtctimer_interrupt2(unsigned long private_data)
{
	snd_timer_t *timer = (snd_timer_t *)private_data;
	snd_assert(timer != NULL, return);
	do {
		snd_timer_interrupt(timer, 1);
	} while (--rtc_inc > 0);
}
#endif /* USE_TASKLET */

static void rtctimer_private_free(snd_timer_t *timer)
{
	rtc_task_t *rtc = timer->private_data;
	if (rtc)
		rtc_unregister(rtc);
}


/*
 *  ENTRY functions
 */
static int __init rtctimer_init(void)
{
	int order, err;
	snd_timer_t *timer;

	if (rtctimer_freq < 2 || rtctimer_freq > 8192) {
		snd_printk("rtctimer: invalid frequency %d\n", rtctimer_freq);
		return -EINVAL;
	}
	for (order = 1; rtctimer_freq > order; order <<= 1)
		;
	if (rtctimer_freq != order) {
		snd_printk("rtctimer: invalid frequency %d\n", rtctimer_freq);
		return -EINVAL;
	}

	/* Create a new timer and set up the fields */
	err = snd_timer_global_new("rtc", SNDRV_TIMER_GLOBAL_RTC, &timer);
	if (err < 0)
		return err;

#ifdef USE_TASKLET
	tasklet_init(&rtc_tq, rtctimer_interrupt2, (unsigned long)timer);
#endif /* USE_TASKLET */

	strcpy(timer->name, "RTC timer");
	timer->hw = rtc_hw;
	timer->hw.resolution = NANO_SEC / rtctimer_freq;

	/* register RTC callback */
	rtc_task.func = rtctimer_interrupt;
	rtc_task.private_data = timer;
	err = rtc_register(&rtc_task);
	if (err < 0) {
		snd_timer_global_free(timer);
		return err;
	}
	timer->private_data = &rtc_task;
	timer->private_free = rtctimer_private_free;

	err = snd_timer_global_register(timer);
	if (err < 0) {
		snd_timer_global_free(timer);
		return err;
	}
	rtctimer = timer;

	return 0;
}

static void __exit rtctimer_exit(void)
{
	if (rtctimer) {
		snd_timer_global_unregister(rtctimer);
		rtctimer = NULL;
	}
}


/*
 * exported stuffs
 */
module_init(rtctimer_init)
module_exit(rtctimer_exit)

MODULE_PARM(rtctimer_freq, "i");
MODULE_PARM_DESC(rtctimer_freq, "timer frequency in Hz");

MODULE_LICENSE("GPL");

EXPORT_NO_SYMBOLS;

#endif /* CONFIG_RTC || CONFIG_RTC_MODULE */
