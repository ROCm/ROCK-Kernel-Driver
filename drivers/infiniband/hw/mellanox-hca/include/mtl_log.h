/* DON'T EDIT: this file is generated automatically */
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

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/


#ifndef H_MTL_LOG_H
#define H_MTL_LOG_H

#define MTL_TRACE(S, F, A...) _MTL_TRACE(MTL_MODULE, S, F, ## A)
#define _MTL_TRACE(M, S, F, A...) __MTL_TRACE(M, S, F, ## A)
#define __MTL_TRACE(M, S, F, A...) ___MTL_TRACE(#M, S, F, ## A)
#if 1 <= MAX_TRACE
#define ___MTL_TRACE(M, S, F, A...) mtl_log(M, mtl_log_trace, S, F, ## A)
#else
#define ___MTL_TRACE(M, S, F, A...)
#endif

#define MTL_TRACE1(F, A...) _MTL_TRACE1(MTL_MODULE, F, ## A)
#define _MTL_TRACE1(M, F, A...) __MTL_TRACE1(M, F, ## A)
#define __MTL_TRACE1(M, F, A...) ___MTL_TRACE1(#M, F, ## A)
#if 1 <= MAX_TRACE
#define ___MTL_TRACE1(M, F, A...) mtl_log(M, mtl_log_trace, '1', F, ## A)
#else
#define ___MTL_TRACE1(M, F, A...)
#endif

#define MTL_TRACE2(F, A...) _MTL_TRACE2(MTL_MODULE, F, ## A)
#define _MTL_TRACE2(M, F, A...) __MTL_TRACE2(M, F, ## A)
#define __MTL_TRACE2(M, F, A...) ___MTL_TRACE2(#M, F, ## A)
#if 2 <= MAX_TRACE
#define ___MTL_TRACE2(M, F, A...) mtl_log(M, mtl_log_trace, '2', F, ## A)
#else
#define ___MTL_TRACE2(M, F, A...)
#endif

#define MTL_TRACE3(F, A...) _MTL_TRACE3(MTL_MODULE, F, ## A)
#define _MTL_TRACE3(M, F, A...) __MTL_TRACE3(M, F, ## A)
#define __MTL_TRACE3(M, F, A...) ___MTL_TRACE3(#M, F, ## A)
#if 3 <= MAX_TRACE
#define ___MTL_TRACE3(M, F, A...) mtl_log(M, mtl_log_trace, '3', F, ## A)
#else
#define ___MTL_TRACE3(M, F, A...)
#endif

#define MTL_TRACE4(F, A...) _MTL_TRACE4(MTL_MODULE, F, ## A)
#define _MTL_TRACE4(M, F, A...) __MTL_TRACE4(M, F, ## A)
#define __MTL_TRACE4(M, F, A...) ___MTL_TRACE4(#M, F, ## A)
#if 4 <= MAX_TRACE
#define ___MTL_TRACE4(M, F, A...) mtl_log(M, mtl_log_trace, '4', F, ## A)
#else
#define ___MTL_TRACE4(M, F, A...)
#endif

#define MTL_TRACE5(F, A...) _MTL_TRACE5(MTL_MODULE, F, ## A)
#define _MTL_TRACE5(M, F, A...) __MTL_TRACE5(M, F, ## A)
#define __MTL_TRACE5(M, F, A...) ___MTL_TRACE5(#M, F, ## A)
#if 5 <= MAX_TRACE
#define ___MTL_TRACE5(M, F, A...) mtl_log(M, mtl_log_trace, '5', F, ## A)
#else
#define ___MTL_TRACE5(M, F, A...)
#endif

#define MTL_TRACE6(F, A...) _MTL_TRACE6(MTL_MODULE, F, ## A)
#define _MTL_TRACE6(M, F, A...) __MTL_TRACE6(M, F, ## A)
#define __MTL_TRACE6(M, F, A...) ___MTL_TRACE6(#M, F, ## A)
#if 6 <= MAX_TRACE
#define ___MTL_TRACE6(M, F, A...) mtl_log(M, mtl_log_trace, '6', F, ## A)
#else
#define ___MTL_TRACE6(M, F, A...)
#endif

