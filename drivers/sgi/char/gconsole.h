/*
 * This is a temporary measure, we should eventually migrate to
 * Gert's generic graphic console code.
 */

#define cmapsz 8192
#define CHAR_HEIGHT  16

struct console_ops {
	void (*set_origin)(unsigned short offset);
	void (*hide_cursor)(void);
	void (*set_cursor)(int currcons);
	void (*get_scrmem)(int currcons);
	void (*set_scrmem)(int currcons, long offset);
	int  (*set_get_cmap)(unsigned char *arg, int set);
	void (*blitc)(unsigned short charattr, unsigned long addr);
	void (*memsetw)(void *s, unsigned short c, unsigned int count);
	void (*memcpyw)(unsigned short *to, unsigned short *from, unsigned int count);
};

void register_gconsole (struct console_ops *);

/* This points to the system console */
extern struct console_ops *gconsole;

extern void gfx_init (const char **name);

extern void __set_origin (unsigned short offset);
extern void hide_cursor (void);
extern unsigned char vga_font[];

extern void disable_gconsole (void);
extern void enable_gconsole (void);
