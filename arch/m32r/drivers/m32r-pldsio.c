/* $Id$
 *
 * M32R onboard PLD serial module support.
 *
 * Much of the design and some of the code came from serial.c:
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997,
 *              1998, 1999  Theodore Ts'o
 *
 * M32R work:
 *  Copyright 1996, 2001, Mitsubishi Electric Corporation
 *  Copyright (C) 2000,2001  by Hiro Kondo, Hiro Takata, and Hitoshi Yamamoto.
 *
 *  2002-12-25: Support M32700UT Platform by Takeo Takahashi
 *  		Derived from dbg_console.c.
 */

static char *serial_version = "kondo";
static char *serial_revdate = "2002-09-11";
static char *serial_name = "M32R Serial driver";

#define LOCAL_VERSTRING ""

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/tty_driver.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/serialP.h>  /* serial_state */
#include <linux/slab.h>  /* kmalloc */

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/serial.h>
#include <asm/m32r.h>

extern struct console  console_for_debug;

static int psio_write(struct tty_struct *tty, int from_user,
			const unsigned char *buf, int count);
static int psio_write_room(struct tty_struct *tty);
static int psio_chars_in_buffer(struct tty_struct *tty);

static void dbg_console_write(struct console *, const char *, unsigned);
static kdev_t dbg_console_device(struct console *c);
//static void psio_interrupt_single(int, void *, struct pt_regs *);
void psio_interrupt_single(int, void *, struct pt_regs *);
static void psio_receive_chars(struct async_struct *, int *);

static void psio_wait_until_sent(struct tty_struct *, int);
static void change_speed(struct async_struct *,struct termios *);
static void autoconfig(struct serial_state *);
static unsigned detect_uart_irq (struct serial_state *);

static struct tty_driver psio_driver;
static int psio_refcount;

#define RS_STROBE_TIME (10*HZ)
#define RS_ISR_PASS_LIMIT 256

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

static struct async_struct *IRQ_ports[NR_IRQS];
static int IRQ_timeout[NR_IRQS];
#ifdef CONFIG_SERIAL_CONSOLE
static struct console cons;
static int lsr_break_flag;
#endif

#define BAUDRATE 115200        /* Set Baudrate */

/*
 * Here we define the default xmit fifo size used for each type of
 * UART
 */
static struct serial_uart_config uart_config[] = {
	{ "unknown", 1, 0 },
	{ "8250", 1, 0 },
	{ "16450", 1, 0 },
	{ "16550", 1, 0 },
	{ "16550A", 16, UART_CLEAR_FIFO | UART_USE_FIFO },
	{ "cirrus", 1, 0 },     /* usurped by cyclades.c */
	{ "ST16650", 1, UART_CLEAR_FIFO | UART_STARTECH },
	{ "ST16650V2", 32, UART_CLEAR_FIFO | UART_USE_FIFO |
		UART_STARTECH },
	{ "TI16750", 64, UART_CLEAR_FIFO | UART_USE_FIFO},
	{ "Startech", 1, 0},    /* usurped by cyclades.c */
	{ "16C950/954", 128, UART_CLEAR_FIFO | UART_USE_FIFO},
	{ "ST16654", 64, UART_CLEAR_FIFO | UART_USE_FIFO |
		UART_STARTECH },
	{ "XR16850", 128, UART_CLEAR_FIFO | UART_USE_FIFO |
		UART_STARTECH },
	{ "RSA", 2048, UART_CLEAR_FIFO | UART_USE_FIFO },
	{ "m32102", 1, 0 },
	{ 0, 0}
};

static struct serial_state rs_table[RS_TABLE_SIZE] = {
/*    UART CLK        PORT        IRQ   FLAGS        */
      { 0,   BAUDRATE, ((int)PLD_ESIO0CR + NONCACHE_OFFSET), PLD_IRQ_SIO0_RCV,   STD_COM_FLAGS },
};
#define NR_PORTS    (sizeof(rs_table)/sizeof(struct serial_state))


#define HIGH_BITS_OFFSET ((sizeof(long)-sizeof(int))*8)

static DECLARE_TASK_QUEUE(tq_psio_serial);
static struct tty_struct *psio_table[NR_PORTS];
static struct termios *psio_termios[NR_PORTS];
static struct termios *psio_termios_locked[NR_PORTS];

#if defined(MODULE) && defined(SERIAL_DEBUG_MCOUNT)
#define DBG_CNT(s) printk("(%s): [%x] refc=%d, serc=%d, ttyc=%d -> %s\n", \
 kdevname(tty->device), (info->flags), serial_refcount,info->count,tty->count,s
)
#else
#define DBG_CNT(s)
#endif


static struct timer_list serial_timer;

static unsigned char *tmp_buf;
#ifdef DECLARE_MUTEX
static DECLARE_MUTEX(tmp_buf_sem);
#else
static struct semaphore tmp_buf_sem = MUTEX;
#endif

static int dbg_console_setup(struct console *console, char *options);

#undef SIO0CR
#undef SIO0MOD0
#undef SIO0MOD1
#undef SIO0STS
#undef SIO0IMASK
#undef SIO0BAUR
#undef SIO0TXB
#undef SIO0RXB
#define SIO0CR     PLD_ESIO0CR
#define SIO0MOD0   PLD_ESIO0MOD0
#define SIO0MOD1   PLD_ESIO0MOD1
#define SIO0STS    PLD_ESIO0STS
#define SIO0IMASK  PLD_ESIO0INTCR
#define SIO0BAUR   PLD_ESIO0BAUR
#define SIO0TXB    PLD_ESIO0TXB
#define SIO0RXB    PLD_ESIO0RXB

#define SIO_IMASK_TEMPIE       (1UL<<1)  /* b14: enable */
#define SIO_IMASK_RXCEN        (1UL<<2)  /* b13: enable */
#define SIO_IMASK_REIE         (0UL)
#define	SIO_SIO0STS_TEMP       (1UL<<0)  /* Transmitter Register Empty */
#define	SIO_SIO0STS_TXCP       (1UL<<1)
#define	SIO_SIO0STS_RXCP       (1UL<<2)
#define	SIO_SIO0STS_OERR       (0UL)
#define	SIO_SIO0STS_PERR       (0UL)
#define	SIO_SIO0STS_FERR       (0UL)
#define	SIO_SIO0MOD0_CTSS      (1UL<<6)
#define	SIO_SIO0MOD0_RTSS      (1UL<<7)
#define	SIO_NONE               (0UL)

#define	UART_TX   	((unsigned char *)SIO0TXB - (unsigned char *)SIO0CR)
#define	UART_RX         ((unsigned char *)SIO0RXB - (unsigned char *)SIO0CR)
#define	UART_IER        ((unsigned char *)SIO0IMASK - (unsigned char *)SIO0CR)
#define UART_IER_THRI   	SIO_IMASK_TEMPIE
#define UART_IER_MSI    	SIO_NONE
#define UART_IER_RLSI   	SIO_IMASK_RXCEN
#define UART_IER_RDI    	SIO_IMASK_REIE
#define	UART_LSR        ((unsigned char *)SIO0STS - (unsigned char *)SIO0CR)
#define	UART_LSR_DR     	SIO_SIO0STS_RXCP
#define	UART_LSR_THRE   	SIO_SIO0STS_TEMP
#define	UART_LSR_TEMT   	SIO_SIO0STS_TXCP
#define UART_EMPTY		(UART_LSR_TEMT | UART_LSR_THRE)
#define	UART_LSR_BI     	SIO_NONE
#define	UART_LSR_PE     	SIO_SIO0STS_PERR
#define	UART_LSR_FE     	SIO_SIO0STS_FERR
#define	UART_LSR_OE     	SIO_SIO0STS_OERR
#define	UART_IIR	((unsigned char *)SIO0STS - (unsigned char *)SIO0CR)
#define	UART_LCR        ((unsigned char *)SIO0CR - (unsigned char *)SIO0CR)
#define	UART_LCR_SBC    	SIO_NONE
#define	UART_LCR_PARITY 	SIO_NONE
#define	UART_LCR_EPAR   	SIO_NONE
#define	UART_LCR_SPAR   	SIO_NONE
#define	UART_MCR        ((unsigned char *)SIO0MOD0 - (unsigned char *)SIO0CR)
#define	UART_MCR_RTS    	SIO_SIO0MOD0_RTSS
#define	UART_MCR_DTR    	SIO_NONE	/* SIO_SIO0MOD0_CTSS */
#define	UART_MCR_LOOP   	SIO_NONE
#define	UART_MCR_OUT1   	SIO_NONE
#define	UART_MCR_OUT2   	SIO_NONE
#define	UART_MSR        ((unsigned char *)SIO0MOD0 - (unsigned char *)SIO0CR)
#define	UART_MSR_DCD           	SIO_NONE
#define	UART_MSR_RI            	SIO_NONE
#define	UART_MSR_DSR           	SIO_NONE
#define	UART_MSR_CTS           	SIO_NONE

#define	UART_BAUR       ((unsigned char *)SIO0BAUR - (unsigned char *)SIO0CR)
#define	UART_MOD0   	((unsigned char *)SIO0MOD0 - (unsigned char *)SIO0CR)
#define	UART_MOD1       ((unsigned char *)SIO0MOD1 - (unsigned char *)SIO0CR)

static  inline unsigned int psio_in(struct async_struct *info,int offset)
{
	return *(volatile unsigned short *)(info->port + offset);
}
static inline void psio_out(struct async_struct *info,int offset,int value)
{
	*(volatile unsigned short *)(info->port + offset) = value;
}

#define serial_in(info, offset)            psio_in(info,(int)offset)
#define serial_out(info, offset, value)    psio_out(info,(int)offset, value)
#define serial_inp(info, offset)           psio_in(info,(int)offset)
#define serial_outp(info, offset, value)   psio_out(info,(int)offset, value)


static inline int serial_paranoia_check(struct async_struct *info,
				        kdev_t device, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct (%s) in %s\n";
	static const char *badinfo =
		"Warning: null async_struct for (%s) in %s\n";

	if (!info) {
		printk(badinfo, kdevname(device), routine);
		return 1;
	}
	if (info->magic != SERIAL_MAGIC) {
		printk(badmagic, kdevname(device), routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * ------------------------------------------------------------
 * psio_stop() and psio_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void psio_stop(struct tty_struct *tty)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "psio_stop"))
		return;

	save_flags(flags); cli();
	if (info->IER & UART_IER_THRI) {
		info->IER &= ~UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
	}
	restore_flags(flags);

}

static void psio_start(struct tty_struct *tty)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "psio_start"))
		return;

	save_flags(flags); cli();
	if (info->xmit.head != info->xmit.tail
		&& info->xmit.buf
		&& !(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
		serial_out(info, UART_TX, info->xmit.buf[info->xmit.tail]);
		info->xmit.tail = (info->xmit.tail + 1) & (SERIAL_XMIT_SIZE-1);
		info->state->icount.tx++;
	}
	restore_flags(flags);
}


