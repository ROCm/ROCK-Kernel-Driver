/* $Id$ */
#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#define BUG() do { printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); *(int *)0=0; } while (0)
#define PAGE_BUG(page) do {  BUG(); } while (0)

#endif
