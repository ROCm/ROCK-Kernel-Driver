/*
 * Copyright (C) 1995-2001 Russell King
 *               2001-2002 Keith Owens
 *     
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

/*
 * Make sure that the compiler and target are compatible.
 */
#if defined(__APCS_26__)
#error Sorry, your compiler targets APCS-26 but this kernel requires APCS-32
#endif
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 95)
#error Sorry, your compiler is known to miscompile kernels.  Only use gcc 2.95.3 and later.
#endif
#if __GNUC__ == 2 && __GNUC_MINOR__ == 95
/* shame we can't detect the .1 or .2 releases */
#warning GCC 2.95.2 and earlier miscompiles kernels.
#endif

/* Use marker if you need to separate the values later */

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
  DEFINE(TSK_USED_MATH,		offsetof(struct task_struct, used_math));
  DEFINE(TSK_ACTIVE_MM,		offsetof(struct task_struct, active_mm));
  BLANK();
  DEFINE(VMA_VM_MM,		offsetof(struct vm_area_struct, vm_mm));
  DEFINE(VMA_VM_FLAGS,		offsetof(struct vm_area_struct, vm_flags));
  BLANK();
  DEFINE(VM_EXEC,	       	VM_EXEC);
  BLANK();
  DEFINE(HPTE_TYPE_SMALL,      	PTE_TYPE_SMALL);
  DEFINE(HPTE_AP_READ,		PTE_AP_READ);
  DEFINE(HPTE_AP_WRITE,		PTE_AP_WRITE);
  BLANK();
  DEFINE(LPTE_PRESENT,		L_PTE_PRESENT);
  DEFINE(LPTE_YOUNG,		L_PTE_YOUNG);
  DEFINE(LPTE_BUFFERABLE,      	L_PTE_BUFFERABLE);
  DEFINE(LPTE_CACHEABLE,       	L_PTE_CACHEABLE);
  DEFINE(LPTE_USER,		L_PTE_USER);
  DEFINE(LPTE_WRITE,		L_PTE_WRITE);
  DEFINE(LPTE_EXEC,		L_PTE_EXEC);
  DEFINE(LPTE_DIRTY,		L_PTE_DIRTY);
  BLANK();
  BLANK();
  DEFINE(PAGE_SZ,	       	PAGE_SIZE);
  BLANK();
  DEFINE(SYS_ERROR0,		0x9f0000);
  return 0; 
}