/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 */
static inline void psio_sched_event(struct async_struct *info,int event)
{
	info->event |= 1 << event;
	queue_task(&info->tqueue, &tq_psio_serial);
	mark_bh(SERIAL_BH);
}

static inline void psio_receive_chars(struct async_struct *info,int *status)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch;
	struct  async_icount *icount;
	int max_count = 256;

	icount = &info->state->icount;
	do {
		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			tty->flip.tqueue.routine((void *) tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE)
				return;     // if TTY_DONT_FLIP is set
		}
		ch = serial_inp(info, UART_RX);
		*tty->flip.char_buf_ptr = ch;
		icount->rx++;

#ifdef SERIAL_DEBUG_INTR
		printk("DR%02x:%02x...", ch, *status);
#endif
		*tty->flip.flag_buf_ptr = 0;
		if (*status & (UART_LSR_BI | UART_LSR_PE |
			       UART_LSR_FE | UART_LSR_OE)) {
			/*
			 * For statistics only
			 */
			if (*status & UART_LSR_BI) {
				*status &= ~(UART_LSR_FE | UART_LSR_PE);
				icount->brk++;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
#if defined(CONFIG_SERIAL_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
				if (info->line == cons.index) {
					if (!break_pressed) {
						break_pressed = jiffies;
						goto ignore_char;
					}
					break_pressed = 0;
				}
#endif
			        if (info->flags & ASYNC_SAK)
					do_SAK(tty);
			} else if (*status & UART_LSR_PE)
				icount->parity++;
			else if (*status & UART_LSR_FE)
				icount->frame++;
			if (*status & UART_LSR_OE)
				icount->overrun++;

			/*
			 * Mask off conditions which should be ignored.
			 */
			*status &= info->read_status_mask;

#ifdef CONFIG_SERIAL_CONSOLE
			if (info->line == cons.index) {
				/* Recover the break flag from console xmit */
				*status |= lsr_break_flag;
				lsr_break_flag = 0;
			}
#endif
			if (*status & (UART_LSR_BI)) {
#ifdef SERIAL_DEBUG_INTR
				printk("handling break....");
#endif
				*tty->flip.flag_buf_ptr = TTY_BREAK;
			} else if (*status & UART_LSR_PE)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
		}
#if defined(CONFIG_SERIAL_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
		if (break_pressed && info->line == cons.index) {
			if (ch != 0 &&
				time_before(jiffies, break_pressed + HZ*5)) {
				handle_sysrq(ch, regs, NULL, NULL);
				break_pressed = 0;
				goto ignore_char;
			}
			break_pressed = 0;
		}
#endif
		if ((*status & info->ignore_status_mask) == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
#if defined(CONFIG_SERIAL_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
	ignore_char:
#endif
		*status = serial_inp(info, UART_LSR);
	} while ((*status & UART_LSR_DR) && (max_count-- > 0));
#if (LINUX_VERSION_CODE > 131394) /* 2.1.66 */
	tty_flip_buffer_push(tty);
#else
	queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
#endif
}

static void transmit_chars(struct async_struct *info, int *intr_done)
{
	int count;

	if (info->x_char) {
		// for M32102 serial
		// serial_outp(info, UART_TX, info->x_char);
		info->state->icount.tx++;
		info->x_char = 0;
		if (intr_done)
			*intr_done = 0;
		while((serial_in(info,UART_LSR) & UART_LSR_TEMT) == 0);
		return;
	}
	if (info->xmit.head == info->xmit.tail
		|| info->tty->stopped
		|| info->tty->hw_stopped) {
		info->IER &= ~UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
		return;
	}
	count = info->xmit_fifo_size;
	count = SERIAL_XMIT_SIZE-1;
	do {
		serial_out(info, UART_TX, info->xmit.buf[info->xmit.tail]);
		info->xmit.tail = (info->xmit.tail + 1) & (SERIAL_XMIT_SIZE-1);
		info->state->icount.tx++;

		if (info->xmit.head == info->xmit.tail)
			break;

		while((serial_in(info,UART_LSR) & UART_LSR_THRE) == 0);
	} while (--count > 0);
	while((serial_in(info,UART_LSR) & UART_LSR_TEMT) == 0);

	if (CIRC_CNT(info->xmit.head,
			 info->xmit.tail,
			 SERIAL_XMIT_SIZE) < WAKEUP_CHARS)
		psio_sched_event(info, RS_EVENT_WRITE_WAKEUP);

#ifdef SERIAL_DEBUG_INTR
	printk("THRE...");
#endif
	if (intr_done)
		*intr_done = 0;

	if (info->xmit.head == info->xmit.tail) {
		info->IER &= ~UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
	}
}


/*

#ifdef CONFIG_SERIAL_SHARE_IRQ
static void rs_interrupt(int irq, void *dev_id, struct pt_regs * regs)
#endif
*/

static void sio_reset(struct async_struct *info)
{
	unsigned int dummy;
	/* reset sio */
	/* read receive buffer */
	dummy = serial_inp(info, UART_RX);
	dummy = serial_inp(info, UART_RX);
	dummy = serial_inp(info, UART_LSR);
	serial_outp(info, UART_LCR, 0x0300);	/* RSCLR:1, TSCLR:1 */
	serial_outp(info, UART_LCR, 0x0003);	/* RSEN:1, TXEN:1 */
}

static void sio_error(struct async_struct *info,int status)
{
	unsigned int dummy;
	/* reset sio */
	printk("sio[%d] error[%04x]\n", info->line,status);
	/* read receive buffer */
	dummy = serial_inp(info, UART_RX);
	dummy = serial_inp(info, UART_RX);
	dummy = serial_inp(info, UART_LSR);
	serial_outp(info, UART_LCR, 0x0300);	/* RSCLR:1, TSCLR:1 */
	serial_outp(info, UART_LCR, 0x0003);	/* RSEN:1, TXEN:1 */
}

//static void psio_interrupt_single(int irq, void *dev_id, struct pt_regs * regs)
void psio_interrupt_single(int irq, void *dev_id, struct pt_regs * regs)
{
	int status;
	// int pass_counter = 0;
	struct async_struct * info;

#ifdef CONFIG_SERIAL_MULTIPORT
	int first_multi = 0;
	struct rs_multiport_struct *multi;
#endif

#ifdef SERIAL_DEBUG_INTR
	printk("psio_interrupt_single(%d)...", irq);
#endif

	info = IRQ_ports[irq&(~1)];
	if (!info || !info->tty)
		return;

#ifdef CONFIG_SERIAL_MULTIPORT
	multi = &rs_multiport[irq];
	if (multi->port_monitor)
		first_multi = inb(multi->port_monitor);
#endif

	{
		status = serial_inp(info, UART_LSR);
#ifdef SERIAL_DEBUG_INTR
		printk("status = %x...", status);
#endif
		if (status & UART_LSR_DR){
			psio_receive_chars(info, &status);
		}
		if ((serial_in(info,UART_LSR) & UART_EMPTY) != UART_EMPTY)
			sio_error(info, status);
		if (status & UART_LSR_THRE)
			transmit_chars(info, 0);
#ifdef SERIAL_DEBUG_INTR
		printk("IIR = %x...", serial_in(info, UART_IIR));
#endif
	}

	info->last_active = jiffies;
#ifdef CONFIG_SERIAL_MULTIPORT
	if (multi->port_monitor)
			printk("rs port monitor (single) irq %d: 0x%x, 0x%x\n",
			info->state->irq, first_multi,
			inb(multi->port_monitor));
#endif
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}
#ifdef CONFIG_SERIAL_MULTIPORT
static void rs_interrupt_multi(int irq, void *dev_id, struct pt_regs * regs)
{}
#endif

static void do_psio_serial_bh(void)
{
	run_task_queue(&tq_psio_serial);
}

static void do_softint(void *private_)
{
	struct async_struct *info = (struct async_struct *) private_;
	struct tty_struct *tty;

	tty = info->tty;
	if (!tty)
		return;

	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event)) {
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
			tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
#ifdef SERIAL_HAVE_POLL_WAIT
		wake_up_interruptible(&tty->poll_wait);
#endif
	}
}

static void psio_timer(void)
{
	static unsigned long last_strobe = 0;
	struct async_struct *info;
	unsigned int  i;
	unsigned long flags;

	if ((jiffies - last_strobe) >= RS_STROBE_TIME) {
		for (i=0; i < NR_IRQS; i++) {
			info = IRQ_ports[i];
			if (!info)
				continue;
			save_flags(flags); cli();
			psio_interrupt_single(i, NULL, NULL);
			restore_flags(flags);
		}
	}
	last_strobe = jiffies;

#if 1
	mod_timer(&serial_timer, jiffies + 10);
#else
	mod_timer(&serial_timer, jiffies + RS_STROBE_TIME);
#endif

	if (IRQ_ports[0]) {
		save_flags(flags); cli();
#ifdef CONFIG_SERIAL_SHARE_IRQ
		psio_interrupt(0, NULL, NULL);
#else
		psio_interrupt_single(0, NULL, NULL);
#endif
		restore_flags(flags);

		mod_timer(&serial_timer, jiffies + IRQ_timeout[0]);
	}
}

/*
 * ---------------------------------------------------------------
 * Low level utility subroutines for the serial driver:  routines to
 * figure out the appropriate timeout for an interrupt chain, routines
 * to initialize and startup a serial port, and routines to shutdown a
 * serial port.  Useful stuff like that.
 * ---------------------------------------------------------------
 */

/*
 * This routine figures out the correct timeout for a particular IRQ.
 * It uses the smallest timeout of all of the serial ports in a
 * particular interrupt chain.  Now only used for IRQ 0....
 */
static void figure_IRQ_timeout(int irq)
{
	struct  async_struct    *info;
	int timeout = 60*HZ;    /* 60 seconds === a long time :-) */

	info = IRQ_ports[irq];
	if (!info) {
		IRQ_timeout[irq] = 60*HZ;
		return;
	}
	while (info) {
		if (info->timeout < timeout)
			timeout = info->timeout;
		info = info->next_port;
	}
	if (!irq)
		timeout = timeout / 2;
	IRQ_timeout[irq] = timeout ? timeout : 1;
}

#ifdef CONFIG_SERIAL_RSA
/* Attempts to turn on the RSA FIFO.  Returns zero on failure */
static int enable_rsa(struct async_struct *info) {}

/* Attempts to turn off the RSA FIFO.  Returns zero on failure */
static int disable_rsa(struct async_struct *info) { }
#endif /* CONFIG_SERIAL_RSA */

