#ifndef __ASM_XENCONS_H__
#define __ASM_XENCONS_H__

int xprintk(const char *, ...) __attribute__ ((__format__(__printf__, 1, 2)));

struct dom0_vga_console_info;
void dom0_init_screen_info(const struct dom0_vga_console_info *, size_t);

void xencons_force_flush(void);
void xencons_resume(void);

#endif /* __ASM_XENCONS_H__ */
