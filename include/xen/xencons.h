#ifndef __ASM_XENCONS_H__
#define __ASM_XENCONS_H__

#ifdef CONFIG_XEN_PRIVILEGED_GUEST
struct dom0_vga_console_info;
void dom0_init_screen_info(const struct dom0_vga_console_info *, size_t);
#else
#define dom0_init_screen_info(info) ((void)(info))
#endif

#ifdef CONFIG_XEN_CONSOLE
void xencons_force_flush(void);
void xencons_resume(void);

/* Interrupt work hooks. Receive data, or kick data out. */
void xencons_rx(char *buf, unsigned len);
void xencons_tx(void);

int xencons_ring_init(void);
int xencons_ring_send(const char *data, unsigned len);
#else
static inline void xencons_force_flush(void) {}
static inline void xencons_resume(void) {}
#endif

#endif /* __ASM_XENCONS_H__ */
