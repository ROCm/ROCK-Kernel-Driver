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
#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while(0)
#define PAGE_BUG(page) BUG()
void out_of_line_bug(void);

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

#endif
