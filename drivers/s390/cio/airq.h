#ifndef S390_AINTERRUPT_H
#define S390_AINTERRUPT_H

typedef	int (*adapter_int_handler_t)(__u32 intparm);

extern int s390_register_adapter_interrupt(adapter_int_handler_t handler);
extern int s390_unregister_adapter_interrupt(adapter_int_handler_t handler);
extern void do_adapter_IO (__u32 intparm);

#endif
