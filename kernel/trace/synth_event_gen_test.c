// SPDX-License-Identifier: GPL-2.0
/*
 * Test module for in-kernel sythetic event creation and generation.
 *
 * Copyright (C) 2019 Tom Zanussi <zanussi@kernel.org>
 */

#include <linux/module.h>
#include <linux/trace_events.h>
#include "trace.h"

/*
 * This module is a simple test of basic functionality for in-kernel
 * synthetic event creation and generation, the first test using
 * create_synth_event() with a static field array and a version that
 * builds the same thing dynamically using create_empty_synth_event(),
 * add_synth_field(s), and finalize_synth_event().
 *
 * To test, select CONFIG_SYNTH_EVENT_GEN_TEST and build the module.
 * Then:
 *
 * # insmod kernel/trace/synth_event_gen_test.ko
 * # cat /sys/kernel/debug/tracing/trace
 *
 * You should see two events in the trace buffer - "synthtest" and
 * "dyn_synthtest".
 *
 * To remove the events, remove the module:
 *
 * # rmmod synth_event_gen_test
 *
 */

static struct synth_field_desc synthtest_fields[] = {
	{ .type = "pid_t",		.name = "next_pid_field" },
	{ .type = "char[16]",		.name = "next_comm_field" },
	{ .type = "u64",		.name = "ts_ns" },
	{ .type = "u64",		.name = "ts_ms" },
	{ .type = "unsigned int",	.name = "cpu" },
	{ .type = "char[64]",		.name = "my_string_field" },
	{ .type = "int",		.name = "my_int_field" },
};

static struct trace_event_file *synthtest_event_file;
static struct trace_event_file *dyn_synthtest_event_file;

static int __init test_synth_event(void)
{
	u64 vals[7];
	int ret;

	/* Create the synthtest synthetic event with the fields above */
	ret = create_synth_event("synthtest", synthtest_fields,
				 ARRAY_SIZE(synthtest_fields), THIS_MODULE);
	if (ret)
		goto out;

	/*
	 * Now get the synthtest event file.  We need to prevent the
	 * instance and event from disappearing from underneath us,
	 * which get_event_file() does (though in this case we're
	 * using the top-level instance which never goes away).
	 */
	synthtest_event_file = get_event_file(NULL, "synthetic", "synthtest");
	if (IS_ERR(synthtest_event_file)) {
		ret = PTR_ERR(synthtest_event_file);
		goto delete;
	}

	/* Enable the event or you won't see anything */
	ret = trace_array_set_clr_event(synthtest_event_file->tr,
					"synthetic", "synthtest", true);
	if (ret) {
		put_event_file(synthtest_event_file);
		goto delete;
	}

	/* Create some bogus values just for testing */

	vals[0] = 777;			/* next_pid_field */
	vals[1] = (u64)"tiddlywinks";	/* next_comm_field */
	vals[2] = 1000000;		/* ts_ns */
	vals[3] = 1000;			/* ts_ms */
	vals[4] = smp_processor_id();	/* cpu */
	vals[5] = (u64)"thneed";	/* my_string_field */
	vals[6] = 398;			/* my_int_field */

	/* Now generate the event */
	ret = generate_synth_event(synthtest_event_file, vals,
				   ARRAY_SIZE(vals));
 out:
	return ret;
 delete:
	ret = delete_synth_event("synthtest");

	goto out;
}

static int __init test_dynamic_synth_event(void)
{
	struct synth_event *se = NULL;
	u64 vals[7];
	int ret;

	/* Create the empty synthtest synthetic event */
	se = create_empty_synth_event("dyn_synthtest", THIS_MODULE);
	if (IS_ERR(se)) {
		ret = PTR_ERR(se);
		goto free;
	}

	/* Use add_synth_fields to add the first 4 synthtest fields */
	ret = add_synth_fields(se, synthtest_fields, 4);
	if (ret)
		goto out;

	/* Use add_synth_field to add the rest of the fields */

	ret = add_synth_field(se, "unsigned int", "cpu");
	if (ret)
		goto free;

	ret = add_synth_field(se, "char[64]", "my_string_field");
	if (ret)
		goto free;

	ret = add_synth_field(se, "int", "my_int_field");
	if (ret)
		goto free;

	/* All fields have been added, close and register the synth event */
	ret = finalize_synth_event(se);
	if (ret)
		goto free;

	/*
	 * Now get the dyn_synthtest event file.  We need to prevent
	 * the instance and event from disappearing from underneath
	 * us, which get_event_file() does (though in this case we're
	 * using the top-level instance which never goes away).
	 */
	dyn_synthtest_event_file = get_event_file(NULL, "synthetic",
						  "dyn_synthtest");
	if (IS_ERR(dyn_synthtest_event_file)) {
		ret = PTR_ERR(dyn_synthtest_event_file);
		goto delete;
	}

	/* Enable the event or you won't see anything */
	ret = trace_array_set_clr_event(dyn_synthtest_event_file->tr,
					"synthetic", "dyn_synthtest", true);
	if (ret) {
		put_event_file(dyn_synthtest_event_file);
		goto delete;
	}

	/* Create some bogus values just for testing */

	vals[0] = 777;			/* next_pid_field */
	vals[1] = (u64)"tiddlywinks";	/* next_comm_field */
	vals[2] = 1000000;		/* ts_ns */
	vals[3] = 1000;			/* ts_ms */
	vals[4] = smp_processor_id();	/* cpu */
	vals[5] = (u64)"thneed_2.0";	/* my_string_field */
	vals[6] = 399;			/* my_int_field */

	/* Now generate the event */
	ret = generate_synth_event(dyn_synthtest_event_file, vals,
				   ARRAY_SIZE(vals));
 out:
	return ret;
 free: /* If not finalized, just free */
	free_synth_event(se);
	goto out;
 delete: /* Event was finalized, delete */
	delete_synth_event("dyn_synthtest");
	goto out;
}

