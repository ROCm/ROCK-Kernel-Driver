#ifndef AMDKCL_RESERVATION_BACKPORT_H
#define AMDKCL_RESERVATION_BACKPORT_H

#include <kcl/kcl_reservation.h>

#define reservation_ww_class (*_kcl_reservation_ww_class)
#define reservation_seqcount_class (*_kcl_reservation_seqcount_class)
#define reservation_seqcount_string (_kcl_reservation_seqcount_string)
#endif
