/*
 * init.c: PROM library initialisation code.
 *
 * Copyright (C) 1998 Harald Koerfgen
 *
 * $Id: init.c,v 1.3 1999/08/09 19:43:13 harald Exp $
 */
#include <linux/init.h>
#include <linux/config.h>
#include <asm/bootinfo.h>
#include "prom.h"

/*
 * PROM Interface (whichprom.c)
 */
typedef struct {
	int pagesize;
	unsigned char bitmap[0];
} memmap;

int (*rex_bootinit)(void);
int (*rex_bootread)(void);
int (*rex_getbitmap)(memmap *);
unsigned long *(*rex_slot_address)(int);
void *(*rex_gettcinfo)(void);
int (*rex_getsysid)(void);
void (*rex_clear_cache)(void);

int (*prom_getchar)(void);
char *(*prom_getenv)(char *);
int (*prom_printf)(char *, ...);

int (*pmax_open)(char*, int);
int (*pmax_lseek)(int, long, int);
int (*pmax_read)(int, void *, int);
int (*pmax_close)(int);

extern void prom_meminit(unsigned int);
extern void prom_identify_arch(unsigned int);
extern void prom_init_cmdline(int, char **, unsigned long);

/*
 * Detect which PROM's the DECSTATION has, and set the callback vectors
 * appropriately.
 */
void __init which_prom(unsigned long magic, int *prom_vec)
{
	/*
	 * No sign of the REX PROM's magic number means we assume a non-REX
	 * machine (i.e. we're on a DS2100/3100, DS5100 or DS5000/2xx)
	 */
	if (magic == REX_PROM_MAGIC)
	{
		/*
		 * Set up prom abstraction structure with REX entry points.
		 */
		rex_bootinit = (int (*)(void)) *(prom_vec + REX_PROM_BOOTINIT);
		rex_bootread = (int (*)(void)) *(prom_vec + REX_PROM_BOOTREAD);
		rex_getbitmap = (int (*)(memmap *)) *(prom_vec + REX_PROM_GETBITMAP);
		prom_getchar = (int (*)(void)) *(prom_vec + REX_PROM_GETCHAR);
		prom_getenv = (char *(*)(char *)) *(prom_vec + REX_PROM_GETENV);
		rex_getsysid = (int (*)(void)) *(prom_vec + REX_PROM_GETSYSID);
		rex_gettcinfo = (void *(*)(void)) *(prom_vec + REX_PROM_GETTCINFO);
		prom_printf = (int (*)(char *, ...)) *(prom_vec + REX_PROM_PRINTF);
		rex_slot_address = (unsigned long *(*)(int)) *(prom_vec + REX_PROM_SLOTADDR);
		rex_clear_cache = (void (*)(void)) * (prom_vec + REX_PROM_CLEARCACHE);
	}
	else
	{
		/*
		 * Set up prom abstraction structure with non-REX entry points.
		 */
		prom_getchar = (int (*)(void)) PMAX_PROM_GETCHAR;
		prom_getenv = (char *(*)(char *)) PMAX_PROM_GETENV;
		prom_printf = (int (*)(char *, ...)) PMAX_PROM_PRINTF;
		pmax_open = (int (*)(char *, int)) PMAX_PROM_OPEN;
		pmax_lseek = (int (*)(int, long, int)) PMAX_PROM_LSEEK;
		pmax_read = (int (*)(int, void *, int)) PMAX_PROM_READ;
		pmax_close = (int (*)(int)) PMAX_PROM_CLOSE;
	}
} 

int __init prom_init(int argc, char **argv,
	       unsigned long magic, int *prom_vec)
{
	extern void dec_machine_halt(void);

	/* Determine which PROM's we have (and therefore which machine we're on!) */
	which_prom(magic, prom_vec);

	if (magic == REX_PROM_MAGIC)
		rex_clear_cache();

	/* Were we compiled with the right CPU option? */
#if defined(CONFIG_CPU_R3000)
	if ((mips_cputype == CPU_R4000SC) || (mips_cputype == CPU_R4400SC)) {
		prom_printf("Sorry, this kernel is compiled for the wrong CPU type!\n");
		prom_printf("Please recompile with \"CONFIG_CPU_R4x00 = y\"\n");
		dec_machine_halt();
	}
#endif

#if defined(CONFIG_CPU_R4x00)
	if ((mips_cputype == CPU_R3000) || (mips_cputype == CPU_R3000A)) {
		prom_printf("Sorry, this kernel is compiled for the wrong CPU type!\n");
		prom_printf("Please recompile with \"CONFIG_CPU_R3000 = y\"\n");
		dec_machine_halt();
	}
#endif

	prom_meminit(magic);
	prom_identify_arch(magic);
	prom_init_cmdline(argc, argv, magic);

	return 0;
}
