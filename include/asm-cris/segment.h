#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

/* argh. really legacy. totally misnomed. */

#define __KERNEL_CS	0x10
#define __KERNEL_DS	0x18

#define __USER_CS	0x23
#define __USER_DS	0x2B

typedef struct {
  unsigned long seg;
} mm_segment_t;

#endif
