#include <linux/string.h>
#include <asm/io.h>
#include <linux/module.h>

void *memcpy_toio(void *dst,const void*src,unsigned len)
{
	return __inline_memcpy(dst,src,len);
}

void *memcpy_fromio(void *dst,const void*src,unsigned len)
{
	return __inline_memcpy(dst,src,len);
}
