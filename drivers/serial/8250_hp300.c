/*
 * Driver for the 98626/98644/internal serial interface on hp300/hp400
 * (based on the National Semiconductor INS8250/NS16550AF/WD16C552 UARTs)
 *
 * Ported from 2.2 and modified to use the normal 8250 driver
 * by Kars de Jong <jongk@linux-m68k.org>, May 2004.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/serial_core.h>
#include <linux/delay.h>
#include <linux/dio.h>
#include <linux/console.h>
#include <asm/io.h>

#if !defined(CONFIG_HPDCA) && !defined(CONFIG_HPAPCI)
#warning CONFIG_8250 defined but neither CONFIG_HPDCA nor CONFIG_HPAPCI defined, are you sure?
#endif

struct hp300_port
{
	struct hp300_port *next;	/* next port */
	unsigned long dio_base;		/* start of DIO registers */
	int scode;                      /* select code of this board */
	int line;			/* line (tty) number */
};

extern int hp300_uart_scode;

static struct hp300_port *hp300_ports;

/* Offset to UART registers from base of DCA */
#define UART_OFFSET	17

#define DCA_ID		0x01	/* ID (read), reset (write) */
#define DCA_IC		0x03	/* Interrupt control        */

/* Interrupt control */
#define DCA_IC_IE	0x80	/* Master interrupt enable  */

#define HPDCA_BAUD_BASE 153600

/* Base address of the Frodo part */
#define FRODO_BASE	(0x41c000)

/*
 * Where we find the 8250-like APCI ports, and how far apart they are.
 */
#define FRODO_APCIBASE		0x0
#define FRODO_APCISPACE		0x20
#define FRODO_APCI_OFFSET(x)	(FRODO_APCIBASE + ((x) * FRODO_APCISPACE))

#define HPAPCI_BAUD_BASE 500400

#ifdef CONFIG_SERIAL_8250_CONSOLE
/*
 * Parse the bootinfo to find descriptions for headless console and 
 * debug serial ports and register them with the 8250 driver.
 * This function should be called before serial_console_init() is called
 * to make sure the serial console will be available for use. IA-64 kernel
 * calls this function from setup_arch() after the EFI and ACPI tables have
 * been parsed.
 */
int __init hp300_setup_serial_console(void)
{
	int scode;
	struct uart_port port;

	memset(&port, 0, sizeof(port));

	if (hp300_uart_scode < 0 || hp300_uart_scode > 256)
		return 0;

	scode = hp300_uart_scode;

	/* Memory mapped I/O */
	port.iotype = UPIO_MEM;
	port.flags = UPF_SKIP_TEST | UPF_SHARE_IRQ | UPF_BOOT_AUTOCONF;
	port.type = PORT_UNKNOWN;

	/* Check for APCI console */
	if (scode == 256)
	{
#ifdef CONFIG_HPAPCI
		printk(KERN_INFO "Serial console is HP APCI 1\n");

		port.uartclk = HPAPCI_BAUD_BASE * 16;
		port.mapbase = (FRODO_BASE + FRODO_APCI_OFFSET(1));
		port.membase = (char *)(port.mapbase + DIO_VIRADDRBASE);
		port.regshift = 2;
		add_preferred_console("ttyS", port.line, "9600n8");
#else
		printk(KERN_WARNING "Serial console is APCI but support is disabled (CONFIG_HPAPCI)!\n");
		return 0;
#endif
	}
	else
	{
#ifdef CONFIG_HPDCA
		unsigned long pa = dio_scodetophysaddr(scode);
		if (!pa) {
			return 0;
		}

		printk(KERN_INFO "Serial console is HP DCA at select code %d\n", scode);

		port.uartclk = HPDCA_BAUD_BASE * 16;
		port.mapbase = (pa + UART_OFFSET);
		port.membase = (char *)(port.mapbase + DIO_VIRADDRBASE);
		port.regshift = 1;
		port.irq = DIO_IPL(pa + DIO_VIRADDRBASE);

		/* Enable board-interrupts */
		out_8(pa + DIO_VIRADDRBASE + DCA_IC, DCA_IC_IE);

		if (DIO_ID(pa + DIO_VIRADDRBASE) & 0x80) {
			add_preferred_console("ttyS", port.line, "9600n8");
		}
#else
		printk(KERN_WARNING "Serial console is DCA but support is disabled (CONFIG_HPDCA)!\n");
		return 0;
#endif
	}

	if (early_serial_setup(&port) < 0) {
		printk(KERN_WARNING "hp300_setup_serial_console(): early_serial_setup() failed.\n");
	}

	return 0;
}
#endif /* CONFIG_SERIAL_8250_CONSOLE */

