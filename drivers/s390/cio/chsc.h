#ifndef S390_CHSC_H
#define S390_CHSC_H

extern void s390_process_css( void );
extern int chsc_chpid_logical (int irq, int chp);
extern void chsc_validate_chpids(int irq);
extern void switch_off_chpids(int irq, __u8 mask);
#endif
