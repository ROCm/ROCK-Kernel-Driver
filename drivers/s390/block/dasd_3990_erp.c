/* 
 * File...........: linux/drivers/s390/block/dasd_3990_erp.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Horst  Hummel    <Horst.Hummel@de.ibm.com> 
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */

#include <asm/ccwcache.h>
#include <asm/dasd.h>

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#define PRINTK_HEADER "dasd_erp(3990)"
#endif /* PRINTK_HEADER */

/*
 * DASD_3990_ERP_EXAMINE_32 
 *
 * DESCRIPTION
 *   Checks only for fatal/no/recoverable error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 * RETURN VALUES
 *   dasd_era_none      no error 
 *   dasd_era_fatal     for all fatal (unrecoverable errors)
 *   dasd_era_recover   for recoverable others.
 */
dasd_era_t
dasd_3990_erp_examine_32 (char *sense)
{

	switch (sense[25]) {
	case 0x00:
		return dasd_era_none;
	case 0x01:
		return dasd_era_fatal;
	default:
		return dasd_era_recover;
	}

}				/* end dasd_3990_erp_examine_32 */

/*
 * DASD_3990_ERP_EXAMINE_24 
 *
 * DESCRIPTION
 *   Checks only for fatal (unrecoverable) error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 *   Each bit configuration leading to an action code 2 (Exit with
 *   programming error or unusual condition indication)
 *   and 10 (disabled interface) are handled as fatal error´s.
 * 
 *   All other configurations are handled as recoverable errors.
 *
 * RETURN VALUES
 *   dasd_era_fatal     for all fatal (unrecoverable errors)
 *   dasd_era_recover   for all others.
 */
dasd_era_t
dasd_3990_erp_examine_24 (char *sense)
{

	/* check for 'Command Recejct' whithout environmental data present  */
	if (sense[0] & 0x80) {
                if (sense[2] &0x10){
                        return dasd_era_recover;
                } else {
                        return dasd_era_fatal;
                }
        }
	
        /* check for 'Invalid Track Format' whithout environmental data present  */
	if (sense[1] & 0x40) {
                if (sense[2] &0x10){
                        return dasd_era_recover;
                } else {
                        return dasd_era_fatal;
                }
	}
	/* check for 'No Record Found'                                */
	if (sense[1] & 0x08) {
		return dasd_era_fatal;
	}
	/* return recoverable for all others                          */
	return dasd_era_recover;

}				/* END dasd_3990_erp_examine_24 */

/*
 * DASD_3990_ERP_EXAMINE 
 *
 * DESCRIPTION
 *   Checks only for fatal/no/recover error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 *   The logic is based on the 'IBM 3990 Storage Control  Reference' manual
 *   'Chapter 7. Error Recovery Procedures'.
 *
 * RETURN VALUES
 *   dasd_era_none      no error 
 *   dasd_era_fatal     for all fatal (unrecoverable errors)
 *   dasd_era_recover   for all others.
 */
dasd_era_t
dasd_3990_erp_examine (ccw_req_t * cqr, devstat_t * stat)
{

	char *sense = stat->ii.sense.data;

	/* check for successful execution first */
	if (stat->cstat == 0x00 &&
	    stat->dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END))
		return dasd_era_none;

	/* distinguish between 24 and 32 byte sense data */
	if (sense[27] & 0x80) {

		/* examine the 32 byte sense data */
		return dasd_3990_erp_examine_32 (sense);

	} else {

		/* examine the 24 byte sense data */
		return dasd_3990_erp_examine_24 (sense);

	}			/* end distinguish between 24 and 32 byte sense data */

}				/* END dasd_3990_erp_examine */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
