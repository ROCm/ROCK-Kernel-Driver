#include <linux/string.h>
#include <asm/io.h>
#include <linux/module.h>

void *memcpy_toio(void *dst,const void*src,unsigned len)
{
	return __inline_memcpy(__io_virt(dst),src,len);
}

void *memcpy_fromio(void *dst,const void*src,unsigned len)
{
	return __inline_memcpy(dst,__io_virt(src),len);
}

EXPORT_SYMBOL(memcpy_toio);
EXPORT_SYMBOL(memcpy_fromio);

