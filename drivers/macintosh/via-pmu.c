/*
 * Device driver for the via-pmu on Apple Powermacs.
 *
 * The VIA (versatile interface adapter) interfaces to the PMU,
 * a 6805 microprocessor core whose primary function is to control
 * battery charging and system power on the PowerBook 3400 and 2400.
 * The PMU also controls the ADB (Apple Desktop Bus) which connects
 * to the keyboard and mouse, as well as the non-volatile RAM
 * and the RTC (real time clock) chip.
 *
 * Copyright (C) 1998 Paul Mackerras and Fabio Riccardi.
 * 
 * todo: - Check this driver for smp safety (new Core99 motherboards).
 *       - Cleanup synchro between VIA interrupt and GPIO-based PMU
 *         interrupt.
 *
 *
 */
#include <stdarg.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/cuda.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/init.h>
#include <asm/irq.h>
#include <asm/hardirq.h>
#include <asm/feature.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

/* Some compile options */
#undef SUSPEND_USES_PMU

/* Misc minor number allocated for /dev/pmu */
#define PMU_MINOR	154

static volatile unsigned char *via;

/* VIA registers - spaced 0x200 bytes apart */
#define RS		0x200		/* skip between registers */
#define B		0		/* B-side data */
#define A		RS		/* A-side data */
#define DIRB		(2*RS)		/* B-side direction (1=output) */
#define DIRA		(3*RS)		/* A-side direction (1=output) */
#define T1CL		(4*RS)		/* Timer 1 ctr/latch (low 8 bits) */
#define T1CH		(5*RS)		/* Timer 1 counter (high 8 bits) */
#define T1LL		(6*RS)		/* Timer 1 latch (low 8 bits) */
#define T1LH		(7*RS)		/* Timer 1 latch (high 8 bits) */
#define T2CL		(8*RS)		/* Timer 2 ctr/latch (low 8 bits) */
#define T2CH		(9*RS)		/* Timer 2 counter (high 8 bits) */
#define SR		(10*RS)		/* Shift register */
#define ACR		(11*RS)		/* Auxiliary control register */
#define PCR		(12*RS)		/* Peripheral control register */
#define IFR		(13*RS)		/* Interrupt flag register */
#define IER		(14*RS)		/* Interrupt enable register */
#define ANH		(15*RS)		/* A-side data, no handshake */

/* Bits in B data register: both active low */
#define TACK		0x08		/* Transfer acknowledge (input) */
#define TREQ		0x10		/* Transfer request (output) */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */
#define CB2_INT		0x08
#define CB1_INT		0x10		/* transition on CB1 input */

static volatile enum pmu_state {
	idle,
	sending,
	intack,
	reading,
	reading_intr,
} pmu_state;

static struct adb_request *current_req;
static struct adb_request *last_req;
static struct adb_request *req_awaiting_reply;
static unsigned char interrupt_data[256]; /* Made bigger: I've been told that might happen */
static unsigned char *reply_ptr;
static int data_index;
static int data_len;
static volatile int adb_int_pending;
static int pmu_adb_flags;
static int adb_dev_map = 0;
static struct adb_request bright_req_1, bright_req_2, bright_req_3;
static struct device_node *vias;
static int pmu_kind = PMU_UNKNOWN;
static int pmu_fully_inited = 0;
static int pmu_has_adb;
static unsigned char *gpio_reg = NULL;
static int gpio_irq = -1;
static volatile int pmu_suspended = 0;
static spinlock_t pmu_lock;

int asleep;
struct notifier_block *sleep_notifier_list;

#ifdef CONFIG_ADB
static int pmu_probe(void);
static int pmu_init(void);
static int pmu_send_request(struct adb_request *req, int sync);
static int pmu_adb_autopoll(int devs);
static int pmu_adb_reset_bus(void);
#endif /* CONFIG_ADB */

static int init_pmu(void);
static int pmu_queue_request(struct adb_request *req);
static void pmu_start(void);
static void via_pmu_interrupt(int irq, void *arg, struct pt_regs *regs);
static void send_byte(int x);
static void recv_byte(void);
static void pmu_sr_intr(struct pt_regs *regs);
static void pmu_done(struct adb_request *req);
static void pmu_handle_data(unsigned char *data, int len,
			    struct pt_regs *regs);
static void set_volume(int level);
static void gpio1_interrupt(int irq, void *arg, struct pt_regs *regs);
#ifdef CONFIG_PMAC_BACKLIGHT
static int pmu_set_backlight_level(int level, void* data);
static int pmu_set_backlight_enable(int on, int level, void* data);
#endif /* CONFIG_PMAC_BACKLIGHT */
#ifdef CONFIG_PMAC_PBOOK
static void pmu_pass_intr(unsigned char *data, int len);
#endif

#ifdef CONFIG_ADB
struct adb_driver via_pmu_driver = {
	"PMU",
	pmu_probe,
	pmu_init,
	pmu_send_request,
	pmu_adb_autopoll,
	pmu_poll,
	pmu_adb_reset_bus
};
#endif /* CONFIG_ADB */

extern void low_sleep_handler(void);
extern void sleep_save_intrs(int);
extern void sleep_restore_intrs(void);

extern int grackle_pcibios_read_config_word(unsigned char bus,
	unsigned char dev_fn, unsigned char offset, unsigned short *val);

extern int grackle_pcibios_write_config_word(unsigned char bus,
	unsigned char dev_fn, unsigned char offset, unsigned short val);

/*
 * This table indicates for each PMU opcode:
 * - the number of data bytes to be sent with the command, or -1
 *   if a length byte should be sent,
 * - the number of response bytes which the PMU will return, or
 *   -1 if it will send a length byte.
 */
