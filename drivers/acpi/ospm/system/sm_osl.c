/******************************************************************************
 *
 * Module Name: sm_osl.c
 *   $Revision: 16 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/pm.h>
#include <asm/uaccess.h>
#include <linux/acpi.h>
#include <asm/io.h>
#include <linux/mc146818rtc.h>
#include <linux/delay.h>

#include <acpi.h>
#include "sm.h"


MODULE_AUTHOR("Andrew Grover");
MODULE_DESCRIPTION("ACPI Component Architecture (CA) - ACPI System Driver");
MODULE_LICENSE("GPL");


#define SM_PROC_INFO		"info"
#define SM_PROC_DSDT		"dsdt"

extern struct proc_dir_entry	*bm_proc_root;
struct proc_dir_entry		*sm_proc_root = NULL;
static void 			(*sm_pm_power_off)(void) = NULL;

static ssize_t sm_osl_read_dsdt(struct file *, char *, size_t, loff_t *);

static struct file_operations proc_dsdt_operations = {
	read:		sm_osl_read_dsdt,
};

static acpi_status sm_osl_suspend(u32 state);

struct proc_dir_entry *bm_proc_sleep;
struct proc_dir_entry *bm_proc_alarm;
struct proc_dir_entry *bm_proc_gpe;

static int
sm_osl_proc_read_sleep (
        char                    *page,
        char                    **start,
        off_t                   off,
        int                     count,
        int                     *eof,
        void                    *context)
{
	SM_CONTEXT    *system = (SM_CONTEXT*) context;
	char          *str = page;
	int           len;
	int           i;

	if (!system)
               goto end;

	if (off != 0)
               goto end;

	for (i = 0; i <= ACPI_S5; i++) {
		if (system->states[i])
			str += sprintf(str,"S%d ", i);
	}

	str += sprintf(str, "\n");

end:

	len = (str - page);
	if (len < (off + count))
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len > count)
		len = count;

	if (len < 0)
		len = 0;

	return (len);
}

int sm_osl_proc_write_sleep (struct file *file,
			     const char *buffer,
			     unsigned long count,
			     void *data)
{
	SM_CONTEXT    *system = (SM_CONTEXT*) data;
	char          str[10];
	char          *strend;
	unsigned long value;
	
	if (count > (sizeof(str) - 1))
		return -EINVAL;
	
	if (copy_from_user(str,buffer,count))
		return -EFAULT;
	
	str[count] = '\0';
	
	value = simple_strtoul(str,&strend,0);
	if (str == strend)
		return -EINVAL;
	
	if (value == 0 || value >= ACPI_S5)
		return -EINVAL;
	
	/*
	 * make sure that the sleep state is supported
	 */
	if (system->states[value] != TRUE)
		return -EINVAL;
	
	sm_osl_suspend(value);
	
	return (count);
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_proc_read_info
 *
 ****************************************************************************/

static int
sm_osl_proc_read_info (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*context)
{
	acpi_status		status = AE_OK;
	SM_CONTEXT		*system = NULL;
	char			*p = page;
	int			len;
	acpi_system_info	system_info;
	acpi_buffer		buffer;
	u32			i = 0;

	if (!context) {
		goto end;
	}

	system = (SM_CONTEXT*) context;

	/* don't get status more than once for a single proc read */
	if (off != 0) {
		goto end;
	}

	/*
	 * Get ACPI CA Information.
	 */
	buffer.length  = sizeof(system_info);
	buffer.pointer = &system_info;

	status = acpi_get_system_info(&buffer);
	if (ACPI_FAILURE(status)) {
		p += sprintf(p, "ACPI-CA Version:         unknown\n");
	}
	else {
		p += sprintf(p, "ACPI-CA Version:         %x\n",
			system_info.acpi_ca_version);
	}

	p += sprintf(p, "Sx States Supported:     ");
	for (i=0; i<SM_MAX_SYSTEM_STATES; i++) {
		if (system->states[i]) {
			p += sprintf(p, "S%d ", i);
		}
	}
	p += sprintf(p, "\n");

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return(len);
}

