/*======================================================================

  Device driver for the PCMCIA control functionality of PXA2xx
  microprocessors.

    The contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL")

    (c) Ian Molton (spyro@f2s.com) 2003
    (c) Stefan Eletzhofer (stefan.eletzhofer@inquant.de) 2003,4

    derived from sa11xx_base.c

     Portions created by John G. Dorsey are
     Copyright (C) 1999 John G. Dorsey.

  ======================================================================*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/cpufreq.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>

#include "cs_internal.h"
#include "soc_common.h"
#include "pxa2xx_base.h"


#define MCXX_SETUP_MASK     (0x7f)
#define MCXX_ASST_MASK      (0x1f)
#define MCXX_HOLD_MASK      (0x3f)
#define MCXX_SETUP_SHIFT    (0)
#define MCXX_ASST_SHIFT     (7)
#define MCXX_HOLD_SHIFT     (14)

static inline u_int pxa2xx_mcxx_hold(u_int pcmcia_cycle_ns,
				     u_int mem_clk_10khz)
{
	u_int code = pcmcia_cycle_ns * mem_clk_10khz;
	return (code / 300000) + ((code % 300000) ? 1 : 0) - 1;
}

static inline u_int pxa2xx_mcxx_asst(u_int pcmcia_cycle_ns,
				     u_int mem_clk_10khz)
{
	u_int code = pcmcia_cycle_ns * mem_clk_10khz;
	return (code / 300000) + ((code % 300000) ? 1 : 0) - 1;
}

static inline u_int pxa2xx_mcxx_setup(u_int pcmcia_cycle_ns,
				      u_int mem_clk_10khz)
{
	u_int code = pcmcia_cycle_ns * mem_clk_10khz;
	return (code / 100000) + ((code % 100000) ? 1 : 0) - 1;
}

/* This function returns the (approximate) command assertion period, in
 * nanoseconds, for a given CPU clock frequency and MCXX_ASST value:
 */
static inline u_int pxa2xx_pcmcia_cmd_time(u_int mem_clk_10khz,
					   u_int pcmcia_mcxx_asst)
{
	return (300000 * (pcmcia_mcxx_asst + 1) / mem_clk_10khz);
}

static int pxa2xx_pcmcia_set_mcmem( int sock, int speed, int clock )
{
	MCMEM(sock) = ((pxa2xx_mcxx_setup(speed, clock)
		& MCXX_SETUP_MASK) << MCXX_SETUP_SHIFT)
		| ((pxa2xx_mcxx_asst(speed, clock)
		& MCXX_ASST_MASK) << MCXX_ASST_SHIFT)
		| ((pxa2xx_mcxx_hold(speed, clock)
		& MCXX_HOLD_MASK) << MCXX_HOLD_SHIFT);

	return 0;
}

static int pxa2xx_pcmcia_set_mcio( int sock, int speed, int clock )
{
	MCIO(sock) = ((pxa2xx_mcxx_setup(speed, clock)
		& MCXX_SETUP_MASK) << MCXX_SETUP_SHIFT)
		| ((pxa2xx_mcxx_asst(speed, clock)
		& MCXX_ASST_MASK) << MCXX_ASST_SHIFT)
		| ((pxa2xx_mcxx_hold(speed, clock)
		& MCXX_HOLD_MASK) << MCXX_HOLD_SHIFT);

	return 0;
}

static int pxa2xx_pcmcia_set_mcatt( int sock, int speed, int clock )
{
	MCATT(sock) = ((pxa2xx_mcxx_setup(speed, clock)
		& MCXX_SETUP_MASK) << MCXX_SETUP_SHIFT)
		| ((pxa2xx_mcxx_asst(speed, clock)
		& MCXX_ASST_MASK) << MCXX_ASST_SHIFT)
		| ((pxa2xx_mcxx_hold(speed, clock)
		& MCXX_HOLD_MASK) << MCXX_HOLD_SHIFT);

	return 0;
}