static const s8 pmu_data_len[256][2] __openfirmwaredata = {
/*	   0	   1	   2	   3	   4	   5	   6	   7  */
/*00*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*08*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*10*/	{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*18*/	{ 0, 1},{ 0, 1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{ 0, 0},
/*20*/	{-1, 0},{ 0, 0},{ 2, 0},{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},
/*28*/	{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{ 0,-1},
/*30*/	{ 4, 0},{20, 0},{-1, 0},{ 3, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*38*/	{ 0, 4},{ 0,20},{ 2,-1},{ 2, 1},{ 3,-1},{-1,-1},{-1,-1},{ 4, 0},
/*40*/	{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*48*/	{ 0, 1},{ 0, 1},{-1,-1},{ 1, 0},{ 1, 0},{-1,-1},{-1,-1},{-1,-1},
/*50*/	{ 1, 0},{ 0, 0},{ 2, 0},{ 2, 0},{-1, 0},{ 1, 0},{ 3, 0},{ 1, 0},
/*58*/	{ 0, 1},{ 1, 0},{ 0, 2},{ 0, 2},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},
/*60*/	{ 2, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*68*/	{ 0, 3},{ 0, 3},{ 0, 2},{ 0, 8},{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},
/*70*/	{ 1, 0},{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*78*/	{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},{ 5, 1},{ 4, 1},{ 4, 1},
/*80*/	{ 4, 0},{-1, 0},{ 0, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*88*/	{ 0, 5},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*90*/	{ 1, 0},{ 2, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*98*/	{ 0, 1},{ 0, 1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*a0*/	{ 2, 0},{ 2, 0},{ 2, 0},{ 4, 0},{-1, 0},{ 0, 0},{-1, 0},{-1, 0},
/*a8*/	{ 1, 1},{ 1, 0},{ 3, 0},{ 2, 0},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*b0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*b8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*c0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*c8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*d0*/	{ 0, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*d8*/	{ 1, 1},{ 1, 1},{-1,-1},{-1,-1},{ 0, 1},{ 0,-1},{-1,-1},{-1,-1},
/*e0*/	{-1, 0},{ 4, 0},{ 0, 1},{-1, 0},{-1, 0},{ 4, 0},{-1, 0},{-1, 0},
/*e8*/	{ 3,-1},{-1,-1},{ 0, 1},{-1,-1},{ 0,-1},{-1,-1},{-1,-1},{ 0, 0},
/*f0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*f8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
};

static char *pbook_type[] = {
	"Unknown PowerBook",
	"PowerBook 2400/3400/3500(G3)",
	"PowerBook G3 Series",
	"1999 PowerBook G3",
	"Core99"
};

#ifdef CONFIG_PMAC_BACKLIGHT
static struct backlight_controller pmu_backlight_controller = {
	pmu_set_backlight_enable,
	pmu_set_backlight_level
};
#endif /* CONFIG_PMAC_BACKLIGHT */

int __openfirmware
find_via_pmu()
{
	if (via != 0)
		return 1;
	vias = find_devices("via-pmu");
	if (vias == 0)
		return 0;
	if (vias->next != 0)
		printk(KERN_WARNING "Warning: only using 1st via-pmu\n");

	if (vias->n_addrs < 1 || vias->n_intrs < 1) {
		printk(KERN_ERR "via-pmu: %d addresses, %d interrupts!\n",
		       vias->n_addrs, vias->n_intrs);
		if (vias->n_addrs < 1 || vias->n_intrs < 1)
			return 0;
	}

	spin_lock_init(&pmu_lock);

	pmu_has_adb = 1;

	if (vias->parent->name && ((strcmp(vias->parent->name, "ohare") == 0)
	    || device_is_compatible(vias->parent, "ohare")))
		pmu_kind = PMU_OHARE_BASED;
	else if (device_is_compatible(vias->parent, "paddington"))
		pmu_kind = PMU_PADDINGTON_BASED;
	else if (device_is_compatible(vias->parent, "heathrow"))
		pmu_kind = PMU_HEATHROW_BASED;
	else if (device_is_compatible(vias->parent, "Keylargo")) {
		struct device_node *gpio, *gpiop;

		pmu_kind = PMU_KEYLARGO_BASED;
		pmu_has_adb = (find_type_devices("adb") != NULL);

		gpiop = find_devices("gpio");
		if (gpiop && gpiop->n_addrs) {
			gpio_reg = ioremap(gpiop->addrs->address, 0x10);
			gpio = find_devices("extint-gpio1");
			if (gpio && gpio->parent == gpiop && gpio->n_intrs)
				gpio_irq = gpio->intrs[0].line;
		}
	} else
		pmu_kind = PMU_UNKNOWN;

	via = (volatile unsigned char *) ioremap(vias->addrs->address, 0x2000);

	out_8(&via[IER], IER_CLR | 0x7f);	/* disable all intrs */
	out_8(&via[IFR], 0x7f);			/* clear IFR */

	pmu_state = idle;

	if (!init_pmu()) {
		via = NULL;
		return 0;
	}

	printk(KERN_INFO "PMU driver initialized for %s\n",
	       pbook_type[pmu_kind]);
	       
	sys_ctrler = SYS_CTRLER_PMU;
	
	return 1;
}

#ifdef CONFIG_ADB
static int __openfirmware
pmu_probe()
{
	return vias == NULL? -ENODEV: 0;
}

static int __openfirmware
pmu_init(void)
{
	if (vias == NULL)
		return -ENODEV;
	return 0;
}
#endif /* CONFIG_ADB */

/*
 * We can't wait until pmu_init gets called, that happens too late.
 * It happens after IDE and SCSI initialization, which can take a few
 * seconds, and by that time the PMU could have given up on us and
 * turned us off.
 * This is called from arch/ppc/kernel/pmac_setup.c:pmac_init2().
 */
int via_pmu_start(void)
{
	if (vias == NULL)
		return -ENODEV;

	bright_req_1.complete = 1;
	bright_req_2.complete = 1;
	bright_req_3.complete = 1;

	if (request_irq(vias->intrs[0].line, via_pmu_interrupt, 0, "VIA-PMU",
			(void *)0)) {
		printk(KERN_ERR "VIA-PMU: can't get irq %d\n",
		       vias->intrs[0].line);
		return -EAGAIN;
	}

	if (pmu_kind == PMU_KEYLARGO_BASED && gpio_irq != -1) {
		if (request_irq(gpio_irq, gpio1_interrupt, 0, "GPIO1/ADB", (void *)0))
			printk(KERN_ERR "pmu: can't get irq %d (GPIO1)\n", gpio_irq);
	}

	/* Enable interrupts */
	out_8(&via[IER], IER_SET | SR_INT | CB1_INT);

	pmu_fully_inited = 1;

#ifdef CONFIG_PMAC_BACKLIGHT
	/* Enable backlight */
	register_backlight_controller(&pmu_backlight_controller, NULL, "pmu");
#endif /* CONFIG_PMAC_BACKLIGHT */

	/* Make sure PMU settle down before continuing. This is _very_ important
	 * since the IDE probe may shut interrupts down for quite a bit of time. If
	 * a PMU communication is pending while this happens, the PMU may timeout
	 * Not that on Core99 machines, the PMU keeps sending us environement
	 * messages, we should find a way to either fix IDE or make it call
	 * pmu_suspend() before masking interrupts. This can also happens while
	 * scolling with some fbdevs.
	 */
	do {
		pmu_poll();
	} while (pmu_state != idle);

	return 0;
}

static int __openfirmware
init_pmu()
{
	int timeout;
	struct adb_request req;

	out_8(&via[B], via[B] | TREQ);			/* negate TREQ */
	out_8(&via[DIRB], (via[DIRB] | TREQ) & ~TACK);	/* TACK in, TREQ out */

	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, 0xfc);
	timeout =  100000;
	while (!req.complete) {
		if (--timeout < 0) {
			printk(KERN_ERR "init_pmu: no response from PMU\n");
			return 0;
		}
		udelay(10);
		pmu_poll();
	}

	/* ack all pending interrupts */
	timeout = 100000;
	interrupt_data[0] = 1;
	while (interrupt_data[0] || pmu_state != idle) {
		if (--timeout < 0) {
			printk(KERN_ERR "init_pmu: timed out acking intrs\n");
			return 0;
		}
		if (pmu_state == idle)
			adb_int_pending = 1;
		via_pmu_interrupt(0, 0, 0);
		udelay(10);
	}

	/* Tell PMU we are ready. Which PMU support this ? */
	if (pmu_kind == PMU_KEYLARGO_BASED) {
		pmu_request(&req, NULL, 2, PMU_SYSTEM_READY, 2);
		while (!req.complete)
			pmu_poll();
	}
		
	return 1;
}

int
pmu_get_model(void)
{
	return pmu_kind;
}

#ifdef CONFIG_ADB
/* Send an ADB command */
static int __openfirmware
pmu_send_request(struct adb_request *req, int sync)
{
	int i, ret;

	if ((vias == NULL) || (!pmu_fully_inited)) {
		req->complete = 1;
		return -ENXIO;
	}

	ret = -EINVAL;

	switch (req->data[0]) {
	case PMU_PACKET:
		for (i = 0; i < req->nbytes - 1; ++i)
			req->data[i] = req->data[i+1];
		--req->nbytes;
		if (pmu_data_len[req->data[0]][1] != 0) {
			req->reply[0] = ADB_RET_OK;
			req->reply_len = 1;
		} else
			req->reply_len = 0;
		ret = pmu_queue_request(req);
		break;
	case CUDA_PACKET:
		switch (req->data[1]) {
		case CUDA_GET_TIME:
			if (req->nbytes != 2)
				break;
			req->data[0] = PMU_READ_RTC;
			req->nbytes = 1;
			req->reply_len = 3;
			req->reply[0] = CUDA_PACKET;
			req->reply[1] = 0;
			req->reply[2] = CUDA_GET_TIME;
			ret = pmu_queue_request(req);
			break;
		case CUDA_SET_TIME:
			if (req->nbytes != 6)
				break;
			req->data[0] = PMU_SET_RTC;
			req->nbytes = 5;
			for (i = 1; i <= 4; ++i)
				req->data[i] = req->data[i+1];
			req->reply_len = 3;
			req->reply[0] = CUDA_PACKET;
			req->reply[1] = 0;
			req->reply[2] = CUDA_SET_TIME;
			ret = pmu_queue_request(req);
			break;
		}
		break;
	case ADB_PACKET:
	    	if (!pmu_has_adb)
    			return -ENXIO;
		for (i = req->nbytes - 1; i > 1; --i)
			req->data[i+2] = req->data[i];
		req->data[3] = req->nbytes - 2;
		req->data[2] = pmu_adb_flags;
		/*req->data[1] = req->data[1];*/
		req->data[0] = PMU_ADB_CMD;
		req->nbytes += 2;
		req->reply_expected = 1;
		req->reply_len = 0;
		ret = pmu_queue_request(req);
		break;
	}
	if (ret) {
		req->complete = 1;
		return ret;
	}

	if (sync)
		while (!req->complete)
			pmu_poll();

	return 0;
}

/* Enable/disable autopolling */
static int __openfirmware
pmu_adb_autopoll(int devs)
{
	struct adb_request req;

	if ((vias == NULL) || (!pmu_fully_inited) || !pmu_has_adb)
		return -ENXIO;

	if (devs) {
		adb_dev_map = devs;
		pmu_request(&req, NULL, 5, PMU_ADB_CMD, 0, 0x86,
			    adb_dev_map >> 8, adb_dev_map);
		pmu_adb_flags = 2;
	} else {
		pmu_request(&req, NULL, 1, PMU_ADB_POLL_OFF);
		pmu_adb_flags = 0;
	}
	while (!req.complete)
		pmu_poll();
	return 0;
}

/* Reset the ADB bus */
static int __openfirmware
pmu_adb_reset_bus(void)
{
	struct adb_request req;
	int save_autopoll = adb_dev_map;

	if ((vias == NULL) || (!pmu_fully_inited) || !pmu_has_adb)
		return -ENXIO;

	/* anyone got a better idea?? */
	pmu_adb_autopoll(0);

	req.nbytes = 5;
	req.done = NULL;
	req.data[0] = PMU_ADB_CMD;
	req.data[1] = 0;
	req.data[2] = ADB_BUSRESET; /* 3 ??? */
	req.data[3] = 0;
	req.data[4] = 0;
	req.reply_len = 0;
	req.reply_expected = 1;
	if (pmu_queue_request(&req) != 0) {
		printk(KERN_ERR "pmu_adb_reset_bus: pmu_queue_request failed\n");
		return -EIO;
	}
	while (!req.complete)
		pmu_poll();

	if (save_autopoll != 0)
		pmu_adb_autopoll(save_autopoll);

	return 0;
}
#endif /* CONFIG_ADB */

/* Construct and send a pmu request */
int __openfirmware
pmu_request(struct adb_request *req, void (*done)(struct adb_request *),
	    int nbytes, ...)
{
	va_list list;
	int i;

	if (vias == NULL)
		return -ENXIO;

	if (nbytes < 0 || nbytes > 32) {
		printk(KERN_ERR "pmu_request: bad nbytes (%d)\n", nbytes);
		req->complete = 1;
		return -EINVAL;
	}
	req->nbytes = nbytes;
	req->done = done;
	va_start(list, nbytes);
	for (i = 0; i < nbytes; ++i)
		req->data[i] = va_arg(list, int);
	va_end(list);
	if (pmu_data_len[req->data[0]][1] != 0) {
		req->reply[0] = ADB_RET_OK;
		req->reply_len = 1;
	} else
		req->reply_len = 0;
	req->reply_expected = 0;
	return pmu_queue_request(req);
}

int __openfirmware
pmu_queue_request(struct adb_request *req)
{
	unsigned long flags;
	int nsend;

	if (via == NULL) {
		req->complete = 1;
		return -ENXIO;
	}
	if (req->nbytes <= 0) {
		req->complete = 1;
		return 0;
	}
	nsend = pmu_data_len[req->data[0]][0];
	if (nsend >= 0 && req->nbytes != nsend + 1) {
		req->complete = 1;
		return -EINVAL;
	}

	req->next = 0;
	req->sent = 0;
	req->complete = 0;

	spin_lock_irqsave(&pmu_lock, flags);
	if (current_req != 0) {
		last_req->next = req;
		last_req = req;
	} else {
		current_req = req;
		last_req = req;
		if (pmu_state == idle)
			pmu_start();
	}
	spin_unlock_irqrestore(&pmu_lock, flags);

	return 0;
}

static void __openfirmware
wait_for_ack(void)
{
	/* Sightly increased the delay, I had one occurence of the message
	 * reported
	 */
	int timeout = 4000;
	while ((in_8(&via[B]) & TACK) == 0) {
		if (--timeout < 0) {
			printk(KERN_ERR "PMU not responding (!ack)\n");
			return;
		}
		udelay(10);
	}
}

/* New PMU seems to be very sensitive to those timings, so we make sure
 * PCI is flushed immediately */
static void __openfirmware
send_byte(int x)
{
	volatile unsigned char *v = via;

	out_8(&v[ACR], in_8(&v[ACR]) | SR_OUT | SR_EXT);
	out_8(&v[SR], x);
	out_8(&v[B], in_8(&v[B]) & ~TREQ);		/* assert TREQ */
	(void)in_8(&v[B]);
}

static void __openfirmware
recv_byte()
{
	volatile unsigned char *v = via;

	out_8(&v[ACR], (in_8(&v[ACR]) & ~SR_OUT) | SR_EXT);
	in_8(&v[SR]);		/* resets SR */
	out_8(&v[B], in_8(&v[B]) & ~TREQ);
	(void)in_8(&v[B]);
}

static volatile int disable_poll;

static void __openfirmware
pmu_start()
{
	struct adb_request *req;

	/* assert pmu_state == idle */
	/* get the packet to send */
	req = current_req;
	if (req == 0 || pmu_state != idle
	    || (/*req->reply_expected && */req_awaiting_reply))
		return;

	pmu_state = sending;
	data_index = 1;
	data_len = pmu_data_len[req->data[0]][0];

	/* Sounds safer to make sure ACK is high before writing. This helped
	 * kill a problem with ADB and some iBooks
	 */
	wait_for_ack();
	/* set the shift register to shift out and send a byte */
	send_byte(req->data[0]);
}

void __openfirmware
pmu_poll()
{
	if (!via)
		return;
	if (disable_poll)
		return;
	/* Kicks ADB read when PMU is suspended */
	if (pmu_suspended)
		adb_int_pending = 1;
	do {
		via_pmu_interrupt(0, 0, 0);
	} while (pmu_suspended && (adb_int_pending || pmu_state != idle
		|| req_awaiting_reply));
}

/* This function loops until the PMU is idle and prevents it from
 * anwsering to ADB interrupts. pmu_request can still be called.
 * This is done to avoid spurrious shutdowns when we know we'll have
 * interrupts switched off for a long time
 */
void __openfirmware
pmu_suspend(void)
{
	unsigned long flags;
#ifdef SUSPEND_USES_PMU
	struct adb_request *req;
#endif
	if (!via)
		return;
	
	spin_lock_irqsave(&pmu_lock, flags);
	pmu_suspended++;
	if (pmu_suspended > 1) {
		spin_unlock_irqrestore(&pmu_lock, flags);
		return;
	}

	do {
		spin_unlock(&pmu_lock);
		via_pmu_interrupt(0, 0, 0);
		spin_lock(&pmu_lock);
		if (!adb_int_pending && pmu_state == idle && !req_awaiting_reply) {
#ifdef SUSPEND_USES_PMU
			pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, 0);
			spin_unlock_irqrestore(&pmu_lock, flags);
			while(!req.complete)
				pmu_poll();
#else /* SUSPEND_USES_PMU */
			if (gpio_irq >= 0)
				disable_irq(gpio_irq);
			out_8(&via[IER], CB1_INT | IER_CLR);
			spin_unlock_irqrestore(&pmu_lock, flags);
#endif /* SUSPEND_USES_PMU */
			break;
		}
	} while (1);
}

void __openfirmware
pmu_resume(void)
{
	unsigned long flags;

	if (!via || (pmu_suspended < 1))
		return;

	spin_lock_irqsave(&pmu_lock, flags);
	pmu_suspended--;
	if (pmu_suspended > 0) {
		spin_unlock_irqrestore(&pmu_lock, flags);
		return;
	}
	adb_int_pending = 1;
#ifdef SUSPEND_USES_PMU
	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, 0xfc);
	spin_unlock_irqrestore(&pmu_lock, flags);
	while(!req.complete)
		pmu_poll();
#else /* SUSPEND_USES_PMU */
	if (gpio_irq >= 0)
		enable_irq(gpio_irq);
	out_8(&via[IER], CB1_INT | IER_SET);
	spin_unlock_irqrestore(&pmu_lock, flags);
	pmu_poll();
#endif /* SUSPEND_USES_PMU */
}

static void __openfirmware
via_pmu_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	unsigned long flags;
	int intr;
	int nloop = 0;

	/* This is a bit brutal, we can probably do better */
	spin_lock_irqsave(&pmu_lock, flags);
	++disable_poll;
		
	while ((intr = in_8(&via[IFR])) != 0) {
		if (++nloop > 1000) {
			printk(KERN_DEBUG "PMU: stuck in intr loop, "
			       "intr=%x pmu_state=%d\n", intr, pmu_state);
			break;
		}
		if (intr & SR_INT)
			pmu_sr_intr(regs);
		else if (intr & CB1_INT) {
			adb_int_pending = 1;
			out_8(&via[IFR], CB1_INT);
		}
		intr &= ~(SR_INT | CB1_INT);
		if (intr != 0) {
			out_8(&via[IFR], intr);
		}
	}
	/* This is not necessary except if synchronous ADB requests are done
	 * with interrupts off, which should not happen. Since I'm not sure
	 * this "wiring" will remain, I'm commenting it out for now. Please do
	 * not remove. -- BenH.
	 */
#if 0
	if (gpio_reg && !pmu_suspended && (in_8(gpio_reg + 0x9) & 0x02) == 0)
		adb_int_pending = 1;
#endif

	if (pmu_state == idle) {
		if (adb_int_pending) {
			pmu_state = intack;
			/* Sounds safer to make sure ACK is high before writing.
			 * This helped kill a problem with ADB and some iBooks
			 */
			wait_for_ack();
			send_byte(PMU_INT_ACK);
			adb_int_pending = 0;
		} else if (current_req) {
			pmu_start();
		}
	}
	
	--disable_poll;
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void __openfirmware
gpio1_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	adb_int_pending = 1;
	via_pmu_interrupt(0, 0, 0);
}

static void __openfirmware
pmu_sr_intr(struct pt_regs *regs)
{
	struct adb_request *req;
	int bite;

	if (via[B] & TREQ) {
		printk(KERN_ERR "PMU: spurious SR intr (%x)\n", via[B]);
		out_8(&via[IFR], SR_INT);
		return;
	}
	/* This one seems to appear with PMU99. According to OF methods,
	 * the protocol didn't change...
	 */
	if (via[B] & TACK) {
		while ((in_8(&via[B]) & TACK) != 0)
			;
	}

	/* reset TREQ and wait for TACK to go high */
	out_8(&via[B], in_8(&via[B]) | TREQ);
	wait_for_ack();

	/* if reading grab the byte, and reset the interrupt */
	if (pmu_state == reading || pmu_state == reading_intr)
		bite = in_8(&via[SR]);

	out_8(&via[IFR], SR_INT);

	switch (pmu_state) {
	case sending:
		req = current_req;
		if (data_len < 0) {
			data_len = req->nbytes - 1;
			send_byte(data_len);
			break;
		}
		if (data_index <= data_len) {
			send_byte(req->data[data_index++]);
			break;
		}
		req->sent = 1;
		data_len = pmu_data_len[req->data[0]][1];
		if (data_len == 0) {
			pmu_state = idle;
			current_req = req->next;
			if (req->reply_expected)
				req_awaiting_reply = req;
			else {
				spin_unlock(&pmu_lock);
				pmu_done(req);
				spin_lock(&pmu_lock);
			}
		} else {
			pmu_state = reading;
			data_index = 0;
			reply_ptr = req->reply + req->reply_len;
			recv_byte();
		}
		break;

	case intack:
		data_index = 0;
		data_len = -1;
		pmu_state = reading_intr;
		reply_ptr = interrupt_data;
		recv_byte();
		break;

	case reading:
	case reading_intr:
		if (data_len == -1) {
			data_len = bite;
			if (bite > 32)
				printk(KERN_ERR "PMU: bad reply len %d\n",
				       bite);
		} else {
			reply_ptr[data_index++] = bite;
		}
		if (data_index < data_len) {
			recv_byte();
			break;
		}

		if (pmu_state == reading_intr) {
			spin_unlock(&pmu_lock);
			pmu_handle_data(interrupt_data, data_index, regs);
			spin_lock(&pmu_lock);
		} else {
			req = current_req;
			current_req = req->next;
			req->reply_len += data_index;
			spin_unlock(&pmu_lock);
			pmu_done(req);
			spin_lock(&pmu_lock);
		}
		pmu_state = idle;

		break;

	default:
		printk(KERN_ERR "via_pmu_interrupt: unknown state %d?\n",
		       pmu_state);
	}
}

static void __openfirmware
pmu_done(struct adb_request *req)
{
	req->complete = 1;
	if (req->done)
		(*req->done)(req);
}

/* Interrupt data could be the result data from an ADB cmd */
static void __openfirmware
pmu_handle_data(unsigned char *data, int len, struct pt_regs *regs)
{
	asleep = 0;
	if (len < 1) {
//		xmon_printk("empty ADB\n");
		adb_int_pending = 0;
		return;
	}
	if (data[0] & PMU_INT_ADB) {
		if ((data[0] & PMU_INT_ADB_AUTO) == 0) {
			struct adb_request *req = req_awaiting_reply;
			if (req == 0) {
				printk(KERN_ERR "PMU: extra ADB reply\n");
				return;
			}
			req_awaiting_reply = 0;
			if (len <= 2)
				req->reply_len = 0;
			else {
				memcpy(req->reply, data + 1, len - 1);
				req->reply_len = len - 1;
			}
			pmu_done(req);
		} else {
#ifdef CONFIG_XMON
			if (len == 4 && data[1] == 0x2c) {
				extern int xmon_wants_key, xmon_adb_keycode;
				if (xmon_wants_key) {
					xmon_adb_keycode = data[2];
					return;
				}
			}
#endif /* CONFIG_XMON */
#ifdef CONFIG_ADB
			/*
			 * XXX On the [23]400 the PMU gives us an up
			 * event for keycodes 0x74 or 0x75 when the PC
			 * card eject buttons are released, so we
			 * ignore those events.
			 */
			if (!(pmu_kind == PMU_OHARE_BASED && len == 4
			      && data[1] == 0x2c && data[3] == 0xff
			      && (data[2] & ~1) == 0xf4))
				adb_input(data+1, len-1, regs, 1);
#endif /* CONFIG_ADB */		
		}
	} else if (data[0] == 0x08 && len == 3) {
		/* sound/brightness buttons pressed */
#ifdef CONFIG_PMAC_BACKLIGHT
		set_backlight_level(data[1] >> 4);
#endif
		set_volume(data[2]);
	} else {
#ifdef CONFIG_PMAC_PBOOK
		pmu_pass_intr(data, len);
#endif
	}
}

#ifdef CONFIG_PMAC_BACKLIGHT
static int backlight_to_bright[] = {
	0x7f, 0x46, 0x42, 0x3e, 0x3a, 0x36, 0x32, 0x2e,
	0x2a, 0x26, 0x22, 0x1e, 0x1a, 0x16, 0x12, 0x0e
};
 
static int __openfirmware
pmu_set_backlight_enable(int on, int level, void* data)
{
	struct adb_request req;
	
	if (vias == NULL)
		return -ENODEV;

	if (on) {
		pmu_request(&req, NULL, 2, PMU_BACKLIGHT_BRIGHT,
			    backlight_to_bright[level]);
		while (!req.complete)
			pmu_poll();
	}
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL,
		    PMU_POW_BACKLIGHT | (on ? PMU_POW_ON : PMU_POW_OFF));
	while (!req.complete)
		pmu_poll();

	return 0;
}

static int __openfirmware
pmu_set_backlight_level(int level, void* data)
{
	if (vias == NULL)
		return -ENODEV;

	if (!bright_req_1.complete)
		return -EAGAIN;
	pmu_request(&bright_req_1, NULL, 2, PMU_BACKLIGHT_BRIGHT,
		backlight_to_bright[level]);
	if (!bright_req_2.complete)
		return -EAGAIN;
	pmu_request(&bright_req_2, NULL, 2, PMU_POWER_CTRL, PMU_POW_BACKLIGHT
		| (level > BACKLIGHT_OFF ? PMU_POW_ON : PMU_POW_OFF));

	return 0;
}
#endif /* CONFIG_PMAC_BACKLIGHT */

void __openfirmware
pmu_enable_irled(int on)
{
	struct adb_request req;

	if (vias == NULL)
		return ;
	if (pmu_kind == PMU_KEYLARGO_BASED)
		return ;

	pmu_request(&req, NULL, 2, PMU_POWER_CTRL, PMU_POW_IRLED |
	    (on ? PMU_POW_ON : PMU_POW_OFF));
	while (!req.complete)
		pmu_poll();
}

static void __openfirmware
set_volume(int level)
{
}

void __openfirmware
pmu_restart(void)
{
	struct adb_request req;

	cli();

	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, PMU_INT_ADB |
					PMU_INT_TICK );
	while(!req.complete)
		pmu_poll();

	pmu_request(&req, NULL, 1, PMU_RESET);
	while(!req.complete || (pmu_state != idle))
		pmu_poll();
	for (;;)
		;
}

void __openfirmware
pmu_shutdown(void)
{
	struct adb_request req;

	cli();

	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, PMU_INT_ADB |
					PMU_INT_TICK );
	while(!req.complete)
		pmu_poll();

	pmu_request(&req, NULL, 5, PMU_SHUTDOWN,
		    'M', 'A', 'T', 'T');
	while(!req.complete || (pmu_state != idle))
		pmu_poll();
	for (;;)
		;
}

int
pmu_present(void)
{
	return via != 0;
}

#ifdef CONFIG_PMAC_PBOOK

static LIST_HEAD(sleep_notifiers);

int
pmu_register_sleep_notifier(struct pmu_sleep_notifier *n)
{
	struct list_head *list;
	struct pmu_sleep_notifier *notifier;

	for (list = sleep_notifiers.next; list != &sleep_notifiers;
	     list = list->next) {
		notifier = list_entry(list, struct pmu_sleep_notifier, list);
		if (n->priority > notifier->priority)
			break;
	}
	__list_add(&n->list, list->prev, list);
	return 0;
}

int
pmu_unregister_sleep_notifier(struct pmu_sleep_notifier* n)
{
	if (n->list.next == 0)
		return -ENOENT;
	list_del(&n->list);
	n->list.next = 0;
	return 0;
}

/* Sleep is broadcast last-to-first */
static int
broadcast_sleep(int when, int fallback)
{
	int ret = PBOOK_SLEEP_OK;
	struct list_head *list;
	struct pmu_sleep_notifier *notifier;

	for (list = sleep_notifiers.prev; list != &sleep_notifiers;
	     list = list->prev) {
		notifier = list_entry(list, struct pmu_sleep_notifier, list);
		ret = notifier->notifier_call(notifier, when);
		if (ret != PBOOK_SLEEP_OK) {
			printk(KERN_DEBUG "sleep %d rejected by %p (%p)\n",
			       when, notifier, notifier->notifier_call);
			for (; list != &sleep_notifiers; list = list->next) {
				notifier = list_entry(list, struct pmu_sleep_notifier, list);
				notifier->notifier_call(notifier, fallback);
			}
			return ret;
		}
	}
	return ret;
}

/* Wake is broadcast first-to-last */
static int
broadcast_wake(void)
{
	int ret = PBOOK_SLEEP_OK;
	struct list_head *list;
	struct pmu_sleep_notifier *notifier;

	for (list = sleep_notifiers.next; list != &sleep_notifiers;
	     list = list->next) {
		notifier = list_entry(list, struct pmu_sleep_notifier, list);
		notifier->notifier_call(notifier, PBOOK_WAKE);
	}
	return ret;
}

/*
 * This struct is used to store config register values for
 * PCI devices which may get powered off when we sleep.
 */
static struct pci_save {
	u16	command;
	u16	cache_lat;
	u16	intr;
	u32	rom_address;
} *pbook_pci_saves;
static int n_pbook_pci_saves;

static void __openfirmware
pbook_pci_save(void)
{
	int npci;
	struct pci_dev *pd;
	struct pci_save *ps;

	npci = 0;
	pci_for_each_dev(pd) {
		++npci;
	}
	n_pbook_pci_saves = npci;
	if (npci == 0)
		return;
	ps = (struct pci_save *) kmalloc(npci * sizeof(*ps), GFP_KERNEL);
	pbook_pci_saves = ps;
	if (ps == NULL)
		return;

	pci_for_each_dev(pd) {
		pci_read_config_word(pd, PCI_COMMAND, &ps->command);
		pci_read_config_word(pd, PCI_CACHE_LINE_SIZE, &ps->cache_lat);
		pci_read_config_word(pd, PCI_INTERRUPT_LINE, &ps->intr);
		pci_read_config_dword(pd, PCI_ROM_ADDRESS, &ps->rom_address);
		++ps;
	}
}

static void __openfirmware
pbook_pci_restore(void)
{
	u16 cmd;
	struct pci_save *ps = pbook_pci_saves - 1;
	struct pci_dev *pd;
	int j;

	pci_for_each_dev(pd) {
		ps++;
		if (ps->command == 0)
			continue;
		pci_read_config_word(pd, PCI_COMMAND, &cmd);
		if ((ps->command & ~cmd) == 0)
			continue;
		switch (pd->hdr_type) {
		case PCI_HEADER_TYPE_NORMAL:
			for (j = 0; j < 6; ++j)
				pci_write_config_dword(pd,
					PCI_BASE_ADDRESS_0 + j*4,
					pd->resource[j].start);
			pci_write_config_dword(pd, PCI_ROM_ADDRESS,
				ps->rom_address);
			pci_write_config_word(pd, PCI_CACHE_LINE_SIZE,
				ps->cache_lat);
			pci_write_config_word(pd, PCI_INTERRUPT_LINE,
				ps->intr);
			pci_write_config_word(pd, PCI_COMMAND, ps->command);
			break;
			/* other header types not restored at present */
		}
	}
}

#if 0
/* N.B. This doesn't work on the 3400 */
void pmu_blink(int n)
{
	struct adb_request req;

	for (; n > 0; --n) {
		pmu_request(&req, NULL, 4, 0xee, 4, 0, 1);
		while (!req.complete) pmu_poll();
		udelay(50000);
		pmu_request(&req, NULL, 4, 0xee, 4, 0, 0);
		while (!req.complete) pmu_poll();
		udelay(50000);
	}
	udelay(150000);
}
#endif

/*
 * Put the powerbook to sleep.
 */
 
static u32 save_via[8];
static void save_via_state(void)
{
	save_via[0] = in_8(&via[ANH]);
	save_via[1] = in_8(&via[DIRA]);
	save_via[2] = in_8(&via[B]);
	save_via[3] = in_8(&via[DIRB]);
	save_via[4] = in_8(&via[PCR]);
	save_via[5] = in_8(&via[ACR]);
	save_via[6] = in_8(&via[T1CL]);
	save_via[7] = in_8(&via[T1CH]);
}
static void restore_via_state(void)
{
	out_8(&via[ANH], save_via[0]);
	out_8(&via[DIRA], save_via[1]);
	out_8(&via[B], save_via[2]);
	out_8(&via[DIRB], save_via[3]);
	out_8(&via[PCR], save_via[4]);
	out_8(&via[ACR], save_via[5]);
	out_8(&via[T1CL], save_via[6]);
	out_8(&via[T1CH], save_via[7]);
	out_8(&via[IER], IER_CLR | 0x7f);	/* disable all intrs */
	out_8(&via[IFR], 0x7f);				/* clear IFR */
	out_8(&via[IER], IER_SET | SR_INT | CB1_INT);
}

#define FEATURE_CTRL(base)	((unsigned int *)(base + 0x38))
#define	GRACKLE_PM	(1<<7)
#define GRACKLE_DOZE	(1<<5)
#define	GRACKLE_NAP	(1<<4)
#define	GRACKLE_SLEEP	(1<<3)

int __openfirmware powerbook_sleep_G3(void)
{
	unsigned long save_l2cr;
	unsigned long wait;
	unsigned short pmcr1;
	struct adb_request req;
	int ret, timeout;

	/* Notify device drivers */
	ret = broadcast_sleep(PBOOK_SLEEP_REQUEST, PBOOK_SLEEP_REJECT);
	if (ret != PBOOK_SLEEP_OK) {
		printk("pmu: sleep rejected\n");
		return -EBUSY;
	}

	/* Sync the disks. */
	/* XXX It would be nice to have some way to ensure that
	 * nobody is dirtying any new buffers while we wait.
	 * BenH: Moved to _after_ sleep request and changed video
	 * drivers to vmalloc() during sleep request. This way, all
	 * vmalloc's are done before actual sleep of block drivers */
	fsync_dev(0);

	/* Sleep can fail now. May not be very robust but useful for debugging */
	ret = broadcast_sleep(PBOOK_SLEEP_NOW, PBOOK_WAKE);
	if (ret != PBOOK_SLEEP_OK) {
		printk("pmu: sleep failed\n");
		return -EBUSY;
	}

	/* Give the disks a little time to actually finish writing */
	for (wait = jiffies + (HZ/2); time_before(jiffies, wait); )
		mb();

	/* Wait for completion of async backlight requests */
	while (!bright_req_1.complete || !bright_req_2.complete || !bright_req_3.complete)
		pmu_poll();
	
	/* Turn off various things. Darwin does some retry tests here... */
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL0, PMU_POW0_OFF|PMU_POW0_HARD_DRIVE);
	while (!req.complete)
		pmu_poll();
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL,
		PMU_POW_OFF|PMU_POW_BACKLIGHT|PMU_POW_IRLED|PMU_POW_MEDIABAY);
	while (!req.complete)
		pmu_poll();

	/* Disable all interrupts except pmu */
	sleep_save_intrs(vias->intrs[0].line);

	/* Make sure the PMU is idle */
	while (pmu_state != idle)
		pmu_poll();

	/* Make sure the decrementer won't interrupt us */
	asm volatile("mtdec %0" : : "r" (0x7fffffff));
	/* Make sure any pending DEC interrupt occuring while we did
	 * the above didn't re-enable the DEC */
	mb();
	asm volatile("mtdec %0" : : "r" (0x7fffffff));
	
	/* Giveup the FPU */
	if (current->thread.regs && (current->thread.regs->msr & MSR_FP) != 0)
		giveup_fpu(current);

	/* For 750, save backside cache setting and disable it */
	save_l2cr = _get_L2CR();	/* (returns 0 if not 750) */
	if (save_l2cr)
		_set_L2CR(0);

	/* Ask the PMU to put us to sleep */
	pmu_request(&req, NULL, 5, PMU_SLEEP, 'M', 'A', 'T', 'T');
	while (!req.complete)
		pmu_poll();

	/* The VIA is supposed not to be restored correctly*/
	save_via_state();
	/* We shut down some HW */
	feature_prepare_for_sleep();

	grackle_pcibios_read_config_word(0,0,0x70,&pmcr1);
	/* Apparently, MacOS uses NAP mode for Grackle ??? */
	pmcr1 &= ~(GRACKLE_DOZE|GRACKLE_SLEEP); 
	pmcr1 |= GRACKLE_PM|GRACKLE_NAP;
	grackle_pcibios_write_config_word(0, 0, 0x70, pmcr1);

	/* Call low-level ASM sleep handler */
	low_sleep_handler();

	/* We're awake again, stop grackle PM */
	grackle_pcibios_read_config_word(0, 0, 0x70, &pmcr1);
	pmcr1 &= ~(GRACKLE_PM|GRACKLE_DOZE|GRACKLE_SLEEP|GRACKLE_NAP); 
	grackle_pcibios_write_config_word(0, 0, 0x70, pmcr1);
	
	/* Restore things */
	feature_wake_up();
	restore_via_state();
	
	/* Restore L2 cache */
	if (save_l2cr)
 		_set_L2CR(save_l2cr);
	
	/* Restore userland MMU context */
	set_context(current->mm->context, current->mm->pgd);

	/* Re-enable DEC interrupts and kick DEC */
	asm volatile("mtdec %0" : : "r" (0x7fffffff));
	sti();
	asm volatile("mtdec %0" : : "r" (0x10000000));

	/* Power things up */
	pmu_request(&req, NULL, 2, PMU_SET_INTR_MASK, 0xfc);
	while (!req.complete)
		pmu_poll();
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL0,
			PMU_POW0_ON|PMU_POW0_HARD_DRIVE);
	while (!req.complete)
		pmu_poll();
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL,
			PMU_POW_ON|PMU_POW_BACKLIGHT|PMU_POW_CHARGER|PMU_POW_IRLED|PMU_POW_MEDIABAY);
	while (!req.complete)
		pmu_poll();

	/* ack all pending interrupts */
	timeout = 100000;
	interrupt_data[0] = 1;
	while (interrupt_data[0] || pmu_state != idle) {
		if (--timeout < 0)
			break;
		if (pmu_state == idle)
			adb_int_pending = 1;
		via_pmu_interrupt(0, 0, 0);
		udelay(10);
	}

	/* reenable interrupt controller */
	sleep_restore_intrs();

	/* Leave some time for HW to settle down */
	mdelay(100);

	/* Notify drivers */
	mdelay(10);
	broadcast_wake();

	return 0;
}

