/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmap_lock

#if !defined(_TRACE_MMAP_LOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMAP_LOCK_H

#include <linux/tracepoint.h>
#include <linux/types.h>

struct mm_struct;

DECLARE_EVENT_CLASS(
	mmap_lock_template,

	TP_PROTO(struct mm_struct *mm, const char *memcg_path, u64 duration,
		bool write, bool success),

	TP_ARGS(mm, memcg_path, duration, write, success),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__string(memcg_path, memcg_path)
		__field(u64, duration)
		__field(bool, write)
		__field(bool, success)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__assign_str(memcg_path, memcg_path);
		__entry->duration = duration;
		__entry->write = write;
		__entry->success = success;
	),

	TP_printk(
		"mm=%p memcg_path=%s duration=%llu write=%s success=%s\n",
		__entry->mm,
		__get_str(memcg_path),
		__entry->duration,
		__entry->write ? "true" : "false",
		__entry->success ? "true" : "false")
	);

DEFINE_EVENT(mmap_lock_template, mmap_lock_start_locking,

	TP_PROTO(struct mm_struct *mm, const char *memcg_path, u64 duration,
		bool write, bool success),

	TP_ARGS(mm, memcg_path, duration, write, success)
);

DEFINE_EVENT(mmap_lock_template, mmap_lock_acquire_returned,

	TP_PROTO(struct mm_struct *mm, const char *memcg_path, u64 duration,
		bool write, bool success),

	TP_ARGS(mm, memcg_path, duration, write, success)
);

DEFINE_EVENT(mmap_lock_template, mmap_lock_released,

	TP_PROTO(struct mm_struct *mm, const char *memcg_path, u64 duration,
		bool write, bool success),

	TP_ARGS(mm, memcg_path, duration, write, success)
);

#endif /* _TRACE_MMAP_LOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
