#ifndef AMDKCL_RESERVATION_H
#define AMDKCL_RESERVATION_H

extern struct ww_class *_kcl_reservation_ww_class;
extern struct lock_class_key *_kcl_reservation_seqcount_class;
extern const char *_kcl_reservation_seqcount_string;

#endif /* AMDKCL_RESERVATION_H */
