/*
 * linux/arch/arm/mach-sa1100/sa1111.h
 */

/*
 * Probe for a SA1111 chip.
 */
extern int sa1111_probe(unsigned long phys);

/*
 * Wake up a SA1111 chip.
 */
extern void sa1111_wake(void);

/*
 * Doze the SA1111 chip.
 */
extern void sa1111_doze(void);

/*
 * Configure the SA1111 shared memory controller.
 */
extern void sa1111_configure_smc(int sdram, unsigned int drac, unsigned int cas_latency);
extern void sa1111_init_irq(int irq_nr);
