/*
 * Copyright 2003, 2004, Egbert Eich
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * EGBERT EICH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CON-
 * NECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Egbert Eich shall not
 * be used in advertising or otherwise to promote the sale, use or other deal-
 *ings in this Software without prior written authorization from Egbert Eich.
 *
 */
#ifndef _DRM_IOCTL32_H
# define _DRM_IOCTL32_H

# ifdef IOCTL32_PRIVATE

#define DEBUG(x) /**/ /* printk(KERN_DEBUG"%s\n",x) */

#define SYS_IOCTL_FUNC sys_ioctl

#  define GET_USER_ARGS(x32,x64,elem) err |= get_user(x64.elem,&x32->elem)
#  define PUT_USER_ARGS(x32,x64,elem) err |= put_user(x64.elem,&x32->elem)
#  define GET_USER(elem) GET_USER_ARGS(arg32,arg64,elem)
#  define PUT_USER(elem) PUT_USER_ARGS(arg32,arg64,elem)
#  define GET_USER_P_ARGS(x32,x64,elem) do { \
    err |= get_user(dummy,&x32->elem); \
    x64.elem = (void *) dummy; \
} while (0);
#  define PUT_USER_P_ARGS(x32,x64,elem) do { \
    dummy = (u64) x64.elem; \
    err |= put_user((u32)dummy,&x32->elem); \
} while (0);

#  define GET_USER_P(elem) GET_USER_P_ARGS(arg32,arg64,elem)
#  define PUT_USER_P(elem) PUT_USER_P_ARGS(arg32,arg64,elem)

#  define SYS_IOCTL do { \
    old_fs = get_fs(); \
    set_fs(KERNEL_DS); \
    DEBUG("SYS_IOCTL_FUNC called"); \
    err = SYS_IOCTL_FUNC(fd,cmd,(unsigned long)&arg64); \
    DEBUG("SYS_IOCTL_FUNC done"); \
    set_fs(old_fs); \
    } while (0);

#  define REG_IOCTL32(nr,c_func) \
  err = register_ioctl32_conversion(nr,c_func); \
  if (err)  goto failed; 

#  define UNREG_IOCTL32(nr) \
  unregister_ioctl32_conversion(nr);

#  define K_ALLOC(x) kmalloc(x,GFP_KERNEL)
#  define K_FREE(x)  kfree(x)

#  define ASSERT32(x) do { \
      if (arg64.x & 0xFFFFFFFF00000000) \
           printk(KERN_WARNING "ioctl32: var " #x " is > 32bit in ioctl(0x%x)\n",cmd); \
   } while (0)

extern int drm32_default_handler(unsigned int fd, unsigned int cmd, 
			       unsigned long arg, struct file *file);

# endif

extern int drm32_register(void);
extern void drm32_unregister(void);

#endif