/****************************************************************************
 *
 * FUNCTION:	sm_osl_read_dsdt
 *
 ****************************************************************************/

static ssize_t
sm_osl_read_dsdt(
	struct file		*file,
	char			*buf,
	size_t			count,
	loff_t			*ppos)
{
	acpi_buffer		acpi_buf;
	void			*data;
	size_t			size = 0;

	acpi_buf.length = 0;
	acpi_buf.pointer = NULL;


	/* determine what buffer size we will need */
	if (acpi_get_table(ACPI_TABLE_DSDT, 1, &acpi_buf) != AE_BUFFER_OVERFLOW) {
		return 0;
	}

	acpi_buf.pointer = kmalloc(acpi_buf.length, GFP_KERNEL);
	if (!acpi_buf.pointer) {
		return -ENOMEM;
	}

	/* get the table for real */
	if (!ACPI_SUCCESS(acpi_get_table(ACPI_TABLE_DSDT, 1, &acpi_buf))) {
		kfree(acpi_buf.pointer);
		return 0;
	}

	if (*ppos < acpi_buf.length) {
		data = acpi_buf.pointer + file->f_pos;
		size = acpi_buf.length - file->f_pos;
		if (size > count)
			size = count;
		if (copy_to_user(buf, data, size)) {
			kfree(acpi_buf.pointer);
			return -EFAULT;
		}
	}

	kfree(acpi_buf.pointer);

	*ppos += size;

	return size;
}

static int
sm_osl_proc_read_alarm (
	char                    *page,
	char                    **start,
	off_t                   off,
	int                     count,
	int                     *eof,
	void                    *context)
{
	char *str = page;
	int len;
	u32 sec,min,hr;
	u32 day,mo,yr;

	if (off != 0) goto out;

	spin_lock(&rtc_lock);
	sec = CMOS_READ(RTC_SECONDS_ALARM);
	min = CMOS_READ(RTC_MINUTES_ALARM);
	hr = CMOS_READ(RTC_HOURS_ALARM);

#if 0
	/* if I ever get an FACP with proper values, maybe I'll enable this code */
	if (acpi_gbl_FADT->day_alrm)
		day = CMOS_READ(acpi_gbl_FADT->day_alrm);
	else
		day =  CMOS_READ(RTC_DAY_OF_MONTH);
	if (acpi_gbl_FADT->mon_alrm)
		mo = CMOS_READ(acpi_gbl_FADT->mon_alrm);
	else
		mo = CMOS_READ(RTC_MONTH);;
	if (acpi_gbl_FADT->century)
		yr = CMOS_READ(acpi_gbl_FADT->century) * 100 + CMOS_READ(RTC_YEAR);
	else
		yr = CMOS_READ(RTC_YEAR);
#else
	day = CMOS_READ(RTC_DAY_OF_MONTH);
	mo = CMOS_READ(RTC_MONTH);
	yr = CMOS_READ(RTC_YEAR);
#endif
	spin_unlock(&rtc_lock);

	BCD_TO_BIN(sec);
	BCD_TO_BIN(min);
	BCD_TO_BIN(hr);
	BCD_TO_BIN(day);
	BCD_TO_BIN(mo);
	BCD_TO_BIN(yr);

	str += sprintf(str,"%4.4u-",yr);

	str += (mo > 12) ?
		sprintf(str,"**-") :
		sprintf(str,"%2.2u-",mo);

	str += (day > 31) ?
		sprintf(str,"** ") :
		sprintf(str,"%2.2u ",day);

	str += (hr > 23) ?
		sprintf(str,"**:") :
		sprintf(str,"%2.2u:",hr);

	str += (min > 59) ?
		sprintf(str,"**:") :
		sprintf(str,"%2.2u:",min);

	str += (sec > 59) ?
		sprintf(str,"**\n") :
		sprintf(str,"%2.2u\n",sec);

 out:
	len = str - page;

	if (len < count) *eof = 1;
	else if (len > count) len = count;

	if (len < 0) len = 0;

	*start = page;

	return len;
}

