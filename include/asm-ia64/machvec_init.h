#define __MACHVEC_HDR(n)		<asm/machvec_##n##.h>
#define __MACHVEC_EXPAND(n)		__MACHVEC_HDR(n)
#define MACHVEC_PLATFORM_HEADER		__MACHVEC_EXPAND(MACHVEC_PLATFORM_NAME)

#include <asm/machvec.h>

extern ia64_mv_send_ipi_t ia64_send_ipi;
extern ia64_mv_inb_t __ia64_inb;
extern ia64_mv_inw_t __ia64_inw;
extern ia64_mv_inl_t __ia64_inl;
extern ia64_mv_outb_t __ia64_outb;
extern ia64_mv_outw_t __ia64_outw;
extern ia64_mv_outl_t __ia64_outl;

#define MACHVEC_HELPER(name)									\
 struct ia64_machine_vector machvec_##name __attribute__ ((unused, __section__ (".machvec")))	\
	= MACHVEC_INIT(name);

#define MACHVEC_DEFINE(name)	MACHVEC_HELPER(name)

MACHVEC_DEFINE(MACHVEC_PLATFORM_NAME)
