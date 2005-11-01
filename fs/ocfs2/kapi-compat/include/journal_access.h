#ifndef KAPI_JOURNAL_ACCESS_WITH_CREDITS_H
#define KAPI_JOURNAL_ACCESS_WITH_CREDITS_H

/* bzzz 1, universe 0 */

#ifdef JOURNAL_ACCESS_WITH_CREDITS

#define kapi_journal_get_write_access(a, b) \
		journal_get_write_access(a, b, NULL)
#define kapi_journal_get_read_access(a, b) \
		journal_get_read_access(a, b, NULL)
#define kapi_journal_get_undo_access(a, b) \
		journal_get_undo_access(a, b, NULL)

#else

#define kapi_journal_get_write_access(a, b) \
		journal_get_write_access(a, b)
#define kapi_journal_get_read_access(a, b) \
		journal_get_read_access(a, b)
#define kapi_journal_get_undo_access(a, b) \
		journal_get_undo_access(a, b)

#endif

#endif /* KAPI_JOURNAL_ACCESS_WITH_CREDITS_H */
