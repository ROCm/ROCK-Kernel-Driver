#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/suspend.h>
#include <linux/bcd.h>
#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#ifdef CONFIG_X86
#include <linux/mc146818rtc.h>
#endif

#include "sleep.h"

#define ACPI_SYSTEM_FILE_SLEEP		"sleep"
#define ACPI_SYSTEM_FILE_ALARM		"alarm"

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME		("sleep")


static int acpi_system_sleep_seq_show(struct seq_file *seq, void *offset)
{
	int			i;

	ACPI_FUNCTION_TRACE("acpi_system_sleep_seq_show");

	for (i = 0; i <= ACPI_STATE_S5; i++) {
		if (sleep_states[i]) {
			seq_printf(seq,"S%d ", i);
			if (i == ACPI_STATE_S4 && acpi_gbl_FACS->S4bios_f)
				seq_printf(seq, "S4bios ");
		}
	}

	seq_puts(seq, "\n");

	return 0;
}

static int acpi_system_sleep_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_system_sleep_seq_show, PDE(inode)->data);
}

static int
acpi_system_write_sleep (
	struct file		*file,
	const char		*buffer,
	size_t			count,
	loff_t			*ppos)
{
	char	str[12];
	u32	state = 0;
	int	error = 0;

	if (count > sizeof(str) - 1)
		goto Done;
	memset(str,0,sizeof(str));
	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	/* Check for S4 bios request */
	if (!strcmp(str,"4b")) {
		error = acpi_suspend(4);
		goto Done;
	}
	state = simple_strtoul(str, NULL, 0);
#ifdef CONFIG_SOFTWARE_SUSPEND
	if (state == 4) {
		software_suspend();
		goto Done;
	}
#endif
	error = acpi_suspend(state);
 Done:
	return error ? error : count;
}

static int acpi_system_alarm_seq_show(struct seq_file *seq, void *offset)
{
	u32			sec, min, hr;
	u32			day, mo, yr;

	ACPI_FUNCTION_TRACE("acpi_system_alarm_seq_show");

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

	seq_printf(seq,"%4.4u-", yr);
	(mo > 12)  ? seq_puts(seq, "**-")  : seq_printf(seq, "%2.2u-", mo);
	(day > 31) ? seq_puts(seq, "** ")  : seq_printf(seq, "%2.2u ", day);
	(hr > 23)  ? seq_puts(seq, "**:")  : seq_printf(seq, "%2.2u:", hr);
	(min > 59) ? seq_puts(seq, "**:")  : seq_printf(seq, "%2.2u:", min);
	(sec > 59) ? seq_puts(seq, "**\n") : seq_printf(seq, "%2.2u\n", sec);

	return 0;
}

static int acpi_system_alarm_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_system_alarm_seq_show, PDE(inode)->data);
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
	size_t			count,
	loff_t			*ppos)
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


static struct file_operations acpi_system_sleep_fops = {
	.open		= acpi_system_sleep_open_fs,
	.read		= seq_read,
	.write		= acpi_system_write_sleep,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct file_operations acpi_system_alarm_fops = {
	.open		= acpi_system_alarm_open_fs,
	.read		= seq_read,
	.write		= acpi_system_write_alarm,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static int acpi_sleep_proc_init(void)
{
	struct proc_dir_entry	*entry = NULL;

	/* 'sleep' [R/W]*/
	entry = create_proc_entry(ACPI_SYSTEM_FILE_SLEEP,
				  S_IFREG|S_IRUGO|S_IWUSR, acpi_root_dir);
	if (entry)
		entry->proc_fops = &acpi_system_sleep_fops;

	/* 'alarm' [R/W] */
	entry = create_proc_entry(ACPI_SYSTEM_FILE_ALARM,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_root_dir);
	if (entry)
		entry->proc_fops = &acpi_system_alarm_fops;
	return 0;
}

late_initcall(acpi_sleep_proc_init);
