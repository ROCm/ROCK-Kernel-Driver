#ifndef S390_S390IO_H
#define S390_S390IO_H

extern __u64 chpids_logical[4];

/* cio_show_msg is needed in chsc.c */
extern int cio_show_msg;

/* cio_count_irqs is used in proc.c and cio.c */
extern int cio_count_irqs;
extern unsigned long s390_irq_count[NR_CPUS];
extern irb_t p_init_irb;

extern void s390_device_recognition_irq (int irq);
extern int s390_validate_subchannel (int irq, int enable);
extern int s390_DevicePathVerification (int irq, __u8 usermask);

extern int s390_send_nop(int irq, __u8 lpm);

#endif
