/*
 * linux/arch/arm/mach-sa1100/sa1111.h
 */
struct device;

/*
 * Probe for a SA1111 chip.
 */
extern int
sa1111_init(struct device *parent, unsigned long phys, unsigned int irq);

/*
 * Wake up a SA1111 chip.
 */
extern void sa1111_wake(void);

/*
 * Doze the SA1111 chip.
 */
extern void sa1111_doze(void);