static int startup(struct async_struct * info)
{
	unsigned long flags;
	int retval=0;
	void (*handler)(int, void *, struct pt_regs *);
	struct serial_state *state= info->state;
	unsigned long page;
#ifdef CONFIG_SERIAL_MANY_PORTS
	unsigned short ICP;
#endif

	page = get_zeroed_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	save_flags(flags); cli();

	if (info->flags & ASYNC_INITIALIZED) {
		free_page(page);
		goto errout;
	}
	if (info->xmit.buf)
		free_page(page);
	else
		info->xmit.buf = (unsigned char *) page;

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttyD%d (irq %d)...", info->line, state->irq);
#endif

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */

	/*
	 * Clear the interrupt registers.
	 */
	sio_reset(info);

	/*
	 * Allocate the IRQ if necessary
	 */
	if (state->irq && (!IRQ_ports[state->irq] ||
			   !IRQ_ports[state->irq]->next_port)) {
		if (IRQ_ports[state->irq]) {
#ifdef CONFIG_SERIAL_SHARE_IRQ
			free_irq(state->irq, &IRQ_ports[state->irq]);
			free_irq(state->irq+1, &IRQ_ports[state->irq]);
#ifdef CONFIG_SERIAL_MULTIPORT
			if (rs_multiport[state->irq].port1)
				handler = rs_interrupt_multi;
			else
#endif
				handler = psio_interrupt;
#else
			retval = -EBUSY;
			goto errout;
#endif /* CONFIG_SERIAL_SHARE_IRQ */
		} else
			handler = psio_interrupt_single;

		/* 020116 */
		retval = request_irq(state->irq, handler, SA_SHIRQ,
			"serial_rx", &IRQ_ports[state->irq]);
		retval = request_irq(state->irq+1, handler, SA_SHIRQ,
			"serial_tx", &IRQ_ports[state->irq]);
		if (retval) {
			if (capable(CAP_SYS_ADMIN)) {
				if (info->tty)
					set_bit(TTY_IO_ERROR,
						&info->tty->flags);
				retval = 0;
			}
			goto errout;
		}
	}

	/*
	 * Insert serial port into IRQ chain.
	 */
	info->prev_port = 0;
	info->next_port = IRQ_ports[state->irq];
	if (info->next_port)
		info->next_port->prev_port = info;
	IRQ_ports[state->irq] = info;
	figure_IRQ_timeout(state->irq);

	/*
	 * Now, initialize the UART
	 */
	/* for m32r @020113 */
	sio_reset(info);

	info->MCR = 0;
	if (info->tty->termios->c_cflag & CBAUD)
		info->MCR = UART_MCR_DTR | UART_MCR_RTS;
#ifdef CONFIG_SERIAL_MANY_PORTS
	if (info->flags & ASYNC_FOURPORT) {
		if (state->irq == 0)
			info->MCR |= UART_MCR_OUT1;
	} else
#endif
	{
		if (state->irq != 0)
			info->MCR |= UART_MCR_OUT2;
	}
	info->MCR |= ALPHA_KLUDGE_MCR;      /* Don't ask */
	serial_outp(info, UART_MCR, info->MCR);

	/*
	 * Finally, enable interrupts
	 */
	info->IER = UART_IER_MSI | UART_IER_RLSI | UART_IER_RDI;
	serial_outp(info, UART_IER, info->IER); /* enable interrupts */

#ifdef CONFIG_SERIAL_MANY_PORTS
	if (info->flags & ASYNC_FOURPORT) {
		/* Enable interrupts on the AST Fourport board */
		ICP = (info->port & 0xFE0) | 0x01F;
		outb_p(0x80, ICP);
		(void) inb_p(ICP);
	}
#endif

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void)serial_inp(info, UART_LSR);
	(void)serial_inp(info, UART_RX);
	(void)serial_inp(info, UART_IIR);
	(void)serial_inp(info, UART_MSR);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit.head = info->xmit.tail = 0;

	/*
	 * Set up serial timers...
	 */
	mod_timer(&serial_timer, jiffies + 2*HZ/100);

	/*
	 * Set up the tty->alt_speed kludge
	 */
#if (LINUX_VERSION_CODE >= 131394) /* Linux 2.1.66 */
	if (info->tty) {
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			info->tty->alt_speed = 57600;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			info->tty->alt_speed = 115200;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
			info->tty->alt_speed = 230400;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
			info->tty->alt_speed = 460800;
	}
#endif

	/*
	 * and set the speed of the serial port
	 */
	change_speed(info, 0);

	info->flags |= ASYNC_INITIALIZED;
	restore_flags(flags);
	return 0;

errout:
	restore_flags(flags);
	return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct async_struct * info)
{
	unsigned long   flags;
	struct serial_state *state;
	int     retval;

	if (!(info->flags & ASYNC_INITIALIZED))
		return;

	state = info->state;

#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....", info->line,
		   state->irq);
#endif

	save_flags(flags); cli(); /* Disable interrupts */

	/*
	 * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
	 * here so the queue might never be waken up
	 */
	wake_up_interruptible(&info->delta_msr_wait);

	/*
	 * First unlink the serial port from the IRQ chain...
	 */
	if (info->next_port)
		info->next_port->prev_port = info->prev_port;
	if (info->prev_port)
		info->prev_port->next_port = info->next_port;
	else
		IRQ_ports[state->irq] = info->next_port;
	figure_IRQ_timeout(state->irq);

	/*
	 * Free the IRQ, if necessary
	 */
	if (state->irq && (!IRQ_ports[state->irq] ||
			  !IRQ_ports[state->irq]->next_port)) {
		if (IRQ_ports[state->irq]) {
			/* 020116 */
			free_irq(state->irq+1, &IRQ_ports[state->irq]);
			retval = request_irq(state->irq+1, psio_interrupt_single,
				         SA_SHIRQ, "serial_xx",
				         &IRQ_ports[state->irq]);
			free_irq(state->irq, &IRQ_ports[state->irq]);
			retval = request_irq(state->irq, psio_interrupt_single,
				         SA_SHIRQ, "serial",
				         &IRQ_ports[state->irq]);

			if (retval)
				printk("serial shutdown: request_irq: error %d"
				       "  Couldn't reacquire IRQ.\n", retval);
		} else{
			free_irq(state->irq, &IRQ_ports[state->irq]);
			/* 020116 */
			free_irq(state->irq+1, &IRQ_ports[state->irq]);
		}
	}
	if (info->xmit.buf) {
		unsigned long pg = (unsigned long) info->xmit.buf;
		info->xmit.buf = 0;
		free_page(pg);
	}

	info->IER = 0;
	serial_outp(info, UART_IER, 0x00);  /* disable all intrs */
#ifdef CONFIG_SERIAL_MANY_PORTS
	if (info->flags & ASYNC_FOURPORT) {
		/* reset interrupts on the AST Fourport board */
		(void) inb((info->port & 0xFE0) | 0x01F);
		info->MCR |= UART_MCR_OUT1;
	} else
#endif
	if (!info->tty || (info->tty->termios->c_cflag & HUPCL))
		info->MCR &= ~(UART_MCR_DTR|UART_MCR_RTS);
	serial_outp(info, UART_MCR, info->MCR);

#ifdef CONFIG_SERIAL_RSA
	/*
	 * Reset the RSA board back to 115kbps compat mode.
	 */
	if ((state->type == PORT_RSA) &&
		(state->baud_base == SERIAL_RSA_BAUD_BASE &&
		 disable_rsa(info)))
		state->baud_base = SERIAL_RSA_BAUD_BASE_LO;
#endif


	(void)serial_in(info, UART_RX);    /* read data port to reset things */

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	sio_reset(info);

	info->flags &= ~ASYNC_INITIALIZED;
	restore_flags(flags);
}

static void change_speed(struct async_struct *info,struct termios *old_termios)
{
	int quot = 0, baud_base, baud;
	unsigned cflag, cval = 0;
	int bits;
	unsigned long   flags;
	unsigned mod0, mod1;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;
	if (!CONFIGURED_SERIAL_PORT(info))
		return;

	/* byte size and parity */
	switch (cflag & CSIZE) {
		  case CS5: mod1 = 0x05; bits = 7; break;
		  case CS6: mod1 = 0x06; bits = 8; break;
		  case CS7: mod1 = 0x07; bits = 9; break;
		  case CS8: mod1 = 0x08; bits = 10; break;
		  /* Never happens, but GCC is too dumb to figure it out */
		  default:  mod1 = 0x05; bits = 7; break;
	}
	mod1 <<= 8;
	mod0 = 0;
	if (cflag & CSTOPB) {
		mod0 |= 0x03;
		bits++;
	}
	if (cflag & PARENB) {
		mod0 |= 0x10;
		bits++;
	}
	if (!(cflag & PARODD)) {
		mod0 |= 0x4;
	}
	mod0 = 0x80;	/* Use RTS# output only */

	serial_outp(info, UART_MOD0, mod0);      /* */
	//serial_outp(info, UART_MOD1, mod1);
	//mod1 = 0;
	info->LCR = mod1;               /* Save LCR */

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(info->tty);
	if (!baud)
		baud = 9600;    /* B0 transition handled in rs_set_termios */
#ifdef CONFIG_SERIAL_RSA
	if ((info->state->type == PORT_RSA) &&
		(info->state->baud_base != SERIAL_RSA_BAUD_BASE) &&
		enable_rsa(info))
		info->state->baud_base = SERIAL_RSA_BAUD_BASE;
#endif
	baud_base = info->state->baud_base;

	if (baud == 38400 &&
		((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST))
		quot = info->state->custom_divisor;
	else {
		if (baud == 134)
			/* Special case since 134 is really 134.5 */
			quot = (2*baud_base / 269);
		else if (baud)
			quot = baud_base / baud;
	}
	/* If the quotient is zero refuse the change */
	if (!quot && old_termios) {
		info->tty->termios->c_cflag &= ~CBAUD;
		info->tty->termios->c_cflag |= (old_termios->c_cflag & CBAUD);
		baud = tty_get_baud_rate(info->tty);
		if (!baud)
			baud = 9600;
		if (baud == 38400 &&
			((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST))
			quot = info->state->custom_divisor;
		else {
			if (baud == 134)
				/* Special case since 134 is really 134.5 */
				quot = (2*baud_base / 269);
			else if (baud)
				quot = baud_base / baud ;
		}
	}
	quot = baud_base / (baud*4) ;
	/* As a last resort, if the quotient is zero, default to 9600 bps */
	if (!quot)
		quot = baud_base / 9600;
	/*
	 * Work around a bug in the Oxford Semiconductor 952 rev B
	 * chip which causes it to seriously miscalculate baud rates
	 * when DLL is 0.
	 */
	if (((quot & 0xFF) == 0) && (info->state->type == PORT_16C950) &&
		(info->state->revision == 0x5201))
		quot++;

	info->quot = quot;
	info->timeout = ((info->xmit_fifo_size*HZ*bits*quot) / baud_base);
	info->timeout += HZ/50;     /* Add .02 seconds of slop */

	/* CTS flow control flag and modem status interrupts */
	info->IER &= ~UART_IER_MSI;
	if (info->flags & ASYNC_HARDPPS_CD)
		info->IER |= UART_IER_MSI;
	if (cflag & CRTSCTS) {
		info->flags |= ASYNC_CTS_FLOW;
		info->IER |= UART_IER_MSI;
	} else
		info->flags &= ~ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~ASYNC_CHECK_CD;
	else {
		info->flags |= ASYNC_CHECK_CD;
		info->IER |= UART_IER_MSI;
	}
	serial_out(info, UART_IER, info->IER);

	/*
	 * Set up parity check flag
	 */
#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	info->read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (I_INPCK(info->tty))
		info->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (I_BRKINT(info->tty) || I_PARMRK(info->tty))
		info->read_status_mask |= UART_LSR_BI;

	/*
	 * Characters to ignore
	 */
	info->ignore_status_mask = 0;
	if (I_IGNPAR(info->tty))
		info->ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (I_IGNBRK(info->tty)) {
		info->ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(info->tty))
			info->ignore_status_mask |= UART_LSR_OE;
	}
	/*
	 * !!! ignore all characters if CREAD is not set
	 */
	if ((cflag & CREAD) == 0)
		info->ignore_status_mask |= UART_LSR_DR;
	cval = (baud_base / (baud * 4)) - 1;

	save_flags(flags); cli();
	serial_outp(info, UART_BAUR, cval );  /* set baurate reg */
	serial_outp(info, UART_LCR, 0x03);
	restore_flags(flags);
}

static void psio_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "psio_put_char"))
		return;

	if (!tty || !info->xmit.buf)
		return;

	save_flags(flags); cli();
	if (CIRC_SPACE(info->xmit.head,
			   info->xmit.tail,
			   SERIAL_XMIT_SIZE) == 0) {
		restore_flags(flags);
		return;
	}

	info->xmit.buf[info->xmit.head] = ch;
	info->xmit.head = (info->xmit.head + 1) & (SERIAL_XMIT_SIZE-1);
	restore_flags(flags);
}

