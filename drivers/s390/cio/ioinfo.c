/*
 *  drivers/s390/cio/ioinfo.c
 *   S/390 common I/O routines -- the ioinfo structure
 *   $Revision: 1.4 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *                            IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *               Cornelia Huck (cohuck@de.ibm.com) 
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *    ChangeLog: 11/04/2002 Arnd Bergmann Split s390io.c into multiple files,
 *					  see s390io.c for complete list of
 * 					  changes.
 */

#include <linux/module.h>
#include <linux/config.h>

#include <asm/irq.h>
#include <asm/debug.h>

#include "ioinfo.h"

unsigned int highest_subchannel;

ioinfo_t *ioinfo_head;
ioinfo_t *ioinfo_tail;
ioinfo_t *ioinfo[__MAX_SUBCHANNELS]
#if 1
= { [0 ... (__MAX_SUBCHANNELS - 1)] = INVALID_STORAGE_AREA} /*FIXME: define this to 0 */
#endif
;

static inline int
get_next_available_irq (ioinfo_t * pi)
{
	int ret_val = -ENODEV;

	while (pi != NULL) {
		if ((!pi->st) && (pi->ui.flags.oper)) {
			ret_val = pi->irq;
			break;
		} else {
			pi = pi->next;
		}
	}

	return ret_val;
}

int
get_irq_first (void)
{
	int ret_irq;

	if (ioinfo_head) {
		if ((ioinfo_head->ui.flags.oper) && 
		    (!ioinfo_head->st)) {
			ret_irq = ioinfo_head->irq;
		} else if (ioinfo_head->next) {
			ret_irq = get_next_available_irq (ioinfo_head->next);

		} else {
			ret_irq = -ENODEV;

		}
	} else {
		ret_irq = -ENODEV;

	}

	return ret_irq;
}

int
get_irq_next (int irq)
{
	int ret_irq;

	if (ioinfo[irq] != INVALID_STORAGE_AREA) {
		if (ioinfo[irq]->next) {
			if ((ioinfo[irq]->next->ui.flags.oper) &&
			    (!ioinfo[irq]->next->st)) {
				ret_irq = ioinfo[irq]->next->irq;
			} else {
				ret_irq =
				    get_next_available_irq (ioinfo[irq]->next);

			}
		} else {
			ret_irq = -ENODEV;

		}
	} else {
		ret_irq = -EINVAL;

	}

	return ret_irq;
}

int
get_dev_info_by_irq (int irq, s390_dev_info_t * pdi)
{
	if (irq > highest_subchannel || irq < 0) 
		return -ENODEV; 
	if (ioinfo[irq] == INVALID_STORAGE_AREA) 
		return -ENODEV; 
        if (ioinfo[irq]->st) 
                return -ENODEV; 

	if (pdi == NULL)
		return -EINVAL;

	pdi->devno = ioinfo[irq]->schib.pmcw.dev;
	pdi->irq = irq;

	if (ioinfo[irq]->ui.flags.oper && !ioinfo[irq]->ui.flags.unknown) {
		pdi->status = 0;
		memcpy (&(pdi->sid_data),
			&ioinfo[irq]->senseid, sizeof (senseid_t));
	} else if (ioinfo[irq]->ui.flags.unknown) {
		pdi->status = DEVSTAT_UNKNOWN_DEV;
		memset (&(pdi->sid_data), '\0', sizeof (senseid_t));
		pdi->sid_data.cu_type = 0xFFFF;

	} else {
		pdi->status = DEVSTAT_NOT_OPER;
		memset (&(pdi->sid_data), '\0', sizeof (senseid_t));
		pdi->sid_data.cu_type = 0xFFFF;

	}

	if (ioinfo[irq]->ui.flags.ready)
		pdi->status |= DEVSTAT_DEVICE_OWNED;

	return 0;
}

