#ifndef __ASM_X8664_BUG_H
#define __ASM_X8664_BUG_H 1

#include <linux/stringify.h>

/*
 * Tell the user there is some problem.  The exception handler decodes 
 * this frame.
 */ 
struct bug_frame { 
       unsigned char ud2[2];          
	/* should use 32bit offset instead, but the assembler doesn't 
	   like it */ 
	char *filename;   
	unsigned short line; 
} __attribute__((packed)); 

#define BUG() \
	asm volatile("ud2 ; .quad %c1 ; .short %c0" :: \
		     "i"(__LINE__), "i" (__stringify(KBUILD_BASENAME)))
#define PAGE_BUG(page) BUG()
void out_of_line_bug(void);

#endif