static void psio_flush_chars(struct tty_struct *tty)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "psio_flush_chars"))
		return;

	if (info->xmit.head == info->xmit.tail
		|| tty->stopped
		|| tty->hw_stopped
		|| !info->xmit.buf)
		return;

	save_flags(flags); cli();
	if (!(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
		serial_out(info, UART_TX, info->xmit.buf[info->xmit.tail]);
		info->xmit.tail = (info->xmit.tail + 1) & (SERIAL_XMIT_SIZE-1);
		info->state->icount.tx++;
	}
	restore_flags(flags);
	while((serial_in(info,UART_LSR) & UART_EMPTY) != UART_EMPTY);
}

static int psio_write(struct tty_struct *tty, int from_user,
		    const unsigned char *buf, int count)
{
	int c, ret = 0;
	struct async_struct *info = (struct async_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "psio_write"))
		return 0;

	if (!tty || !info->xmit.buf || !tmp_buf)
		return 0;

	save_flags(flags);
	if (from_user) {
		down(&tmp_buf_sem);
		while (1) {
			int c1;
			c = CIRC_SPACE_TO_END(info->xmit.head,
					info->xmit.tail,
					SERIAL_XMIT_SIZE);
			if (count < c)
				c = count;
			if (c <= 0)
				break;

			c -= copy_from_user(tmp_buf, buf, c);
			if (!c) {
				if (!ret)
					ret = -EFAULT;
				break;
			}
			cli();
			c1 = CIRC_SPACE_TO_END(info->xmit.head,
					info->xmit.tail,
					SERIAL_XMIT_SIZE);
			if (c1 < c)
				c = c1;
			memcpy(info->xmit.buf + info->xmit.head, tmp_buf, c);
			info->xmit.head = ((info->xmit.head + c) &
					(SERIAL_XMIT_SIZE-1));
			restore_flags(flags);
			buf += c;
			count -= c;
			ret += c;
		}
		up(&tmp_buf_sem);
	} else {
		cli();
		while (1) {
			c = CIRC_SPACE_TO_END(info->xmit.head,
					info->xmit.tail,
					SERIAL_XMIT_SIZE);
			if (count < c)
				c = count;
			if (c <= 0) {
				break;
			}
			memcpy(info->xmit.buf + info->xmit.head, buf, c);
			info->xmit.head = ((info->xmit.head + c) &
				 (SERIAL_XMIT_SIZE-1));
			buf += c;
			count -= c;
			ret += c;
		}
		restore_flags(flags);
	}
	save_flags(flags); cli();
	if (info->xmit.head != info->xmit.tail
		&& !tty->stopped
		&& !tty->hw_stopped
		&& !(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		serial_out(info, UART_IER, info->IER);
		serial_out(info, UART_TX, info->xmit.buf[info->xmit.tail]);
		info->xmit.tail = (info->xmit.tail + 1) & (SERIAL_XMIT_SIZE-1);
		info->state->icount.tx++;
	}
	restore_flags(flags);
	while((serial_in(info,UART_LSR) & UART_EMPTY) != UART_EMPTY);
	return ret;
}

static int psio_write_room(struct tty_struct *tty)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;

	if (serial_paranoia_check(info, tty->device, "psio_write_room"))
		return 0;
	return CIRC_SPACE(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

static int psio_chars_in_buffer(struct tty_struct *tty)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;

	if (serial_paranoia_check(info, tty->device, "psio_chars_in_buffer"))
		return 0;
	return CIRC_CNT(info->xmit.head, info->xmit.tail, SERIAL_XMIT_SIZE);
}

static void psio_flush_buffer(struct tty_struct *tty)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "psio_flush_buffer"))
		return;
	save_flags(flags); cli();
	info->xmit.head = info->xmit.tail = 0;
	restore_flags(flags);
	wake_up_interruptible(&tty->write_wait);
#ifdef SERIAL_HAVE_POLL_WAIT
	wake_up_interruptible(&tty->poll_wait);
#endif
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
			 tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void psio_send_xchar(struct tty_struct *tty, char ch)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;

	if (serial_paranoia_check(info, tty->device, "psio_send_char"))
		return;

	info->x_char = ch;
	if (ch) {
		unsigned long flags;
		save_flags(flags); cli();
		if (!(info->IER & UART_IER_THRI)) {
	   		info->IER |= UART_IER_THRI;
			serial_out(info, UART_IER, info->IER);
			serial_out(info, UART_TX, info->x_char);
		}
		restore_flags(flags);
	}
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 *
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void psio_throttle(struct tty_struct * tty)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	char    buf[64];

	printk("throttle %s: %d....\n", tty_name(tty, buf),
		tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "psio_throttle"))
		return;

	if (I_IXOFF(tty))
		psio_send_xchar(tty, STOP_CHAR(tty));

	if (tty->termios->c_cflag & CRTSCTS)
		info->MCR &= ~UART_MCR_RTS;

	save_flags(flags); cli();
	serial_out(info, UART_MCR, info->MCR);
	restore_flags(flags);
}

