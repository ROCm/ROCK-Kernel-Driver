#ifndef _CRIS_BUG_H
#define _CRIS_BUG_H

#define BUG() do { \
  printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
} while (0)

#define PAGE_BUG(page) do { \
         BUG(); \
} while (0)

#endif
