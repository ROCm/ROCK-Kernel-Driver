#ifndef _H8300_BUG_H
#define _H8300_BUG_H

#define BUG() do { \
  printk("%s(%d): kernel BUG!\n", __FILE__, __LINE__); \
} while (0)

#define PAGE_BUG(page) do { \
         BUG(); \
} while (0)

#endif
