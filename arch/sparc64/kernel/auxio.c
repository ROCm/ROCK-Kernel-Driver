/* auxio.c: Probing for the Sparc AUXIO register at boot time.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>

#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/auxio.h>
#include <asm/sbus.h>
#include <asm/ebus.h>
#include <asm/fhc.h>
#include <asm/starfire.h>

/* Probe and map in the Auxiliary I/O register */
unsigned long auxio_register = 0;

void __init auxio_probe(void)
{
        struct sbus_bus *sbus;
        struct sbus_dev *sdev = 0;

        for_each_sbus(sbus) {
                for_each_sbusdev(sdev, sbus) {
                        if(!strcmp(sdev->prom_name, "auxio"))
				goto found_sdev;
                }
        }

found_sdev:
	if (!sdev) {
#ifdef CONFIG_PCI
		struct linux_ebus *ebus;
		struct linux_ebus_device *edev = 0;
		unsigned long led_auxio;

		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if (!strcmp(edev->prom_name, "auxio"))
					goto ebus_done;
			}
		}
	ebus_done:

		if (edev) {
			led_auxio = edev->resource[0].start;
			outl(0x01, led_auxio);
			return;
		}
#endif
		if(central_bus || this_is_starfire) {
			auxio_register = 0UL;
			return;
		}
		prom_printf("Cannot find auxio node, cannot continue...\n");
		prom_halt();
	}

	/* Map the register both read and write */
	auxio_register = sbus_ioremap(&sdev->resource[0], 0,
				      sdev->reg_addrs[0].reg_size, "auxiliaryIO");
	TURN_ON_LED;
}
