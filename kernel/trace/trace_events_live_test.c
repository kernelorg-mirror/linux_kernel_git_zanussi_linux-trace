// SPDX-License-Identifier: GPL-2.0
/*
 * Test module for live event tracing interface.
 *
 * Copyright (C) 2019 Tom Zanussi <zanussi@kernel.org>
 */

#include <linux/module.h>
#include <linux/trace_events.h>
#include "trace.h"

static struct event_trigger_data *trigger_data;

/*
 * There are 2 tests you can try, depending on whether you compile
 * this module with or without SLOW_TEST defined.  The SLOW_TEST test
 * is meant just to test that the basic functionality works - it
 * generates a very simple synthetic event and also writes output to
 * dmesg.  To test this way, just comment in SLOW_TEST and then:
 *
 * # insmod kernel/trace/trace_events_live_test.ko/trace_events_live_test.ko
 * # sync
 * # cat /sys/kernel/debug/tracing/trace
 * # dmesg
 * # rmmod trace_events_live_test
 *
 * You should see a "synctest" event for each sync you do, along with
 * several lines of output in dmesg.
 *
 * To do the more realistic sched_test, comment out SLOW_TEST and
 * then:
 *
 * # insmod kernel/trace/trace_events_live_test.ko/trace_events_live_test.ko
 * # cat /sys/kernel/debug/tracing/trace
 * # dmesg
 * # rmmod trace_events_live_test
 *
 * You should see a "schedtest" event for each sched_switch event.
 * Normally, there will be no output in dmesg, since the printk_n
 * local variable in the sched_switch handler is set to 0.  Make this
 * non-zero to see printk output, but beware - printing to much on
 * each sched_switch event can livelock, thus the existence of this
 * variable in the first place.
 */

/* comment out to do sched_switch test */
#define SLOW_TEST

u64 get_timestamp(struct live_field *timestamp_field,
		  void *rec,
		  struct ring_buffer_event *rbe,
		  bool ns)
{
	u64 val = 0;

	if (timestamp_field) {
		val = timestamp_field->fn(timestamp_field->field, rbe, rec);
		if (ns)
			return val;
		val += 500;
		do_div(val, 1000);
	}

	return val;
}

void print_timestamp(struct live_field *timestamp_field,
		     void *rec,
		     struct ring_buffer_event *rbe)
{
	u64 val;

	if (timestamp_field) {
		val = timestamp_field->fn(timestamp_field->field, rbe, rec);
		printk("timestamp(ns): %llu\n", val);
		val += 500;
		do_div(val, 1000);
		printk("timestamp(ms): %llu\n", val);
	}
}

#ifdef SLOW_TEST
static struct trace_event_file *synctest_event_file;

void syscalls_sys_enter_sync_event_trigger(struct event_trigger_data *data,
					   void *rec,
					   struct ring_buffer_event *rbe)
{
	struct live_field *common_pid_field, *syscall_nr_field;
	struct live_accessors *live_accessors = data->private_data;
	u64 common_pid_val, syscall_nr_val;
	u64 vals[1];

	common_pid_field = live_accessors->accessors[0];
	common_pid_val = common_pid_field->fn(common_pid_field->field, rbe, rec);

	syscall_nr_field = live_accessors->accessors[1];
	syscall_nr_val = syscall_nr_field->fn(syscall_nr_field->field, rbe, rec);

	printk("syscalls_sys_enter_sync_event_trigger:\n\tval=%llu (%s)\n\tval=%llu (%s)\n",
	       common_pid_val, common_pid_field->field->name,
	       syscall_nr_val, syscall_nr_field->field->name);
	printk("cpu: %u\n", smp_processor_id());
	print_timestamp(live_accessors->timestamp_accessor, rec, rbe);

	/* now generate our synthetic event */
	vals[0] = common_pid_val;

	generate_synth_event(synctest_event_file, vals, ARRAY_SIZE(vals));
}
#else
static struct live_field *prev_comm_field, *prev_pid_field;
static struct live_field *next_comm_field, *next_pid_field;