#define MTL_TRACE7(F, A...) _MTL_TRACE7(MTL_MODULE, F, ## A)
#define _MTL_TRACE7(M, F, A...) __MTL_TRACE7(M, F, ## A)
#define __MTL_TRACE7(M, F, A...) ___MTL_TRACE7(#M, F, ## A)
#if 7 <= MAX_TRACE
#define ___MTL_TRACE7(M, F, A...) mtl_log(M, mtl_log_trace, '7', F, ## A)
#else
#define ___MTL_TRACE7(M, F, A...)
#endif

#define MTL_TRACE8(F, A...) _MTL_TRACE8(MTL_MODULE, F, ## A)
#define _MTL_TRACE8(M, F, A...) __MTL_TRACE8(M, F, ## A)
#define __MTL_TRACE8(M, F, A...) ___MTL_TRACE8(#M, F, ## A)
#if 8 <= MAX_TRACE
#define ___MTL_TRACE8(M, F, A...) mtl_log(M, mtl_log_trace, '8', F, ## A)
#else
#define ___MTL_TRACE8(M, F, A...)
#endif

#define MTL_TRACE9(F, A...) _MTL_TRACE9(MTL_MODULE, F, ## A)
#define _MTL_TRACE9(M, F, A...) __MTL_TRACE9(M, F, ## A)
#define __MTL_TRACE9(M, F, A...) ___MTL_TRACE9(#M, F, ## A)
#if 9 <= MAX_TRACE
#define ___MTL_TRACE9(M, F, A...) mtl_log(M, mtl_log_trace, '9', F, ## A)
#else
#define ___MTL_TRACE9(M, F, A...)
#endif

#define MTL_DEBUG(S, F, A...) _MTL_DEBUG(MTL_MODULE, S, F, ## A)
#define _MTL_DEBUG(M, S, F, A...) __MTL_DEBUG(M, S, F, ## A)
#define __MTL_DEBUG(M, S, F, A...) ___MTL_DEBUG(#M, S, F, ## A)
#if 1 <= MAX_DEBUG
#define ___MTL_DEBUG(M, S, F, A...) mtl_log(M, mtl_log_debug, S, F, ## A)
#else
#define ___MTL_DEBUG(M, S, F, A...)
#endif

#define MTL_DEBUG1(F, A...) _MTL_DEBUG1(MTL_MODULE, F, ## A)
#define _MTL_DEBUG1(M, F, A...) __MTL_DEBUG1(M, F, ## A)
#define __MTL_DEBUG1(M, F, A...) ___MTL_DEBUG1(#M, F, ## A)
#if 1 <= MAX_DEBUG
#define ___MTL_DEBUG1(M, F, A...) mtl_log(M, mtl_log_debug, '1', F, ## A)
#else
#define ___MTL_DEBUG1(M, F, A...)
#endif

#define MTL_DEBUG2(F, A...) _MTL_DEBUG2(MTL_MODULE, F, ## A)
#define _MTL_DEBUG2(M, F, A...) __MTL_DEBUG2(M, F, ## A)
#define __MTL_DEBUG2(M, F, A...) ___MTL_DEBUG2(#M, F, ## A)
#if 2 <= MAX_DEBUG
#define ___MTL_DEBUG2(M, F, A...) mtl_log(M, mtl_log_debug, '2', F, ## A)
#else
#define ___MTL_DEBUG2(M, F, A...)
#endif

#define MTL_DEBUG3(F, A...) _MTL_DEBUG3(MTL_MODULE, F, ## A)
#define _MTL_DEBUG3(M, F, A...) __MTL_DEBUG3(M, F, ## A)
#define __MTL_DEBUG3(M, F, A...) ___MTL_DEBUG3(#M, F, ## A)
#if 3 <= MAX_DEBUG
#define ___MTL_DEBUG3(M, F, A...) mtl_log(M, mtl_log_debug, '3', F, ## A)
#else
#define ___MTL_DEBUG3(M, F, A...)
#endif

#define MTL_DEBUG4(F, A...) _MTL_DEBUG4(MTL_MODULE, F, ## A)
#define _MTL_DEBUG4(M, F, A...) __MTL_DEBUG4(M, F, ## A)
#define __MTL_DEBUG4(M, F, A...) ___MTL_DEBUG4(#M, F, ## A)
#if 4 <= MAX_DEBUG
#define ___MTL_DEBUG4(M, F, A...) mtl_log(M, mtl_log_debug, '4', F, ## A)
#else
#define ___MTL_DEBUG4(M, F, A...)
#endif