static int __init hp300_8250_init(void)
{
	static int called = 0;
#ifdef CONFIG_HPDCA
	int scode;
#endif
	int line, num_ports;
	unsigned long base;
	struct serial_struct serial_req;
	struct hp300_port *port;

	if (called)
		return -ENODEV;
	called = 1;
	num_ports = 0;

	if (!MACH_IS_HP300) {
		return -ENODEV;
	}

#ifdef CONFIG_HPDCA
	while (1) {
                /* We detect boards by looking for DIO boards which match a
                 * given subset of IDs. dio_find() returns the board's scancode.
                 * The scancode to physaddr mapping is a property of the hardware,
                 * as is the scancode to IPL (interrupt priority) mapping.
                 */
                scode = dio_find(DIO_ID_DCA0);
                if (scode < 0)
			scode = dio_find(DIO_ID_DCA0REM);
                if (scode < 0)
			scode = dio_find(DIO_ID_DCA1);
                if (scode < 0)
			scode = dio_find(DIO_ID_DCA1REM);
                if (scode < 0)
			break;		/* no, none at all */

#ifdef CONFIG_SERIAL_8250_CONSOLE
		if (hp300_uart_scode == scode) {
			/* Already got it */
			dio_config_board(scode);
			continue;
		}
#endif

		/* Create new serial device */
		port = kmalloc(sizeof(struct hp300_port), GFP_KERNEL);
		if (!port)
			return -ENOMEM;

		memset(&serial_req, 0, sizeof(struct serial_struct));
		
		base = dio_scodetophysaddr(scode);

                /* If we want to tell the DIO code that this board is configured,
                 * we should do that here.
                 */
                dio_config_board(scode);

		/* Memory mapped I/O */
		serial_req.io_type = SERIAL_IO_MEM;
		serial_req.flags = UPF_SKIP_TEST | UPF_SHARE_IRQ | UPF_BOOT_AUTOCONF;
		serial_req.irq = dio_scodetoipl(scode);
		serial_req.baud_base = HPDCA_BAUD_BASE;
		serial_req.iomap_base = (base + UART_OFFSET);
		serial_req.iomem_base = (char *)(serial_req.iomap_base + DIO_VIRADDRBASE);
		serial_req.iomem_reg_shift = 1;

#ifdef CONFIG_SERIAL_8250_CONSOLE
		if (hp300_uart_scode != scode) {
#endif
                /* Reset the DCA */
                out_8(base + DIO_VIRADDRBASE + DCA_ID, 0xff);
                udelay(100);
#ifdef CONFIG_SERIAL_8250_CONSOLE
		}
#endif

		line = register_serial(&serial_req);

		if (line < 0) {
			printk(KERN_NOTICE "8250_hp300: register_serial() DCA scode %d"
			       " irq %d failed\n", scode, serial_req.irq);
			kfree(port);
			continue;
		}

		/* Enable board-interrupts */
		out_8(base + DIO_VIRADDRBASE + DCA_IC, DCA_IC_IE);

		port->dio_base = base + DIO_VIRADDRBASE;
		port->scode = scode;
		port->line = line;
		port->next = hp300_ports;
		hp300_ports = port;

		num_ports++;
        }
#endif

#ifdef CONFIG_HPAPCI
	if (hp300_model >= HP_400)
	{
		int i;

		/* These models have the Frodo chip.
		 * Port 0 is reserved for the Apollo Domain keyboard.
		 * Port 1 is either the console or the DCA.
		 */
		for (i = 1; i < 4; i++) {
			/* Port 1 is the console on a 425e, on other machines it's mapped to
			 * DCA.
			 */
#ifdef CONFIG_SERIAL_8250_CONSOLE
			if (i == 1) {
				continue;
			}
#endif

			/* Create new serial device */
			port = kmalloc(sizeof(struct hp300_port), GFP_KERNEL);
			if (!port)
				return -ENOMEM;

			memset(&serial_req, 0, sizeof(struct serial_struct));

			base = (FRODO_BASE + FRODO_APCI_OFFSET(i));

			/* Memory mapped I/O */
			serial_req.io_type = SERIAL_IO_MEM;
			serial_req.flags = UPF_SKIP_TEST | UPF_SHARE_IRQ | UPF_BOOT_AUTOCONF;
			/* XXX - no interrupt support yet */
			serial_req.irq = 0;
			serial_req.baud_base = HPAPCI_BAUD_BASE;
			serial_req.iomap_base = base;
			serial_req.iomem_base = (char *)(serial_req.iomap_base + DIO_VIRADDRBASE);
			serial_req.iomem_reg_shift = 2;

			line = register_serial(&serial_req);

			if (line < 0) {
				printk(KERN_NOTICE "8250_hp300: register_serial() APCI %d"
				       " irq %d failed\n", i, serial_req.irq);
				kfree(port);
				continue;
			}

			port->dio_base = 0;
			port->line = line;
			port->next = hp300_ports;
			hp300_ports = port;

			num_ports++;
		}
	}
#endif

	/* Any boards found? */
	if (!num_ports)
		return -ENODEV;

	return 0;
}

static void __exit hp300_8250_exit(void)
{
	struct hp300_port *port, *to_free;

	for (port = hp300_ports; port; ) {
		unregister_serial(port->line);

#ifdef CONFIG_HPDCA
		if (port->dio_base) {
			/* Disable board-interrupts */
			out_8(port->dio_base + DCA_IC, 0);

			dio_unconfig_board(port->scode);
		}
#endif

		to_free = port;
		port = port->next;
		kfree(to_free);
	}

	hp300_ports = NULL;
}

module_init(hp300_8250_init);
module_exit(hp300_8250_exit);
MODULE_DESCRIPTION("HP DCA/APCI serial driver");
MODULE_AUTHOR("Kars de Jong <jongk@linux-m68k.org>");
MODULE_LICENSE("GPL");
