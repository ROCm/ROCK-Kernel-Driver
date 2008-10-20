#ifndef _LINUX_KMSG_H
#define _LINUX_KMSG_H

#define kmsg_printk(level, format, ...) \
	printk(level KMSG_COMPONENT  ": " format, ##__VA_ARGS__)

#if defined(__KMSG_CHECKER)
/* generate magic string for scripts/kmsg-doc to parse */
#define kmsg_printk_hash(level, format, ...) \
	__KMSG_PRINT(level _FMT_ format _ARGS_ ##__VA_ARGS__ _END_)
#elif defined(CONFIG_KMSG_IDS)
int printk_hash(const char *, const char *, ...);
#define kmsg_printk_hash(level, format, ...) \
	printk_hash(level KMSG_COMPONENT ".%06x" ": ", format, ##__VA_ARGS__)
#else /* !defined(CONFIG_KMSG_IDS) */
#define kmsg_printk_hash kmsg_printk
#endif

#define kmsg_emerg(fmt, ...) \
	kmsg_printk_hash(KERN_EMERG, fmt, ##__VA_ARGS__)
#define kmsg_alert(fmt, ...) \
	kmsg_printk_hash(KERN_ALERT, fmt, ##__VA_ARGS__)
#define kmsg_crit(fmt, ...) \
	kmsg_printk_hash(KERN_CRIT, fmt, ##__VA_ARGS__)
#define kmsg_err(fmt, ...) \
	kmsg_printk_hash(KERN_ERR, fmt, ##__VA_ARGS__)
#define kmsg_warn(fmt, ...) \
	kmsg_printk_hash(KERN_WARNING, fmt, ##__VA_ARGS__)
#define kmsg_notice(fmt, ...) \
	kmsg_printk_hash(KERN_NOTICE, fmt, ##__VA_ARGS__)
#define kmsg_info(fmt, ...) \
	kmsg_printk_hash(KERN_INFO, fmt, ##__VA_ARGS__)

#ifdef DEBUG
#define kmsg_dbg(fmt, ...) \
	kmsg_printk(KERN_DEBUG, fmt, ##__VA_ARGS__)
#else
#define kmsg_dbg(fmt, ...) \
	({ if (0) kmsg_printk(KERN_DEBUG, fmt, ##__VA_ARGS__); 0; })
#endif

#endif /* _LINUX_KMSG_H */
