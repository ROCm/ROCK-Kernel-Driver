#ifndef S390_DEBUG_H
#define S390_DEBUG_H

#define SANITY_CHECK(irq) do { \
if (irq > highest_subchannel || irq < 0) \
		return -ENODEV; \
	if (ioinfo[irq] == INVALID_STORAGE_AREA) \
		return -ENODEV; \
        if (ioinfo[irq]->st) \
                return -ENODEV; \
	} while(0)

#define CIO_TRACE_EVENT(imp, txt) do { \
	if (cio_debug_initialized) \
		debug_text_event(cio_debug_trace_id, \
				 imp, \
				 txt); \
        }while (0)

#define CIO_MSG_EVENT(imp, args...) do { \
        if (cio_debug_initialized) \
                debug_sprintf_event(cio_debug_msg_id, \
                                    imp , \
                                    ##args); \
        } while (0)

#define CIO_CRW_EVENT(imp, args...) do { \
        if (cio_debug_initialized) \
                debug_sprintf_event(cio_debug_crw_id, \
                                    imp , \
                                    ##args); \
        } while (0)

#undef  CONFIG_DEBUG_IO
#define CONFIG_DEBUG_CRW
#define CONFIG_DEBUG_CHSC

#ifdef CONFIG_DEBUG_IO
#define DBG printk
#else /* CONFIG_DEBUG_IO */
#define DBG(args,...)  do {} while (0) 
#endif /* CONFIG_DEBUG_IO */

#define CIO_DEBUG(printk_level,event_level,msg...) ({\
	DBG(printk_level msg); \
	CIO_MSG_EVENT (event_level, msg); \
})

#define CIO_DEBUG_IFMSG(printk_level,event_level,msg...) ({\
	if (cio_show_msg) printk(printk_level msg); \
	CIO_MSG_EVENT (event_level, msg); \
})

#define CIO_DEBUG_ALWAYS(printk_level,event_level,msg...) ({\
	printk(printk_level msg); \
	CIO_MSG_EVENT (event_level, msg); \
})

#define CIO_DEBUG_NOCONS(irq,printk_level,func,event_level,msg...) ({\
	if (irq != cons_dev) func(printk_level msg); \
	CIO_MSG_EVENT (event_level, msg); \
})

/* for use of debug feature */
extern debug_info_t *cio_debug_msg_id;
extern debug_info_t *cio_debug_trace_id;
extern debug_info_t *cio_debug_crw_id;
extern int cio_debug_initialized;

#endif
