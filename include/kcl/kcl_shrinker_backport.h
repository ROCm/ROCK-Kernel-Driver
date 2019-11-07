#ifndef AMDKCL_SHRINKER_BACKPORT_H
#define AMDKCL_SHRINKER_BACKPORT_H

#include <linux/mm.h>
#ifndef HAVE_REGISTER_SHRINKER_RETURN_INT
static inline int _kcl_register_shrinker(struct shrinker *shrinker)
{
	register_shrinker(shrinker);
	return 0;
}
#define register_shrinker _kcl_register_shrinker
#endif
#endif
