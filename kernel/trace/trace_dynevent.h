/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common header file for generic dynamic events.
 */

#ifndef _TRACE_DYNEVENT_H
#define _TRACE_DYNEVENT_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>

#include "trace.h"


/* Register new dyn_event type -- must be called at first */
int dyn_event_register(struct dyn_event_operations *ops);

extern struct list_head dyn_event_list;

static inline
int dyn_event_init(struct dyn_event *ev, struct dyn_event_operations *ops)
{
	if (!ev || !ops)
		return -EINVAL;

	INIT_LIST_HEAD(&ev->list);
	ev->ops = ops;
	return 0;
}

static inline int dyn_event_add(struct dyn_event *ev)
{
	lockdep_assert_held(&event_mutex);

	if (!ev || !ev->ops)
		return -EINVAL;

	list_add_tail(&ev->list, &dyn_event_list);
	return 0;
}

static inline void dyn_event_remove(struct dyn_event *ev)
{
	lockdep_assert_held(&event_mutex);
	list_del_init(&ev->list);
}

void *dyn_event_seq_start(struct seq_file *m, loff_t *pos);
void *dyn_event_seq_next(struct seq_file *m, void *v, loff_t *pos);
void dyn_event_seq_stop(struct seq_file *m, void *v);
int dyn_events_release_all(struct dyn_event_operations *type);
int dyn_event_release(int argc, char **argv, struct dyn_event_operations *type);

/*
 * for_each_dyn_event	-	iterate over the dyn_event list
 * @pos:	the struct dyn_event * to use as a loop cursor
 *
 * This is just a basement of for_each macro. Wrap this for
 * each actual event structure with ops filtering.
 */
#define for_each_dyn_event(pos)	\
	list_for_each_entry(pos, &dyn_event_list, list)

/*
 * for_each_dyn_event	-	iterate over the dyn_event list safely
 * @pos:	the struct dyn_event * to use as a loop cursor
 * @n:		the struct dyn_event * to use as temporary storage
 */
#define for_each_dyn_event_safe(pos, n)	\
	list_for_each_entry_safe(pos, n, &dyn_event_list, list)

#endif
