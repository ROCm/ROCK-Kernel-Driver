/*
 * Wrap-around code for a console using the
 * SGI PROM io-routines.
 *
 * Copyright (c) 1999 Ulf Carlsson
 *
 * Derived from DECstation promcon.c
 * Copyright (c) 1998 Harald Koerfgen 
 */

#include <linux/tty.h>
#include <linux/major.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/fs.h>
/*
#include <asm/sgialib.h>
*/
extern void prom_printf(char *fmt, ...);
unsigned long splx(unsigned long mask){return 0;}
#if 0
unsigned long ramsize=0x100000;
unsigned long RamSize(){return ramsize;}
extern void prom_printf(char *fmt, ...);
unsigned long splx(unsigned long mask){return 0;}
long PssSetIntHandler(unsigned long intnum, void *handler){}
long PssEnableInt(unsigned long intnum){}
long PssDisableInt(unsigned long intnum){}
unsigned long t_ident(char name[4], unsigned long node, unsigned long *tid){}
#endif

extern void  SerialPollConout(unsigned char c);
static void prom_console_write(struct console *co, const char *s,
			       unsigned count)
{
    unsigned i;

    /*
     *    Now, do each character
     */
    for (i = 0; i < count; i++) {
	if (*s == 10)
	  SerialPollConout(13);
	SerialPollConout(*s++);
    }
}
extern int prom_getchar(void);
static int prom_console_wait_key(struct console *co)
{
    return prom_getchar();
}

extern void SerialPollInit(void);
extern void  SerialSetup(unsigned long  baud, unsigned long  console, unsigned long  host, unsigned long  intr_desc);
static int __init prom_console_setup(struct console *co, char *options)
{
  SerialSetup(19200,1,1,3);
  SerialPollInit();
  SerialPollOn();
  return 0;
}

static kdev_t prom_console_device(struct console *c)
{
    return MKDEV(TTY_MAJOR, 64 + c->index);
}

static struct console sercons =
{
    name:	"ttyS",
    write:	prom_console_write,
    device:	prom_console_device,
    wait_key:	prom_console_wait_key,
    setup:	prom_console_setup,
    flags:	CON_PRINTBUFFER,
    index:	-1,
};

/*
 *    Register console.
 */

void serial_console_init(void)
{
	register_console(&sercons);
}

extern void prom_putchar(int mychar);

static char ppbuf[1000];


void prom_printf(char *fmt, ...)
{
	va_list args;
	char ch, *bptr;
	int i;

	va_start(args, fmt);
	i = vsprintf(ppbuf, fmt, args);

	bptr = ppbuf;

	while((ch = *(bptr++)) != 0) {
		if(ch == '\n')
			prom_putchar('\r');

		prom_putchar(ch);
	}
	va_end(args);
	return;
}



void prom_putchar(int mychar){}
int prom_getchar(void){return 0;}
struct app_header_s {
  unsigned long    MAGIC_JMP;
  unsigned long    MAGIC_NOP;
  unsigned long    header_tag;    
  unsigned long    header_flags;
  unsigned long    header_length;
  unsigned long    header_cksum;
     
  void             *load_addr;
  void             *end_addr;
  void             *start_addr;
  char             *app_name_p;
  char             *version_p;
  char             *date_p;
  char             *time_p;
  unsigned long    type;
  unsigned long    crc;
  unsigned long    reserved;
};
typedef struct app_header_s app_header_t;
char linked_app_name[]="linux";
char *linked_app_name_p=&linked_app_name[0];

char linked_app_ver[]="2.4 -test1";
char *linked_app_ver_p=&linked_app_ver[0];

char linked_app_date[]="today";
char *linked_app_date_p=&linked_app_date[0];

char linked_app_time[]="now";
char *linked_app_time_p=&linked_app_time[0];
extern void *__bss_start;
extern void *kernel_entry;

app_header_t app_header __attribute__ ((section (".app_header"))) = {
  (0x10000000 | (((sizeof(app_header_t)>>2)-1) & 0xffff)) ,
  0 ,
  (((( 0x4321  ) & 0xFFFF) << 16) | ((  0x0100  ) & 0xFFFF))  ,
  0x80000000 ,
  sizeof(app_header_t),
  0,
  &app_header,
  &__bss_start,
  &kernel_entry,
  linked_app_name,
  linked_app_ver,
  linked_app_date,
  linked_app_time,
  0
};
