/*
 *  arch/s390/kernel/s390mach.c
 *   S/390 machine check handler,
 *            currently only channel-reports are supported
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 */

#include <linux/init.h>

#include <asm/irq.h>
#include <asm/lowcore.h>
#include <asm/semaphore.h>
#include <asm/s390io.h>
#include <asm/s390dyn.h>
#include <asm/s390mach.h>

#define S390_MACHCHK_DEBUG

static mchchk_queue_element_t *mchchk_queue_head = NULL;
static mchchk_queue_element_t *mchchk_queue_tail = NULL;
static mchchk_queue_element_t *mchchk_queue_free = NULL;
static spinlock_t              mchchk_queue_lock;
static struct semaphore        s_sem[2];

//
// initialize machine check handling
//
void s390_init_machine_check( void )
{
	init_MUTEX_LOCKED( &s_sem[0] );
	init_MUTEX_LOCKED( &s_sem[1] );

#if 0
	//
	// fix me ! initialize a machine check queue with 100 elements
	//
#ifdef S390_MACHCHK_DEBUG
	printk( "init_mach : starting kernel thread\n");
#endif	

	kernel_thread( s390_machine_check_handler, s_sem, 0);

	//
	// wait for the machine check handler to be ready
	//
#ifdef S390_MACHCHK_DEBUG
	printk( "init_mach : waiting for kernel thread\n");
#endif	

	down( &sem[0]);

#ifdef S390_MACHCHK_DEBUG
	printk( "init_mach : kernel thread ready\n");
#endif	

	//
	// fix me ! we have to initialize CR14 to allow for CRW pending
	//           conditions

	//
	// fix me ! enable machine checks in the PSW
	//
#endif
	return;
}

//
// machine check pre-processor
//
void __init s390_do_machine_check( void )
{
   // fix me ! we have to check for machine check and
   //          post the handler eventually

	return;
}

//
// machine check handler
//
static void __init s390_machine_check_handler( struct semaphore *sem )
{
#ifdef S390_MACHCHK_DEBUG
	printk( "mach_handler : kernel thread up\n");
#endif	

	up( &sem[0] );

#ifdef S390_MACHCHK_DEBUG
	printk( "mach_handler : kernel thread ready\n");
#endif	

	do {

#ifdef S390_MACHCHK_DEBUG
	printk( "mach_handler : waiting for wakeup\n");
#endif	

		down_interruptible( &sem[1] );
#ifdef S390_MACHCHK_DEBUG
	printk( "mach_handler : wakeup\n");
#endif	

		break;	// fix me ! unconditional surrender ...
  	
		// fix me ! check for machine checks and
		//          call do_crw_pending() eventually
	
	} while (1);

	return;
}

mchchk_queue_element_t *s390_get_mchchk( void )
{
	unsigned long           flags;
	mchchk_queue_element_t *qe;

	spin_lock_irqsave( &mchchk_queue_lock, flags );

	// fix me ! dequeue first element if available
	qe = NULL;

	spin_unlock_irqrestore( &mchchk_queue_lock, flags );

	return qe;
}

void s390_free_mchchk( mchchk_queue_element_t *mchchk )
{
	unsigned long flags;

	if ( mchchk != NULL)
	{
		spin_lock_irqsave( &mchchk_queue_lock, flags );
		
		mchchk->next      = mchchk_queue_free;

		if ( mchchk_queue_free != NULL )
		{
			mchchk_queue_free->prev = mchchk;

		} /* endif */

		mchchk->prev      = NULL;
		mchchk_queue_free = mchchk;

		spin_unlock_irqrestore( &mchchk_queue_lock, flags );

	} /* endif */

	return;
}