static int get_date_field(char **str, u32 *value)
{
	char *next,*strend;
	int error = -EINVAL;

	/* try to find delimeter, only to insert null;
	 *  the end of string won't have one, but is still valid
	 */
	next = strpbrk(*str,"- :");
	if (next) *next++ = '\0';

	*value = simple_strtoul(*str,&strend,10);

	/* signal success if we got a good digit */
	if (strend != *str) error = 0;

	if (next) *str = next;
	return error;
}



int sm_osl_proc_write_alarm (
	struct file *file,
	const char *buffer,
	unsigned long count,
	void *data)
{
	char buf[30];
	char *str = buf;
	u32 sec,min,hr;
	u32 day,mo,yr;
	int adjust = 0;
	unsigned char rtc_control;
	int error = -EINVAL;

	if (count > sizeof(buf) - 1) return -EINVAL;
	
	if (copy_from_user(str,buffer,count)) return -EFAULT;

	str[count] = '\0';
	/* check for time adjustment */
	if (str[0] == '+') {
		str++;
		adjust = 1;
	}

	if ((error = get_date_field(&str,&yr)))  goto out;
	if ((error = get_date_field(&str,&mo)))  goto out;
	if ((error = get_date_field(&str,&day))) goto out;
	if ((error = get_date_field(&str,&hr)))  goto out;
	if ((error = get_date_field(&str,&min))) goto out;
	if ((error = get_date_field(&str,&sec))) goto out;


	if (sec > 59) {
		min += 1;
		sec -= 60;
	}
	if (min > 59) {
		hr += 1;
		min -= 60;
	} 
	if (hr > 23) {
		day += 1;
		hr -= 24;
	}
	if (day > 31) { 
		mo += 1;
		day -= 31;
	}
	if (mo > 12) {
		yr += 1;
		mo -= 12;
	}

	spin_lock_irq(&rtc_lock);
	rtc_control = CMOS_READ(RTC_CONTROL);
	if (!(rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BIN_TO_BCD(yr);
		BIN_TO_BCD(mo);
		BIN_TO_BCD(day);
		BIN_TO_BCD(hr);
		BIN_TO_BCD(min);
		BIN_TO_BCD(sec);
	}

	if (adjust) {
		yr  += CMOS_READ(RTC_YEAR);
		mo  += CMOS_READ(RTC_MONTH);
		day += CMOS_READ(RTC_DAY_OF_MONTH);
		hr  += CMOS_READ(RTC_HOURS);
		min += CMOS_READ(RTC_MINUTES);
		sec += CMOS_READ(RTC_SECONDS);
	}
	spin_unlock_irq(&rtc_lock);

	if (!(rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BCD_TO_BIN(yr);
		BCD_TO_BIN(mo);
		BCD_TO_BIN(day);
		BCD_TO_BIN(hr);
		BCD_TO_BIN(min);
		BCD_TO_BIN(sec);
	}

	if (sec > 59) {
		min++;
		sec -= 60;
	}
	if (min > 59) {
		hr++;
		min -= 60;
	}
	if (hr > 23) {
		day++;
		hr -= 24;
	}
	if (day > 31) {
		mo++;
		day -= 31;
	}
	if (mo > 12) {
		yr++;
		mo -= 12;
	}
	if (!(rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BIN_TO_BCD(yr);
		BIN_TO_BCD(mo);
		BIN_TO_BCD(day);
		BIN_TO_BCD(hr);
		BIN_TO_BCD(min);
		BIN_TO_BCD(sec);
	}

	spin_lock_irq(&rtc_lock);
	/* write the fields the rtc knows about */
	CMOS_WRITE(hr,RTC_HOURS_ALARM);
	CMOS_WRITE(min,RTC_MINUTES_ALARM);
	CMOS_WRITE(sec,RTC_SECONDS_ALARM);

	/* If the system supports an enhanced alarm, it will have non-zero
	 * offsets into the CMOS RAM here.
	 * Which for some reason are pointing to the RTC area of memory.
	 */
#if 0
	if (acpi_gbl_FADT->day_alrm) CMOS_WRITE(day,acpi_gbl_FADT->day_alrm);
	if (acpi_gbl_FADT->mon_alrm) CMOS_WRITE(mo,acpi_gbl_FADT->mon_alrm);
	if (acpi_gbl_FADT->century)  CMOS_WRITE(yr / 100,acpi_gbl_FADT->century);
#endif
	/* enable the rtc alarm interrupt */
	if (!(rtc_control & RTC_AIE)) {
		rtc_control |= RTC_AIE;
		CMOS_WRITE(rtc_control,RTC_CONTROL);
		CMOS_READ(RTC_INTR_FLAGS);
	}

	/* unlock the lock on the rtc now that we're done with it */
	spin_unlock_irq(&rtc_lock);

	acpi_hw_register_bit_access(ACPI_WRITE,ACPI_MTX_LOCK, RTC_EN, 1);

	file->f_pos += count;

	error = 0;
 out:
	return error ? error : count;
}

static int 
sm_osl_proc_read_gpe(
	char                    *page,
	char                    **start,
	off_t                   off,
	int                     count,
	int                     *eof,
	void                    *context)
{
	char *str = page;
	int size;
	int length;
	int i;
	u32 addr,data;
	
	if (off) goto out;

	if (acpi_gbl_FADT->V1_gpe0blk) {
		length = acpi_gbl_FADT->gpe0blk_len / 2;

		str += sprintf(str,"GPE0: ");

		for (i = length; i > 0; i--) {
			addr = GPE0_EN_BLOCK | (i - 1);
			data = acpi_hw_register_read(ACPI_MTX_LOCK,addr);
			str += sprintf(str,"%2.2x ",data);
		}
		str += sprintf(str,"\n");

		str += sprintf(str,"Status: ");
		for (i = length; i > 0; i--) {
			addr = GPE0_STS_BLOCK | (i - 1);
			data = acpi_hw_register_read(ACPI_MTX_LOCK,addr);
			str += sprintf(str,"%2.2x ",data);
		}
		str += sprintf(str,"\n");
	}

	if (acpi_gbl_FADT->V1_gpe1_blk) {
		length = acpi_gbl_FADT->gpe1_blk_len / 2;


		str += sprintf(str,"GPE1: ");
		for (i = length; i > 0; i--) {
			addr = GPE1_EN_BLOCK | (i - 1);
			data = acpi_hw_register_read(ACPI_MTX_LOCK,addr);
			str += sprintf(str,"%2.2x",data);
		}
		str += sprintf(str,"\n");

		str += sprintf(str,"Status: ");
		for (i = length; i > 0; i--) {
			addr = GPE1_STS_BLOCK | (i - 1);
			data = acpi_hw_register_read(ACPI_MTX_LOCK,addr);
			str += sprintf(str,"%2.2x",data);
		}
		str += sprintf(str,"\n");
	}
 out:
	size = str - page;
	if (size < count) *eof = 1;
	else if (size > count) size = count;

	if (size < 0) size = 0;
	*start = page;

	return size;
}

static int
sm_osl_proc_write_gpe (
	struct file *file,
	const char *buffer,
	unsigned long count,
	void *data)
{
	char buf[256];
	char *str = buf;
	char *next;
	int error = -EINVAL;
	u32 addr,value = 0;

	if (count > sizeof(buf) + 1) return -EINVAL;
	
	if (copy_from_user(str,buffer,count)) return -EFAULT;

	str[count] = '\0';

	/* set addr to which block to refer to */
	if (!strncmp(str,"GPE0 ",5))      addr = GPE0_EN_BLOCK;
	else if (!strncmp(str,"GPE1 ",5)) addr = GPE1_EN_BLOCK;
	else goto out;

	str += 5;

	/* set low order bits to index of bit to set */
	addr |= simple_strtoul(str,&next,0);
	if (next == str) goto out;

	if (next) {
		str = ++next;
		value = simple_strtoul(str,&next,0);
		if (next == str) value = 1;
	}

	value = acpi_hw_register_bit_access(ACPI_WRITE,ACPI_MTX_LOCK,addr,(value ? 1 : 0));

	error = 0;
 out:
	return error ? error : count;
}


/****************************************************************************
 *
 * FUNCTION:    sm_osl_suspend
 *
 * PARAMETERS:  %state: Sleep state to enter. Assumed that caller has filtered
 *              out bogus values, so it's one of S1, S2, S3 or S4
 *
 * RETURN:      ACPI_STATUS, whether or not we successfully entered and
 *              exited sleep.
 *
 * DESCRIPTION:
 * This function is the meat of the sleep routine, as far as the ACPI-CA is
 * concerned.
 *
 * See Chapter 9 of the ACPI 2.0 spec for details concerning the methodology here.
 *
 * It will do the following things:
 * - Call arch-specific routines to save the processor and kernel state
 * - Call acpi_enter_sleep_state to actually go to sleep
 * ....
 * When we wake back up, we will:
 * - Restore the processor and kernel state
 * - Return to the user
 *
 * By having this routine in here, it hides it from every part of the CA,
 * so it can remain OS-independent. The only function that calls this is
 * sm_proc_write_sleep, which gets the sleep state to enter from the user.
 *
 ****************************************************************************/
static acpi_status
sm_osl_suspend(u32 state)
{
	acpi_status status = AE_ERROR;
	unsigned long wakeup_address;

	/* get out if state is invalid */
	if (state < ACPI_S1 || state > ACPI_S5) 
		goto acpi_sleep_done;

	/* make sure we don't get any suprises */
	disable();

	/* TODO: save device state and suspend them */
	
	/* save the processor state to memory if going into S2 or S3;
	 * save it to disk if going into S4.
	 * Also, set the FWV if going into an STR state
	 */
	if (state == ACPI_S2 || state == ACPI_S3) {
#ifdef DONT_USE_UNTIL_LOWLEVEL_CODE_EXISTS
		wakeup_address = acpi_save_state_mem((unsigned long)&&acpi_sleep_done);

		if (!wakeup_address) goto acpi_sleep_done;

		acpi_set_firmware_waking_vector(
			(ACPI_PHYSICAL_ADDRESS)wakeup_address);
#endif
	} else if (state == ACPI_S4)
#ifdef DONT_USE_UNTIL_LOWLEVEL_CODE_EXISTS
		if (acpi_save_state_disk((unsigned long)&&acpi_sleep_done)) 
			goto acpi_sleep_done;
#endif

	/* set status, since acpi_enter_sleep_state won't return unless something
	 * goes wrong, or it's just S1.
	 */
	status = AE_OK;

	mdelay(10);
	status = acpi_enter_sleep_state(state);

 acpi_sleep_done:

	/* pause for a bit to allow devices to come back on */
	mdelay(10);

	/* make sure that the firmware waking vector is reset */
	acpi_set_firmware_waking_vector((ACPI_PHYSICAL_ADDRESS)0);

	acpi_leave_sleep_state(state);

	/* TODO: resume devices and restore their state */

	enable();
	return status;
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_power_down
 *
 ****************************************************************************/

void
sm_osl_power_down (void)
{
	/* Power down the system (S5 = soft off). */
	sm_osl_suspend(ACPI_S5);
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_add_device
 *
 ****************************************************************************/

acpi_status
sm_osl_add_device(
	SM_CONTEXT		*system)
{
	u32			i = 0;
	struct proc_dir_entry	*bm_proc_dsdt;

	if (!system) {
		return(AE_BAD_PARAMETER);
	}

	printk("ACPI: System firmware supports");
	for (i=0; i<SM_MAX_SYSTEM_STATES; i++) {
		if (system->states[i]) {
			printk(" S%d", i);
		}
	}
	printk("\n");

	if (system->states[ACPI_STATE_S5]) {
		sm_pm_power_off = pm_power_off;
		pm_power_off = sm_osl_power_down;
	}

	create_proc_read_entry(SM_PROC_INFO, S_IRUGO,
		sm_proc_root, sm_osl_proc_read_info, (void*)system);

	bm_proc_sleep = create_proc_read_entry("sleep", S_IFREG | S_IRUGO | S_IWUSR,
					    sm_proc_root, sm_osl_proc_read_sleep, (void*)system);
	if (bm_proc_sleep)
		bm_proc_sleep->write_proc = sm_osl_proc_write_sleep;

	bm_proc_alarm = create_proc_read_entry("alarm", S_IFREG | S_IRUGO | S_IWUSR,
					       sm_proc_root,sm_osl_proc_read_alarm, NULL);
	if (bm_proc_alarm)
		bm_proc_alarm->write_proc = sm_osl_proc_write_alarm;

	bm_proc_gpe = create_proc_read_entry("gpe", S_IFREG | S_IRUGO | S_IWUSR,
					     sm_proc_root,sm_osl_proc_read_gpe,NULL);
	if (bm_proc_gpe)
		bm_proc_gpe->write_proc = sm_osl_proc_write_gpe;
	
	/*
	 * Get a wakeup address for use when we come back from sleep.
	 * At least on IA-32, this needs to be in low memory.
	 * When sleep is supported on other arch's, then we may want
	 * to move this out to another place, but GFP_LOW should suffice
	 * for now.
	 */
#if 0
	if (system->states[ACPI_S3] || system->states[ACPI_S4]) {
		acpi_wakeup_address = (unsigned long)virt_to_phys(get_free_page(GFP_LOWMEM));
		printk(KERN_INFO "ACPI: Have wakeup address 0x%8.8x\n",acpi_wakeup_address);
	}
#endif

	/*
	 * This returns more than a page, so we need to use our own file ops,
	 * not proc's generic ones
	 */
	bm_proc_dsdt = create_proc_entry(SM_PROC_DSDT, S_IRUSR, sm_proc_root);
	if (bm_proc_dsdt) {
		bm_proc_dsdt->proc_fops = &proc_dsdt_operations;
	}

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_remove_device
 *
 ****************************************************************************/

acpi_status
sm_osl_remove_device (
	SM_CONTEXT		*system)
{
	if (!system) {
		return(AE_BAD_PARAMETER);
	}

	remove_proc_entry(SM_PROC_INFO, sm_proc_root);
	remove_proc_entry(SM_PROC_DSDT, sm_proc_root);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_generate_event
 *
 ****************************************************************************/

acpi_status
sm_osl_generate_event (
	u32			event,
	SM_CONTEXT		*system)
{
	acpi_status		status = AE_OK;

	if (!system) {
		return(AE_BAD_PARAMETER);
	}

	switch (event) {

	default:
		return(AE_BAD_PARAMETER);
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_init
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	0: Success
 *
 * DESCRIPTION: Module initialization.
 *
 ****************************************************************************/

static int __init
sm_osl_init (void)
{
	acpi_status		status = AE_OK;

	/* abort if no busmgr */
	if (!bm_proc_root)
		return -ENODEV;

	sm_proc_root = bm_proc_root;
	if (!sm_proc_root) {
		status = AE_ERROR;
	}
	else {
		status = sm_initialize();
	}

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_cleanup
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	<none>
 *
 * DESCRIPTION: Module cleanup.
 *
 ****************************************************************************/

static void __exit
sm_osl_cleanup (void)
{
	sm_terminate();

	return;
}


module_init(sm_osl_init);
module_exit(sm_osl_cleanup);