/* Not finished yet */
int __openfirmware powerbook_sleep_Core99(void)
{
	int ret;
	unsigned long save_l2cr;
	unsigned long wait;
	struct adb_request req;

	/* Notify device drivers */
	ret = broadcast_sleep(PBOOK_SLEEP_REQUEST, PBOOK_SLEEP_REJECT);
	if (ret != PBOOK_SLEEP_OK) {
		printk("pmu: sleep rejected\n");
		return -EBUSY;
	}

	/* Sync the disks. */
	/* XXX It would be nice to have some way to ensure that
	 * nobody is dirtying any new buffers while we wait.
	 * BenH: Moved to _after_ sleep request and changed video
	 * drivers to vmalloc() during sleep request. This way, all
	 * vmalloc's are done before actual sleep of block drivers */
	fsync_dev(0);

	/* Sleep can fail now. May not be very robust but useful for debugging */
	ret = broadcast_sleep(PBOOK_SLEEP_NOW, PBOOK_WAKE);
	if (ret != PBOOK_SLEEP_OK) {
		printk("pmu: sleep failed\n");
		return -EBUSY;
	}

	/* Give the disks a little time to actually finish writing */
	for (wait = jiffies + (HZ/4); time_before(jiffies, wait); )
		mb();

	/* Tell PMU what events will wake us up */
	pmu_request(&req, NULL, 4, PMU_POWER_EVENTS, PMU_PWR_CLR_WAKEUP_EVENTS,
		0xff, 0xff);
	while (!req.complete)
		pmu_poll();
	pmu_request(&req, NULL, 4, PMU_POWER_EVENTS, PMU_PWR_SET_WAKEUP_EVENTS,
		0, PMU_PWR_WAKEUP_KEY | PMU_PWR_WAKEUP_LID_OPEN);
	while (!req.complete)
		pmu_poll();
		
	/* Disable all interrupts except pmu */
	sleep_save_intrs(vias->intrs[0].line);

	/* Make sure the decrementer won't interrupt us */
	asm volatile("mtdec %0" : : "r" (0x7fffffff));

	/* Save the state of PCI config space for some slots */
	pbook_pci_save();

	feature_prepare_for_sleep();

	/* For 750, save backside cache setting and disable it */
	save_l2cr = _get_L2CR();	/* (returns 0 if not 750) */
	if (save_l2cr)
		_set_L2CR(0);

	if (current->thread.regs && (current->thread.regs->msr & MSR_FP) != 0)
		giveup_fpu(current);

	/* Ask the PMU to put us to sleep */
	pmu_request(&req, NULL, 5, PMU_SLEEP, 'M', 'A', 'T', 'T');
	while (!req.complete)
		mb();

	cli();
	while (pmu_state != idle)
		pmu_poll();

	/* Call low-level ASM sleep handler */
	low_sleep_handler();

	/* Make sure the PMU is idle */
	while (pmu_state != idle)
		pmu_poll();

	sti();

	feature_wake_up();
	pbook_pci_restore();

	set_context(current->mm->context, current->mm->pgd);

	/* Restore L2 cache */
	if (save_l2cr)
 		_set_L2CR(save_l2cr | 0x200000); /* set invalidate bit */

	/* reenable interrupts */
	sleep_restore_intrs();

	/* Tell PMU we are ready */
	pmu_request(&req, NULL, 2, PMU_SYSTEM_READY, 2);
	while (!req.complete)
		pmu_poll();
		
	/* Notify drivers */
	mdelay(10);
	broadcast_wake();

	return 0;
}

