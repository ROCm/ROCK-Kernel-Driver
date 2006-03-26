#ifndef __ASM_XENCONS_H__
#define __ASM_XENCONS_H__

#ifdef CONFIG_XEN_CONSOLE
void xencons_force_flush(void);
void xencons_resume(void);
#else
#ifdef CONFIG_XEN_CONSOLE_MODULE
extern void (*__xencons_force_flush)(void);
extern void (*__xencons_resume)(void);
#endif
static inline void xencons_force_flush(void)
{
#ifdef CONFIG_XEN_CONSOLE_MODULE
	if (__xencons_force_flush)
		__xencons_force_flush();
#endif
}
static inline void xencons_resume(void)
{
#ifdef CONFIG_XEN_CONSOLE_MODULE
	if (__xencons_resume)
		__xencons_resume();
#endif
}
#endif

/* Interrupt work hooks. Receive data, or kick data out. */
void xencons_rx(char *buf, unsigned len, struct pt_regs *regs);
void xencons_tx(void);

int xencons_ring_init(void);
int xencons_ring_send(const char *data, unsigned len);

#endif /* __ASM_XENCONS_H__ */