static void psio_unthrottle(struct tty_struct * tty)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	char    buf[64];

	printk("unthrottle %s: %d....\n", tty_name(tty, buf),
		tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "psio_unthrottle"))
		return;

	if (I_IXOFF(tty)) {
		if (info->x_char) info->x_char = 0;
	}
	if (tty->termios->c_cflag & CRTSCTS)
		info->MCR |= UART_MCR_RTS;
	save_flags(flags); cli();
	serial_out(info, UART_MCR, info->MCR);
	restore_flags(flags);
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct async_struct * info,
			   struct serial_struct * retinfo)
{
	struct serial_struct tmp;
	struct serial_state *state = info->state;

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = state->type;
	tmp.line = state->line;
	tmp.port = state->port;
	if (HIGH_BITS_OFFSET)
		tmp.port_high = state->port >> HIGH_BITS_OFFSET;
	else
		tmp.port_high = 0;
	tmp.irq = state->irq;
	tmp.flags = state->flags;
	tmp.xmit_fifo_size = state->xmit_fifo_size;
	tmp.baud_base = state->baud_base;
	tmp.close_delay = state->close_delay;
	tmp.closing_wait = state->closing_wait;
	tmp.custom_divisor = state->custom_divisor;
	tmp.hub6 = state->hub6;
	tmp.io_type = state->io_type;
	if (copy_to_user(retinfo,&tmp,sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int set_serial_info(struct async_struct * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct serial_state old_state, *state;
	unsigned int		i,change_irq,change_port;
	int			retval = 0;
	unsigned long		new_port;

	if (copy_from_user(&new_serial,new_info,sizeof(new_serial)))
		return -EFAULT;
	state = info->state;
	old_state = *state;

	new_port = new_serial.port;
	if (HIGH_BITS_OFFSET)
		new_port += (unsigned long) new_serial.port_high << HIGH_BITS_OFFSET;

	change_irq = new_serial.irq != state->irq;
	change_port = (new_port != ((int) state->port)) ||
		(new_serial.hub6 != state->hub6);

	if (!capable(CAP_SYS_ADMIN)) {
		if (change_irq || change_port ||
		    (new_serial.baud_base != state->baud_base) ||
		    (new_serial.type != state->type) ||
		    (new_serial.close_delay != state->close_delay) ||
		    (new_serial.xmit_fifo_size != state->xmit_fifo_size) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		    (state->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		state->flags = ((state->flags & ~ASYNC_USR_MASK) |
					(new_serial.flags & ASYNC_USR_MASK));
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
				   (new_serial.flags & ASYNC_USR_MASK));
		state->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	new_serial.irq = irq_cannonicalize(new_serial.irq);

	if ((new_serial.irq >= NR_IRQS) || (new_serial.irq < 0) ||
	    (new_serial.baud_base < 9600)|| (new_serial.type < PORT_UNKNOWN) ||
	    (new_serial.type > PORT_MAX) || (new_serial.type == PORT_CIRRUS) ||
	    (new_serial.type == PORT_STARTECH)) {
		return -EINVAL;
	}

	if ((new_serial.type != state->type) ||
		(new_serial.xmit_fifo_size <= 0))
		new_serial.xmit_fifo_size =
			uart_config[new_serial.type].dfl_xmit_fifo_size;

	/* Make sure address is not already in use */
	if (new_serial.type) {
		for (i = 0 ; i < NR_PORTS; i++)
			if ((state != &rs_table[i]) &&
			    (rs_table[i].port == new_port) &&
			    rs_table[i].type)
				return -EADDRINUSE;
	}

	if ((change_port || change_irq) && (state->count > 1))
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	state->baud_base = new_serial.baud_base;
	state->flags = ((state->flags & ~ASYNC_FLAGS) |
			(new_serial.flags & ASYNC_FLAGS));
	info->flags = ((state->flags & ~ASYNC_INTERNAL_FLAGS) |
			   (info->flags & ASYNC_INTERNAL_FLAGS));
	state->custom_divisor = new_serial.custom_divisor;
	state->close_delay = new_serial.close_delay * HZ/100;
	state->closing_wait = new_serial.closing_wait * HZ/100;
#if (LINUX_VERSION_CODE > 0x20100)
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;
#endif
	info->xmit_fifo_size = state->xmit_fifo_size =
		new_serial.xmit_fifo_size;

	if ((state->type != PORT_UNKNOWN) && state->port) {
#ifdef CONFIG_SERIAL_RSA
		if (old_state.type == PORT_RSA)
			release_region(state->port + UART_RSA_BASE, 16);
		else
#endif
		release_region(state->port,8);
	}
	state->type = new_serial.type;
	if (change_port || change_irq) {
		/*
		 * We need to shutdown the serial port at the old
		 * port/irq combination.
		 */
		shutdown(info);
		state->irq = new_serial.irq;
		info->port = state->port = new_port;
		info->hub6 = state->hub6 = new_serial.hub6;
		if (info->hub6)
			info->io_type = state->io_type = SERIAL_IO_HUB6;
		else if (info->io_type == SERIAL_IO_HUB6)
			info->io_type = state->io_type = SERIAL_IO_PORT;
	}
	if ((state->type != PORT_UNKNOWN) && state->port) {
#ifdef CONFIG_SERIAL_RSA
		if (state->type == PORT_RSA)
			request_region(state->port + UART_RSA_BASE,
				       16, "serial_rsa(set)");
		else
#endif
			request_region(state->port,8,"serial(set)");
	}


check_and_exit:
	if (!state->port || !state->type)
		return 0;
	if (info->flags & ASYNC_INITIALIZED) {
		if (((old_state.flags & ASYNC_SPD_MASK) !=
			 (state->flags & ASYNC_SPD_MASK)) ||
			(old_state.custom_divisor != state->custom_divisor)) {
#if (LINUX_VERSION_CODE >= 131394) /* Linux 2.1.66 */
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
				info->tty->alt_speed = 57600;
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
				info->tty->alt_speed = 115200;
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
				info->tty->alt_speed = 230400;
			if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
				info->tty->alt_speed = 460800;
#endif
			change_speed(info, 0);
		}
	} else
		retval = startup(info);
	return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *      is emptied.  On bus types like RS485, the transmitter must
 *      release the bus after transmitting. This must be done when
 *      the transmit shift register is empty, not be done when the
 *      transmit holding register is empty.  This functionality
 *      allows an RS485 driver to be written in user space.
 */
static int get_lsr_info(struct async_struct * info, unsigned int *value)
{
	unsigned char status;
	unsigned int result;
	unsigned long flags;

	save_flags(flags); cli();
	status = serial_in(info, UART_LSR);
	restore_flags(flags);
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);

	/*
	 * If we're about to load something into the transmit
	 * register, we'll pretend the transmitter isn't empty to
	 * avoid a race condition (depending on when the transmit
	 * interrupt happens).
	 */
	if (info->x_char ||
		((CIRC_CNT(info->xmit.head, info->xmit.tail,
			   SERIAL_XMIT_SIZE) > 0) &&
		 !info->tty->stopped && !info->tty->hw_stopped))
		result &= ~TIOCSER_TEMT;

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}


static int get_modem_info(struct async_struct * info, unsigned int *value)
{
	unsigned char control, status;
	unsigned int result;
	unsigned long flags;

	control = info->MCR;
	save_flags(flags); cli();
	status = serial_in(info, UART_MSR);
	restore_flags(flags);
	result =  ((control & UART_MCR_RTS) ? TIOCM_RTS : 0)
		| ((control & UART_MCR_DTR) ? TIOCM_DTR : 0)
#ifdef TIOCM_OUT1
		| ((control & UART_MCR_OUT1) ? TIOCM_OUT1 : 0)
		| ((control & UART_MCR_OUT2) ? TIOCM_OUT2 : 0)
#endif
		| ((status  & UART_MSR_DCD) ? TIOCM_CAR : 0)
		| ((status  & UART_MSR_RI) ? TIOCM_RNG : 0)
		| ((status  & UART_MSR_DSR) ? TIOCM_DSR : 0)
		| ((status  & UART_MSR_CTS) ? TIOCM_CTS : 0);

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}
static int set_modem_info(struct async_struct * info, unsigned int cmd,
			  unsigned int *value)
{
	unsigned int arg;
	unsigned long flags;

	if (copy_from_user(&arg, value, sizeof(int)))
		return -EFAULT;

	switch (cmd) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS)
			info->MCR |= UART_MCR_RTS;
		if (arg & TIOCM_DTR)
			info->MCR |= UART_MCR_DTR;
#ifdef TIOCM_OUT1
		if (arg & TIOCM_OUT1)
			info->MCR |= UART_MCR_OUT1;
		if (arg & TIOCM_OUT2)
			info->MCR |= UART_MCR_OUT2;
#endif
		if (arg & TIOCM_LOOP)
			info->MCR |= UART_MCR_LOOP;
		break;
	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			info->MCR &= ~UART_MCR_RTS;
		if (arg & TIOCM_DTR)
			info->MCR &= ~UART_MCR_DTR;
#ifdef TIOCM_OUT1
		if (arg & TIOCM_OUT1)
			info->MCR &= ~UART_MCR_OUT1;
		if (arg & TIOCM_OUT2)
			info->MCR &= ~UART_MCR_OUT2;
#endif
		if (arg & TIOCM_LOOP)
			info->MCR &= ~UART_MCR_LOOP;
		break;
	case TIOCMSET:
		info->MCR = ((info->MCR & ~(UART_MCR_RTS |
#ifdef TIOCM_OUT1
				        UART_MCR_OUT1 |
				        UART_MCR_OUT2 |
#endif
				        UART_MCR_LOOP |
				        UART_MCR_DTR))
				 | ((arg & TIOCM_RTS) ? UART_MCR_RTS : 0)
#ifdef TIOCM_OUT1
				 | ((arg & TIOCM_OUT1) ? UART_MCR_OUT1 : 0)
				 | ((arg & TIOCM_OUT2) ? UART_MCR_OUT2 : 0)
#endif
				 | ((arg & TIOCM_LOOP) ? UART_MCR_LOOP : 0)
				 | ((arg & TIOCM_DTR) ? UART_MCR_DTR : 0));
		break;
	default:
		return -EINVAL;
	}
	save_flags(flags); cli();
	info->MCR |= ALPHA_KLUDGE_MCR;      /* Don't ask */
	serial_out(info, UART_MCR, info->MCR);
	restore_flags(flags);
	return 0;
}

static int do_autoconfig(struct async_struct * info)
{
	int irq, retval;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (info->state->count > 1)
		return -EBUSY;

	shutdown(info);

	autoconfig(info->state);
	if ((info->state->flags & ASYNC_AUTO_IRQ) &&
		(info->state->port != 0  || info->state->iomem_base != 0) &&
		(info->state->type != PORT_UNKNOWN)) {
		irq = detect_uart_irq(info->state);
		if (irq > 0)
			info->state->irq = irq;
	}

	retval = startup(info);
	if (retval)
		return retval;
	return 0;
}

/*
 * rs_break() --- routine which turns the break handling on or off
 */
#if (LINUX_VERSION_CODE < 131394) /* Linux 2.1.66 */
static void send_break( struct async_struct * info, int duration)
{
	if (!CONFIGURED_SERIAL_PORT(info))
		return;
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + duration;
	cli();
	info->LCR |= UART_LCR_SBC;
	serial_out(info, UART_LCR, 0x3);
	schedule();
	info->LCR &= ~UART_LCR_SBC;
	serial_out(info, UART_LCR, 0x3);
	sti();
}
#else
static void psio_break(struct tty_struct *tty, int break_state)
{
	struct async_struct * info = (struct async_struct *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_break"))
		return;

	if (!CONFIGURED_SERIAL_PORT(info))
		return;
	save_flags(flags); cli();
	if (break_state == -1)
		info->LCR |= UART_LCR_SBC;
	else
		info->LCR &= ~UART_LCR_SBC;
	restore_flags(flags);
}
#endif

#ifdef CONFIG_SERIAL_MULTIPORT
static int get_multiport_struct(struct async_struct * info,
				struct serial_multiport_struct *retinfo)
{
	struct serial_multiport_struct ret;
	struct rs_multiport_struct *multi;

	multi = &rs_multiport[info->state->irq];

	ret.port_monitor = multi->port_monitor;

	ret.port1 = multi->port1;
	ret.mask1 = multi->mask1;
	ret.match1 = multi->match1;

	ret.port2 = multi->port2;
	ret.mask2 = multi->mask2;
	ret.match2 = multi->match2;

	ret.port3 = multi->port3;
	ret.mask3 = multi->mask3;
	ret.match3 = multi->match3;

	ret.port4 = multi->port4;
	ret.mask4 = multi->mask4;
	ret.match4 = multi->match4;

	ret.irq = info->state->irq;

	if (copy_to_user(retinfo,&ret,sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int set_multiport_struct(struct async_struct * info,
				struct serial_multiport_struct *in_multi)
{
	struct serial_multiport_struct new_multi;
	struct rs_multiport_struct *multi;
	struct serial_state *state;
	int was_multi, now_multi;
	int retval;
	void (*handler)(int, void *, struct pt_regs *);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	state = info->state;

	if (copy_from_user(&new_multi, in_multi,
			   sizeof(struct serial_multiport_struct)))
		return -EFAULT;

	if (new_multi.irq != state->irq || state->irq == 0 ||
		!IRQ_ports[state->irq])
		return -EINVAL;

	multi = &rs_multiport[state->irq];
	was_multi = (multi->port1 != 0);

	multi->port_monitor = new_multi.port_monitor;

	if (multi->port1)
		release_region(multi->port1,1);
	multi->port1 = new_multi.port1;
	multi->mask1 = new_multi.mask1;
	multi->match1 = new_multi.match1;
	if (multi->port1)
		request_region(multi->port1,1,"serial(multiport1)");

	if (multi->port2)
		release_region(multi->port2,1);
	multi->port2 = new_multi.port2;
	multi->mask2 = new_multi.mask2;
	multi->match2 = new_multi.match2;
	if (multi->port2)
		request_region(multi->port2,1,"serial(multiport2)");

	if (multi->port3)
		release_region(multi->port3,1);
	multi->port3 = new_multi.port3;
	multi->mask3 = new_multi.mask3;
	multi->match3 = new_multi.match3;
	if (multi->port3)
		request_region(multi->port3,1,"serial(multiport3)");

	if (multi->port4)
		release_region(multi->port4,1);
	multi->port4 = new_multi.port4;
	multi->mask4 = new_multi.mask4;
	multi->match4 = new_multi.match4;
	if (multi->port4)
		request_region(multi->port4,1,"serial(multiport4)");

	now_multi = (multi->port1 != 0);

	if (IRQ_ports[state->irq]->next_port &&
		(was_multi != now_multi)) {
		free_irq(state->irq, &IRQ_ports[state->irq]);
		if (now_multi)
			handler = rs_interrupt_multi;
		else
			handler = rs_interrupt;

		retval = request_irq(state->irq, handler, SA_SHIRQ,
				     "serial", &IRQ_ports[state->irq]);
		if (retval) {
			printk("Couldn't reallocate serial interrupt "
				   "driver!!\n");
		}
	}
	return 0;
}
#endif

static int psio_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	struct async_struct * info = (struct async_struct *)tty->driver_data;
	struct async_icount cprev, cnow;    /* kernel counter temps */
	struct serial_icounter_struct icount;
	unsigned long flags;
#if (LINUX_VERSION_CODE < 131394) /* Linux 2.1.66 */
	int retval, tmp;
#endif

	if (serial_paranoia_check(info, tty->device, "rs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
		(cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
		(cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}

	switch (cmd) {
#if (LINUX_VERSION_CODE < 131394) /* Linux 2.1.66 */
		case TCSBRK:    /* SVID version: non-zero arg --> no break */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (signal_pending(current))
				return -EINTR;
			if (!arg) {
				send_break(info, HZ/4); /* 1/4 second */
				if (signal_pending(current))
				    return -EINTR;
			}
			return 0;
		case TCSBRKP:   /* support for POSIX tcsendbreak() */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (signal_pending(current))
				return -EINTR;
			send_break(info, arg ? arg*(HZ/10) : HZ/4);
			if (signal_pending(current))
				return -EINTR;
			return 0;
		case TIOCGSOFTCAR:
			tmp = C_CLOCAL(tty) ? 1 : 0;
			if (copy_to_user((void *)arg, &tmp, sizeof(int)))
				return -EFAULT;
			return 0;
		case TIOCSSOFTCAR:
			if (copy_from_user(&tmp, (void *)arg, sizeof(int)))
				return -EFAULT;

			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				 (tmp ? CLOCAL : 0));
			return 0;
#endif
		case TIOCMGET:
			return get_modem_info(info, (unsigned int *) arg);
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			return set_modem_info(info, cmd, (unsigned int *) arg);
		case TIOCGSERIAL:
			return get_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
				               (struct serial_struct *) arg);
		case TIOCSERCONFIG:
			return do_autoconfig(info);

		case TIOCSERGETLSR: /* Get line status register */
			return get_lsr_info(info, (unsigned int *) arg);

		case TIOCSERGSTRUCT:
			if (copy_to_user((struct async_struct *) arg,
			    info, sizeof(struct async_struct)))
				return -EFAULT;
			return 0;

#ifdef CONFIG_SERIAL_MULTIPORT
		case TIOCSERGETMULTI:
			return get_multiport_struct(info,
				       (struct serial_multiport_struct *) arg);
		case TIOCSERSETMULTI:
			return set_multiport_struct(info,
				       (struct serial_multiport_struct *) arg);
#endif

		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
		case TIOCMIWAIT:
			save_flags(flags); cli();
			/* note the counters on entry */
			cprev = info->state->icount;
			restore_flags(flags);
			/* Force modem status interrupts on */
			info->IER |= UART_IER_MSI;
			serial_out(info, UART_IER, info->IER);
			while (1) {
				interruptible_sleep_on(&info->delta_msr_wait);
				/* see if a signal did it */
				if (signal_pending(current))
				    return -ERESTARTSYS;
				save_flags(flags); cli();
				cnow = info->state->icount; /* atomic copy */
				restore_flags(flags);
				if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
				    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				    return -EIO; /* no change => error */
				if ( ((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
				     ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
				     ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
				     ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts)) ) {
				    return 0;
				}
				cprev = cnow;
			}
			/* NOTREACHED */

		/*
		 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
		 * Return: write counters to the user passed counter struct
		 * NB: both 1->0 and 0->1 transitions are counted except for
		 *     RI where only 0->1 is counted.
		 */
		case TIOCGICOUNT:
			save_flags(flags); cli();
			cnow = info->state->icount;
			restore_flags(flags);
			icount.cts = cnow.cts;
			icount.dsr = cnow.dsr;
			icount.rng = cnow.rng;
			icount.dcd = cnow.dcd;
			icount.rx = cnow.rx;
			icount.tx = cnow.tx;
			icount.frame = cnow.frame;
			icount.overrun = cnow.overrun;
			icount.parity = cnow.parity;
			icount.brk = cnow.brk;
			icount.buf_overrun = cnow.buf_overrun;

			if (copy_to_user((void *)arg, &icount, sizeof(icount)))
				return -EFAULT;
			return 0;
		case TIOCSERGWILD:
		case TIOCSERSWILD:
			/* "setserial -W" is called in Debian boot */
			printk ("TIOCSER?WILD ioctl obsolete, ignored.\n");
			return 0;

		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void psio_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct async_struct *info = (struct async_struct *)tty->driver_data;
	unsigned long flags;
	unsigned int cflag = tty->termios->c_cflag;

	if (   (cflag == old_termios->c_cflag)
		&& (   RELEVANT_IFLAG(tty->termios->c_iflag)
		== RELEVANT_IFLAG(old_termios->c_iflag)))
	  return;

	change_speed(info, old_termios);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) &&
		!(cflag & CBAUD)) {
		info->MCR &= ~(UART_MCR_DTR|UART_MCR_RTS);
		save_flags(flags); cli();
		serial_out(info, UART_MCR, info->MCR);
		restore_flags(flags);
	}

	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) &&
		(cflag & CBAUD)) {
		info->MCR |= UART_MCR_DTR;
		if (!(tty->termios->c_cflag & CRTSCTS) ||
			!test_bit(TTY_THROTTLED, &tty->flags)) {
			info->MCR |= UART_MCR_RTS;
		}
		save_flags(flags); cli();
		serial_out(info, UART_MCR, info->MCR);
		restore_flags(flags);
	}

	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) &&
		!(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		psio_start(tty);
	}
}

/*
 * -----------------------------------------------------------
 * psio_close()
 *
 * This routine is called when the debug console port gets closed.
 * First, we wait for the last remaining data to be sent.  Then, we unlink
 * its async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * -----------------------------------------------------------
 */
static void psio_close(struct tty_struct *tty, struct file *filp)
{
	struct async_struct * info = (struct async_struct *)tty->driver_data;
	struct serial_state *state;
	unsigned long flags;

	if (!info || serial_paranoia_check(info, tty->device, "rs_close"))
		return;

	state = info->state;

	save_flags(flags); cli();

	if (tty_hung_up_p(filp)) {
		DBG_CNT("before DEC-hung");
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return;
	}

#ifdef SERIAL_DEBUG_OPEN
	printk("psio_close ttyD%d, count = %d\n", info->line, state->count);
#endif
	if ((tty->count == 1) && (state->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  state->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("rs_close: bad serial port count; tty->count is 1, "
			   "state->count is %d\n", state->count);
		state->count = 1;
	}
	if (--state->count < 0) {
		printk("psio_close: bad serial port count for ttyD%d: %d\n",
			   info->line, state->count);
		state->count = 0;
	}
	if (state->count) {
		DBG_CNT("before DEC-2");
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return;
	}
	info->flags |= ASYNC_CLOSING;
	restore_flags(flags);
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ASYNC_NORMAL_ACTIVE)
		info->state->normal_termios = *tty->termios;
	if (info->flags & ASYNC_CALLOUT_ACTIVE)
		info->state->callout_termios = *tty->termios;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	info->IER &= ~UART_IER_RLSI;
	info->read_status_mask &= ~UART_LSR_DR;
	if (info->flags & ASYNC_INITIALIZED) {
		serial_out(info, UART_IER, info->IER);
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		psio_wait_until_sent(tty, info->timeout);
	}
	shutdown(info);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;
	if (info->blocked_open) {
		if (info->close_delay) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(info->close_delay);
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE|
			 ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	MOD_DEC_USE_COUNT;
}

/*
 * rs_wait_until_sent() --- wait until the transmitter is empty
 */
static void psio_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct async_struct * info = (struct async_struct *)tty->driver_data;
	unsigned long orig_jiffies, char_time;
	int lsr;

	if (serial_paranoia_check(info, tty->device, "psio_wait_until_sent"))
		return;

	if (info->state->type == PORT_UNKNOWN)
		return;

	if (info->xmit_fifo_size == 0)
		return; /* Just in case.... */

	orig_jiffies = jiffies;
	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 *
	 * Note: we have to use pretty tight timings here to satisfy
	 * the NIST-PCTS.
	 */
	char_time = (info->timeout - HZ/50) / info->xmit_fifo_size;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout && timeout < char_time)
		char_time = timeout;
	/*
	 * If the transmitter hasn't cleared in twice the approximate
	 * amount of time to send the entire FIFO, it probably won't
	 * ever clear.  This assumes the UART isn't doing flow
	 * control, which is currently the case.  Hence, if it ever
	 * takes longer than info->timeout, this is probably due to a
	 * UART bug of some kind.  So, we clamp the timeout parameter at
	 * 2*info->timeout.
	 */
	if (!timeout || timeout > 2*info->timeout)
		timeout = 2*info->timeout;
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk("In rs_wait_until_sent(%d) check=%lu...", timeout, char_time);
	printk("jiff=%lu...", jiffies);
#endif
	while (!((lsr = serial_inp(info, UART_LSR)) & UART_LSR_TEMT)) {
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
		printk("lsr = %d (jiff=%lu)...", lsr, jiffies);
#endif
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(char_time);
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
	}
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk("lsr = %d (jiff=%lu)...done\n", lsr, jiffies);
#endif
}

/*
 * psio_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void psio_hangup(struct tty_struct *tty)
{
	struct async_struct * info = (struct async_struct *)tty->driver_data;
	struct serial_state *state = info->state;

	if (serial_paranoia_check(info, tty->device, "psio_hangup"))
		return;

	state = info->state;

	psio_flush_buffer(tty);
	if (info->flags & ASYNC_CLOSING)
		return;
	shutdown(info);
	info->event = 0;
	state->count = 0;
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}



/*
static void rs_wait_until_sent(struct tty_struct *tty, int timeout)
static void rs_hangup(struct tty_struct *tty)
*/

/*
 * ------------------------------------------------------------
 * psio_open() and friends
 * ------------------------------------------------------------
 */
#define SERIAL_DEBUG_OPEN
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct async_struct *info)
{
	DECLARE_WAITQUEUE(wait, current);
	struct serial_state *state = info->state;
	int     retval;
	int     do_clocal = 0, extra_count = 0;
	unsigned long   flags;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
		(info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		return ((info->flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
#else
		return -EAGAIN;
#endif
	}

	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
		if (info->flags & ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
			(info->flags & ASYNC_SESSION_LOCKOUT) &&
			(info->session != current->session))
			return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
			(info->flags & ASYNC_PGRP_LOCKOUT) &&
			(info->pgrp != current->pgrp))
			return -EBUSY;
		info->flags |= ASYNC_CALLOUT_ACTIVE;
		return 0;
	}

#if 1 /* ? 020906 */
filp->f_flags |= O_NONBLOCK;
#endif /* ? 020906 */
	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
		(tty->flags & (1 << TTY_IO_ERROR))) {
		if (info->flags & ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (info->flags & ASYNC_CALLOUT_ACTIVE) {
		if (state->normal_termios.c_cflag & CLOCAL)
			do_clocal = 1;
	} else {
		if (tty->termios->c_cflag & CLOCAL)
			do_clocal = 1;
	}

	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, state->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttyD%d, count = %d\n",
		   state->line, state->count);
#endif
	save_flags(flags); cli();
	if (!tty_hung_up_p(filp)) {
		extra_count = 1;
		state->count--;
	}
	restore_flags(flags);
	info->blocked_open++;
	while (1) {
		save_flags(flags); cli();
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
			(tty->termios->c_cflag & CBAUD))
			serial_out(info, UART_MCR,
				   serial_inp(info, UART_MCR) |
				   (UART_MCR_DTR | UART_MCR_RTS));
		restore_flags(flags);
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
			!(info->flags & ASYNC_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
			!(info->flags & ASYNC_CLOSING) &&
			(do_clocal || (serial_in(info, UART_MSR) &
				   UART_MSR_DCD)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: ttyD%d, count = %d\n",
			   info->line, state->count);
#endif
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&info->open_wait, &wait);
	if (extra_count)
		state->count++;
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: ttyD%d, count = %d\n",
		   info->line, state->count);
#endif
	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}
