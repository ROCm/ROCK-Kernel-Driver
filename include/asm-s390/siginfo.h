/*
 *  include/asm-s390/siginfo.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/siginfo.h"
 */

#ifndef _S390_SIGINFO_H
#define _S390_SIGINFO_H

#define HAVE_ARCH_SI_CODES
#ifdef __s390x__
#define __ARCH_SI_PREAMBLE_SIZE (4 * sizeof(int))
#endif

#ifdef CONFIG_ARCH_S390X
#define SIGEV_PAD_SIZE ((SIGEV_MAX_SIZE/sizeof(int)) - 4)
#else
#define SIGEV_PAD_SIZE ((SIGEV_MAX_SIZE/sizeof(int)) - 3)
#endif

#include <asm-generic/siginfo.h>

/*
 * SIGILL si_codes
 */
#define ILL_ILLOPC	(__SI_FAULT|1)	/* illegal opcode */
#define ILL_ILLOPN	(__SI_FAULT|2)	/* illegal operand */
#define ILL_ILLADR	(__SI_FAULT|3)	/* illegal addressing mode */
#define ILL_ILLTRP	(__SI_FAULT|4)	/* illegal trap */
#define ILL_PRVOPC	(__SI_FAULT|5)	/* privileged opcode */
#define ILL_PRVREG	(__SI_FAULT|6)	/* privileged register */
#define ILL_COPROC	(__SI_FAULT|7)	/* coprocessor error */
#define ILL_BADSTK	(__SI_FAULT|8)	/* internal stack error */
#define NSIGILL		8

/*
 * SIGFPE si_codes
 */
#define FPE_INTDIV	(__SI_FAULT|1)	/* integer divide by zero */
#define FPE_INTOVF	(__SI_FAULT|2)	/* integer overflow */
#define FPE_FLTDIV	(__SI_FAULT|3)	/* floating point divide by zero */
#define FPE_FLTOVF	(__SI_FAULT|4)	/* floating point overflow */
#define FPE_FLTUND	(__SI_FAULT|5)	/* floating point underflow */
#define FPE_FLTRES	(__SI_FAULT|6)	/* floating point inexact result */
#define FPE_FLTINV	(__SI_FAULT|7)	/* floating point invalid operation */
#define FPE_FLTSUB	(__SI_FAULT|8)	/* subscript out of range */
#define NSIGFPE		8

/*
 * SIGSEGV si_codes
 */
#define SEGV_MAPERR	(__SI_FAULT|1)	/* address not mapped to object */
#define SEGV_ACCERR	(__SI_FAULT|2)	/* invalid permissions for mapped object */
#define NSIGSEGV	2

/*
 * SIGBUS si_codes
 */
#define BUS_ADRALN	(__SI_FAULT|1)	/* invalid address alignment */
#define BUS_ADRERR	(__SI_FAULT|2)	/* non-existant physical address */
#define BUS_OBJERR	(__SI_FAULT|3)	/* object specific hardware error */
#define NSIGBUS		3

/*
 * SIGTRAP si_codes
 */
#define TRAP_BRKPT	(__SI_FAULT|1)	/* process breakpoint */
#define TRAP_TRACE	(__SI_FAULT|2)	/* process trace trap */
#define NSIGTRAP	2

/*
 * SIGCHLD si_codes
 */
#define CLD_EXITED	(__SI_CHLD|1)	/* child has exited */
#define CLD_KILLED	(__SI_CHLD|2)	/* child was killed */
#define CLD_DUMPED	(__SI_CHLD|3)	/* child terminated abnormally */
#define CLD_TRAPPED	(__SI_CHLD|4)	/* traced child has trapped */
#define CLD_STOPPED	(__SI_CHLD|5)	/* child has stopped */
#define CLD_CONTINUED	(__SI_CHLD|6)	/* stopped child has continued */
#define NSIGCHLD	6

/*
 * SIGPOLL si_codes
 */
#define POLL_IN		(__SI_POLL|1)	/* data input available */
#define POLL_OUT	(__SI_POLL|2)	/* output buffers available */
#define POLL_MSG	(__SI_POLL|3)	/* input message available */
#define POLL_ERR	(__SI_POLL|4)	/* i/o error */
#define POLL_PRI	(__SI_POLL|5)	/* high priority input available */
#define POLL_HUP	(__SI_POLL|6)	/* device disconnected */
#define NSIGPOLL	6

#endif
