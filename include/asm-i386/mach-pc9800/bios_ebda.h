#ifndef _MACH_BIOS_EBDA_H
#define _MACH_BIOS_EBDA_H

/*
 * PC-9800 has no EBDA.
 * Its BIOS uses 0x40E for other purpose,
 * Not pointer to 4K EBDA area.
 */
static inline unsigned int get_bios_ebda(void)
{
	return 0;	/* 0 means none */
}

#endif /* _MACH_BIOS_EBDA_H */