#undef SERIAL_DEBUG_OPEN


static int get_async_struct(int line, struct async_struct **ret_info)
{
	struct async_struct *info;
	struct serial_state *sstate;

	sstate = rs_table + line;
	sstate->count++;
	if (sstate->info) {
		*ret_info = sstate->info;
		return 0;
	}
	info = kmalloc(sizeof(struct async_struct), GFP_KERNEL);
	if (!info) {
		sstate->count--;
		return -ENOMEM;
	}
	memset(info, 0, sizeof(struct async_struct));
	init_waitqueue_head(&info->open_wait);
	init_waitqueue_head(&info->close_wait);
	init_waitqueue_head(&info->delta_msr_wait);
	info->magic = SERIAL_MAGIC;
	info->port = sstate->port;
	info->flags = sstate->flags;
	info->io_type = sstate->io_type;
	info->iomem_base = sstate->iomem_base;
	info->iomem_reg_shift = sstate->iomem_reg_shift;
	info->xmit_fifo_size = sstate->xmit_fifo_size=0;
	info->line = line;
	info->tqueue.routine = do_softint;
	info->tqueue.data = info;
	info->state = sstate;

	if (sstate->info) {
		kfree(info);

		*ret_info = sstate->info;
		return 0;
	}
	*ret_info = sstate->info = info;
	return 0;
}

