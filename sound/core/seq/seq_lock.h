#ifndef __SND_SEQ_LOCK_H
#define __SND_SEQ_LOCK_H

#if defined(__SMP__) || defined(CONFIG_SND_DEBUG)

typedef atomic_t snd_use_lock_t;

/* initialize lock */
#define snd_use_lock_init(lockp) atomic_set(lockp, 0)

/* increment lock */
#define snd_use_lock_use(lockp) atomic_inc(lockp)

/* release lock */
#define snd_use_lock_free(lockp) atomic_dec(lockp)

/* wait until all locks are released */
void snd_use_lock_sync_helper(snd_use_lock_t *lock, const char *file, int line);
#define snd_use_lock_sync(lockp) snd_use_lock_sync_helper(lockp, __BASE_FILE__, __LINE__)

/* (interruptible) sleep_on during the specified spinlock */
void snd_seq_sleep_in_lock(wait_queue_head_t *p, spinlock_t *lock);

/* (interruptible) sleep_on with timeout during the specified spinlock */
long snd_seq_sleep_timeout_in_lock(wait_queue_head_t *p, spinlock_t *lock, long timeout);

#else /* SMP || CONFIG_SND_DEBUG */

typedef spinlock_t snd_use_lock_t;	/* dummy */
#define snd_use_lock_init(lockp) /**/
#define snd_use_lock_use(lockp) /**/
#define snd_use_lock_free(lockp) /**/
#define snd_use_lock_sync(lockp) /**/

#define snd_seq_sleep_in_lock(p,lock)	interruptible_sleep_on(p)
#define snd_seq_sleep_timeout_in_lock(p,lock,timeout)	interruptible_sleep_on_timeout(p,timeout)

#endif /* SMP || CONFIG_SND_DEBUG */

#endif /* __SND_SEQ_LOCK_H */
