/*
** mux.c:
**	serial driver for the Mux console found in some PA-RISC servers.
**
**	(c) Copyright 2002 Ryan Bradetich
**	(c) Copyright 2002 Hewlett-Packard Company
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This Driver currently only supports the console (port 0) on the MUX.
** Additional work will be needed on this driver to enable the full
** functionality of the MUX.
**
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/parisc-device.h>

#ifdef CONFIG_MAGIC_SYSRQ
#include <linux/sysrq.h>
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

#define MUX_OFFSET 0x800
#define MUX_LINE_OFFSET 0x80

#define MUX_FIFO_SIZE 255
#define MUX_POLL_DELAY (30 * HZ / 1000)

#define IO_DATA_REG_OFFSET 0x3c
#define IO_DCOUNT_REG_OFFSET 0x40

#define MUX_EOFIFO(status) ((status & 0xF000) == 0xF000)
#define MUX_STATUS(status) ((status & 0xF000) == 0x8000)
#define MUX_BREAK(status) ((status & 0xF000) == 0x2000)

#define UART_NR 8
struct mux_card {
	struct uart_port ports[UART_NR];
	struct uart_driver drv;
	struct mux_card *next;
};

static struct mux_card mux_card_head = {
	.next = NULL,
};

static struct timer_list mux_timer;

#define UART_PUT_CHAR(p, c) __raw_writel((c), (unsigned long)(p)->membase + IO_DATA_REG_OFFSET)
#define UART_GET_FIFO_CNT(p) __raw_readl((unsigned long)(p)->membase + IO_DCOUNT_REG_OFFSET)
#define GET_MUX_PORTS(iodc_data) ((((iodc_data)[4] & 0xf0) >> 4) * 8) + 8

/**
 * mux_tx_empty - Check if the transmitter fifo is empty.
 * @port: Ptr to the uart_port.
 *
 * This function test if the transmitter fifo for the port
 * described by 'port' is empty.  If it is empty, this function
 * should return TIOCSER_TEMT, otherwise return 0.
 */
static unsigned int mux_tx_empty(struct uart_port *port)
{
	unsigned int cnt = __raw_readl((unsigned long)port->membase 
				+ IO_DCOUNT_REG_OFFSET);

	return cnt ? 0 : TIOCSER_TEMT;
} 

/**
 * mux_set_mctrl - Set the current state of the modem control inputs.
 * @ports: Ptr to the uart_port.
 * @mctrl: Modem control bits.
 *
 * The Serial MUX does not support CTS, DCD or DSR so this function
 * is ignored.
 */
static void mux_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

/**
 * mux_get_mctrl - Returns the current state of modem control inputs.
 * @port: Ptr to the uart_port.
 *
 * The Serial MUX does not support CTS, DCD or DSR so these lines are
 * treated as permanently active.
 */
static unsigned int mux_get_mctrl(struct uart_port *port)
{ 
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/**
 * mux_stop_tx - Stop transmitting characters.
 * @port: Ptr to the uart_port.
 * @tty_stop: tty layer issue this command?
 *
 * The Serial MUX does not support this function.
 */
static void mux_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
}

/**
 * mux_start_tx - Start transmitting characters.
 * @port: Ptr to the uart_port.
 * @tty_start: tty layer issue this command?
 *
 * The Serial Mux does not support this function.
 */
static void mux_start_tx(struct uart_port *port, unsigned int tty_start)
{
}

/**
 * mux_stop_rx - Stop receiving characters.
 * @port: Ptr to the uart_port.
 *
 * The Serial Mux does not support this function.
 */
static void mux_stop_rx(struct uart_port *port)
{
}

/**
 * mux_enable_ms - Enable modum status interrupts.
 * @port: Ptr to the uart_port.
 *
 * The Serial Mux does not support this function.
 */
static void mux_enable_ms(struct uart_port *port)
{
}

/**
 * mux_break_ctl - Control the transmitssion of a break signal.
 * @port: Ptr to the uart_port.
 * @break_state: Raise/Lower the break signal.
 *
 * The Serial Mux does not support this function.
 */
static void mux_break_ctl(struct uart_port *port, int break_state)
{
}

/**
 * mux_write - Write chars to the mux fifo.
 * @port: Ptr to the uart_port.
 *
 * This function writes all the data from the uart buffer to
 * the mux fifo.
 */
