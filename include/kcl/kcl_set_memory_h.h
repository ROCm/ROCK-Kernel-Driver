#ifndef AMDKCL_SET_MEMORY_H_H
#define AMDKCL_SET_MEMORY_H_H

#if defined(HAVE_SET_MEMORY_H)
#include <asm/set_memory.h>
#else
#include <asm/cacheflush.h>
#endif
#endif