#define PB3400_MEM_CTRL		((unsigned int *)0xf8000070)

int __openfirmware powerbook_sleep_3400(void)
{
	int ret, i, x;
	unsigned long msr;
	unsigned int hid0;
	unsigned long p, wait;
	struct adb_request sleep_req;

	/* Notify device drivers */
	ret = broadcast_sleep(PBOOK_SLEEP_REQUEST, PBOOK_SLEEP_REJECT);
	if (ret != PBOOK_SLEEP_OK) {
		printk("pmu: sleep rejected\n");
		return -EBUSY;
	}

	/* Sync the disks. */
	/* XXX It would be nice to have some way to ensure that
	 * nobody is dirtying any new buffers while we wait.
	 * BenH: Moved to _after_ sleep request and changed video
	 * drivers to vmalloc() during sleep request. This way, all
	 * vmalloc's are done before actual sleep of block drivers */
	fsync_dev(0);

	/* Sleep can fail now. May not be very robust but useful for debugging */
	ret = broadcast_sleep(PBOOK_SLEEP_NOW, PBOOK_WAKE);
	if (ret != PBOOK_SLEEP_OK) {
		printk("pmu: sleep failed\n");
		return -EBUSY;
	}

	/* Give the disks a little time to actually finish writing */
	for (wait = jiffies + (HZ/4); time_before(jiffies, wait); )
		mb();

	/* Disable all interrupts except pmu */
	sleep_save_intrs(vias->intrs[0].line);

	/* Make sure the decrementer won't interrupt us */
	asm volatile("mtdec %0" : : "r" (0x7fffffff));

	/* Save the state of PCI config space for some slots */
	pbook_pci_save();

	/* Set the memory controller to keep the memory refreshed
	   while we're asleep */
	for (i = 0x403f; i >= 0x4000; --i) {
		out_be32(PB3400_MEM_CTRL, i);
		do {
			x = (in_be32(PB3400_MEM_CTRL) >> 16) & 0x3ff;
		} while (x == 0);
		if (x >= 0x100)
			break;
	}

	/* Ask the PMU to put us to sleep */
	pmu_request(&sleep_req, NULL, 5, PMU_SLEEP, 'M', 'A', 'T', 'T');
	while (!sleep_req.complete)
		mb();

	/* displacement-flush the L2 cache - necessary? */
	for (p = KERNELBASE; p < KERNELBASE + 0x100000; p += 0x1000)
		i = *(volatile int *)p;
	asleep = 1;

	/* Put the CPU into sleep mode */
	asm volatile("mfspr %0,1008" : "=r" (hid0) :);
	hid0 = (hid0 & ~(HID0_NAP | HID0_DOZE)) | HID0_SLEEP;
	asm volatile("mtspr 1008,%0" : : "r" (hid0));
	save_flags(msr);
	msr |= MSR_POW | MSR_EE;
	restore_flags(msr);
	udelay(10);

	/* OK, we're awake again, start restoring things */
	out_be32(PB3400_MEM_CTRL, 0x3f);
	pbook_pci_restore();

	/* wait for the PMU interrupt sequence to complete */
	while (asleep)
		mb();

	/* reenable interrupts */
	sleep_restore_intrs();

	/* Notify drivers */
	broadcast_wake();

	return 0;
}

