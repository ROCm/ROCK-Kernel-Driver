#ifndef _KCL_LINUX_CONST_H
#define _KCL_LINUX_CONST_H

#include <uapi/linux/const.h>

#ifndef _ULL
#define _ULL(x)		(_AC(x, ULL))
#define ULL(x)		(_ULL(x))
#endif

#endif /* _KCL_LINUX_CONST_H */