static struct trace_event_file *schedtest_event_file;

static int print_all_fields = 0; /* 0 or 1, anything else may livelock */

static void sched_switch_event_trigger(struct event_trigger_data *data,
				       void *rec,
				       struct ring_buffer_event *rbe)
{
	struct live_accessors *live_accessors = data->private_data;
	u64 next_pid_val, next_comm_val;
	struct live_field *field;
	u64 ts_ns, ts_ms;
	unsigned int cpu;
	unsigned int i;
	u64 vals[7];
	u64 val;

	ts_ns = get_timestamp(live_accessors->timestamp_accessor, rec, rbe, true);
	ts_ms = get_timestamp(live_accessors->timestamp_accessor, rec, rbe, false);
	cpu = smp_processor_id();

	for (i = 0; i < MAX_ACCESSORS; i++) {
		field = live_accessors->accessors[i];
		if (!field)
			break;

		val = field->fn(field->field, rbe, rec);

		if (print_all_fields) {
			if (is_string_field(field->field))
				printk("\tval=%s (%s)\n", (char *)(long)val, field->field->name);
			else
				printk("\tval=%llu (%s)\n", val, field->field->name);
		}
	}

	val = prev_comm_field->fn(prev_comm_field->field, rbe, rec);
	val = prev_pid_field->fn(prev_pid_field->field, rbe, rec);
	next_comm_val = next_comm_field->fn(next_comm_field->field, rbe, rec);
	next_pid_val = next_pid_field->fn(next_pid_field->field, rbe, rec);

	if (print_all_fields)
		print_all_fields--;

	/* now generate our synthetic event */
	vals[0] = next_pid_val;
	vals[1] = next_comm_val;
	vals[2] = ts_ns;
	vals[3] = ts_ms;
	vals[4] = cpu;
	vals[5] = (u64)"thneed";
	vals[6] = 398;

	generate_synth_event(schedtest_event_file, vals, ARRAY_SIZE(vals));
}
#endif /* SLOW_TEST */

static int event_live_trigger_print(struct seq_file *m,
				    struct event_trigger_ops *ops,
				    struct event_trigger_data *data)
{
	seq_printf(m, "Custom event trigger active (see %s module for details).\n", KBUILD_MODNAME);

	return 0;
}

static int event_live_trigger_init(struct event_trigger_ops *ops,
				   struct event_trigger_data *data)
{
	struct live_accessors *live_accessors = NULL;
	int ret = 0;

	if (!data->ref) {
#ifdef SLOW_TEST
		live_accessors = __create_live_field_accessors("syscalls", "sys_enter_sync");
		if (IS_ERR(live_accessors)) {
			ret = PTR_ERR(live_accessors);
			goto free;
		}

		ret = add_live_field_accessor(live_accessors, "common_pid");
		if (ret)
			goto free;

		ret = add_live_field_accessor(live_accessors, "__syscall_nr");
		if (ret)
			goto free;

		ret = add_live_field_accessor(live_accessors, "cpu");
		if (ret)
			goto free;

		ret = add_live_field_accessor(live_accessors, "comm");
		if (ret)
			goto free;

		ret = add_live_field_accessor(live_accessors, "common_timestamp");
		if (ret)
			goto free;
#else
		live_accessors = create_live_field_accessors("sched", "sched_switch");
		if (IS_ERR(live_accessors)) {
			ret = PTR_ERR(live_accessors);
			goto free;
		}

		ret = add_live_field_accessor(live_accessors, "common_timestamp");
		if (ret)
			goto free;

		prev_pid_field = find_live_field_accessor(live_accessors, "prev_pid");
		if (!prev_pid_field) {
			ret = -EINVAL;
			goto free;
		}

		prev_comm_field = find_live_field_accessor(live_accessors, "prev_comm");
		if (!prev_comm_field) {
			ret = -EINVAL;
			goto free;
		}

		next_pid_field = find_live_field_accessor(live_accessors, "next_pid");
		if (!next_pid_field) {
			ret = -EINVAL;
			goto free;
		}

		next_comm_field = find_live_field_accessor(live_accessors, "next_comm");
		if (!next_comm_field) {
			ret = -EINVAL;
			goto free;
		}

#endif /* SLOW_TEST */

		data->private_data = live_accessors;
	}

