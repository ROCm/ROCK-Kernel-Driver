/*
 * linux/drivers/char/s3c2410.c
 *
 * Driver for onboard UARTs on the Samsung S3C2410
 *
 * Based on drivers/char/serial.c and drivers/char/21285.c
 *
 * Ben Dooks, (c) 2003 Simtec Electronics
 *
 * Changelog:
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/serial_core.h>
#include <linux/serial.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/hardware.h>
#include <asm/arch/regs-serial.h>

#include <asm/mach-types.h>

#if 0
#include <asm/debug-ll.h>
#define dbg(x...) llprintk(x)
#else
#define dbg(x...)
#endif

#define SERIAL_S3C2410_NAME	"ttySAC"
#define SERIAL_S3C2410_MAJOR	204
#define SERIAL_S3C2410_MINOR	4

/* we can support 3 uarts, but not always use them */

#define NR_PORTS (3)

static const char serial_s3c2410_name[] = "Samsung S3C2410 UART";

/* port irq numbers */

#define TX_IRQ(port) ((port)->irq + 1)
#define RX_IRQ(port) ((port)->irq)

#define tx_enabled(port) ((port)->unused[0])
#define rx_enabled(port) ((port)->unused[1])

/* flag to ignore all characters comming in */
#define RXSTAT_DUMMY_READ (0x10000000)

/* access functions */

#define portaddr(port, reg) ((void *)((port)->membase + (reg)))

#define rd_regb(port, reg) (__raw_readb(portaddr(port, reg)))
#define rd_regl(port, reg) (__raw_readl(portaddr(port, reg)))

#define wr_regb(port, reg, val) \
  do { __raw_writeb(val, portaddr(port, reg)); } while(0)

#define wr_regl(port, reg, val) \
  do { __raw_writel(val, portaddr(port, reg)); } while(0)




/* code */

static void
serial_s3c2410_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	if (tx_enabled(port)) {
		disable_irq(TX_IRQ(port));
		tx_enabled(port) = 0;
	}
}

static void
serial_s3c2410_start_tx(struct uart_port *port, unsigned int tty_start)
{
	if (!tx_enabled(port)) {
		enable_irq(TX_IRQ(port));
		tx_enabled(port) = 1;
	}
}

static void serial_s3c2410_stop_rx(struct uart_port *port)
{
	if (rx_enabled(port)) {
		dbg("serial_s3c2410_stop_rx: port=%p\n", port);
		disable_irq(RX_IRQ(port));
		rx_enabled(port) = 0;
	}
}

static void serial_s3c2410_enable_ms(struct uart_port *port)
{
}

/* ? - where has parity gone?? */
#define S3C2410_UERSTAT_PARITY (0x1000)

static irqreturn_t
serial_s3c2410_rx_chars(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_port *port = dev_id;
	struct tty_struct *tty = port->info->tty;
	unsigned int ufcon, ch, rxs, ufstat;
	int max_count = 256;

	while (max_count-- > 0) {
		ufcon = rd_regl(port, S3C2410_UFCON);
		ufstat = rd_regl(port, S3C2410_UFSTAT);

		if (S3C2410_UFCON_RXC(ufstat) == 0)
			break;

		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			tty->flip.work.func((void *)tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
				printk(KERN_WARNING "TTY_DONT_FLIP set\n");
				goto out;
			}
		}

		ch = rd_regb(port, S3C2410_URXH);

		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		port->icount.rx++;

		rxs = rd_regb(port, S3C2410_UERSTAT) | RXSTAT_DUMMY_READ;

		if (rxs & S3C2410_UERSTAT_ANY) {
			if (rxs & S3C2410_UERSTAT_FRAME)
				port->icount.frame++;
			if (rxs & S3C2410_UERSTAT_OVERRUN)
				port->icount.overrun++;

			rxs &= port->read_status_mask;

			if (rxs & S3C2410_UERSTAT_PARITY)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (rxs & ( S3C2410_UERSTAT_FRAME | S3C2410_UERSTAT_OVERRUN))
				*tty->flip.flag_buf_ptr = TTY_FRAME;
		}

		if ((rxs & port->ignore_status_mask) == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}

		if ((rxs & S3C2410_UERSTAT_OVERRUN) &&
		    tty->flip.count < TTY_FLIPBUF_SIZE) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character.
			 */
			*tty->flip.char_buf_ptr++ = 0;
			*tty->flip.flag_buf_ptr++ = TTY_OVERRUN;
			tty->flip.count++;
		}
	}
	tty_flip_buffer_push(tty);

 out:
	return IRQ_HANDLED;
}