/*
 * -----------------------------------------------------------
 * psio_open()
 *
 * This routine is called whenever a debug console port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.  It also performs the serial-specific
 * initialization for the tty structure.
 * -----------------------------------------------------------
 */
static int psio_open(struct tty_struct *tty, struct file *filp)
{
	struct async_struct *info;
	int retval,line=0;
	unsigned long   page;

	MOD_INC_USE_COUNT;
	line = MINOR(tty->device) - tty->driver.minor_start;
	if ((line < 0) || (line >= NR_PORTS)) {
		MOD_DEC_USE_COUNT;
		return -ENODEV;
	}

	retval = get_async_struct(line, &info);
	if (retval) {
		printk("psio_open ttyD%d fail...",line);
		MOD_DEC_USE_COUNT;
		return retval;
	}
	tty->driver_data = info;
	info->tty = tty;
	if (serial_paranoia_check(info, tty->device, "psio_open"))
		return -ENODEV;

#ifdef SERIAL_DEBUG_OPEN
	printk("psio_open %s%d, count = %d\n", tty->driver.name, info->line,
		   info->state->count);
#endif
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	/*
	 *  This relies on lock_kernel() stuff so wants tidying for 2.5
	 */
	if (!tmp_buf) {
		page = get_zeroed_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		if (tmp_buf)
			free_page(page);
		else
			tmp_buf = (unsigned char *) page;
	}
	/*
	 * If the port is the middle of closing, bail out now
	 */
	if (tty_hung_up_p(filp) ||
	  (info->flags & ASYNC_CLOSING)) {
	if (info->flags & ASYNC_CLOSING)
	  interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
	return ((info->flags & ASYNC_HUP_NOTIFY) ?
	  -EAGAIN : -ERESTARTSYS);
#else
	return -EAGAIN;
#endif
	}

	/*
	 * Start up serial port
	 */
	retval=startup(info);
	if (retval)
		return retval;

	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("psio_open returning after block_til_ready with %d\n",
			   retval);
#endif
		return retval;
	}

	if ((info->state->count == 1) &&
		(info->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = info->state->normal_termios;
		else
			*tty->termios = info->state->callout_termios;
		change_speed(info, 0);
	}
#ifdef CONFIG_SERIAL_CONSOLE
	if (cons.cflag && cons.index == line) {
		tty->termios->c_cflag = cons.cflag;
		cons.cflag = 0;
		change_speed(info, 0);
	}
#endif
	info->session = current->session;
	info->pgrp = current->pgrp;

	return 0;
}

/*
 * /proc fs routines....
 */
static inline int line_info(char *buf, struct serial_state *state)
{
	struct async_struct *info = state->info, scr_info;
	char  stat_buf[30], control, status;
	int ret;
	unsigned long flags;

	ret = sprintf(buf, "%d: uart:%s port:%lX irq:%d",
		  state->line, uart_config[state->type].name,
		  state->port, state->irq);

	if (!state->port || (state->type == PORT_UNKNOWN)) {
		ret += sprintf(buf+ret, "\n");
		return ret;
	}

	/*
	 * Figure out the current RS-232 lines
	 */
	if (!info) {
		info = &scr_info; /* This is just for serial_{in,out} */

		info->magic = SERIAL_MAGIC;
		info->port = state->port;
		info->flags = state->flags;
		info->hub6 = state->hub6;
		info->io_type = state->io_type;
		info->iomem_base = state->iomem_base;
		info->iomem_reg_shift = state->iomem_reg_shift;
		info->quot = 0;
		info->tty = 0;
	}
	save_flags(flags); cli();
	status = serial_in(info, UART_MSR);
	control = info != &scr_info ? info->MCR : serial_in(info, UART_MCR);
	restore_flags(flags);

	stat_buf[0] = 0;
	stat_buf[1] = 0;
	if (control & UART_MCR_RTS)
	strcat(stat_buf, "|RTS");
	if (status & UART_MSR_CTS)
	strcat(stat_buf, "|CTS");
	if (control & UART_MCR_DTR)
	strcat(stat_buf, "|DTR");
	if (status & UART_MSR_DSR)
	strcat(stat_buf, "|DSR");
	if (status & UART_MSR_DCD)
	strcat(stat_buf, "|CD");
	if (status & UART_MSR_RI)
	strcat(stat_buf, "|RI");

	if (info->quot) {
		ret += sprintf(buf+ret, " baud:%d",
			 state->baud_base / (16*info->quot));
	}

	ret += sprintf(buf+ret, " tx:%d rx:%d",
		  state->icount.tx, state->icount.rx);

	if (state->icount.frame)
		ret += sprintf(buf+ret, " fe:%d", state->icount.frame);

	if (state->icount.parity)
		ret += sprintf(buf+ret, " pe:%d", state->icount.parity);

	if (state->icount.brk)
		ret += sprintf(buf+ret, " brk:%d", state->icount.brk);

	if (state->icount.overrun)
		ret += sprintf(buf+ret, " oe:%d", state->icount.overrun);

	/*
	 * Last thing is the RS-232 status lines
	 */
	ret += sprintf(buf+ret, " %s\n", stat_buf+1);
	return ret;
}



