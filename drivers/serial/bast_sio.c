#include <linux/module.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/serial.h>
#include <asm/mach-types.h>

#include <asm/arch/map.h>
#include <asm/arch/irqs.h>
#include <asm/arch/bast-map.h>
#include <asm/arch/bast-irq.h>

static int __init serial_bast_register(unsigned long port, unsigned int irq)
{
	struct serial_struct serial_req;

#if 0
	printk("BAST: SuperIO serial (%08lx,%d)\n", port, irq);
#endif

	serial_req.flags      = UPF_AUTOPROBE | UPF_RESOURCES | UPF_SHARE_IRQ;
	serial_req.baud_base  = BASE_BAUD;
	serial_req.irq        = irq;
	serial_req.io_type    = UPIO_MEM;
	serial_req.iomap_base = port;
	serial_req.iomem_base = ioremap(port, 0x10);
	serial_req.iomem_reg_shift = 0;

	return register_serial(&serial_req);
}

#define SERIAL_BASE (S3C2410_CS2 + BAST_PA_SUPERIO)

static int __init serial_bast_init(void)
{
	if (machine_is_bast()) {
		serial_bast_register(SERIAL_BASE + 0x2f8, IRQ_PCSERIAL1);
		serial_bast_register(SERIAL_BASE + 0x3f8, IRQ_PCSERIAL2);
	}

	return 0;
}

static void __exit serial_bast_exit(void)
{
	/* todo -> remove both our ports */
}


module_init(serial_bast_init);
module_exit(serial_bast_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Dooks, ben@simtec.co.uk");
MODULE_DESCRIPTION("BAST Onboard Serial setup");


