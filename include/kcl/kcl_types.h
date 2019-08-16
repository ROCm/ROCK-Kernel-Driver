#ifndef AMDKCL_TYPES_H
#define AMDKCL_TYPES_H

#ifndef HAVE_TYPE__POLL_T
#ifdef __CHECK_POLL
typedef unsigned __bitwise __poll_t;
#else
typedef unsigned __poll_t;
#endif
#endif
#endif
