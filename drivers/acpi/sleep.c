/*
 * sleep.c - ACPI sleep support.
 * 
 *  Copyright (c) 2000-2002 Patrick Mochel
 *
 *  Portions are
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#include <linux/proc_fs.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/suspend.h>

#include <asm/uaccess.h>
#include <asm/acpi.h>

#include "acpi_bus.h"
#include "acpi_drivers.h"

#ifdef CONFIG_X86
#include <linux/mc146818rtc.h>
#endif

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME		("sleep")

static u8 sleep_states[ACPI_S_STATE_COUNT];

static void
acpi_power_off (void)
{
	acpi_enter_sleep_state_prep(ACPI_STATE_S5);
	ACPI_DISABLE_IRQS();
	acpi_enter_sleep_state(ACPI_STATE_S5);
}

/**
 * acpi_system_restore_state - OS-specific restoration of state
 * @state:	sleep state we're exiting
 *
 * Note that if we're coming back from S4, the memory image should have already
 * been loaded from the disk and is already in place. (Otherwise how else would we
 * be here?).
 */
acpi_status
acpi_system_restore_state (
	u32			state)
{
	/* restore processor state
	 * We should only be here if we're coming back from STR or STD.
	 * And, in the case of the latter, the memory image should have already
	 * been loaded from disk.
	 */
	if (state > ACPI_STATE_S1)
		acpi_restore_state_mem();

	/* wait for power to come back */
	mdelay(10);

	/* turn all the devices back on */
	device_resume(RESUME_POWER_ON);

	/* enable interrupts once again */
	ACPI_ENABLE_IRQS();

	/* restore device context */
	device_resume(RESUME_RESTORE_STATE);

	if (dmi_broken & BROKEN_INIT_AFTER_S1) {
		printk("Broken toshiba laptop -> kicking interrupts\n");
		init_8259A(0);
	}

	return AE_OK;
}

/**
 * acpi_system_save_state - save OS specific state and power down devices
 * @state:	sleep state we're entering.
 *
 * This handles saving all context to memory, and possibly disk.
 * First, we call to the device driver layer to save device state.
 * Once we have that, we save whatevery processor and kernel state we
 * need to memory.
 * If we're entering S4, we then write the memory image to disk.
 *
 * Only then is it safe for us to power down devices, since we may need
 * the disks and upstream buses to write to.
 */
acpi_status
acpi_system_save_state(
	u32			state)
{
	int			error = 0;

	/* Send notification to devices that they will be suspended.
	 * If any device or driver cannot make the transition, either up
	 * or down, we'll get an error back.
	 */
	error = device_suspend(state, SUSPEND_NOTIFY);
	if (error)
		return AE_ERROR;

	if (state < ACPI_STATE_S5) {

		/* Tell devices to stop I/O and actually save their state.
		 * It is theoretically possible that something could fail,
		 * so handle that gracefully..
		 */
		error = device_suspend(state, SUSPEND_SAVE_STATE);
		if (error) {
			/* tell devices to restore state if they have
			 * it saved and to start taking I/O requests.
			 */
			device_resume(RESUME_RESTORE_STATE);
			return error;
		}

		/* flush caches */
		ACPI_FLUSH_CPU_CACHE();

		/* Do arch specific saving of state. */
		if (state > ACPI_STATE_S1) {
			error = acpi_save_state_mem();

			if (!error && (state == ACPI_STATE_S4))
				error = acpi_save_state_disk();

			if (error) {
				device_resume(RESUME_RESTORE_STATE);
				return error;
			}
		}
	}

	/* disable interrupts
	 * Note that acpi_suspend -- our caller -- will do this once we return.
	 * But, we want it done early, so we don't get any suprises during
	 * the device suspend sequence.
	 */
	ACPI_DISABLE_IRQS();

	/* Unconditionally turn off devices.
	 * Obvious if we enter a sleep state.
	 * If entering S5 (soft off), this should put devices in a
	 * quiescent state.
	 */
	error = device_suspend(state, SUSPEND_POWER_DOWN);

	/* We're pretty screwed if we got an error from this.
	 * We try to recover by simply calling our own restore_state
	 * function; see above for definition.
	 *
	 * If it's S5 though, go through with it anyway..
	 */
	if (error && state != ACPI_STATE_S5)
		acpi_system_restore_state(state);

	return error ? AE_ERROR : AE_OK;
}


