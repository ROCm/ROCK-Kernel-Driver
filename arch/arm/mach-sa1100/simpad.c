/*
 * linux/arch/arm/mach-sa1100/simpad.c
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"

void init_simpad_cs3()
{
   cs3_shadow =(DISPLAY_ON | RS232_ON | PCMCIA_BUFF_DIS | RESET_SIMCARD);
   printk("\nCall CS3 init:%x\n",cs3_shadow);
   *(CS3BUSTYPE *)(CS3_BASE) = cs3_shadow;
   cs3_shadow =(DISPLAY_ON| RS232_ON |RESET_SIMCARD);
}

void PCMCIA_setbit(int value)
{
  cs3_shadow |= value;
}

void PCMCIA_clearbit(int value)
{
  cs3_shadow &= ~value;
}

static void __init
fixup_simpad(struct machine_desc *desc, struct param_struct *params,
		   char **cmdline, struct meminfo *mi)
{
#ifdef CONFIG_SA1100_SIMPAD_64MB
	SET_BANK( 0, 0xc0000000, 64*1024*1024 );
#else
	SET_BANK( 0, 0xc0000000, 32*1024*1024 );
#endif
	mi->nr_banks = 1;
	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( __phys_to_virt(0xc0800000), 8*1024*1024 );
}


static struct map_desc simpad_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0, neccessary for mtd */
  { 0xf2800000, 0x4b800000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* MQ200 */
  { 0xf1000000, 0x18000000, 0x00100000, DOMAIN_IO, 0, 1, 0, 0 }, /* Paules CS3, write only */
  LAST_DESC
};

static void __init simpad_map_io(void)
{
	sa1100_map_io();
	iotable_init(simpad_io_desc);

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}


MACHINE_START(SIMPAD, "Simpad")
	MAINTAINER("Juergen Messerer")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_simpad)
	MAPIO(simpad_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
