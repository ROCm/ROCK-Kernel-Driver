/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_DEVICE_H
#define AMDKCL_DEVICE_H

#include <linux/ratelimit.h>

#if !defined(dev_err_once)
#ifdef CONFIG_PRINTK
#define dev_level_once(dev_level, dev, fmt, ...)			\
do {									\
	static bool __print_once __read_mostly;				\
									\
	if (!__print_once) {						\
		__print_once = true;					\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
	}								\
} while (0)
#else
#define dev_level_once(dev_level, dev, fmt, ...)			\
do {									\
	if (0)								\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
} while (0)
#endif

#define dev_err_once(dev, fmt, ...)					\
	dev_level_once(dev_err, dev, fmt, ##__VA_ARGS__)
#endif

#if !defined(dev_err_ratelimited)
#define dev_level_ratelimited(dev_level, dev, fmt, ...)			\
do {									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
	if (__ratelimit(&_rs))						\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
} while (0)

#define dev_err_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_err, dev, fmt, ##__VA_ARGS__)
#endif
#endif /* AMDKCL_DEVICE_H */