#define MTL_DEBUG5(F, A...) _MTL_DEBUG5(MTL_MODULE, F, ## A)
#define _MTL_DEBUG5(M, F, A...) __MTL_DEBUG5(M, F, ## A)
#define __MTL_DEBUG5(M, F, A...) ___MTL_DEBUG5(#M, F, ## A)
#if 5 <= MAX_DEBUG
#define ___MTL_DEBUG5(M, F, A...) mtl_log(M, mtl_log_debug, '5', F, ## A)
#else
#define ___MTL_DEBUG5(M, F, A...)
#endif

#define MTL_DEBUG6(F, A...) _MTL_DEBUG6(MTL_MODULE, F, ## A)
#define _MTL_DEBUG6(M, F, A...) __MTL_DEBUG6(M, F, ## A)
#define __MTL_DEBUG6(M, F, A...) ___MTL_DEBUG6(#M, F, ## A)
#if 6 <= MAX_DEBUG
#define ___MTL_DEBUG6(M, F, A...) mtl_log(M, mtl_log_debug, '6', F, ## A)
#else
#define ___MTL_DEBUG6(M, F, A...)
#endif

#define MTL_DEBUG7(F, A...) _MTL_DEBUG7(MTL_MODULE, F, ## A)
#define _MTL_DEBUG7(M, F, A...) __MTL_DEBUG7(M, F, ## A)
#define __MTL_DEBUG7(M, F, A...) ___MTL_DEBUG7(#M, F, ## A)
#if 7 <= MAX_DEBUG
#define ___MTL_DEBUG7(M, F, A...) mtl_log(M, mtl_log_debug, '7', F, ## A)
#else
#define ___MTL_DEBUG7(M, F, A...)
#endif

#define MTL_DEBUG8(F, A...) _MTL_DEBUG8(MTL_MODULE, F, ## A)
#define _MTL_DEBUG8(M, F, A...) __MTL_DEBUG8(M, F, ## A)
#define __MTL_DEBUG8(M, F, A...) ___MTL_DEBUG8(#M, F, ## A)
#if 8 <= MAX_DEBUG
#define ___MTL_DEBUG8(M, F, A...) mtl_log(M, mtl_log_debug, '8', F, ## A)
#else
#define ___MTL_DEBUG8(M, F, A...)
#endif

#define MTL_DEBUG9(F, A...) _MTL_DEBUG9(MTL_MODULE, F, ## A)
#define _MTL_DEBUG9(M, F, A...) __MTL_DEBUG9(M, F, ## A)
#define __MTL_DEBUG9(M, F, A...) ___MTL_DEBUG9(#M, F, ## A)
#if 9 <= MAX_DEBUG
#define ___MTL_DEBUG9(M, F, A...) mtl_log(M, mtl_log_debug, '9', F, ## A)
#else
#define ___MTL_DEBUG9(M, F, A...)
#endif

#define MTL_ERROR(S, F, A...) _MTL_ERROR(MTL_MODULE, S, F, ## A)
#define _MTL_ERROR(M, S, F, A...) __MTL_ERROR(M, S, F, ## A)
#define __MTL_ERROR(M, S, F, A...) ___MTL_ERROR(#M, S, F, ## A)
#if 1 <= MAX_ERROR
#define ___MTL_ERROR(M, S, F, A...) mtl_log(M, mtl_log_error, S, F, ## A)
#else
#define ___MTL_ERROR(M, S, F, A...)
#endif

#define MTL_ERROR1(F, A...) _MTL_ERROR1(MTL_MODULE, F, ## A)
#define _MTL_ERROR1(M, F, A...) __MTL_ERROR1(M, F, ## A)
#define __MTL_ERROR1(M, F, A...) ___MTL_ERROR1(#M, F, ## A)
#if 1 <= MAX_ERROR
#define ___MTL_ERROR1(M, F, A...) mtl_log(M, mtl_log_error, '1', F, ## A)
#else
#define ___MTL_ERROR1(M, F, A...)
#endif