int psio_read_proc(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	int i, len = 0, l;
	off_t begin = 0;

	len += sprintf(page, "sioinfo:1.0 driver:%s%s revision:%s\n",
		   serial_version, LOCAL_VERSTRING, serial_revdate);
	for (i = 0; i < NR_PORTS && len < 4000; i++) {
		l = line_info(page + len, &rs_table[i]);
		len += l;
		if (len+begin > off+count)
			goto done;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
done:
	if (off >= len+begin)
		return 0;
	*start = page + (off-begin);
	return ((count < begin+len-off) ? count : begin+len-off);
}

static char serial_options[] __initdata =
	   " no serial options enabled\n";


static inline void show_serial_version(void)
{
	printk(KERN_INFO "%s version %s%s (%s) with%s", serial_name,
		serial_version, LOCAL_VERSTRING, serial_revdate,
		serial_options);
}
static unsigned detect_uart_irq (struct serial_state * state)
{
	if(! state->irq)
		printk(KERN_INFO "detect_uart_irq: Ohh irq = 0\n");

	return state->irq;
}

static void autoconfig(struct serial_state * state)
{
	struct async_struct *info, scr_info;
	//unsigned long flags;

	state->type = PORT_UNKNOWN;

#ifdef SERIAL_DEBUG_AUTOCONF
	printk("Testing ttyD%d (0x%04lx, 0x%04x)...\n", state->line,
		   state->port, (unsigned) state->iomem_base);
#endif

	if (!CONFIGURED_SERIAL_PORT(state))
		return;

	info = &scr_info;   /* This is just for serial_{in,out} */

	info->magic = SERIAL_MAGIC;
	info->state = state;
	info->port = state->port;
	info->flags = state->flags;
	sio_reset(info);
}

/*
 * The debug console driver boot-time initialization code!
 */
/* 20020830 */
int __init psio_init(void)
{
	int i;
	struct serial_state * state;

	init_bh(SERIAL_BH, do_psio_serial_bh);
	init_timer(&serial_timer);
	serial_timer.function = (void *)psio_timer;
#if 1
	mod_timer(&serial_timer, jiffies + 10);
#else /* 1 */
	mod_timer(&serial_timer, jiffies + RS_STROBE_TIME);
#endif /* 1 */

	for (i = 0; i < NR_IRQS; i++) {
		IRQ_ports[i] = 0;
		IRQ_timeout[i] = 0;
	}

	/*
	 * Initialize the tty_driver structure
	 */
	memset(&psio_driver, 0, sizeof(struct tty_driver));
	psio_driver.magic = TTY_DRIVER_MAGIC;
#if (LINUX_VERSION_CODE > 0x20100)
	psio_driver.driver_name = "serial_m32102";
#endif
#if 1
	psio_driver.name = "ttyD";
#else
#if (LINUX_VERSION_CODE > 0x2032D && defined(CONFIG_DEVFS_FS))
	psio_driver.name = "ttyd/%d";
#else
	psio_driver.name = "ttyD";
#endif
#endif	/* 1 */

	psio_driver.major = TTY_MAJOR;
	psio_driver.minor_start = 80;
	psio_driver.name_base = 0;
	psio_driver.num = NR_PORTS;
	psio_driver.type = TTY_DRIVER_TYPE_SERIAL;

	psio_driver.subtype = SERIAL_TYPE_NORMAL;
	psio_driver.init_termios = tty_std_termios;
	psio_driver.init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	psio_driver.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	psio_driver.refcount = &psio_refcount;
	psio_driver.table = psio_table;
	psio_driver.termios = psio_termios;
	psio_driver.termios_locked = psio_termios_locked;

	psio_driver.open = psio_open;
	psio_driver.close = psio_close;
	psio_driver.write = psio_write;
	psio_driver.put_char = psio_put_char;
	psio_driver.flush_chars = psio_flush_chars;
	psio_driver.write_room = psio_write_room;
	psio_driver.chars_in_buffer = psio_chars_in_buffer;
	psio_driver.flush_buffer = psio_flush_buffer;
	psio_driver.ioctl = psio_ioctl;
	psio_driver.throttle = psio_throttle;
	psio_driver.unthrottle = psio_unthrottle;
	psio_driver.set_termios = psio_set_termios;
	psio_driver.stop = psio_stop;
	psio_driver.start = psio_start;
	psio_driver.hangup = psio_hangup;

#if (LINUX_VERSION_CODE >= 131394) /* Linux 2.1.66 */
	psio_driver.break_ctl = psio_break;
#endif
#if (LINUX_VERSION_CODE >= 131343)
	psio_driver.send_xchar = psio_send_xchar;
	psio_driver.wait_until_sent = psio_wait_until_sent;
	psio_driver.read_proc = psio_read_proc;
#endif

	if (tty_register_driver(&psio_driver))
		panic("Couldn't register debug console driver\n");

	for (i = 0, state = rs_table; i < NR_PORTS; i++,state++) {
		state->magic = SSTATE_MAGIC;
		state->line = i;
		state->type = 14;
		state->custom_divisor = 0;
		state->close_delay = 5*HZ/10;
		state->closing_wait = 30*HZ;
		state->callout_termios = psio_driver.init_termios;
		state->normal_termios = psio_driver.init_termios;
		state->icount.cts = state->icount.dsr =
			state->icount.rng = state->icount.dcd = 0;
		state->icount.rx = state->icount.tx = 0;
		state->icount.frame = state->icount.parity = 0;
		state->icount.overrun = state->icount.brk = 0;
		state->irq = irq_cannonicalize(state->irq);

#if 0
		if (check_region(state->port,8))
			continue;
		if (state->flags & ASYNC_BOOT_AUTOCONF)
			autoconfig(psio_table);
#endif
		state->baud_base = boot_cpu_data.bus_clock;
		printk(KERN_INFO "ttyD%d initialized.\n",i);
	   		tty_register_devfs(&psio_driver, 0,
						   psio_driver.minor_start + state->line);
	}

	return 0;
}

/* 20020830 */
static void __exit psio_fini(void)
{
	unsigned long flags;
	// int e1, e2;
	int e1;
	// int i;
	// struct async_struct *info;

	/* printk("Unloading %s: version %s\n", serial_name, serial_version); */
	del_timer_sync(&serial_timer);
	save_flags(flags); cli();
		remove_bh(SERIAL_BH);
	if ((e1 = tty_unregister_driver(&psio_driver)))
		printk("psio_serial: failed to unregister serial driver (%d)\n",e1);
	restore_flags(flags);

	if (tmp_buf) {
		unsigned long pg = (unsigned long) tmp_buf;
		tmp_buf = NULL;
		free_page(pg);
	}
}

module_init(psio_init);
module_exit(psio_fini);
MODULE_DESCRIPTION("M32R/M32102 (dumb) serial driver");
MODULE_AUTHOR("Hiroyuki Kondo <kondo.hiroyuki@renesas.com>, Takeo Takahashi <takahashi.takeo@renesas.com>");
MODULE_LICENSE("GPL");


/*
 * -----------------------------------------------------------
 * Debug console driver
 * -----------------------------------------------------------
 */

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

static struct async_struct async_dbgcons;

/*
 *  Wait for transmitter & holding register to empty
 */
static inline void wait_for_xmitr(struct async_struct *info)
{
	unsigned int status, tmout = 1000000;

	do {
		status = serial_in(info, UART_LSR);

		if (status & UART_LSR_BI)
			lsr_break_flag = UART_LSR_BI;

		if (--tmout == 0)
			break;
	} while((status & BOTH_EMPTY) != BOTH_EMPTY);

	/* Wait for flow control if necessary */
	if (info->flags & ASYNC_CONS_FLOW) {
		tmout = 1000000;
		while (--tmout &&
			((serial_in(info, UART_MSR) & UART_MSR_CTS) == 0));
	}
}
static void dbg_console_write(struct console *co, const char *s,
				unsigned count)
{
	static struct async_struct *info = &async_dbgcons;
	int ier;
	unsigned i;

	/*
	 *  First save the IER then disable the interrupts
	 */
	ier = serial_in(info, UART_IER);
	serial_out(info, UART_IER, 0x00);

	/*
	 *  Now, do each character
	 */
	for (i = 0; i < count; i++, s++) {
		wait_for_xmitr(info);

		/*
		 *	Send the character out.
		 *	If a LF, also do CR...
		 */
		serial_out(info, UART_TX, *s);
		if (*s == 10) {
			wait_for_xmitr(info);
			serial_out(info, UART_TX, 13);
		}
	}

	/*
	 *	Finally, Wait for transmitter & holding register to empty
	 *	and restore the IER
	 */
	wait_for_xmitr(info);
	serial_out(info, UART_IER, ier);
}

#if (LINUX_VERSION_CODE <= 132114) /* Linux 2.4.18 */
/*
 *  Receive character from the serial port
 */
static int dbg_console_wait_key(struct console *console)
{
	static struct async_struct *info;
	int ier, c;

	info = &async_dbgcons;

	/*
	 *  First save the IER then disable the interrupts so
	 *  that the real driver for the port does not get the
	 *  character.
	 */
	ier = serial_in(info, UART_IER);
	serial_out(info, UART_IER, 0x00);

	while ((serial_in(info, UART_LSR) & UART_LSR_DR) == 0);
	c = serial_in(info, UART_RX);

	/*
	 *  Restore the interrupts
	 */
	serial_out(info, UART_IER, ier);

	return c;
}
#endif

static kdev_t dbg_console_device(struct console *c)
{
	return MKDEV(TTY_MAJOR, 80 + c->index);
}


static int __init dbg_console_setup(struct console *co, char *options)
{
	static struct async_struct *info;
	struct serial_state *state;
	int baud = BAUDRATE;
	int baud_base= boot_cpu_data.bus_clock;
	int bits = 8;
	int parity = 'n';
	int doflow = 0;
	unsigned int cflag = CREAD | HUPCL | CLOCAL | CRTSCTS;
	int cval;
	char *s;

	if (options) {
		baud = simple_strtoul(options, NULL, 10);
		s = options;
		while(*s >= '0' && *s <= '9')
			s++;
		if (*s) parity = *s++;
		if (*s) bits   = *s++ - '0';
		if (*s) doflow = (*s++ == 'r');
	}

	co->flags |= CON_ENABLED;

	/*
	 * 	Now construct a cflag setting.
	 */
	switch(baud) {
		case 1200:
			cflag |= B1200;
			break;
		case 2400:
			cflag |= B2400;
			break;
		case 4800:
			cflag |= B4800;
			break;
		case 19200:
			cflag |= B19200;
			break;
		case 38400:
			cflag |= B38400;
			break;
		case 57600:
			cflag |= B57600;
			break;
		case 115200:
			cflag |= B115200;
			break;
		case 9600:
		default:
			cflag |= B9600;
			baud  = 9600;
			break;
	}
	switch(bits) {
		case 7:
			cflag |= CS7;
			break;
		default:
		case 8:
			cflag |= CS8;
			break;
	}
	switch(parity) {
		case 'o': case 'O':
			cflag |= PARODD;
			break;
		case 'e': case 'E':
			cflag |= PARENB;
			break;
	}
	co->cflag = cflag;

	state = rs_table + co->index;
	if (doflow)
		state->flags |= ASYNC_CONS_FLOW;
	info = &async_dbgcons;
	info->magic = SERIAL_MAGIC;
	info->state = state;
	info->port = state->port;
	info->flags = state->flags;
	info->io_type = state->io_type;
	info->iomem_base = state->iomem_base;
	info->iomem_reg_shift = state->iomem_reg_shift;

	cval = (baud_base / (baud * 4)) - 1;

	serial_outp(info, UART_LCR,  0x0300);  /* init status         */
	//serial_outp(info, UART_MOD1, 0x0800);  /* 8bit                */
	serial_outp(info, UART_MOD0, 0x80);    /* cts/rts 1stop nonpari */
	//serial_outp(info, UART_MOD0, 0x180);    /* rts 1stop nonpari */
	//serial_outp(info, UART_MOD0, 0xc0);    /* cts/rts 1stop nonpari */

	serial_outp(info, UART_BAUR, cval);    /* set baurate reg     */
	//serial_outp(info, UART_RBAUR, adj);    /* set adj baurate reg */
	serial_outp(info, UART_IER, 0x00);     /* intr mask           */
	serial_outp(info, UART_LCR, 0x03);

	return 0;
}

static struct console cons = {
	name:		"ttyD",
	write:		dbg_console_write,
	device:		dbg_console_device,
#if (LINUX_VERSION_CODE <= 132114) /* Linux 2.4.18 */
	wait_key:	dbg_console_wait_key,
#endif
	setup:		dbg_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};


/*
 *	Register console.
 */
void __init psio_console_init(void)
{
	register_console(&cons);
}