/*
 * Support for /dev/pmu device
 */
#define RB_SIZE		10
struct pmu_private {
	struct list_head list;
	int	rb_get;
	int	rb_put;
	struct rb_entry {
		unsigned short len;
		unsigned char data[16];
	}	rb_buf[RB_SIZE];
	wait_queue_head_t wait;
	spinlock_t lock;
};

static LIST_HEAD(all_pmu_pvt);
static spinlock_t all_pvt_lock = SPIN_LOCK_UNLOCKED;

static void pmu_pass_intr(unsigned char *data, int len)
{
	struct pmu_private *pp;
	struct list_head *list;
	int i;
	unsigned long flags;

	if (len > sizeof(pp->rb_buf[0].data))
		len = sizeof(pp->rb_buf[0].data);
	spin_lock_irqsave(&all_pvt_lock, flags);
	for (list = &all_pmu_pvt; (list = list->next) != &all_pmu_pvt; ) {
		pp = list_entry(list, struct pmu_private, list);
		i = pp->rb_put + 1;
		if (i >= RB_SIZE)
			i = 0;
		if (i != pp->rb_get) {
			struct rb_entry *rp = &pp->rb_buf[pp->rb_put];
			rp->len = len;
			memcpy(rp->data, data, len);
			pp->rb_put = i;
			wake_up_interruptible(&pp->wait);
		}
	}
	spin_unlock_irqrestore(&all_pvt_lock, flags);
}

