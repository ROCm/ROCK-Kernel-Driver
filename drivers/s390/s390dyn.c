/*
 *  arch/s390/kernel/s390dyn.c
 *   S/390 dynamic device attachment
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 */

#include <linux/init.h>
#include <linux/smp_lock.h>

#include <asm/irq.h>
#include <asm/s390io.h>
#include <asm/s390dyn.h>

static devreg_t   *devreg_anchor = NULL;
static spinlock_t  dyn_lock      = SPIN_LOCK_UNLOCKED;


int s390_device_register( devreg_t *drinfo )
{
	unsigned long  flags;
	int            pdevflag,drflag;

	int            ret     = 0;
	devreg_t      *pdevreg = devreg_anchor;

	if ( drinfo == NULL )
		return( -EINVAL );

	drflag = drinfo->flag;

	if ( (drflag & DEVREG_TYPE_DEVNO) == (drflag & DEVREG_TYPE_DEVCHARS) )
		return( -EINVAL ); 

	spin_lock_irqsave( &dyn_lock, flags ); 	

	while ( (pdevreg != NULL) && (ret ==0) )
	{
		if ( pdevreg == drinfo )
		{
			ret = -EINVAL;
		}
		else
		{
			pdevflag = pdevreg->flag;

			/*
			 * we don't allow multiple drivers to register 
			 *  for the same device number 
			 */
			if (    (    (pdevflag & DEVREG_TYPE_DEVNO)
			          && (pdevreg->ci.devno                ) )
			     && (    (drflag & DEVREG_TYPE_DEVNO )
			          && (drinfo->ci.devno                 ) ) )
			{
				ret = -EBUSY;
			}           	
			else if ( drflag == (   DEVREG_TYPE_DEVCHARS 
			                      | DEVREG_EXACT_MATCH   )) 
			{
				if ( !memcmp(&drinfo->ci.hc,
				             &pdevreg->ci.hc,
				             sizeof(devreg_hc_t)))
					ret=-EBUSY;
			} /* endif */

		} /* endif */

		pdevreg = pdevreg->next;
	
	} /* endwhile */          	

	/*
	 * only enqueue if no collision was found ...	
	 */
	if(ret==0)	
	{
		drinfo->next = devreg_anchor;
		drinfo->prev = NULL;

		if ( devreg_anchor != NULL )
		{
			devreg_anchor->prev = drinfo;       	

		} /* endif */
		
		devreg_anchor=drinfo;

	} /* endif */

	spin_unlock_irqrestore( &dyn_lock, flags ); 	
 	
	return( ret);
}


int s390_device_unregister( devreg_t *dreg )
{
	unsigned long  flags;

	int            ret     = -EINVAL;
	devreg_t      *pdevreg = devreg_anchor;

	if ( dreg == NULL )
		return( -EINVAL );

	spin_lock_irqsave( &dyn_lock, flags ); 	

	while (    (pdevreg != NULL )
	        && (    ret != 0    ) )
	{
		if ( pdevreg == dreg )
		{
			devreg_t *dprev = pdevreg->prev;
			devreg_t *dnext = pdevreg->next;

			if ( (dprev != NULL) && (dnext != NULL) )
			{
				dnext->prev = dprev;
				dprev->next = dnext;
			}
			if ( (dprev != NULL) && (dnext == NULL) )
			{
				dprev->next = NULL;
			}
			if ( (dprev == NULL) && (dnext != NULL) )
			{
				dnext->prev = NULL;

			} /* else */

			ret = 0;
		}
		else
		{
			pdevreg = pdevreg->next;

		} /* endif */

	} /* endwhile */          	

	spin_unlock_irqrestore( &dyn_lock, flags ); 	
 	
	return( ret);
}


devreg_t * s390_search_devreg( ioinfo_t *ioinfo )
{
	unsigned long  flags;
	devreg_hc_t    match;
	devreg_t *pdevreg = devreg_anchor;

	if ( ioinfo == NULL )
		return( NULL );

	spin_lock_irqsave( &dyn_lock, flags ); 	

	while ( pdevreg != NULL )
	{
		int flag = pdevreg->flag;

		if (    (flag & DEVREG_TYPE_DEVNO )
		     && (ioinfo->ui.flags.dval == 1        )
		     && (ioinfo->devno == pdevreg->ci.devno) )
		{
			break;
		}           	
		else if (flag & DEVREG_TYPE_DEVCHARS )
		{
			if ( flag & DEVREG_EXACT_MATCH ) 
			{
				if ( !memcmp( &pdevreg->ci.hc, 
				              &ioinfo->senseid.cu_type,
				              sizeof(devreg_hc_t)))
					break; 
			} 
			else
			{			
				memcpy( &match, &ioinfo->senseid.cu_type, 
				        sizeof(match));

				if( flag & DEVREG_NO_CU_INFO )
				{
					match.ctype = pdevreg->ci.hc.ctype;
					match.cmode = pdevreg->ci.hc.cmode;
				}
				if( flag & DEVREG_NO_DEV_INFO )
				{
					match.dtype = pdevreg->ci.hc.dtype;
					match.dmode = pdevreg->ci.hc.dmode;
				}
				if ( flag & DEVREG_MATCH_CU_TYPE )
					match.cmode = pdevreg->ci.hc.cmode;
				if( flag & DEVREG_MATCH_DEV_TYPE)
					match.dmode = pdevreg->ci.hc.dmode;
				if ( !memcmp( &pdevreg->ci.hc,
				              &match, sizeof(match)))
					break;
			} /* endif */
		} /* endif */

		pdevreg = pdevreg->next;

	} /* endwhile */          	

	spin_unlock_irqrestore( &dyn_lock, flags ); 	
 	
	return( pdevreg);
}