int
get_dev_info_by_devno (__u16 devno, s390_dev_info_t * pdi)
{
	int i;
	int rc = -ENODEV;

	if (devno > 0x0000ffff)
		return -ENODEV;
	if (pdi == NULL)
		return -EINVAL;

	for (i = 0; i <= highest_subchannel; i++) {

		if ((ioinfo[i] != INVALID_STORAGE_AREA) &&
		    (!ioinfo[i]->st) &&
		    (ioinfo[i]->schib.pmcw.dev == devno)) {

			pdi->irq = i;
			pdi->devno = devno;

			if (ioinfo[i]->ui.flags.oper
			    && !ioinfo[i]->ui.flags.unknown) {
				pdi->status = 0;
				memcpy (&(pdi->sid_data),
					&ioinfo[i]->senseid,
					sizeof (senseid_t));
			} else if (ioinfo[i]->ui.flags.unknown) {
				pdi->status = DEVSTAT_UNKNOWN_DEV;

				memset (&(pdi->sid_data),
					'\0', sizeof (senseid_t));

				pdi->sid_data.cu_type = 0xFFFF;
			} else {
				pdi->status = DEVSTAT_NOT_OPER;

				memset (&(pdi->sid_data),
					'\0', sizeof (senseid_t));

				pdi->sid_data.cu_type = 0xFFFF;

			}

			if (ioinfo[i]->ui.flags.ready)
				pdi->status |= DEVSTAT_DEVICE_OWNED;

			rc = 0;	/* found */
			break;

		}
	}

	return rc;

}

int
get_irq_by_devno (__u16 devno)
{
	int i;
	int rc = -1;

	if (devno <= 0x0000ffff) {
		for (i = 0; i <= highest_subchannel; i++) {
			if ((ioinfo[i] != INVALID_STORAGE_AREA)
			    && (!ioinfo[i]->st)
			    && (ioinfo[i]->schib.pmcw.dev == devno)
			    && (ioinfo[i]->schib.pmcw.dnv == 1)) {
				rc = i;
				break;
			}
		}
	}

	return rc;
}

unsigned int
get_devno_by_irq (int irq)
{
	if ((irq > highest_subchannel)
	    || (irq < 0)
	    || (ioinfo[irq] == INVALID_STORAGE_AREA)) {
		return -1;

	}

	if (ioinfo[irq]->st) 
		return -1;

	/*
	 * we don't need to check for the device be operational
	 *  as the initial STSCH will always present the device
	 *  number defined by the IOCDS regardless of the device
	 *  existing or not. However, there could be subchannels
	 *  defined who's device number isn't valid ...
	 */
	if (ioinfo[irq]->schib.pmcw.dnv)
		return ioinfo[irq]->schib.pmcw.dev;
	else
		return -1;
}

schib_t *
s390_get_schib (int irq)
{
	if ((irq > highest_subchannel) || (irq < 0))
		return NULL;
	if (ioinfo[irq] == INVALID_STORAGE_AREA)
		return NULL;
	if (ioinfo[irq]->st)
		return NULL;
	return &ioinfo[irq]->schib;
}

/* these are used only by the tape driver to store per subchannel 
 * private data */
int
s390_set_private_data(int irq, void *data)
{
	if (irq > highest_subchannel || irq < 0)
		return -ENODEV;
	if (ioinfo[irq] == INVALID_STORAGE_AREA)
		return -ENODEV;
        if (ioinfo[irq]->st)
                return -ENODEV;
	ioinfo[irq]->private_data = data;
	return 0;
}

void *
s390_get_private_data(int irq)
{
	if ((irq > highest_subchannel) || (irq < 0))
		return NULL;
	if (ioinfo[irq] == INVALID_STORAGE_AREA)
		return NULL;
	if (ioinfo[irq]->st)
		return NULL;
	return ioinfo[irq]->private_data;
}


EXPORT_SYMBOL (ioinfo);
EXPORT_SYMBOL (get_dev_info_by_irq);
EXPORT_SYMBOL (get_dev_info_by_devno);
EXPORT_SYMBOL (get_irq_by_devno);
EXPORT_SYMBOL (get_devno_by_irq);
EXPORT_SYMBOL (get_irq_first);
EXPORT_SYMBOL (get_irq_next);
EXPORT_SYMBOL (s390_get_schib);
EXPORT_SYMBOL (s390_set_private_data);
EXPORT_SYMBOL (s390_get_private_data);