static int __openfirmware pmu_open(struct inode *inode, struct file *file)
{
	struct pmu_private *pp;
	unsigned long flags;

	pp = kmalloc(sizeof(struct pmu_private), GFP_KERNEL);
	if (pp == 0)
		return -ENOMEM;
	pp->rb_get = pp->rb_put = 0;
	spin_lock_init(&pp->lock);
	init_waitqueue_head(&pp->wait);
	spin_lock_irqsave(&all_pvt_lock, flags);
	list_add(&pp->list, &all_pmu_pvt);
	spin_unlock_irqrestore(&all_pvt_lock, flags);
	file->private_data = pp;
	return 0;
}

static ssize_t __openfirmware pmu_read(struct file *file, char *buf,
			size_t count, loff_t *ppos)
{
	struct pmu_private *pp = file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	int ret;

	if (count < 1 || pp == 0)
		return -EINVAL;
	ret = verify_area(VERIFY_WRITE, buf, count);
	if (ret)
		return ret;

	add_wait_queue(&pp->wait, &wait);
	current->state = TASK_INTERRUPTIBLE;

	for (;;) {
		ret = -EAGAIN;
		spin_lock(&pp->lock);
		if (pp->rb_get != pp->rb_put) {
			int i = pp->rb_get;
			struct rb_entry *rp = &pp->rb_buf[i];
			ret = rp->len;
			if (ret > count)
				ret = count;
			if (ret > 0 && copy_to_user(buf, rp->data, ret))
				ret = -EFAULT;
			if (++i >= RB_SIZE)
				i = 0;
			pp->rb_get = i;
		}
		spin_unlock(&pp->lock);
		if (ret >= 0)
			break;

		if (file->f_flags & O_NONBLOCK)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&pp->wait, &wait);

	return ret;
}