static int pxa2xx_pcmcia_set_mcxx(struct soc_pcmcia_socket *skt, unsigned int clk)
{
	struct soc_pcmcia_timing timing;
	int sock = skt->nr;

	soc_common_pcmcia_get_timing(skt, &timing);

	pxa2xx_pcmcia_set_mcmem(sock, timing.mem, clk);
	pxa2xx_pcmcia_set_mcatt(sock, timing.attr, clk);
	pxa2xx_pcmcia_set_mcio(sock, timing.io, clk);

	return 0;
}

static int pxa2xx_pcmcia_set_timing(struct soc_pcmcia_socket *skt)
{
	unsigned int clk = get_memclk_frequency_10khz();
	return pxa2xx_pcmcia_set_mcxx(skt, clk);
}

int pxa2xx_drv_pcmcia_probe(struct device *dev)
{
	int ret;
	struct pcmcia_low_level *ops;
	int first, nr;

	if (!dev || !dev->platform_data)
		return -ENODEV;

	ops = (struct pcmcia_low_level *)dev->platform_data;
	first = ops->first;
	nr = ops->nr;

	/* Setup GPIOs for PCMCIA/CF alternate function mode.
	 *
	 * It would be nice if set_GPIO_mode included support
	 * for driving GPIO outputs to default high/low state
	 * before programming GPIOs as outputs. Setting GPIO
	 * outputs to default high/low state via GPSR/GPCR
	 * before defining them as outputs should reduce
	 * the possibility of glitching outputs during GPIO
	 * setup. This of course assumes external terminators
	 * are present to hold GPIOs in a defined state.
	 *
	 * In the meantime, setup default state of GPIO
	 * outputs before we enable them as outputs.
	 */

	GPSR(GPIO48_nPOE) = GPIO_bit(GPIO48_nPOE) |
		GPIO_bit(GPIO49_nPWE) |
		GPIO_bit(GPIO50_nPIOR) |
		GPIO_bit(GPIO51_nPIOW) |
		GPIO_bit(GPIO52_nPCE_1) |
		GPIO_bit(GPIO53_nPCE_2);

	pxa_gpio_mode(GPIO48_nPOE_MD);
	pxa_gpio_mode(GPIO49_nPWE_MD);
	pxa_gpio_mode(GPIO50_nPIOR_MD);
	pxa_gpio_mode(GPIO51_nPIOW_MD);
	pxa_gpio_mode(GPIO52_nPCE_1_MD);
	pxa_gpio_mode(GPIO53_nPCE_2_MD);
	pxa_gpio_mode(GPIO54_pSKTSEL_MD); /* REVISIT: s/b dependent on num sockets */
	pxa_gpio_mode(GPIO55_nPREG_MD);
	pxa_gpio_mode(GPIO56_nPWAIT_MD);
	pxa_gpio_mode(GPIO57_nIOIS16_MD);

	/* Provide our PXA2xx specific timing routines. */
	ops->set_timing  = pxa2xx_pcmcia_set_timing;

	ret = soc_common_drv_pcmcia_probe(dev, ops, first, nr);

	if (ret == 0) {
		/*
		 * We have at least one socket, so set MECR:CIT
		 * (Card Is There)
		 */
		MECR |= MECR_CIT;

		/* Set MECR:NOS (Number Of Sockets) */
		if (nr > 1)
			MECR |= MECR_NOS;
		else
			MECR &= ~MECR_NOS;
	}

	return ret;
}
EXPORT_SYMBOL(pxa2xx_drv_pcmcia_probe);

static int pxa2xx_drv_pcmcia_suspend(struct device *dev, u32 state, u32 level)
{
	int ret = 0;
	if (level == SUSPEND_SAVE_STATE)
		ret = pcmcia_socket_dev_suspend(dev, state);
	return ret;
}

static int pxa2xx_drv_pcmcia_resume(struct device *dev, u32 level)
{
	int ret = 0;
	if (level == RESUME_RESTORE_STATE)
		ret = pcmcia_socket_dev_resume(dev);
	return ret;
}