static irqreturn_t
serial_s3c2410_tx_chars(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	struct circ_buf *xmit = &port->info->xmit;
	int count = 256;

	if (port->x_char) {
		wr_regb(port, S3C2410_UTXH, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		goto out;
	}

	/* if there isnt anything more to transmit, or the uart is now
	 * stopped, disable the uart and exit
	*/

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		serial_s3c2410_stop_tx(port, 0);
		goto out;
	}

	/* try and drain the buffer... */

	while (!uart_circ_empty(xmit) && count-- > 0) {
		if (rd_regl(port, S3C2410_UFSTAT) & S3C2410_UFSTAT_TXFULL)
			break;

		wr_regb(port, S3C2410_UTXH, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		serial_s3c2410_stop_tx(port, 0);

 out:
	return IRQ_HANDLED;
}

static unsigned int
serial_s3c2410_tx_empty(struct uart_port *port)
{
	unsigned int ufcon = rd_regl(port, S3C2410_UFCON);
	return (S3C2410_UFCON_TXC(ufcon) != 0) ? 0 : TIOCSER_TEMT;
}

/* no modem control lines */
static unsigned int
serial_s3c2410_get_mctrl(struct uart_port *port)
{
	unsigned int umstat = rd_regb(port,S3C2410_UMSTAT);

	if (umstat & S3C2410_UMSTAT_CTS)
		return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
	else
		return TIOCM_CAR | TIOCM_DSR;
}

static void
serial_s3c2410_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* todo - possibly remove AFC and do manual CTS */
}

static void serial_s3c2410_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;
	unsigned int ucon;

	spin_lock_irqsave(&port->lock, flags);

	ucon = rd_regl(port, S3C2410_UCON);

	if (break_state)
		ucon |= S3C2410_UCON_SBREAK;
	else
		ucon &= ~S3C2410_UCON_SBREAK;

	wr_regl(port, S3C2410_UCON, ucon);

	spin_unlock_irqrestore(&port->lock, flags);
}

static int serial_s3c2410_startup(struct uart_port *port)
{
	int ret;

	tx_enabled(port) = 1;
	rx_enabled(port) = 1;

	dbg("serial_s3c2410_startup: port=%p (%p)\n",
	    port, port->mapbase);

	ret = request_irq(RX_IRQ(port), serial_s3c2410_rx_chars, 0,
			  serial_s3c2410_name, port);

	if (ret != 0)
		return ret;

	ret = request_irq(TX_IRQ(port), serial_s3c2410_tx_chars, 0,
			  serial_s3c2410_name, port);

	if (ret) {
		free_irq(RX_IRQ(port), port);
		return ret;
	}

	/* the port reset code should have done the correct
	 * register setup for the port controls */

	return ret;
}

static void serial_s3c2410_shutdown(struct uart_port *port)
{
	free_irq(TX_IRQ(port), port);
	free_irq(RX_IRQ(port), port);
}

static void
serial_s3c2410_set_termios(struct uart_port *port, struct termios *termios,
			   struct termios *old)
{
	unsigned long flags;
	unsigned int baud, quot;
	unsigned int ulcon;

	/*
	 * We don't support modem control lines.
	 */
	termios->c_cflag &= ~(HUPCL | CRTSCTS | CMSPAR);
	termios->c_cflag |= CLOCAL;

	/*
	 * We don't support BREAK character recognition.
	 */
	termios->c_iflag &= ~(IGNBRK | BRKINT);

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		dbg("config: 5bits/char\n");
		ulcon = S3C2410_LCON_CS5;
		break;
	case CS6:
		dbg("config: 6bits/char\n");
		ulcon = S3C2410_LCON_CS6;
		break;
	case CS7:
		dbg("config: 7bits/char\n");
		ulcon = S3C2410_LCON_CS7;
		break;
	case CS8:
	default:
		dbg("config: 8bits/char\n");
		ulcon = S3C2410_LCON_CS8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		ulcon |= S3C2410_LCON_STOPB;

	if (termios->c_cflag & PARENB) {
		if (!(termios->c_cflag & PARODD))
			ulcon |= S3C2410_LCON_PODD;
		else
			ulcon |= S3C2410_LCON_PEVEN;
	} else {
		ulcon |= S3C2410_LCON_PNONE;
	}

	/*
	if (port->fifosize)
	enable_fifo()
	*/