static ssize_t __openfirmware pmu_write(struct file *file, const char *buf,
			 size_t count, loff_t *ppos)
{
	return 0;
}

static unsigned int pmu_fpoll(struct file *filp, poll_table *wait)
{
	struct pmu_private *pp = filp->private_data;
	unsigned int mask = 0;

	if (pp == 0)
		return 0;
	poll_wait(filp, &pp->wait, wait);
	spin_lock(&pp->lock);
	if (pp->rb_get != pp->rb_put)
		mask |= POLLIN;
	spin_unlock(&pp->lock);
	return mask;
}

static int pmu_release(struct inode *inode, struct file *file)
{
	struct pmu_private *pp = file->private_data;
	unsigned long flags;

	lock_kernel();
	if (pp != 0) {
		file->private_data = 0;
		spin_lock_irqsave(&all_pvt_lock, flags);
		list_del(&pp->list);
		spin_unlock_irqrestore(&all_pvt_lock, flags);
		kfree(pp);
	}
	unlock_kernel();
	return 0;
}

/* Note: removed __openfirmware here since it causes link errors */
static int pmu_ioctl(struct inode * inode, struct file *filp,
		     u_int cmd, u_long arg)
{
	int error;

	switch (cmd) {
	case PMU_IOC_SLEEP:
		switch (pmu_kind) {
		case PMU_OHARE_BASED:
			error = powerbook_sleep_3400();
			break;
		case PMU_HEATHROW_BASED:
		case PMU_PADDINGTON_BASED:
			error = powerbook_sleep_G3();
			break;
#if 0 /* Not ready yet */
		case PMU_KEYLARGO_BASED:
			error = powerbook_sleep_Core99();
			break;
#endif			
		default:
			error = -ENOSYS;
		}
		return error;
#ifdef CONFIG_PMAC_BACKLIGHT
	/* Backlight should have its own device or go via
	 * the fbdev
	 */
	case PMU_IOC_GET_BACKLIGHT:
		error = get_backlight_level();
		if (error < 0)
			return error;
		return put_user(error, (__u32 *)arg);
	case PMU_IOC_SET_BACKLIGHT:
	{
		__u32 value;
		error = get_user(value, (__u32 *)arg);
		if (!error)
			error = set_backlight_level(value);
		return error;
	}
#endif /* CONFIG_PMAC_BACKLIGHT */
	case PMU_IOC_GET_MODEL:
	    	return put_user(pmu_kind, (__u32 *)arg);
	case PMU_IOC_HAS_ADB:
		return put_user(pmu_has_adb, (__u32 *)arg);
	}
	return -EINVAL;
}