static int __init test_add_next_synth_val(void)
{
	struct synth_gen_state gen_state;
	int ret;

	ret = generate_synth_event_start(synthtest_event_file, &gen_state);
	if (ret)
		return ret;

	/* Add some bogus values just for testing */

	/* next_pid_field */
	ret = add_next_synth_val(777, &gen_state);
	if (ret)
		goto out;

	/* next_comm_field */
	ret = add_next_synth_val((u64)"slinky", &gen_state);
	if (ret)
		goto out;

	/* ts_ns */
	ret = add_next_synth_val(1000000, &gen_state);
	if (ret)
		goto out;

	/* ts_ms */
	ret = add_next_synth_val(1000, &gen_state);
	if (ret)
		goto out;

	/* cpu */
	ret = add_next_synth_val(smp_processor_id(), &gen_state);
	if (ret)
		goto out;

	/* my_string_field */
	ret = add_next_synth_val((u64)"thneed_2.01", &gen_state);
	if (ret)
		goto out;

	/* my_int_field */
	ret = add_next_synth_val(395, &gen_state);
 out:
	/* Now generate or at least finalize the event */
	ret = generate_synth_event_end(&gen_state);

	return ret;
}

static int __init test_add_synth_val(void)
{
	struct synth_gen_state gen_state;
	int ret;

	ret = generate_synth_event_start(synthtest_event_file, &gen_state);
	if (ret)
		return ret;

	/* Add some bogus values just for testing */

	ret = add_synth_val("next_pid_field", 777, &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("next_comm_field", (u64)"silly putty", &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("ts_ns", 1000000, &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("ts_ms", 1000, &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("cpu", smp_processor_id(), &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("my_string_field", (u64)"thneed_9", &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("my_int_field", 3999, &gen_state);
 out:
	/* Now generate or at least finalize the event */
	ret = generate_synth_event_end(&gen_state);

	return ret;
}

static int __init synth_event_gen_test_init(void)
{
	int ret;

	ret = test_synth_event();
	if (ret)
		return ret;

	ret = test_dynamic_synth_event();
	if (ret) {
		WARN_ON(trace_array_set_clr_event(synthtest_event_file->tr,
						  "synthetic",
						  "synthtest", false));
		put_event_file(synthtest_event_file);
		WARN_ON(delete_synth_event("synthtest"));
		goto out;
	}

	ret = test_add_next_synth_val();
	WARN_ON(ret);

	ret = test_add_synth_val();
	WARN_ON(ret);
 out:
	return ret;
}

static void __exit synth_event_gen_test_exit(void)
{
	/* Disable the event or you can't remove it */
	WARN_ON(trace_array_set_clr_event(dyn_synthtest_event_file->tr,
					  "synthetic",
					  "dyn_synthtest", false));

	/* Now give the file and instance back */
	put_event_file(dyn_synthtest_event_file);

	/* Now unregister and free the synthetic event */
	WARN_ON(delete_synth_event("dyn_synthtest"));

	/* Disable the event or you can't remove it */
	WARN_ON(trace_array_set_clr_event(synthtest_event_file->tr,
					  "synthetic",
					  "synthtest", false));

	/* Now give the file and instance back */
	put_event_file(synthtest_event_file);

	/* Now unregister and free the synthetic event */
	WARN_ON(delete_synth_event("synthtest"));
}

module_init(synth_event_gen_test_init)
module_exit(synth_event_gen_test_exit)

MODULE_AUTHOR("Tom Zanussi");
MODULE_DESCRIPTION("synthetic event generation test");
MODULE_LICENSE("GPL v2");
