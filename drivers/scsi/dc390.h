/***********************************************************************
 *	FILE NAME : DC390.H					       *
 *	     BY   : C.L. Huang					       *
 *	Description: Device Driver for Tekram DC-390(T) PCI SCSI       *
 *		     Bus Master Host Adapter			       *
 ***********************************************************************/
/* $Id: dc390.h,v 2.43.2.22 2000/12/20 00:39:36 garloff Exp $ */

/*
 * DC390/AMD 53C974 driver, header file
 */

#ifndef DC390_H
#define DC390_H

#include <linux/version.h>

#define DC390_BANNER "Tekram DC390/AM53C974"
#define DC390_VERSION "2.1d 2004-05-27"

/* We don't have eh_abort_handler, eh_device_reset_handler, 
 * eh_bus_reset_handler, eh_host_reset_handler yet! 
 * So long: Use old exception handling :-( */
#define OLD_EH

#if LINUX_VERSION_CODE < KERNEL_VERSION (2,1,70) || defined (OLD_EH)
# define NEW_EH
#else
# define NEW_EH use_new_eh_code: 1,
# define USE_NEW_EH
#endif
#endif /* DC390_H */
