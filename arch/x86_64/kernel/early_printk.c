#include <asm/io.h>

/* This is "wrong" address to access it, we should access it using
   0xffff8000000b8000ul; but 0xffff8000000b8000ul is not available
   early at boot. */
#define VGABASE		0xffffffff800b8000ul	

#define MAX_YPOS	25
#define MAX_XPOS	80

static int current_ypos = 1, current_xpos = 0; /* We want to print before clearing BSS */

void
early_clear (void)
{
	int k, i;
	for(k = 0; k < MAX_YPOS; k++)
		for(i = 0; i < MAX_XPOS; i++)
			writew(0, VGABASE + 2*(MAX_XPOS*k + i));
	current_ypos = 0;
}

void
early_puts (const char *str)
{
	char c;
	int  i, k, j;

	while ((c = *str++) != '\0') {
		if (current_ypos >= MAX_YPOS) {
#if 1
			/* scroll 1 line up */
			for(k = 1, j = 0; k < MAX_YPOS; k++, j++) {
				for(i = 0; i < MAX_XPOS; i++) {
					writew(readw(VGABASE + 2*(MAX_XPOS*k + i)),
					       VGABASE + 2*(MAX_XPOS*j + i));
				}
			}
			for(i = 0; i < MAX_XPOS; i++) {
				writew(0x720, VGABASE + 2*(MAX_XPOS*j + i));
			}
			current_ypos = MAX_YPOS-1;
#else
			/* MUCH faster */
			early_clear();
			current_ypos = 0;
#endif
		}
		if (c == '\n') {
			current_xpos = 0;
			current_ypos++;
		} else if (c != '\r')  {
			writew(((0x7 << 8) | (unsigned short) c),
			       VGABASE + 2*(MAX_XPOS*current_ypos + current_xpos++));
			if (current_xpos >= MAX_XPOS) {
				current_xpos = 0;
				current_ypos++;
			}
		}
	}
}

static char buf[1024];

int early_printk(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(buf, fmt, args); /* hopefully i < sizeof(buf)-4 */
	va_end(args);

	early_puts(buf);

	return i;
}