	spin_lock_irqsave(&port->lock, flags);

	dbg("setting ulcon to %08x\n", ulcon);
	//dbg("<flushing output from serial>\n");

	/* set the ulcon register */
	wr_regl(port, S3C2410_ULCON, ulcon);

	dbg("uart: ulcon = 0x%08x, ucon = 0x%08x, ufcon = 0x%08x\n",
	    rd_regl(port, S3C2410_ULCON),
	    rd_regl(port, S3C2410_UCON),
	    rd_regl(port, S3C2410_UFCON));

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * Which character status flags are we interested in?
	 */
	port->read_status_mask = S3C2410_UERSTAT_OVERRUN;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= S3C2410_UERSTAT_FRAME | S3C2410_UERSTAT_PARITY;

	/*
	 * Which character status flags should we ignore?
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= S3C2410_UERSTAT_OVERRUN;
	if (termios->c_iflag & IGNBRK && termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= S3C2410_UERSTAT_FRAME;

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= RXSTAT_DUMMY_READ;

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *serial_s3c2410_type(struct uart_port *port)
{
	return port->type == PORT_S3C2410 ? "S3C2410" : NULL;
}

#define MAP_SIZE (0x100)

static void
serial_s3c2410_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, MAP_SIZE);
}

static int
serial_s3c2410_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, MAP_SIZE, serial_s3c2410_name)
		!= NULL ? 0 : -EBUSY;
}

static void
serial_s3c2410_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE &&
	    serial_s3c2410_request_port(port) == 0)
		port->type = PORT_S3C2410;
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int
serial_s3c2410_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_S3C2410)
		ret = -EINVAL;

	return ret;
}

static struct uart_ops serial_s3c2410_ops = {
	.tx_empty	= serial_s3c2410_tx_empty,
	.get_mctrl	= serial_s3c2410_get_mctrl,
	.set_mctrl	= serial_s3c2410_set_mctrl,
	.stop_tx	= serial_s3c2410_stop_tx,
	.start_tx	= serial_s3c2410_start_tx,
	.stop_rx	= serial_s3c2410_stop_rx,
	.enable_ms	= serial_s3c2410_enable_ms,
	.break_ctl	= serial_s3c2410_break_ctl,
	.startup	= serial_s3c2410_startup,
	.shutdown	= serial_s3c2410_shutdown,
	.set_termios	= serial_s3c2410_set_termios,
	.type		= serial_s3c2410_type,
	.release_port	= serial_s3c2410_release_port,
	.request_port	= serial_s3c2410_request_port,
	.config_port	= serial_s3c2410_config_port,
	.verify_port	= serial_s3c2410_verify_port,
};

static struct uart_port serial_s3c2410_ports[NR_PORTS] = {
	{
		.membase	= 0,
		.mapbase	= 0,
		.iotype		= UPIO_MEM,
		.irq		= IRQ_S3CUART_RX0,
		.uartclk	= 0,
		.fifosize	= 16,
		.ops		= &serial_s3c2410_ops,
		.flags		= UPF_BOOT_AUTOCONF,
		.line		= 0,
	},
	{
		.membase	= 0,
		.mapbase	= 0,
		.iotype		= UPIO_MEM,
		.irq		= IRQ_S3CUART_RX1,
		.uartclk	= 0,
		.fifosize	= 16,
		.ops		= &serial_s3c2410_ops,
		.flags		= UPF_BOOT_AUTOCONF,
		.line		= 1,
	}
#if NR_PORTS > 2
	,
	{
		.membase	= 0,
		.mapbase	= 0,
		.iotype		= UPIO_MEM,
		.irq		= IRQ_S3CUART_RX2,
		.uartclk	= 0,
		.fifosize	= 16,
		.ops		= &serial_s3c2410_ops,
		.flags		= UPF_BOOT_AUTOCONF,
		.line		= 2,
	}
#endif
};

static int
serial_s3c2410_resetport(struct uart_port *port,
			 struct s3c2410_uartcfg *cfg)
{
	/* ensure registers are setup */

	dbg("serial_s3c2410_resetport: port=%p (%08x), cfg=%p\n",
	    port, port->mapbase, cfg);

	wr_regl(port, S3C2410_UCON,  cfg->ucon);
	wr_regl(port, S3C2410_ULCON, cfg->ulcon);

	/* reset both fifos */

	wr_regl(port, S3C2410_UFCON, cfg->ufcon | S3C2410_UFCON_RESETBOTH);
	wr_regl(port, S3C2410_UFCON, cfg->ufcon);

