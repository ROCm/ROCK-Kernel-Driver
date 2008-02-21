#include <asm/cpufeature.h>
#include <asm/page.h>
#include <asm/processor.h>

void scrub_pages(void *v, unsigned int count)
{
	if (likely(cpu_has_xmm2)) {
		unsigned long n = count * (PAGE_SIZE / sizeof(long) / 4);

		for (; n--; v += sizeof(long) * 4)
			asm("movnti %1,(%0)\n\t"
			    "movnti %1,%c2(%0)\n\t"
			    "movnti %1,2*%c2(%0)\n\t"
			    "movnti %1,3*%c2(%0)\n\t"
			    : : "r" (v), "r" (0L), "i" (sizeof(long))
			    : "memory");
		asm volatile("sfence" : : : "memory");
	} else
		for (; count--; v += PAGE_SIZE)
			clear_page(v);
}
