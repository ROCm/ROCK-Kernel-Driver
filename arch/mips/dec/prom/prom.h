/*  
 * Miscellaneous definitions used to call the routines contained in the boot
 * PROMs on various models of DECSTATION's.
 * the rights to redistribute these changes.
 */

#ifndef __ASM_DEC_PROM_H 
#define __ASM_DEC_PROM_H 

/*
 * PMAX/3MAX PROM entry points for DS2100/3100's and DS5000/2xx's.  Many of
 * these will work for MIPSen as well!
 */
#define VEC_RESET		0xBFC00000		/* Prom base address */
#define	PMAX_PROM_ENTRY(x)	(VEC_RESET+((x)*8))	/* Prom jump table */

#define	PMAX_PROM_HALT		PMAX_PROM_ENTRY(2)	/* valid on MIPSen */
#define	PMAX_PROM_AUTOBOOT	PMAX_PROM_ENTRY(5)	/* valid on MIPSen */
#define	PMAX_PROM_OPEN		PMAX_PROM_ENTRY(6)
#define	PMAX_PROM_READ		PMAX_PROM_ENTRY(7)
#define	PMAX_PROM_CLOSE		PMAX_PROM_ENTRY(10)
#define	PMAX_PROM_LSEEK		PMAX_PROM_ENTRY(11)
#define PMAX_PROM_GETCHAR       PMAX_PROM_ENTRY(12)
#define	PMAX_PROM_PUTCHAR	PMAX_PROM_ENTRY(13)	/* 12 on MIPSen */
#define	PMAX_PROM_GETS		PMAX_PROM_ENTRY(15)
#define	PMAX_PROM_PRINTF	PMAX_PROM_ENTRY(17)
#define	PMAX_PROM_GETENV	PMAX_PROM_ENTRY(33)	/* valid on MIPSen */

/*
 * Magic number indicating REX PROM available on DECSTATION.  Found in
 * register a2 on transfer of control to program from PROM.
 */
#define	REX_PROM_MAGIC		0x30464354

/*
 * 3MIN/MAXINE PROM entry points for DS5000/1xx's, DS5000/xx's, and
 * DS5000/2x0.
 */
#define REX_PROM_GETBITMAP	0x84/4	/* get mem bitmap */
#define REX_PROM_GETCHAR	0x24/4	/* getch() */
#define REX_PROM_GETENV		0x64/4	/* get env. variable */
#define REX_PROM_GETSYSID	0x80/4	/* get system id */
#define REX_PROM_GETTCINFO	0xa4/4
#define REX_PROM_PRINTF		0x30/4	/* printf() */
#define REX_PROM_SLOTADDR	0x6c/4	/* slotaddr */
#define REX_PROM_BOOTINIT	0x54/4	/* open() */
#define REX_PROM_BOOTREAD	0x58/4	/* read() */
#define REX_PROM_CLEARCACHE	0x7c/4

#endif /* __ASM_DEC_PROM_H */

