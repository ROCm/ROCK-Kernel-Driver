/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_DRIVER_H
#define _ASM_SN_DRIVER_H

/*
** Interface for device driver handle management.
**
** These functions are mostly for use by the loadable driver code, and
** for use by I/O bus infrastructure code.
*/

typedef struct device_driver_s *device_driver_t;
#define DEVICE_DRIVER_NONE (device_driver_t)NULL

/* == Driver thread priority support == */
typedef int ilvl_t;
/* default driver thread priority level */
#define DRIVER_THREAD_PRI_DEFAULT	(ilvl_t)230
/* invalid driver thread priority level */
#define DRIVER_THREAD_PRI_INVALID	(ilvl_t)-1

/* Associate a thread priority with a driver */
extern int device_driver_thread_pri_set(device_driver_t driver,
					ilvl_t pri);

/* Get the thread priority associated with the driver */
extern ilvl_t device_driver_thread_pri_get(device_driver_t driver);

/* Get the thread priority for a driver from the sysgen paramters */
extern ilvl_t device_driver_sysgen_thread_pri_get(char *driver_prefix);

/* Initialize device driver functions. */
extern void device_driver_init(void);


/* Allocate a driver handle */
extern device_driver_t device_driver_alloc(char *prefix);


/* Free a driver handle */
extern void device_driver_free(device_driver_t driver);


/* Given a device driver prefix, return a handle to the driver. */
extern device_driver_t device_driver_get(char *prefix);

/* Given a device, return a handle to the driver. */
extern device_driver_t device_driver_getbydev(devfs_handle_t device);

struct cdevsw;
struct bdevsw;

/* Associate a driver with bdevsw/cdevsw pointers. */
extern int
device_driver_devsw_put(device_driver_t driver,
			struct bdevsw *my_bdevsw,
			struct cdevsw *my_cdevsw);


/* Given a driver, return the corresponding bdevsw and cdevsw pointers. */
extern void
device_driver_devsw_get(	device_driver_t driver, 
				struct bdevsw **bdevswp,
				struct cdevsw **cdevswp);

/* Given a driver, return its name (prefix). */
extern void device_driver_name_get(device_driver_t driver, char *buffer, int length);


/* 
 * A descriptor for every static device driver in the system.
 * lboot creates a table of these and places in in master.c.
 * device_driver_init runs through this table during initialization
 * in order to "register" every static device driver.
 */
typedef struct static_device_driver_desc_s {
	char 		*sdd_prefix;
	struct bdevsw 	*sdd_bdevsw;
	struct cdevsw 	*sdd_cdevsw;
} *static_device_driver_desc_t;

extern struct static_device_driver_desc_s static_device_driver_table[];
extern int static_devsw_count;


/*====== administration support ========== */
/* structure of each entry in the table created by lboot for
 * device / driver administration
*/
typedef struct dev_admin_info_s {
	char	*dai_name;		/* name of the device or driver
					 * prefix 
					 */
	char	*dai_param_name;	/* device or driver parameter name */
	char	*dai_param_val;		/* value of the parameter */
} dev_admin_info_t;


/* Update all the administrative hints associated with the device */
extern void 	device_admin_info_update(devfs_handle_t	dev_vhdl);

/* Update all the administrative hints associated with the device driver */
extern void	device_driver_admin_info_update(device_driver_t	driver);

/* Get a particular administrative hint associated with a device */
extern char 	*device_admin_info_get(devfs_handle_t	dev_vhdl,
				       char		*info_lbl);

/* Associate a particular administrative hint for a device */
extern int	device_admin_info_set(devfs_handle_t	dev_vhdl,
				      char		*info_lbl,
				      char		*info_val);

/* Get a particular administrative hint associated with a device driver*/
extern char 	*device_driver_admin_info_get(char	*driver_prefix,	
					      char	*info_name);

/* Associate a particular administrative hint for a device driver*/
extern int	device_driver_admin_info_set(char	*driver_prefix,
					     char	*driver_info_lbl,
					     char	*driver_info_val);

/* Initialize the extended device administrative hint table */
extern void	device_admin_table_init(void);

/* Add a hint corresponding to a device to the extended device administrative
 * hint table.
 */
extern void	device_admin_table_update(char *dev_name,
					  char *param_name,
					  char *param_val);

/* Initialize the extended device driver administrative hint table */
extern void	device_driver_admin_table_init(void);

/* Add a hint corresponding to a device to the extended device driver 
 * administrative hint table.
 */
extern void	device_driver_admin_table_update(char *drv_prefix,
						 char *param_name,
						 char *param_val);	
#endif /* _ASM_SN_DRIVER_H */
