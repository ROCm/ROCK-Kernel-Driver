#include <linux/string.h>
#include <asm/io.h>
#include <linux/module.h>

void *__memcpy_toio(unsigned long dst,const void*src,unsigned len)
{
	return __inline_memcpy((void *) dst,src,len);
}
EXPORT_SYMBOL(__memcpy_toio);

void *__memcpy_fromio(void *dst,unsigned long src,unsigned len)
{
	return __inline_memcpy(dst,(const void *) src,len);
}
EXPORT_SYMBOL(__memcpy_fromio);
