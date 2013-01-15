#undef check_tsc_unstable
#define check_tsc_unstable _check_tsc_unstable_
#include_next <asm/tsc.h>
#undef check_tsc_unstable
#define check_tsc_unstable() WARN_ON(true)
