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

  $Id: ts_kernel_trace.h,v 1.13 2004/02/25 00:22:37 roland Exp $
*/

#ifndef _TS_KERNEL_TRACE_H
#define _TS_KERNEL_TRACE_H

/* Define what format to use to print a 64-bit quantity.  This has to
   depend on the architecture because uint64_t will be unsigned long
   on 64-bit architectures but unsigned long long on 32-bit
   architectures.  (And Windows is a whole other problem) */
#if defined(W2K_OS)
#  define TS_U64_FMT "I64"
#elif defined(i386) || defined(PPC) || defined(__x86_64__)
#  define TS_U64_FMT "ll"
#elif defined(__ia64__)
#  define TS_U64_FMT "l"
#else
#  error TS_U64_FMT not defined for this architecture
#endif

#ifdef W2K_OS // Vipul
#include "all/common/include/module_codes.h"
#include "all/common/include/trace_codes.h"
#include "all/common/include/trace_masks.h"
/* For varargs. */
#include <stdarg.h>
#include <stdio.h>

#if !defined(TS_KERNEL_TRACE_LEVEL)
#  define TS_KERNEL_TRACE_LEVEL T_MAX
#endif

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
void _tsKernelTrace(
                   const char *file,
                   int line,
                   const char *function,
                   tMODULE_ID module,
                   tTS_TRACE_LEVEL level,
                   uint32_t flow_mask,
                   const char *format,
                   va_list ap
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

/**
   The following machinery is needed to port the TS_TRACE et al macros
   from Linux to Windows without disturbing the uses of these macros.
   Because the Microsoft C preprocessor does not allow variable # args
   in macros, we resort to changing TS_TRACE etc from macros to
   several function calls and assignments separated by the comma operator.
   This code grabs the values of __FILE__ and __LINE__ and the _ts_trace()
   function uses them.  Because these values are stored in globals, we
   have to lock them to be thread safe.  Not the most elegant solution,
   but it works.
*/

struct {
  KSPIN_LOCK	lock;
  KIRQL		old_irql;
} _g_tr_lock;
static __inline void _tr_lock()
{
  KIRQL	old_irql;

  KeAcquireSpinLock(&_g_tr_lock.lock, &old_irql);
  _g_tr_lock.old_irql = old_irql;
}
static __inline void _tr_unlock()
{
  KeReleaseSpinLock(&_g_tr_lock.lock, _g_tr_lock.old_irql);
}

char		*_g_file;
int		_g_line;
tTS_TRACE_LEVEL	_g_level;
uint32_t	_g_flow;

static __inline void _ts_trace(tMODULE_ID mod,
    			       tTS_TRACE_LEVEL level,
			       uint32_t flow,
			       const char *format, ...)
{
  va_list	ap;
  char		*file;
  int		line;

  file = _g_file;
  line = _g_line;
  _tr_unlock();

  if (level < TS_KERNEL_TRACE_LEVEL) {
    va_start(ap, format);
    _tsKernelTrace(file, line, NULL, mod, level, flow, format, ap);
    va_end(ap);
  }
}

static __inline void _ts_trace2(tMODULE_ID mod, const char *format, ...)
{
  va_list		ap;
  char			*file;
  int			line;
  tTS_TRACE_LEVEL	level;
  uint32_t		flow;

  level = _g_level;
  file  = _g_file;
  line  = _g_line;
  flow  = _g_flow;
  _tr_unlock();

  if (level < TS_KERNEL_TRACE_LEVEL) {
    va_start(ap, format);
    _tsKernelTrace(file, line, NULL, mod, level, flow, format, ap);
    va_end(ap);
  }
}

static __inline void _ts_noop(tMODULE_ID mod, const char *format, ...)
{
}

/**
   Print a general trace message

   @param mod module (tMODULE_ID) message is coming from
   @param level trace level (tTS_TRACE_LEVEL) of message
   @param flow flow that message belongs to
   @param format format (per printf(3)) for message
   @param args extra arguments for printf format

*/

#ifndef NO_TS_TRACE

#define TS_TRACE _tr_lock(), _g_file=__FILE__, _g_line=__LINE__, _ts_trace

/**
   Report to a standard flow at a default trace level
*/

#define TS_REPORT(level,flow) \
	_tr_lock(), \
	_g_file=__FILE__, _g_line=__LINE__, \
	_g_level=(level), _g_flow=(flow), _ts_trace2

#else

/*
 * Trick to ignore trace macros at compile time, so no code whatsoever
 * is generated for them: make the parameter list look like an expression
 * (with many comma operators) that is the right-hand side of "0 && (a,b,c)".
 * The compiler won't bother to generate code for (a,b,c).
 */
#define TS_TRACE 0 &&
#define TS_REPORT(level,flow) 0 &&

#endif

#define TS_REPORT_FATAL		TS_REPORT(T_VERY_TERSE, TRACE_FLOW_FATAL)
#define TS_REPORT_WARN		TS_REPORT(T_TERSE, TRACE_FLOW_WARN)
#define TS_REPORT_INOUT		TS_REPORT(T_VERBOSE, TRACE_FLOW_INOUT)
#define TS_REPORT_INIT		TS_REPORT(T_VERBOSE, TRACE_FLOW_INIT)
#define TS_REPORT_CLEANUP	TS_REPORT(T_VERBOSE, TRACE_FLOW_CLEANUP)
#define TS_REPORT_STAGE		TS_REPORT(T_VERBOSE, TRACE_FLOW_STAGE)
#ifdef _TS_DATA_PATH_TRACE
#define TS_REPORT_DATA		TS_REPORT(T_SCREAM, TRACE_FLOW_DATA)
#else
#define TS_REPORT_DATA		_ts_noop
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

#define TS_EXIT(mod, retval) \
  do { \
    TS_REPORT_INOUT(mod, "Exit: success"); \
    return retval; \
  } while (0)

/**
   Log failure of a function and return (optionally with return value)
*/

#define TS_EXIT_FAIL(mod, retval) \
  do { \
    TS_REPORT_INOUT(mod, "Exit: failure"); \
    return retval; \
  } while (0)

#else // W2K_OS
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
#endif // W2K_OS
#endif /* _TS_KERNEL_TRACE_H */
