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


#ifndef H_MTL_ERRNO_H
#define H_MTL_ERRNO_H

#ifndef __DARWIN__
  #if defined(__KERNEL__)
    #include <asm/errno.h>
  #else
    #include <errno.h>
  #endif
#else
  #include <sys/errno.h>
#endif

  /****************** General Purpose Error Codes (0 to -999) *****************/
#ifndef ETIMEDOUT
#define ETIMEDOUT		(110)
#endif

#ifndef ENOSYS
#define ENOSYS 38      /* Function not implemented */
#endif

#ifndef EINVAL
#define EINVAL 22  /* Invalid argument */
#endif



#define ERROR_LIST_GENERAL \
  INFO( MT_OK,          0,      "success" ) \
  INFO( MT_ERROR,       -1,     "generic error" ) \
  INFO( MT_ENOINIT,     -2,     "module not initialized" ) \
  INFO( MT_EINVAL,      -3,     "invalid argument" ) \
  INFO( MT_ENORSC,      -4,     "No such resource (probably out of range)" ) \
  INFO( MT_EPERM,       -5,     "Not enough permissions to perform operation" ) \
  INFO( MT_ENOSYS,      -6,     "The system doesn't support requested operation" ) \
  INFO( MT_EAGAIN,      -7,     "Resource temporarily unavailable" ) \
  INFO( MT_EALIGN,      -8,     "Alignment error (offset/size not aligned)" ) \
  INFO( MT_EDEADLK,     -9,     "Resource deadlock avoided" ) \
  INFO( MT_ENOENT,     -10,     "No such file or directory" ) \
  INFO( MT_EACCES,     -11,     "Permission denied" ) \
  INFO( MT_EINTR,      -12,     "process received interrupt") \
  INFO( MT_ESTATE,     -13,     "Invalid state") \
  INFO( MT_ESYSCALL,              -14,"Error in an underlying O/S call") \
  INFO( MT_ETIMEDOUT,  -ETIMEDOUT,"Operation timed out" ) \
  INFO( MT_SYS_EINVAL, -EINVAL, "Invalid argument")\
  INFO( MT_ENOMOD,     -ENOSYS, "module not loaded") /* When module not loaded, syscall return ENOSYS */



  /**************** Memory Handling Error Codes (-1000 to -1199) **************/


#define ERROR_LIST_MEMORY \
  INFO( MT_EKMALLOC,    -1000,  "Can't allocate kernel memory" ) \
  INFO( MT_ENOMEM,      -1001,  "Given address doesn't match process address space" ) \
  INFO( MT_EMALLOC,     -1002,  "malloc fail") \
  INFO( MT_EFAULT,      -1003,  "Bad address" )

  /****************** General Device Error Codes (-1200 to -1399) *************/

#define ERROR_LIST_DEVICE \
  INFO( MT_ENODEV,      -1200,  "No such device" ) \
  INFO( MT_EBUSY,       -1201,  "Device or resource busy (or used by another)" ) \
  INFO( MT_EBUSBUSY,    -1202,  "Bus busy" )

  /*********************** I2C Error Codes (-1400 to -1499) *******************/

#define ERROR_LIST_I2C \
  INFO( MT_EI2CNACK,    -1400,   "I2C: received NACK from slave" ) \
  INFO( MT_EI2PINHI,    -1401,   "I2C: Pending Interrupt Not does no become low" ) \
  INFO( MT_EI2TOUT,     -1402,   "I2C: Operation has been timed out" )  
 
#define ERROR_LIST      ERROR_LIST_GENERAL ERROR_LIST_MEMORY ERROR_LIST_DEVICE ERROR_LIST_I2C

  /** 
   **   See at end of file the full list of POSIX errors
   **/


typedef enum {
#define INFO(A,B,C)     A = B,
        ERROR_LIST
#undef INFO
	    MT_DUMMY_ERROR   /* this one is needed to quite warning by -pedantic */
} call_result_t;

#endif  /* H_MTL_ERRNO_H */

#if 0

            The following list derrived automatically from
        ISO/IEC 9945-1: 1996 ANSI/IEEE Std 1003.1, 1996 Edition
        Chapter 2.4 Error Numbers


            If you add a new MT_ error please consider one from this list

  INFO( E2BIG,          xxx,      "Arg list too long" ) \
  INFO( EAGAIN,         xxx,      "Resource temporarily unavailable" ) \
  INFO( EBADF,          xxx,      "Bad file descriptor" ) \
  INFO( EBADMSG,        xxx,      "Bad message" ) \
  INFO( EBUSY,          xxx,      "Resource busy" ) \
  INFO( ECANCELED,      xxx,      "Operation canceled" ) \
  INFO( ECHILD,         xxx,      "No child processes" ) \
  INFO( EDEADLK,        xxx,      "Resource deadlock avoided" ) \
  INFO( EDOM,           xxx,      "Domain error" ) \
  INFO( EEXIST,         xxx,      "File exists" ) \
  INFO( EFAULT,         xxx,      "Bad address" ) \
  INFO( EFBIG,          xxx,      "File too large" ) \
  INFO( EINPROGRESS,    xxx,      "Operation in progress" ) \
  INFO( EINTR,          xxx,      "Interrupted function call" ) \
  INFO( EINVAL,         xxx,      "Invalid argument" ) \
  INFO( EISDIR,         xxx,      "Is a directory" ) \
  INFO( EMFILE,         xxx,      "Too many open files" ) \
  INFO( EMLINK,         xxx,      "Too many links" ) \
  INFO( EMSGSIZE,       xxx,      "Inappropriate message buffer length" ) \
  INFO( ENAMETOOLONG,   xxx,      "Filename too long" ) \
  INFO( ENFILE,         xxx,      "Too many open files in system" ) \
  INFO( ENODEV,         xxx,      "No such device" ) \
  INFO( ENOEXEC,        xxx,      "Exec format error" ) \
  INFO( ENOLCK,         xxx,      "No locks available" ) \
  INFO( ENOMEM,         xxx,      "Not enough space" ) \
  INFO( ENOSPC,         xxx,      "No space left on device" ) \
  INFO( ENOSYS,         xxx,      "Function not implemented" ) \
  INFO( ENOTDIR,        xxx,      "Not a directory" ) \
  INFO( ENOTEMPTY,      xxx,      "Directory not empty" ) \
  INFO( ENOTSUP,        xxx,      "Not supported" ) \
  INFO( ENOTTY,         xxx,      "Inappropriate I/O control operation" ) \
  INFO( ENXIO,          xxx,      "No such device or address" ) \
  INFO( EPERM,          xxx,      "Operation not permitted" ) \
  INFO( EPIPE,          xxx,      "Broken pipe" ) \
  INFO( ERANGE,         xxx,      "Result too large" ) \
  INFO( EROFS,          xxx,      "Read-only file system" ) \
  INFO( ESPIPE,         xxx,      "Invalid seek" ) \
  INFO( ESRCH,          xxx,      "No such process" ) \
  INFO( EXDEV,          xxx,      "Improper link" ) \

#endif

