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
#include <linux/pm.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <asm/arch/simpad.h>
#include <asm/arch/registry.h>

#include <linux/serial_core.h>
#include <linux/ioport.h>
#include <asm/io.h>

#include "generic.h"

long cs3_shadow;

long get_cs3_shadow(void)
{
	return cs3_shadow;
}

void set_cs3(long value)
{
	*(CS3BUSTYPE *)(CS3_BASE) = cs3_shadow = value;
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

EXPORT_SYMBOL(set_cs3_bit);
EXPORT_SYMBOL(clear_cs3_bit);

static struct map_desc simpad_io_desc[] __initdata = {
        /* virtual	physical    length	type */
	/* MQ200 */
	{ 0xf2800000, 0x4b800000, 0x00800000, MT_DEVICE },
	/* Paules CS3, write only */
	{ 0xf1000000, 0x18000000, 0x00100000, MT_DEVICE },
};


static void simpad_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (port->mapbase == (u_int)&Ser1UTCR0) {
		if (state)
		{
			clear_cs3_bit(RS232_ON);
			clear_cs3_bit(DECT_POWER_ON);
		}else
		{
			set_cs3_bit(RS232_ON);
			set_cs3_bit(DECT_POWER_ON);
		}
	}
}

static struct sa1100_port_fns simpad_port_fns __initdata = {
	.pm	   = simpad_uart_pm,
};

static void __init simpad_map_io(void)
{
	sa1100_map_io();

	iotable_init(simpad_io_desc, ARRAY_SIZE(simpad_io_desc));

	set_cs3_bit (EN1 | EN0 | LED2_ON | DISPLAY_ON | RS232_ON |
		      ENABLE_5V | RESET_SIMCARD | DECT_POWER_ON);


        sa1100_register_uart_fns(&simpad_port_fns);
	sa1100_register_uart(0, 3);  /* serial interface */
	sa1100_register_uart(1, 1);  /* DECT             */

	// Reassign UART 1 pins
	GAFR |= GPIO_UART_TXD | GPIO_UART_RXD;
	GPDR |= GPIO_UART_TXD | GPIO_LDD13 | GPIO_LDD15;
	GPDR &= ~GPIO_UART_RXD;
	PPAR |= PPAR_UPR;

	/*
	 * Set up registers for sleep mode.
	 */


	PWER = PWER_GPIO0| PWER_RTC;
	PGSR = 0x818;
	PCFR = 0;
	PSDR = 0;


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
        
	p += sprintf(p, "Chipselect3 : %x\n", (uint)cs3_shadow);
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

static int proc_cs3_write(struct file * file, const char * buffer,
			  size_t count, loff_t *ppos)
{
        unsigned long newRegValue;
	char *endp;

        newRegValue = simple_strtoul(buffer,&endp,0);
	set_cs3( newRegValue );
        return (count+endp-buffer);
}

#endif
 
static int __init cs3_init(void)
{

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc_cs3 = create_proc_entry("CS3", 0, 0);
	if (proc_cs3)
	{
		proc_cs3->read_proc = proc_cs3_read;
		proc_cs3->write_proc = (void*)proc_cs3_write;
	}
#endif



	return 0;
}
 
arch_initcall(cs3_init);

static void simpad_power_off(void)
{
	local_irq_disable(); // was cli
	set_cs3(0x800);        /* only SD_MEDIAQ */

	/* disable internal oscillator, float CS lines */
	PCFR = (PCFR_OPDE | PCFR_FP | PCFR_FS);
	/* enable wake-up on GPIO0 (Assabet...) */
	PWER = GFER = GRER = 1;
	/*
	 * set scratchpad to zero, just in case it is used as a
	 * restart address by the bootloader.
	 */
	PSPR = 0;
	PGSR = 0;
	/* enter sleep mode */
	PMCR = PMCR_SF;
	while(1);

	local_irq_enable(); /* we won't ever call it */


}

static int __init simpad_init(void)
{
	set_power_off_handler( simpad_power_off );
	return 0;
}

arch_initcall(simpad_init);


MACHINE_START(SIMPAD, "Simpad")
	MAINTAINER("Juergen Messerer")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
        BOOT_PARAMS(0xc0000100)
	MAPIO(simpad_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