#define MTL_ERROR2(F, A...) _MTL_ERROR2(MTL_MODULE, F, ## A)
#define _MTL_ERROR2(M, F, A...) __MTL_ERROR2(M, F, ## A)
#define __MTL_ERROR2(M, F, A...) ___MTL_ERROR2(#M, F, ## A)
#if 2 <= MAX_ERROR
#define ___MTL_ERROR2(M, F, A...) mtl_log(M, mtl_log_error, '2', F, ## A)
#else
#define ___MTL_ERROR2(M, F, A...)
#endif

#define MTL_ERROR3(F, A...) _MTL_ERROR3(MTL_MODULE, F, ## A)
#define _MTL_ERROR3(M, F, A...) __MTL_ERROR3(M, F, ## A)
#define __MTL_ERROR3(M, F, A...) ___MTL_ERROR3(#M, F, ## A)
#if 3 <= MAX_ERROR
#define ___MTL_ERROR3(M, F, A...) mtl_log(M, mtl_log_error, '3', F, ## A)
#else
#define ___MTL_ERROR3(M, F, A...)
#endif

#define MTL_ERROR4(F, A...) _MTL_ERROR4(MTL_MODULE, F, ## A)
#define _MTL_ERROR4(M, F, A...) __MTL_ERROR4(M, F, ## A)
#define __MTL_ERROR4(M, F, A...) ___MTL_ERROR4(#M, F, ## A)
#if 4 <= MAX_ERROR
#define ___MTL_ERROR4(M, F, A...) mtl_log(M, mtl_log_error, '4', F, ## A)
#else
#define ___MTL_ERROR4(M, F, A...)
#endif

#define MTL_ERROR5(F, A...) _MTL_ERROR5(MTL_MODULE, F, ## A)
#define _MTL_ERROR5(M, F, A...) __MTL_ERROR5(M, F, ## A)
#define __MTL_ERROR5(M, F, A...) ___MTL_ERROR5(#M, F, ## A)
#if 5 <= MAX_ERROR
#define ___MTL_ERROR5(M, F, A...) mtl_log(M, mtl_log_error, '5', F, ## A)
#else
#define ___MTL_ERROR5(M, F, A...)
#endif

#define MTL_ERROR6(F, A...) _MTL_ERROR6(MTL_MODULE, F, ## A)
#define _MTL_ERROR6(M, F, A...) __MTL_ERROR6(M, F, ## A)
#define __MTL_ERROR6(M, F, A...) ___MTL_ERROR6(#M, F, ## A)
#if 6 <= MAX_ERROR
#define ___MTL_ERROR6(M, F, A...) mtl_log(M, mtl_log_error, '6', F, ## A)
#else
#define ___MTL_ERROR6(M, F, A...)
#endif

#define MTL_ERROR7(F, A...) _MTL_ERROR7(MTL_MODULE, F, ## A)
#define _MTL_ERROR7(M, F, A...) __MTL_ERROR7(M, F, ## A)
#define __MTL_ERROR7(M, F, A...) ___MTL_ERROR7(#M, F, ## A)
#if 7 <= MAX_ERROR
#define ___MTL_ERROR7(M, F, A...) mtl_log(M, mtl_log_error, '7', F, ## A)
#else
#define ___MTL_ERROR7(M, F, A...)
#endif

#define MTL_ERROR8(F, A...) _MTL_ERROR8(MTL_MODULE, F, ## A)
#define _MTL_ERROR8(M, F, A...) __MTL_ERROR8(M, F, ## A)
#define __MTL_ERROR8(M, F, A...) ___MTL_ERROR8(#M, F, ## A)
#if 8 <= MAX_ERROR
#define ___MTL_ERROR8(M, F, A...) mtl_log(M, mtl_log_error, '8', F, ## A)
#else
#define ___MTL_ERROR8(M, F, A...)
#endif

#define MTL_ERROR9(F, A...) _MTL_ERROR9(MTL_MODULE, F, ## A)
#define _MTL_ERROR9(M, F, A...) __MTL_ERROR9(M, F, ## A)
#define __MTL_ERROR9(M, F, A...) ___MTL_ERROR9(#M, F, ## A)
#if 9 <= MAX_ERROR
#define ___MTL_ERROR9(M, F, A...) mtl_log(M, mtl_log_error, '9', F, ## A)
#else
#define ___MTL_ERROR9(M, F, A...)
#endif

#endif /* H_MTL_LOG_H */
