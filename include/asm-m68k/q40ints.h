/*
 * contains some Q40 related interrupt definitions
 */

#define Q40_IRQ_MAX      (34)

#define Q40_IRQ_SAMPLE    (34)
#define Q40_IRQ_KEYBOARD (32)
#define Q40_IRQ_FRAME    (33)


/* masks for interrupt regiosters*/
/* internal, IIRQ_REG */
#define IRQ_KEYB_MASK    (2)
#define IRQ_SER_MASK     (1<<2)
#define IRQ_FRAME_MASK   (1<<3)
#define IRQ_EXT_MASK     (1<<4)    /* is a EIRQ */
/* eirq, EIRQ_REG */
#define IRQ3_MASK        (1)
#define IRQ4_MASK        (1<<1)
#define IRQ5_MASK        (1<<2)
#define IRQ6_MASK        (1<<3)
#define IRQ7_MASK        (1<<4)
#define IRQ10_MASK       (1<<5)
#define IRQ14_MASK       (1<<6)
#define IRQ15_MASK       (1<<7)

extern unsigned long q40_probe_irq_on (void);
extern int q40_probe_irq_off (unsigned long irqs);