	data->ref++;
 out:
	return ret;
 free:
	destroy_live_field_accessors(live_accessors);
	goto out;
}

static void event_live_trigger_free(struct event_trigger_ops *ops,
				    struct event_trigger_data *data)
{
	struct live_accessors *accessors = data->private_data;

	if (WARN_ON_ONCE(data->ref <= 0))
		return;

	data->ref--;
	if (!data->ref) {
		destroy_live_field_accessors(accessors);
		destroy_trigger_data(data);
	}
}

static struct event_trigger_ops event_live_trigger_ops = {
#ifdef SLOW_TEST
	.func			= syscalls_sys_enter_sync_event_trigger,
#else
	.func			= sched_switch_event_trigger,
#endif /* SLOW_TEST */
	.print			= event_live_trigger_print,
	.init			= event_live_trigger_init,
	.free			= event_live_trigger_free,
};

static int __init trace_event_live_test_init(void)
{
	struct trace_array *tr = top_trace_array();
	struct event_trigger_data *data = NULL;
	struct synth_event *se;
	int ret = 0;

#ifdef SLOW_TEST
	se = create_empty_synth_event("synctest");
	if (IS_ERR(se))
		return PTR_ERR(se);

	ret = add_synth_field(se, "int", "intfield");
	if (ret)
		goto free;

	ret = finalize_synth_event(se);
	if (ret)
		goto free;

	synctest_event_file = find_event_file(tr, "synthetic", "synctest");
	if (!synctest_event_file)
		goto free;

	data = create_live_handler("syscalls", "sys_enter_sync",
				   &event_live_trigger_ops);
#else
	se = create_empty_synth_event("schedtest");
	if (IS_ERR(se))
		return PTR_ERR(se);

	ret = add_synth_field(se, "pid_t", "next_pid_field");
	if (ret)
		goto free;

	ret = add_synth_field(se, "char[16]", "next_comm_field");
	if (ret)
		goto free;

	ret = add_synth_field(se, "u64", "ts_ns");
	if (ret)
		goto free;

	ret = add_synth_field(se, "u64", "ts_ms");
	if (ret)
		goto free;

	ret = add_synth_field(se, "unsigned int", "cpu");
	if (ret)
		goto free;

	ret = add_synth_field(se, "char[64]", "my_string_field");
	if (ret)
		goto free;

	ret = add_synth_field(se, "int", "my_int_field");
	if (ret)
		goto free;

	ret = finalize_synth_event(se);
	if (ret)
		goto free;

	schedtest_event_file = find_event_file(tr, "synthetic", "schedtest");
	if (!schedtest_event_file)
		goto free;
	data = create_live_handler("sched", "sched_switch",
				   &event_live_trigger_ops);
#endif /* SLOW_TEST */
	if (IS_ERR(data)) {
		ret = PTR_ERR(data);
		goto free;
	}
	trigger_data = data;
 out:
	return ret;
 free:
	destroy_trigger_data(data);
	free_synth_event(se);

	goto out;
}

static void __exit trace_event_live_test_exit(void)
{
#ifdef SLOW_TEST
	destroy_live_handler("syscalls", "sys_enter_sync", trigger_data);
	delete_synth_event("synctest");
#else
	destroy_live_handler("sched", "sched_switch", trigger_data);
	delete_synth_event("schedtest");
#endif /* SLOW_TEST */

	return;
}

module_init(trace_event_live_test_init)
module_exit(trace_event_live_test_exit)

MODULE_AUTHOR("Tom Zanussi");
MODULE_DESCRIPTION("live tracing test");
MODULE_LICENSE("GPL v2");
