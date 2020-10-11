/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_FTRACE_H
#define AMDKCL_FTRACE_H

#if !defined(HAVE___PRINT_ARRAY)
extern const char * ftrace_print_array_seq(struct trace_seq *p, const void *buf, int count,
										   size_t el_size);
#define __print_array(array, count, el_size)				\
		({								\
				BUILD_BUG_ON(el_size != 1 && el_size != 2 &&		\
						     el_size != 4 && el_size != 8);		\
				ftrace_print_array_seq(p, array, count, el_size);	\
		})
#endif

#endif