/****************************************************************************
 *
 * FUNCTION:    acpi_system_suspend
 *
 * PARAMETERS:  %state: Sleep state to enter.
 *
 * RETURN:      acpi_status, whether or not we successfully entered and
 *              exited sleep.
 *
 * DESCRIPTION: Perform OS-specific action to enter sleep state.
 *              This is the final step in going to sleep, per spec.  If we
 *              know we're coming back (i.e. not entering S5), we save the
 *              processor flags. [ We'll have to save and restore them anyway,
 *              so we use the arch-agnostic save_flags and restore_flags
 *              here.]  We then set the place to return to in arch-specific
 *              globals using arch_set_return_point. Finally, we call the
 *              ACPI function to write the proper values to I/O ports.
 *
 ****************************************************************************/

acpi_status
acpi_system_suspend(
	u32			state)
{
	acpi_status		status = AE_ERROR;
	unsigned long		flags = 0;

	local_irq_save(flags);
	
	switch (state)
	{
	case ACPI_STATE_S1:
		barrier();
		status = acpi_enter_sleep_state(state);
		break;

	case ACPI_STATE_S2:
	case ACPI_STATE_S3:
		do_suspend_lowlevel(0);
		break;
	}
	local_irq_restore(flags);

	return status;
}


/**
 * acpi_suspend - OS-agnostic system suspend/resume support (S? states)
 * @state:	state we're entering
 *
 */
acpi_status
acpi_suspend (
	u32			state)
{
	acpi_status status;

	/* get out if state is invalid */
	if (state < ACPI_STATE_S1 || state > ACPI_STATE_S5)
		return AE_ERROR;

	freeze_processes();		/* device_suspend needs processes to be stopped */

	/* do we have a wakeup address for S2 and S3? */
	if (state == ACPI_STATE_S2 || state == ACPI_STATE_S3) {
		if (!acpi_wakeup_address)
			return AE_ERROR;
		acpi_set_firmware_waking_vector((ACPI_PHYSICAL_ADDRESS) acpi_wakeup_address);
	}

	acpi_enter_sleep_state_prep(state);

	status = acpi_system_save_state(state);
	if (!ACPI_SUCCESS(status))
		return status;

	/* disable interrupts and flush caches */
	ACPI_DISABLE_IRQS();
	ACPI_FLUSH_CPU_CACHE();

	/* perform OS-specific sleep actions */
	status = acpi_system_suspend(state);

	/* Even if we failed to go to sleep, all of the devices are in an suspended
	 * mode. So, we run these unconditionaly to make sure we have a usable system
	 * no matter what.
	 */
	acpi_system_restore_state(state);
	acpi_leave_sleep_state(state);

	/* make sure interrupts are enabled */
	ACPI_ENABLE_IRQS();

	/* reset firmware waking vector */
	acpi_set_firmware_waking_vector((ACPI_PHYSICAL_ADDRESS) 0);
	thaw_processes();

	return status;
}


static int
acpi_system_read_sleep (
        char                    *page,
        char                    **start,
        off_t                   off,
        int                     count,
        int                     *eof,
        void                    *data)
{
	char			*p = page;
	int			size;
	int			i;

	ACPI_FUNCTION_TRACE("acpi_system_read_sleep");

	if (off != 0)
		goto end;

	for (i = 0; i <= ACPI_STATE_S5; i++) {
		if (sleep_states[i])
			p += sprintf(p,"S%d ", i);
	}

	p += sprintf(p, "\n");

end:
	size = (p - page);
	if (size <= off+count) *eof = 1;
	*start = page + off;
	size -= off;
	if (size>count) size = count;
	if (size<0) size = 0;

	return_VALUE(size);
}