	return 0;
}

/* serial_s3c2410_init_ports
 *
 * initialise the serial ports from the machine provided initialisation
 * data.
*/

static int serial_s3c2410_init_ports(void)
{
	struct uart_port *ptr = serial_s3c2410_ports;
	struct s3c2410_uartcfg *cfg = s3c2410_uartcfgs;
	static int inited = 0;
	int i;

	if (inited)
		return 0;
	inited = 1;

	dbg("serial_s3c2410_init_ports: initialising ports...\n");

	for (i = 0; i < NR_PORTS; i++, ptr++, cfg++) {

		if (cfg->hwport > 3)
			continue;

		dbg("serial_s3c2410_init_ports: port %d (hw %d)...\n",
		    i, cfg->hwport);

		if (cfg->clock != NULL)
			ptr->uartclk = *cfg->clock;

		switch (cfg->hwport) {
		case 0:
			ptr->mapbase = S3C2410_PA_UART0;
			ptr->membase = (char *)S3C2410_VA_UART0;
			ptr->irq     = IRQ_S3CUART_RX0;
			break;

		case 1:
			ptr->mapbase = S3C2410_PA_UART1;
			ptr->membase = (char *)S3C2410_VA_UART1;
			ptr->irq     = IRQ_S3CUART_RX1;
			break;

		case 2:
			ptr->mapbase = S3C2410_PA_UART2;
			ptr->membase = (char *)S3C2410_VA_UART2;
			ptr->irq     = IRQ_S3CUART_RX2;
			break;
		}

		if (ptr->mapbase == 0)
			continue;

		/* reset the fifos (and setup the uart */
		serial_s3c2410_resetport(ptr, cfg);
	}

	return 0;
}

#ifdef CONFIG_SERIAL_S3C2410_CONSOLE

static struct uart_port *cons_uart;

static int
serial_s3c2410_console_txrdy(struct uart_port *port, unsigned int ufcon)
{
	unsigned long ufstat, utrstat;

	if (ufcon & S3C2410_UFCON_FIFOMODE) {
		/* fifo mode - check ammount of data in fifo registers... */

		ufstat = rd_regl(port, S3C2410_UFSTAT);

		return S3C2410_UFCON_TXC(ufstat) < 12;
	}

	/* in non-fifo mode, we go and use the tx buffer empty */

	utrstat = rd_regl(port, S3C2410_UTRSTAT);

	return (utrstat & S3C2410_UTRSTAT_TXFE) ? 1 : 0;
}

static void
serial_s3c2410_console_write(struct console *co, const char *s,
			     unsigned int count)
{
	int i;
	unsigned int ufcon = rd_regl(cons_uart, S3C2410_UFCON);

	for (i = 0; i < count; i++) {
		while (!serial_s3c2410_console_txrdy(cons_uart, ufcon))
			barrier();

		wr_regb(cons_uart, S3C2410_UTXH, s[i]);

		if (s[i] == '\n') {
			while (!serial_s3c2410_console_txrdy(cons_uart, ufcon))
				barrier();

			wr_regb(cons_uart, S3C2410_UTXH, '\r');
		}
	}
}

static void __init
serial_s3c2410_get_options(struct uart_port *port, int *baud,
			   int *parity, int *bits)
{

	unsigned int ulcon, ucon, ubrdiv;

	ulcon  = rd_regl(port, S3C2410_ULCON);
	ucon   = rd_regl(port, S3C2410_UCON);
	ubrdiv = rd_regl(port, S3C2410_UBRDIV);

	dbg("serial_s3c2410_get_options: port=%p\n"
	    "registers: ulcon=%08x, ucon=%08x, ubdriv=%08x\n",
	    port, ulcon, ucon, ubrdiv);

	if ((ucon & 0xf) != 0) {
		/* consider the serial port configured if the tx/rx mode set */

		switch (ulcon & S3C2410_LCON_CSMASK) {
		case S3C2410_LCON_CS5:
			*bits = 5;
			break;
		case S3C2410_LCON_CS6:
			*bits = 6;
			break;
		case S3C2410_LCON_CS7:
			*bits = 7;
			break;
		default:
		case S3C2410_LCON_CS8:
			*bits = 8;
			break;
		}

		switch (ulcon & S3C2410_LCON_PMASK) {
		case S3C2410_LCON_PEVEN:
			*parity = 'e';
			break;

		case S3C2410_LCON_PODD:
			*parity = 'o';
			break;

		default:
		case S3C2410_LCON_PNONE:
			/* nothing */
		}

		/* now calculate the baud rate */

		*baud = port->uartclk / ( 16 * (ubrdiv + 1));
		dbg("calculated baud %d\n", *baud);
	}

}

