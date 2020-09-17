// SPDX-License-Identifier: GPL-2.0
#define CREATE_TRACE_POINTS
#include <trace/events/mmap_lock.h>

#include <linux/cgroup.h>
#include <linux/memcontrol.h>
#include <linux/mmap_lock.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/trace_events.h>
#include <linux/sched/clock.h>

#ifdef CONFIG_MEMCG

DEFINE_PER_CPU(char[MAX_FILTER_STR_VAL], trace_memcg_path);

/*
 * Write the given mm_struct's memcg path to a percpu buffer, and return a
 * pointer to it. If the path cannot be determined, the buffer will contain the
 * empty string.
 *
 * Note: buffers are allocated per-cpu to avoid locking, so preemption must be
 * disabled by the caller before calling us, and re-enabled only after the
 * caller is done with the pointer.
 */
static const char *get_mm_memcg_path(struct mm_struct *mm)
{
	struct mem_cgroup *memcg = get_mem_cgroup_from_mm(mm);

	if (memcg != NULL && likely(memcg->css.cgroup != NULL)) {
		char *buf = this_cpu_ptr(trace_memcg_path);

		cgroup_path(memcg->css.cgroup, buf, MAX_FILTER_STR_VAL);
		return buf;
	}
	return "";
}

#define TRACE_MMAP_LOCK_EVENT(type, mm, ...)                                   \
	do {                                                                   \
		if (trace_mmap_lock_##type##_enabled()) {                      \
			get_cpu();                                             \
			trace_mmap_lock_##type(mm, get_mm_memcg_path(mm),      \
					       ##__VA_ARGS__);                 \
			put_cpu();                                             \
		}                                                              \
	} while (0)

#else /* !CONFIG_MEMCG */

#define TRACE_MMAP_LOCK_EVENT(type, mm, ...)                                   \
	trace_mmap_lock_##type(mm, "", ##__VA_ARGS__)

#endif /* CONFIG_MEMCG */

/*
 * Trace calls must be in a separate file, as otherwise there's a circuclar
 * dependency between linux/mmap_lock.h and trace/events/mmap_lock.h.
 */

static void trace_start_locking(struct mm_struct *mm, bool write)
{
	TRACE_MMAP_LOCK_EVENT(start_locking, mm, 0, write, true);
}

static void trace_acquire_returned(struct mm_struct *mm, u64 start_time_ns,
				   bool write, bool success)
{
	TRACE_MMAP_LOCK_EVENT(acquire_returned, mm,
			      sched_clock() - start_time_ns, write, success);
}

static void trace_released(struct mm_struct *mm, bool write)
{
	TRACE_MMAP_LOCK_EVENT(released, mm, 0, write, true);
}

static bool trylock_impl(struct mm_struct *mm,
			 int (*trylock)(struct rw_semaphore *), bool write)
{
	bool ret;

	trace_start_locking(mm, write);
	ret = trylock(&mm->mmap_lock) != 0;
	/* Avoid calling sched_clock() for trylocks; assume duration = 0. */
	TRACE_MMAP_LOCK_EVENT(acquire_returned, mm, 0, write, ret);
	return ret;
}

static inline void lock_impl(struct mm_struct *mm,
			     void (*lock)(struct rw_semaphore *), bool write)
{
	u64 start_time_ns;

	trace_start_locking(mm, write);
	start_time_ns = sched_clock();
	lock(&mm->mmap_lock);
	trace_acquire_returned(mm, start_time_ns, write, true);
}

static inline int lock_return_impl(struct mm_struct *mm,
				   int (*lock)(struct rw_semaphore *),
				   bool write)
{
	u64 start_time_ns;
	int ret;

	trace_start_locking(mm, write);
	start_time_ns = sched_clock();
	ret = lock(&mm->mmap_lock);
	trace_acquire_returned(mm, start_time_ns, write, ret == 0);
	return ret;
}

static inline void unlock_impl(struct mm_struct *mm,
			       void (*unlock)(struct rw_semaphore *),
			       bool write)
{
	unlock(&mm->mmap_lock);
	trace_released(mm, write);
}

void mmap_init_lock(struct mm_struct *mm)
{
	init_rwsem(&mm->mmap_lock);
}

void mmap_write_lock(struct mm_struct *mm)
{
	lock_impl(mm, down_write, true);
}
EXPORT_SYMBOL(mmap_write_lock);

void mmap_write_lock_nested(struct mm_struct *mm, int subclass)
{
	u64 start_time_ns;

	trace_start_locking(mm, true);
	start_time_ns = sched_clock();
	down_write_nested(&mm->mmap_lock, subclass);
	trace_acquire_returned(mm, start_time_ns, true, true);
}
EXPORT_SYMBOL(mmap_write_lock_nested);

int mmap_write_lock_killable(struct mm_struct *mm)
{
	return lock_return_impl(mm, down_write_killable, true);
}
EXPORT_SYMBOL(mmap_write_lock_killable);

bool mmap_write_trylock(struct mm_struct *mm)
{
	return trylock_impl(mm, down_write_trylock, true);
}
EXPORT_SYMBOL(mmap_write_trylock);

void mmap_write_unlock(struct mm_struct *mm)
{
	unlock_impl(mm, up_write, true);
}
EXPORT_SYMBOL(mmap_write_unlock);

void mmap_write_downgrade(struct mm_struct *mm)
{
	downgrade_write(&mm->mmap_lock);
	TRACE_MMAP_LOCK_EVENT(acquire_returned, mm, 0, false, true);
}
EXPORT_SYMBOL(mmap_write_downgrade);

void mmap_read_lock(struct mm_struct *mm)
{
	lock_impl(mm, down_read, false);
}
EXPORT_SYMBOL(mmap_read_lock);

int mmap_read_lock_killable(struct mm_struct *mm)
{
	return lock_return_impl(mm, down_read_killable, false);
}
EXPORT_SYMBOL(mmap_read_lock_killable);

bool mmap_read_trylock(struct mm_struct *mm)
{
	return trylock_impl(mm, down_read_trylock, false);
}
EXPORT_SYMBOL(mmap_read_trylock);

void mmap_read_unlock(struct mm_struct *mm)
{
	unlock_impl(mm, up_read, false);
}
EXPORT_SYMBOL(mmap_read_unlock);

bool mmap_read_trylock_non_owner(struct mm_struct *mm)
{
	if (mmap_read_trylock(mm)) {
		rwsem_release(&mm->mmap_lock.dep_map, _RET_IP_);
		trace_released(mm, false);
		return true;
	}
	return false;
}
EXPORT_SYMBOL(mmap_read_trylock_non_owner);

void mmap_read_unlock_non_owner(struct mm_struct *mm)
{
	up_read_non_owner(&mm->mmap_lock);
	trace_released(mm, false);
}
EXPORT_SYMBOL(mmap_read_unlock_non_owner);

void mmap_assert_locked(struct mm_struct *mm)
{
	lockdep_assert_held(&mm->mmap_lock);
	VM_BUG_ON_MM(!rwsem_is_locked(&mm->mmap_lock), mm);
}
EXPORT_SYMBOL(mmap_assert_locked);

void mmap_assert_write_locked(struct mm_struct *mm)
{
	lockdep_assert_held_write(&mm->mmap_lock);
	VM_BUG_ON_MM(!rwsem_is_locked(&mm->mmap_lock), mm);
}
EXPORT_SYMBOL(mmap_assert_write_locked);