static void mux_write(struct uart_port *port)
{
	int count;
	struct circ_buf *xmit = &port->info->xmit;

	if(port->x_char) {
		UART_PUT_CHAR(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if(uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		mux_stop_tx(port, 0);
		return;
	}

	count = (port->fifosize >> 1) - UART_GET_FIFO_CNT(port);
	do {
		UART_PUT_CHAR(port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if(uart_circ_empty(xmit))
			break;

	} while(--count > 0);

	if(uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		mux_stop_tx(port, 0);
}

/**
 * mux_read - Read chars from the mux fifo.
 * @port: Ptr to the uart_port.
 *
 * This reads all available data from the mux's fifo and pushes
 * the data to the tty layer.
 */
static void mux_read(struct uart_port *port)
{
	int data;
	struct tty_struct *tty = port->info->tty;
	__u32 start_count = port->icount.rx;

	while(1) {
		data = __raw_readl((unsigned long)port->membase
						+ IO_DATA_REG_OFFSET);

		if (MUX_STATUS(data))
			continue;

		if (MUX_EOFIFO(data))
			break;

		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			continue;

		*tty->flip.char_buf_ptr = data & 0xffu;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		port->icount.rx++;

		if (MUX_BREAK(data)) {
			port->icount.brk++;
			if(uart_handle_break(port))
				continue;
		}

		if (uart_handle_sysrq_char(port, data & 0xffu, NULL))
			continue;

		tty->flip.flag_buf_ptr++;
		tty->flip.char_buf_ptr++;
		tty->flip.count++;
	}
	
	if (start_count != port->icount.rx) {
		tty_flip_buffer_push(tty);
	}
}

/**
 * mux_startup - Initialize the port.
 * @port: Ptr to the uart_port.
 *
 * Grab any resources needed for this port and start the
 * mux timer.
 */
static int mux_startup(struct uart_port *port)
{
	mod_timer(&mux_timer, jiffies + MUX_POLL_DELAY);
	return 0;
}

/**
 * mux_shutdown - Disable the port.
 * @port: Ptr to the uart_port.
 *
 * Release any resources needed for the port.
 */
static void mux_shutdown(struct uart_port *port)
{
}

/**
 * mux_set_termios - Chane port parameters.
 * @port: Ptr to the uart_port.
 * @termios: new termios settings.
 * @old: old termios settings.
 *
 * The Serial Mux does not support this function.
 */
static void
mux_set_termios(struct uart_port *port, struct termios *termios,
	        struct termios *old)
{
}

/**
 * mux_type - Describe the port.
 * @port: Ptr to the uart_port.
 *
 * Return a pointer to a string constant describing the
 * specified port.
 */
static const char *mux_type(struct uart_port *port)
{
	return "Mux";
}

/**
 * mux_release_port - Release memory and IO regions.
 * @port: Ptr to the uart_port.
 * 
 * Release any memory and IO region resources currently in use by
 * the port.
 */
static void mux_release_port(struct uart_port *port)
{
}

/**
 * mux_request_port - Request memory and IO regions.
 * @port: Ptr to the uart_port.
 *
 * Request any memory and IO region resources required by the port.
 * If any fail, no resources should be registered when this function
 * returns, and it should return -EBUSY on failure.
 */
static int mux_request_port(struct uart_port *port)
{
	return 0;
}

/**
 * mux_config_port - Perform port autoconfiguration.
 * @port: Ptr to the uart_port.
 * @type: Bitmask of required configurations.
 *
 * Perform any autoconfiguration steps for the port.  This functino is
 * called if the UPF_BOOT_AUTOCONF flag is specified for the port.
 * [Note: This is required for now because of a bug in the Serial core.
 *  rmk has already submitted a patch to linus, should be available for
 *  2.5.47.]
 */
static void mux_config_port(struct uart_port *port, int type)
{
	port->type = PORT_MUX;
}

/**
 * mux_verify_port - Verify the port information.
 * @port: Ptr to the uart_port.
 * @ser: Ptr to the serial information.
 *
 * Verify the new serial port information contained within serinfo is
 * suitable for this port type.
 */
static int mux_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if(port->membase == NULL)
		return -EINVAL;

	return 0;
}

/**
 * mux_drv_poll - Mux poll function.
 * @unused: Unused variable
 *
 * This function periodically polls the Serial MUX to check for new data.
 */
static void mux_poll(unsigned long unused)
{  
	int i;
	struct mux_card *card = &mux_card_head;

	while(card) {
		for(i = 0; i < UART_NR; ++i) {
			if(!card->ports[i].info)
				continue;

			mux_read(&card->ports[i]);
			mux_write(&card->ports[i]);
		}
		card = card->next;
	}
	mod_timer(&mux_timer, jiffies + MUX_POLL_DELAY);
}


#ifdef CONFIG_SERIAL_MUX_CONSOLE
static struct console mux_console = {
	.name =		"ttyB",
	.flags =	CON_PRINTBUFFER,
	.index =	0,
};
#define MUX_CONSOLE	&mux_console
#else
#define MUX_CONSOLE	NULL
#endif

static struct uart_ops mux_pops = {
	.tx_empty =		mux_tx_empty,
	.set_mctrl =		mux_set_mctrl,
	.get_mctrl =		mux_get_mctrl,
	.stop_tx =		mux_stop_tx,
	.start_tx =		mux_start_tx,
	.stop_rx =		mux_stop_rx,
	.enable_ms =		mux_enable_ms,
	.break_ctl =		mux_break_ctl,
	.startup =		mux_startup,
	.shutdown =		mux_shutdown,
	.set_termios =		mux_set_termios,
	.type =			mux_type,
	.release_port =		mux_release_port,
	.request_port =		mux_request_port,
	.config_port =		mux_config_port,
	.verify_port =		mux_verify_port,
};

/**
 * mux_probe - Determine if the Serial Mux should claim this device.
 * @dev: The parisc device.
 *
 * Deterimine if the Serial Mux should claim this chip (return 0)
 * or not (return 1).
 */
static int __init mux_probe(struct parisc_device *dev)
{
	int i, j, ret, ports, port_cnt = 0;
	u8 iodc_data[8];
	unsigned long bytecnt;
	struct uart_port *port;
	struct mux_card *card = &mux_card_head;

	ret = pdc_iodc_read(&bytecnt, dev->hpa, 0, iodc_data, 8);
	if(ret != PDC_OK) {
		printk(KERN_ERR "Serial mux: Unable to read IODC.\n");
		return 1;
	}

	ports = GET_MUX_PORTS(iodc_data);
 	printk(KERN_INFO "Serial mux driver (%d ports) Revision: 0.2\n",
		ports);

	if(!card->drv.nr) {
		init_timer(&mux_timer);
		mux_timer.function = mux_poll;
	} else {
		port_cnt += UART_NR;
		while(card->next) {
			card = card->next;
			port_cnt += UART_NR;
		} 
	}

	for(i = 0; i < ports / UART_NR; ++i) {
		if(card->drv.nr) {
			card->next = kmalloc(sizeof(struct mux_card), GFP_KERNEL);
			if(!card->next) {
				printk(KERN_ERR "Serial mux: Unable to allocate memory.\n");
				return 1;
			}
			memset(card->next, '\0', sizeof(struct mux_card));
			card = card->next;
		}

		card->drv.owner = THIS_MODULE;
		card->drv.driver_name = "ttyB";
		card->drv.dev_name = "ttyB";
		card->drv.major = MUX_MAJOR;
		card->drv.minor = port_cnt;
		card->drv.nr = UART_NR;
		card->drv.cons = MUX_CONSOLE;

		ret = uart_register_driver(&card->drv);
		if(ret) {
			printk(KERN_ERR "Serial mux: Unable to register driver.\n");
			return 1;
		}

		for(j = 0; j < UART_NR; ++j) {
			port = &card->ports[j];

			port->iobase	= 0;
			port->mapbase	= dev->hpa + MUX_OFFSET + (j * MUX_LINE_OFFSET);
			port->membase	= ioremap(port->mapbase, MUX_LINE_OFFSET);
			port->iotype	= SERIAL_IO_MEM;
			port->type	= PORT_MUX;
			port->irq	= SERIAL_IRQ_NONE;
			port->uartclk	= 0;
			port->fifosize	= MUX_FIFO_SIZE;
			port->ops	= &mux_pops;
			port->flags	= UPF_BOOT_AUTOCONF;
			port->line	= j;
			ret = uart_add_one_port(&card->drv, port);
			BUG_ON(ret);
		}
		port_cnt += UART_NR;
	}
	return 0;
}

static struct parisc_device_id mux_tbl[] = {
	{ HPHW_A_DIRECT, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0000D },
	{ 0, }
};

MODULE_DEVICE_TABLE(parisc, mux_tbl);

static struct parisc_driver mux_driver = {
	.name =		"Serial MUX",
	.id_table =	mux_tbl,
	.probe =	mux_probe,
};

/**
 * mux_init - Serial MUX initalization procedure.
 *
 * Register the Serial MUX driver.
 */
static int __init mux_init(void)
{
	return register_parisc_driver(&mux_driver);
}

/**
 * mux_exit - Serial MUX cleanup procedure.
 *
 * Unregister the Serial MUX driver from the tty layer.
 */
static void __exit mux_exit(void)
{
	int i;
	struct mux_card *card = &mux_card_head;

	for (i = 0; i < UART_NR; i++) {
		uart_remove_one_port(&card->drv, &card->ports[i]);
	}

	uart_unregister_driver(&card->drv);
}

module_init(mux_init);
module_exit(mux_exit);

MODULE_AUTHOR("Ryan Bradetich");
MODULE_DESCRIPTION("Serial MUX driver");
MODULE_LICENSE("GPL");