static struct device_driver pxa2xx_pcmcia_driver = {
	.probe		= pxa2xx_drv_pcmcia_probe,
	.remove		= soc_common_drv_pcmcia_remove,
	.suspend 	= pxa2xx_drv_pcmcia_suspend,
	.resume 	= pxa2xx_drv_pcmcia_resume,
	.name		= "pxa2xx-pcmcia",
	.bus		= &platform_bus_type,
};

#ifdef CONFIG_CPU_FREQ

/*
 * When pxa2xx_pcmcia_notifier() decides that a MC{IO,MEM,ATT} adjustment (due
 * to a core clock frequency change) is needed, this routine establishes
 * new values consistent with the clock speed `clock'.
 */
static void pxa2xx_pcmcia_update_mcxx(unsigned int clock)
{
	struct soc_pcmcia_socket *skt;

	down(&soc_sockets_lock);
	list_for_each_entry(skt, &soc_sockets, node) {
		pxa2xx_pcmcia_set_mcxx(skt, clock);
	}
	up(&soc_sockets_lock);
}

/*
 * When changing the processor L clock frequency, it is necessary
 * to adjust the MCXX timings accordingly. We've recorded the timings
 * requested by Card Services, so this is just a matter of finding
 * out what our current speed is, and then recomputing the new MCXX
 * values.
 *
 * Returns: 0 on success, -1 on error
 */
static int
pxa2xx_pcmcia_notifier(struct notifier_block *nb, unsigned long val, void *data)
{
	struct cpufreq_freqs *freqs = data;

#warning "it's not clear if this is right since the core CPU (N) clock has no effect on the memory (L) clock"
	switch (val) {
		case CPUFREQ_PRECHANGE:
			if (freqs->new > freqs->old) {
				debug( 2, "new frequency %u.%uMHz > %u.%uMHz, "
						"pre-updating\n",
						freqs->new / 1000, (freqs->new / 100) % 10,
						freqs->old / 1000, (freqs->old / 100) % 10);
				pxa2xx_pcmcia_update_mcxx(freqs->new);
			}
			break;

		case CPUFREQ_POSTCHANGE:
			if (freqs->new < freqs->old) {
				debug( 2, "new frequency %u.%uMHz < %u.%uMHz, "
						"post-updating\n",
						freqs->new / 1000, (freqs->new / 100) % 10,
						freqs->old / 1000, (freqs->old / 100) % 10);
				pxa2xx_pcmcia_update_mcxx(freqs->new);
			}
			break;
	}

	return 0;
}

static struct notifier_block pxa2xx_pcmcia_notifier_block = {
	.notifier_call	= pxa2xx_pcmcia_notifier
};

static int __init pxa2xx_pcmcia_cpufreq_init(void)
{
	int ret;

	ret = cpufreq_register_notifier(&pxa2xx_pcmcia_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
	if (ret < 0)
		printk(KERN_ERR "Unable to register CPU frequency change "
				"notifier for PCMCIA (%d)\n", ret);
	return ret;
}

static void __exit pxa2xx_pcmcia_cpufreq_exit(void)
{
	cpufreq_unregister_notifier(&pxa2xx_pcmcia_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
}

#else
#define pxa2xx_pcmcia_cpufreq_init()
#define pxa2xx_pcmcia_cpufreq_exit()
#endif

static int __init pxa2xx_pcmcia_init(void)
{
	int ret = driver_register(&pxa2xx_pcmcia_driver);
	if (ret == 0)
		pxa2xx_pcmcia_cpufreq_init();
	return ret;
}

static void __exit pxa2xx_pcmcia_exit(void)
{
	pxa2xx_pcmcia_cpufreq_exit();
	driver_unregister(&pxa2xx_pcmcia_driver);
}

module_init(pxa2xx_pcmcia_init);
module_exit(pxa2xx_pcmcia_exit);

MODULE_AUTHOR("Stefan Eletzhofer <stefan.eletzhofer@inquant.de> and Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("Linux PCMCIA Card Services: PXA2xx core socket driver");
MODULE_LICENSE("GPL");