static int
acpi_system_write_sleep (
	struct file		*file,
	const char		*buffer,
	unsigned long		count,
	void			*data)
{
	acpi_status		status = AE_OK;
	char			state_string[12] = {'\0'};
	u32			state = 0;

	ACPI_FUNCTION_TRACE("acpi_system_write_sleep");

	if (count > sizeof(state_string) - 1)
		return_VALUE(-EINVAL);

	if (copy_from_user(state_string, buffer, count))
		return_VALUE(-EFAULT);
	
	state_string[count] = '\0';
	
	state = simple_strtoul(state_string, NULL, 0);
	
	if (sleep_states[state])
		return_VALUE(-ENODEV);

#ifdef CONFIG_SOFTWARE_SUSPEND
	if (state == 4) {
		software_suspend();
		return_VALUE(count);
	}
#endif
	status = acpi_suspend(state);

	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);
	
	return_VALUE(count);
}


static int
acpi_system_read_alarm (
	char                    *page,
	char                    **start,
	off_t                   off,
	int                     count,
	int                     *eof,
	void                    *context)
{
	char			*p = page;
	int			size = 0;
	u32			sec, min, hr;
	u32			day, mo, yr;

	ACPI_FUNCTION_TRACE("acpi_system_read_alarm");

	if (off != 0)
		goto end;

	spin_lock(&rtc_lock);

	sec = CMOS_READ(RTC_SECONDS_ALARM);
	min = CMOS_READ(RTC_MINUTES_ALARM);
	hr = CMOS_READ(RTC_HOURS_ALARM);

#if 0	/* If we ever get an FACP with proper values... */
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

#if 0
	/* we're trusting the FADT (see above)*/
#else
	/* If we're not trusting the FADT, we should at least make it
	 * right for _this_ century... ehm, what is _this_ century?
	 *
	 * TBD:
	 *  ASAP: find piece of code in the kernel, e.g. star tracker driver,
	 *        which we can trust to determine the century correctly. Atom
	 *        watch driver would be nice, too...
	 *
	 *  if that has not happened, change for first release in 2050:
 	 *        if (yr<50)
	 *                yr += 2100;
	 *        else
	 *                yr += 2000;   // current line of code
	 *
	 *  if that has not happened either, please do on 2099/12/31:23:59:59
	 *        s/2000/2100
	 *
	 */
	yr += 2000;
#endif

	p += sprintf(p,"%4.4u-", yr);
	p += (mo > 12)  ? sprintf(p, "**-")  : sprintf(p, "%2.2u-", mo);
	p += (day > 31) ? sprintf(p, "** ")  : sprintf(p, "%2.2u ", day);
	p += (hr > 23)  ? sprintf(p, "**:")  : sprintf(p, "%2.2u:", hr);
	p += (min > 59) ? sprintf(p, "**:")  : sprintf(p, "%2.2u:", min);
	p += (sec > 59) ? sprintf(p, "**\n") : sprintf(p, "%2.2u\n", sec);

 end:
	size = p - page;
	if (size < count) *eof = 1;
	else if (size > count) size = count;
	if (size < 0) size = 0;
	*start = page;

	return_VALUE(size);
}


static int
get_date_field (
	char			**p,
	u32			*value)
{
	char			*next = NULL;
	char			*string_end = NULL;
	int			result = -EINVAL;

	/*
	 * Try to find delimeter, only to insert null.  The end of the
	 * string won't have one, but is still valid.
	 */
	next = strpbrk(*p, "- :");
	if (next)
		*next++ = '\0';

	*value = simple_strtoul(*p, &string_end, 10);

	/* Signal success if we got a good digit */
	if (string_end != *p)
		result = 0;

	if (next)
		*p = next;

	return result;
}