static int __init
serial_s3c2410_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/* is this a valid port */

	if (co->index == -1 || co->index >= NR_PORTS)
		co->index = 0;

	port = &serial_s3c2410_ports[co->index];

	/* is the port configured? */

	if (port->mapbase == 0x0) {
		co->index = 0;
		port = &serial_s3c2410_ports[co->index];
	}

	cons_uart = port;

	dbg("serial_s3c2410_console_setup: port=%p (%d)\n", port, co->index);

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		serial_s3c2410_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver s3c2410_uart_drv;

static struct console serial_s3c2410_console =
{
	.name		= SERIAL_S3C2410_NAME,
	.write		= serial_s3c2410_console_write,
	.device		= uart_console_device,
	.setup		= serial_s3c2410_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &s3c2410_uart_drv,
};

static int __init s3c2410_console_init(void)
{
	dbg("s3c2410_console_init:\n");

	serial_s3c2410_init_ports();
	register_console(&serial_s3c2410_console);
	return 0;
}
console_initcall(s3c2410_console_init);

#define SERIAL_S3C2410_CONSOLE	&serial_s3c2410_console
#else
#define SERIAL_S3C2410_CONSOLE	NULL
#endif

static struct uart_driver s3c2410_uart_drv = {
	.owner			= THIS_MODULE,
	.driver_name		= SERIAL_S3C2410_NAME,
	.dev_name		= SERIAL_S3C2410_NAME,
	.major			= SERIAL_S3C2410_MAJOR,
	.minor			= SERIAL_S3C2410_MINOR,
	.nr			= 3,
	.cons			= SERIAL_S3C2410_CONSOLE,
};

/* device driver */

static int s3c2410_serial_probe(struct device *_dev);
static int s3c2410_serial_remove(struct device *_dev);

static struct device_driver s3c2410_serial_drv = {
	.name		= "s3c2410-uart",
	.bus		= &platform_bus_type,
	.probe		= s3c2410_serial_probe,
	.remove		= s3c2410_serial_remove,
	.suspend	= NULL,
	.resume		= NULL,
};

#define s3c2410_dev_to_port(__dev) (struct uart_port *)dev_get_drvdata(__dev)

static int s3c2410_serial_probe(struct device *_dev)
{
	struct platform_device *dev = to_platform_device(_dev);
	struct resource *res = dev->resource;
	int i;

	dbg("s3c2410_serial_probe: dev=%p, _dev=%p, res=%p\n", _dev, dev, res);

	for (i = 0; i < dev->num_resources; i++, res++)
		if (res->flags & IORESOURCE_MEM)
			break;

	if (i < dev->num_resources) {
		struct uart_port *ptr = serial_s3c2410_ports;

		for (i = 0; i < NR_PORTS; i++, ptr++) {
			dbg("s3c2410_serial_probe: ptr=%p (%08x, %08x)\n",
			    ptr, ptr->mapbase, ptr->membase);

			if (ptr->mapbase != res->start)
				continue;

			dbg("s3c2410_serial_probe: got device %p: port=%p\n",
			    _dev, ptr);

			uart_add_one_port(&s3c2410_uart_drv, ptr);
			dev_set_drvdata(_dev, ptr);
			break;
		}
	}

	return 0;
}

static int s3c2410_serial_remove(struct device *_dev)
{
	struct uart_port *port = s3c2410_dev_to_port(_dev);

	if (port)
		uart_remove_one_port(&s3c2410_uart_drv, port);

	return 0;
}



static int __init serial_s3c2410_init(void)
{
	int ret;

	printk(KERN_INFO "S3C2410X Serial, (c) 2003 Simtec Electronics\n");

	ret = uart_register_driver(&s3c2410_uart_drv);
	if (ret != 0)
		return ret;

	ret = driver_register(&s3c2410_serial_drv);
	if (ret) {
		uart_unregister_driver(&s3c2410_uart_drv);
	}

	return ret;
}

static void __exit serial_s3c2410_exit(void)
{
	driver_unregister(&s3c2410_serial_drv);
	uart_unregister_driver(&s3c2410_uart_drv);
}

module_init(serial_s3c2410_init);
module_exit(serial_s3c2410_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("Samsung S3C2410X (S3C2410) Serial driver");
