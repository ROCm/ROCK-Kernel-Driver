/*
 * linux/arch/arm/mach-sa1100/simpad.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>
#include <linux/string.h> 

#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <linux/serial_core.h>

#include "generic.h"

long cs3_shadow;

long get_cs3_shadow()
{
	return cs3_shadow;
}

void set_cs3_bit(int value)
{
	cs3_shadow |= value;
	*(CS3BUSTYPE *)(CS3_BASE) = cs3_shadow;
}

void clear_cs3_bit(int value)
{
	cs3_shadow &= ~value;
	*(CS3BUSTYPE *)(CS3_BASE) = cs3_shadow;
}

static struct map_desc simpad_io_desc[] __initdata = {
  /* virtual	physical    length	type */
  { 0xf2800000, 0x4b800000, 0x00800000, MT_DEVICE }, /* MQ200 */  
  { 0xf1000000, 0x18000000, 0x00100000, MT_DEVICE }  /* Paules CS3, write only */
};


static void simpad_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (port->mapbase == (u_int)&Ser1UTCR0) {
		if (state)
			clear_cs3_bit(RS232_ON);
		else
			set_cs3_bit(RS232_ON);
	}
}

static struct sa1100_port_fns simpad_port_fns __initdata = {
	.pm	= simpad_uart_pm,
};

static void __init simpad_map_io(void)
{
	sa1100_map_io();
	iotable_init(simpad_io_desc, ARRAY_SIZE(simpad_io_desc));

	PSPR = 0xc0008000;
	GPDR &= ~GPIO_GPIO0;
	cs3_shadow = (EN1 | EN0 | LED2_ON | DISPLAY_ON | RS232_ON | 
		      ENABLE_5V | RESET_SIMCARD);
	*(CS3BUSTYPE *)(CS3_BASE) = cs3_shadow;

	//It is only possible to register 3 UART in serial_sa1100.c
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);

	set_irq_type(IRQ_GPIO_UCB1300_IRQ, IRQT_RISING);
}

#ifdef CONFIG_PROC_FS

static char* name[]={
  "VCC_5V_EN",
  "VCC_3V_EN",
  "EN1",
  "EN0",
  "DISPLAY_ON",
  "PCMCIA_BUFF_DIS",
  "MQ_RESET",
  "PCMCIA_RESET",
  "DECT_POWER_ON",
  "IRDA_SD",
  "RS232_ON",
  "SD_MEDIAQ",
  "LED2_ON",
  "IRDA_MODE",
  "ENABLE_5V",
  "RESET_SIMCARD"
};

static int proc_cs3_read(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	char *p = page;
	int len, i;
        
	p += sprintf(p, "Chipselect3 : %x\n", cs3_shadow);
	for (i = 0; i <= 15; i++) {
		if(cs3_shadow & (1<<i)) {
			p += sprintf(p, "%s\t: TRUE \n",name[i]);
		} else
			p += sprintf(p, "%s\t: FALSE \n",name[i]);
	}
	len = (p - page) - off;
	if (len < 0)
		len = 0;
 
	*eof = (len <= count) ? 1 : 0;
	*start = page + off;
 
	return len;
}
 
 
static int __init cs3_init(void)
{
	struct proc_dir_entry *proc_cs3 = create_proc_entry("cs3", 0, 0);
	if (proc_cs3)
		proc_cs3->read_proc = proc_cs3_read;
	return 0;
}
 
arch_initcall(cs3_init);

#endif // CONFIG_PROC_FS

MACHINE_START(SIMPAD, "Simpad")
	MAINTAINER("Juergen Messerer")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(simpad_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