static int
acpi_system_write_alarm (
	struct file		*file,
	const char		*buffer,
	unsigned long		count,
	void			*data)
{
	int			result = 0;
	char			alarm_string[30] = {'\0'};
	char			*p = alarm_string;
	u32			sec, min, hr, day, mo, yr;
	int			adjust = 0;
	unsigned char		rtc_control = 0;

	ACPI_FUNCTION_TRACE("acpi_system_write_alarm");

	if (count > sizeof(alarm_string) - 1)
		return_VALUE(-EINVAL);
	
	if (copy_from_user(alarm_string, buffer, count))
		return_VALUE(-EFAULT);

	alarm_string[count] = '\0';

	/* check for time adjustment */
	if (alarm_string[0] == '+') {
		p++;
		adjust = 1;
	}

	if ((result = get_date_field(&p, &yr)))
		goto end;
	if ((result = get_date_field(&p, &mo)))
		goto end;
	if ((result = get_date_field(&p, &day)))
		goto end;
	if ((result = get_date_field(&p, &hr)))
		goto end;
	if ((result = get_date_field(&p, &min)))
		goto end;
	if ((result = get_date_field(&p, &sec)))
		goto end;

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
	CMOS_WRITE(hr, RTC_HOURS_ALARM);
	CMOS_WRITE(min, RTC_MINUTES_ALARM);
	CMOS_WRITE(sec, RTC_SECONDS_ALARM);

	/*
	 * If the system supports an enhanced alarm it will have non-zero
	 * offsets into the CMOS RAM here -- which for some reason are pointing
	 * to the RTC area of memory.
	 */
#if 0
	if (acpi_gbl_FADT->day_alrm)
		CMOS_WRITE(day, acpi_gbl_FADT->day_alrm);
	if (acpi_gbl_FADT->mon_alrm)
		CMOS_WRITE(mo, acpi_gbl_FADT->mon_alrm);
	if (acpi_gbl_FADT->century)
		CMOS_WRITE(yr/100, acpi_gbl_FADT->century);
#endif
	/* enable the rtc alarm interrupt */
	if (!(rtc_control & RTC_AIE)) {
		rtc_control |= RTC_AIE;
		CMOS_WRITE(rtc_control,RTC_CONTROL);
		CMOS_READ(RTC_INTR_FLAGS);
	}

	spin_unlock_irq(&rtc_lock);

	acpi_set_register(ACPI_BITREG_RT_CLOCK_ENABLE, 1, ACPI_MTX_LOCK);

	file->f_pos += count;

	result = 0;
end:
	return_VALUE(result ? result : count);
}


#if defined(CONFIG_MAGIC_SYSRQ) && defined(CONFIG_PM)

/* Simple wrapper calling power down function. */
static void acpi_sysrq_power_off(int key, struct pt_regs *pt_regs,
	struct tty_struct *tty)
{
	acpi_power_off();
}

struct sysrq_key_op sysrq_acpi_poweroff_op = {
	.handler =	&acpi_sysrq_power_off,
	.help_msg =	"Off",
	.action_msg =	"Power Off\n"
};

#endif  /* CONFIG_MAGIC_SYSRQ */

static int __init acpi_sleep_init(void)
{
	struct proc_dir_entry	*entry = NULL;
	acpi_status		status = AE_OK;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_system_add_fs");

	printk(KERN_INFO PREFIX "(supports");
	for (i=0; i<ACPI_S_STATE_COUNT; i++) {
		u8 type_a, type_b;
		status = acpi_get_sleep_type_data(i, &type_a, &type_b);
		if (ACPI_SUCCESS(status)) {
			sleep_states[i] = 1;
			printk(" S%d", i);
		}
	}
	printk(")\n");


	/* 'sleep' [R/W]*/
	entry = create_proc_entry(ACPI_SYSTEM_FILE_SLEEP,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_root_dir);
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_SYSTEM_FILE_SLEEP));
	else {
		entry->read_proc = acpi_system_read_sleep;
		entry->write_proc = acpi_system_write_sleep;
	}

	/* 'alarm' [R/W] */
	entry = create_proc_entry(ACPI_SYSTEM_FILE_ALARM,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_root_dir);
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_SYSTEM_FILE_ALARM));
	else {
		entry->read_proc = acpi_system_read_alarm;
		entry->write_proc = acpi_system_write_alarm;
	}

	/* Install the soft-off (S5) handler. */
	if (sleep_states[ACPI_STATE_S5]) {
		pm_power_off = acpi_power_off;
		register_sysrq_key('o', &sysrq_acpi_poweroff_op);

		/* workaround: some systems don't claim S4 support, but they
                   do support S5 (power-down). That is all we need, so
		   indicate support. */
		sleep_states[ACPI_STATE_S4] = 1;
	}

	return_VALUE(0);
}

subsys_initcall(acpi_sleep_init);
