#ifndef S390_CIO_H
#define S390_CIO_H

extern int
s390_start_IO (int irq,         /* IRQ */
               ccw1_t * cpa,    /* logical channel prog addr */
               unsigned long user_intparm,      /* interruption parameter */
               __u8 lpm,        /* logical path mask */
               unsigned long flag);

extern int cancel_IO (int irq);

extern int enable_cpu_sync_isc (int irq);
extern int disable_cpu_sync_isc (int irq);
extern int cons_dev;
extern int s390_process_IRQ (unsigned int irq);

#endif
