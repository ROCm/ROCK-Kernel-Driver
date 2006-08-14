#ifndef __ASM_XENCONS_H__
#define __ASM_XENCONS_H__

#ifdef CONFIG_XEN_CONSOLE

void xencons_force_flush(void);
void xencons_resume(void);

/* Interrupt work hooks. Receive data, or kick data out. */
void xencons_rx(char *buf, unsigned len, struct pt_regs *regs);
void xencons_tx(void);

int xencons_ring_init(void);
int xencons_ring_send(const char *data, unsigned len);

#else

static inline void xencons_force_flush(void) {}
static inline void xencons_resume(void) {}

#endif

#endif /* __ASM_XENCONS_H__ */
