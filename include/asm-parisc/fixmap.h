#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

/*
 * Allocate a 8 Mb temporary mapping area for copy_user_page/clear_user_page.
 * This area needs to be aligned on a 8 Mb boundary.
 *
 * FIXME:
 *
 * For PA-RISC, this has no meaning.  It is starting to be used on x86
 * for vsyscalls.  PA will probably do this using space registers.
 */

/* This TMPALIAS_MAP_START reserves some of the memory where the
 * FIXMAP region is on x86.  It's only real use is to constrain
 * VMALLOC_END (see pktable.h) */
#define TMPALIAS_MAP_START (__PAGE_OFFSET - 0x01000000)

#endif