static struct file_operations pmu_device_fops = {
	read:		pmu_read,
	write:		pmu_write,
	poll:		pmu_fpoll,
	ioctl:		pmu_ioctl,
	open:		pmu_open,
	release:	pmu_release,
};

static struct miscdevice pmu_device = {
	PMU_MINOR, "pmu", &pmu_device_fops
};

void pmu_device_init(void)
{
	if (via)
		misc_register(&pmu_device);
}
#endif /* CONFIG_PMAC_PBOOK */

#if 0
static inline void polled_handshake(volatile unsigned char *via)
{
	via[B] &= ~TREQ; eieio();
	while ((via[B] & TACK) != 0)
		;
	via[B] |= TREQ; eieio();
	while ((via[B] & TACK) == 0)
		;
}

static inline void polled_send_byte(volatile unsigned char *via, int x)
{
	via[ACR] |= SR_OUT | SR_EXT; eieio();
	via[SR] = x; eieio();
	polled_handshake(via);
}

static inline int polled_recv_byte(volatile unsigned char *via)
{
	int x;

	via[ACR] = (via[ACR] & ~SR_OUT) | SR_EXT; eieio();
	x = via[SR]; eieio();
	polled_handshake(via);
	x = via[SR]; eieio();
	return x;
}

int
pmu_polled_request(struct adb_request *req)
{
	unsigned long flags;
	int i, l, c;
	volatile unsigned char *v = via;

	req->complete = 1;
	c = req->data[0];
	l = pmu_data_len[c][0];
	if (l >= 0 && req->nbytes != l + 1)
		return -EINVAL;

	save_flags(flags); cli();
	while (pmu_state != idle)
		pmu_poll();

	polled_send_byte(v, c);
	if (l < 0) {
		l = req->nbytes - 1;
		polled_send_byte(v, l);
	}
	for (i = 1; i <= l; ++i)
		polled_send_byte(v, req->data[i]);

	l = pmu_data_len[c][1];
	if (l < 0)
		l = polled_recv_byte(v);
	for (i = 0; i < l; ++i)
		req->reply[i + req->reply_len] = polled_recv_byte(v);

	if (req->done)
		(*req->done)(req);

	restore_flags(flags);
	return 0;
}
#endif /* 0 */
