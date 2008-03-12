#ifdef CONFIG_X86_32
# include "../../nmi_32.h"
#else
# include "../../nmi_64.h"
# undef get_nmi_reason
# include "../mach_traps.h"
#endif
