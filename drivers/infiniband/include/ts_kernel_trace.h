/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: ts_kernel_trace.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_TRACE_H
#define _TS_KERNEL_TRACE_H

#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(..,services_export.ver)
#endif

#include <linux/types.h>
#include "module_codes.h"
#include "trace_codes.h"
#include "trace_masks.h"

/* Define what format to use to print a 64-bit quantity.  This has to
   depend on the architecture because uint64_t will be unsigned long
   on 64-bit architectures but unsigned long long on 32-bit
   architectures.  (And Windows is a whole other problem) */
#if defined(i386) || defined(PPC) || defined(__x86_64__)
#  define TS_U64_FMT "ll"
#elif defined(__ia64__) || defined(__PPC64__)
#  define TS_U64_FMT "l"
#else
#  error TS_U64_FMT not defined for this architecture
#endif

#if !defined(TS_KERNEL_TRACE_LEVEL)
#  define TS_KERNEL_TRACE_LEVEL T_MAX
#endif

/**
   Print a general trace message

   @param mod module (tMODULE_ID) message is coming from
   @param level trace level (tTS_TRACE_LEVEL) of message
   @param flow flow that message belongs to
   @param format format (per printf(3)) for message
   @param args extra arguments for printf format

*/

#define TS_TRACE(mod, level, flow, format, args...) \
  do { \
    if (level < TS_KERNEL_TRACE_LEVEL) { \
      tsKernelTrace(__FILE__, __LINE__, __FUNCTION__, \
                    mod, level, flow, format, ## args); \
    } \
  } while (0)

/**
   Report to a standard flow at a default trace level
*/

#define TS_REPORT_FATAL(mod, format, args...) \
  TS_TRACE(mod, T_VERY_TERSE, TRACE_FLOW_FATAL, format, ## args)

#define TS_REPORT_WARN(mod, format, args...) \
  TS_TRACE(mod, T_TERSE, TRACE_FLOW_WARN, format, ## args)

#define TS_REPORT_INOUT(mod, format, args...) \
  TS_TRACE(mod, T_VERBOSE, TRACE_FLOW_INOUT, format, ## args)

#define TS_REPORT_INIT(mod, format, args...) \
  TS_TRACE(mod, T_VERBOSE, TRACE_FLOW_INIT, format, ## args)

#define TS_REPORT_CLEANUP(mod, format, args...) \
  TS_TRACE(mod, T_VERBOSE, TRACE_FLOW_CLEANUP, format, ## args)

#define TS_REPORT_STAGE(mod, format, args...) \
  TS_TRACE(mod, T_VERBOSE, TRACE_FLOW_STAGE, format, ## args)

#ifdef _TS_DATA_PATH_TRACE
#define TS_REPORT_DATA(mod, format, args...) \
  TS_TRACE(mod, T_SCREAM, TRACE_FLOW_DATA, format, ## args)
#else
#define TS_REPORT_DATA(mod, format, args...) \
  do { } while (0)
#endif

/**
   Log entry of a function
 */
#define TS_ENTER(mod) \
  TS_REPORT_INOUT(mod, "Enter")

/**
   Log successful completion of a function and return (optionally with
   return value)
*/
#define TS_EXIT(mod, retval...) \
  do { \
    TS_REPORT_INOUT(mod, "Exit: success"); \
    return retval; \
  } while (0)

/**
   Log failure of a function and return (optionally with return value)
*/
#define TS_EXIT_FAIL(mod, retval...) \
  do { \
    TS_REPORT_INOUT(mod, "Exit: failure"); \
    return retval; \
  } while (0)

/**
   Actual entry point to kernel trace code.  Most code should use
   TS_TRACE or other convenience macros rather than calling this
   function directly.
*/
void tsKernelTrace(
                   const char *file,
                   int line,
                   const char *function,
                   tMODULE_ID module,
                   tTS_TRACE_LEVEL level,
                   uint32_t flow_mask,
                   const char *format,
                   ...
                   );

/**
   Set the trace level for a module.
*/
void tsKernelTraceLevelSet(
                           tMODULE_ID mod,
                           tTS_TRACE_LEVEL level
                           );

/**
   Set the flow mask for a module.
*/
void tsKernelTraceFlowMaskSet(
                              tMODULE_ID mod,
                              uint32_t flow_mask
                              );

#endif /* _TS_KERNEL_TRACE_H */
