/*
 * linux/arch/arm/mach-sa1100/sa1111.h
 */

extern int __init sa1111_init(void);
extern void __init sa1111_init_irq(int gpio_nr);
extern void sa1111_IRQ_demux( int irq, void *dev_id, struct pt_regs *regs );

