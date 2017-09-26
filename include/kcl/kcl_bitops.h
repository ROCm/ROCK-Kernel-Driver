#ifndef AMDKCL_BITOPS_H
#define AMDKCL_BITOPS_H

#ifndef _BITOPS_LONG_SHIFT
#if BITS_PER_LONG == 32
# define _BITOPS_LONG_SHIFT 5
#elif BITS_PER_LONG == 64
# define _BITOPS_LONG_SHIFT 6
#else
# error "Unexpected BITS_PER_LONG"
#endif
#endif

#endif /* AMDKCL_BITOPS_H */
