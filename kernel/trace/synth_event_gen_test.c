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
 * synthetic event creation and generation, the first and second tests
 * using gen_synth_cmd() and add_synth_field(), the third uses
 * create_synth_event() to do it all at once with a static field
 * array.
 *
 * Following that are a few examples using the created events to test
 * various ways of tracing a synthetic event.
 *
 * To test, select CONFIG_SYNTH_EVENT_GEN_TEST and build the module.
 * Then:
 *
 * # insmod kernel/trace/synth_event_gen_test.ko
 * # cat /sys/kernel/debug/tracing/trace
 *
 * You should see several events in the trace buffer -
 * "create_synth_test", "empty_synth_test", and several instances of
 * "gen_synth_test".
 *
 * To remove the events, remove the module:
 *
 * # rmmod synth_event_gen_test
 *
 */

static struct trace_event_file *create_synth_test;
static struct trace_event_file *empty_synth_test;
static struct trace_event_file *gen_synth_test;

/*
 * Test to make sure we can create a synthetic event, then add more
 * fields.
 */
static int __init test_gen_synth_cmd(void)
{
	struct dynevent_cmd cmd;
	u64 vals[7];
	char *buf;
	int ret;

	/* Create a buffer to hold the generated command */
	buf = kzalloc(MAX_DYNEVENT_CMD_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Before generating the command, initialize the cmd object */
	synth_dynevent_cmd_init(&cmd, buf, MAX_DYNEVENT_CMD_LEN);

	/*
	 * Create the empty gen_synth_test synthetic event with the
	 * first 4 fields.
	 */
	ret = gen_synth_cmd(&cmd, "gen_synth_test", THIS_MODULE,
			    "pid_t", "next_pid_field",
			    "char[16]", "next_comm_field",
			    "u64", "ts_ns",
			    "u64", "ts_ms");
	if (ret)
		goto free;

	/* Use add_synth_field to add the rest of the fields */

	ret = add_synth_field(&cmd, "unsigned int", "cpu");
	if (ret)
		goto free;

	ret = add_synth_field(&cmd, "char[64]", "my_string_field");
	if (ret)
		goto free;

	ret = add_synth_field(&cmd, "int", "my_int_field");
	if (ret)
		goto free;

	ret = create_dynevent(&cmd);
	if (ret)
		goto free;

	/*
	 * Now get the gen_synth_test event file.  We need to prevent
	 * the instance and event from disappearing from underneath
	 * us, which get_event_file() does (though in this case we're
	 * using the top-level instance which never goes away).
	 */
	gen_synth_test = get_event_file(NULL, "synthetic", "gen_synth_test");
	if (IS_ERR(gen_synth_test)) {
		ret = PTR_ERR(gen_synth_test);
		goto delete;
	}

	/* Enable the event or you won't see anything */
	ret = trace_array_set_clr_event(gen_synth_test->tr,
					"synthetic", "gen_synth_test", true);
	if (ret) {
		put_event_file(gen_synth_test);
		goto delete;
	}

	/* Create some bogus values just for testing */

	vals[0] = 777;			/* next_pid_field */
	vals[1] = (u64)"hula hoops";	/* next_comm_field */
	vals[2] = 1000000;		/* ts_ns */
	vals[3] = 1000;			/* ts_ms */
	vals[4] = smp_processor_id();	/* cpu */
	vals[5] = (u64)"thneed";	/* my_string_field */
	vals[6] = 598;			/* my_int_field */

	/* Now generate a gen_synth_test event */
	ret = trace_synth_event_array(gen_synth_test, vals, ARRAY_SIZE(vals));
 out:
	return ret;
 delete:
	/* We got an error after creating the event, delete it */
	delete_synth_event("gen_synth_test");
 free:
	kfree(buf);

	goto out;
}

/*
 * Test to make sure we can create an initially empty synthetic event,
 * then add all the fields.
 */
static int __init test_empty_synth_event(void)
{
	struct dynevent_cmd cmd;
	u64 vals[7];
	char *buf;
	int ret;

	/* Create a buffer to hold the generated command */
	buf = kzalloc(MAX_DYNEVENT_CMD_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Before generating the command, initialize the cmd object */
	synth_dynevent_cmd_init(&cmd, buf, MAX_DYNEVENT_CMD_LEN);

	/*
	 * Create the empty_synth_test synthetic event with no fields.
	 */
	ret = gen_synth_cmd(&cmd, "empty_synth_test", THIS_MODULE);
	if (ret)
		goto free;

	/* Use add_synth_field to add all of the fields */

	ret = add_synth_field(&cmd, "pid_t", "next_pid_field");
	if (ret)
		goto free;

	ret = add_synth_field(&cmd, "char[16]", "next_comm_field");
	if (ret)
		goto free;

	ret = add_synth_field(&cmd, "u64", "ts_ns");
	if (ret)
		goto free;

	ret = add_synth_field(&cmd, "u64", "ts_ms");
	if (ret)
		goto free;

	ret = add_synth_field(&cmd, "unsigned int", "cpu");
	if (ret)
		goto free;

	ret = add_synth_field(&cmd, "char[64]", "my_string_field");
	if (ret)
		goto free;

	ret = add_synth_field(&cmd, "int", "my_int_field");
	if (ret)
		goto free;

	/* All fields have been added, close and register the synth event */

	ret = create_dynevent(&cmd);
	if (ret)
		goto free;

	/*
	 * Now get the empty_synth_test event file.  We need to prevent
	 * the instance and event from disappearing from underneath
	 * us, which get_event_file() does (though in this case we're
	 * using the top-level instance which never goes away).
	 */
	empty_synth_test = get_event_file(NULL, "synthetic", "empty_synth_test");
	if (IS_ERR(empty_synth_test)) {
		ret = PTR_ERR(empty_synth_test);
		goto delete;
	}

	/* Enable the event or you won't see anything */
	ret = trace_array_set_clr_event(empty_synth_test->tr,
					"synthetic", "empty_synth_test", true);
	if (ret) {
		put_event_file(empty_synth_test);
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

	/* Now generate an empty_synth_test event */
	ret = trace_synth_event_array(empty_synth_test, vals, ARRAY_SIZE(vals));
 out:
	return ret;
 delete:
	/* We got an error after creating the event, delete it */
	delete_synth_event("empty_synth_test");
 free:
	kfree(buf);

	goto out;
}

static struct synth_field_desc create_synth_test_fields[] = {
	{ .type = "pid_t",		.name = "next_pid_field" },
	{ .type = "char[16]",		.name = "next_comm_field" },
	{ .type = "u64",		.name = "ts_ns" },
	{ .type = "u64",		.name = "ts_ms" },
	{ .type = "unsigned int",	.name = "cpu" },
	{ .type = "char[64]",		.name = "my_string_field" },
	{ .type = "int",		.name = "my_int_field" },
};

/*
 * Test synthetic event creation all at once from array of field
 * descriptors.
 */
static int __init test_create_synth_event(void)
{
	u64 vals[7];
	int ret;

	/* Create the create_synth_test event with the fields above */
	ret = create_synth_event("create_synth_test",
				 create_synth_test_fields,
				 ARRAY_SIZE(create_synth_test_fields),
				 THIS_MODULE);
	if (ret)
		goto out;

	/*
	 * Now get the create_synth_test event file.  We need to
	 * prevent the instance and event from disappearing from
	 * underneath us, which get_event_file() does (though in this
	 * case we're using the top-level instance which never goes
	 * away).
	 */
	create_synth_test = get_event_file(NULL, "synthetic",
					   "create_synth_test");
	if (IS_ERR(create_synth_test)) {
		ret = PTR_ERR(create_synth_test);
		goto delete;
	}

	/* Enable the event or you won't see anything */
	ret = trace_array_set_clr_event(create_synth_test->tr,
					"synthetic", "create_synth_test", true);
	if (ret) {
		put_event_file(create_synth_test);
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

	/* Now generate a create_synth_test event */
	ret = trace_synth_event_array(create_synth_test, vals, ARRAY_SIZE(vals));
 out:
	return ret;
 delete:
	/* We got an error after creating the event, delete it */
	ret = delete_synth_event("create_synth_test");

	goto out;
}

/*
 * Test tracing a synthetic event by reserving trace buffer space,
 * then filling in fields one after another.
 */
static int __init test_add_next_synth_val(void)
{
	struct synth_gen_state gen_state;
	int ret;

	/* Start by reserving space in the trace buffer */
	ret = trace_synth_event_start(gen_synth_test, &gen_state);
	if (ret)
		return ret;

	/* Write some bogus values into the trace buffer, one after another */

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
	/* Finally, commit the event */
	ret = trace_synth_event_end(&gen_state);

	return ret;
}

/*
 * Test tracing a synthetic event by reserving trace buffer space,
 * then filling in fields using field names, which can be done in any
 * order.
 */
static int __init test_add_synth_val(void)
{
	struct synth_gen_state gen_state;
	int ret;

	/* Start by reserving space in the trace buffer */
	ret = trace_synth_event_start(gen_synth_test, &gen_state);
	if (ret)
		return ret;

	/* Write some bogus values into the trace buffer, using field names */

	ret = add_synth_val("ts_ns", 1000000, &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("ts_ms", 1000, &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("cpu", smp_processor_id(), &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("next_pid_field", 777, &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("next_comm_field", (u64)"silly putty", &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("my_string_field", (u64)"thneed_9", &gen_state);
	if (ret)
		goto out;

	ret = add_synth_val("my_int_field", 3999, &gen_state);
 out:
	/* Finally, commit the event */
	ret = trace_synth_event_end(&gen_state);

	return ret;
}

/*
 * Test tracing a synthetic event all at once from array of values.
 */
static int __init test_trace_synth_event(void)
{
	int ret;

	/* Trace some bogus values just for testing */
	ret = trace_synth_event(create_synth_test, 7, /* number of values */
				444,		/* next_pid_field */
				(u64)"clackers",	/* next_comm_field */
				1000000,		/* ts_ns */
				1000,		/* ts_ms */
				smp_processor_id(),/* cpu */
				(u64)"Thneed",	/* my_string_field */
				999);		/* my_int_field */
	return ret;
}

static int __init synth_event_gen_test_init(void)
{
	int ret;

	ret = test_gen_synth_cmd();
	if (ret)
		return ret;

	ret = test_empty_synth_event();
	if (ret) {
		WARN_ON(trace_array_set_clr_event(gen_synth_test->tr,
						  "synthetic",
						  "gen_synth_test", false));
		put_event_file(gen_synth_test);
		WARN_ON(delete_synth_event("gen_synth_test"));
		goto out;
	}

	ret = test_create_synth_event();
	if (ret) {
		WARN_ON(trace_array_set_clr_event(gen_synth_test->tr,
						  "synthetic",
						  "gen_synth_test", false));
		put_event_file(gen_synth_test);
		WARN_ON(delete_synth_event("gen_synth_test"));

		WARN_ON(trace_array_set_clr_event(empty_synth_test->tr,
						  "synthetic",
						  "empty_synth_test", false));
		put_event_file(empty_synth_test);
		WARN_ON(delete_synth_event("empty_synth_test"));
		goto out;
	}

	ret = test_add_next_synth_val();
	WARN_ON(ret);

	ret = test_add_synth_val();
	WARN_ON(ret);

	ret = test_trace_synth_event();
	WARN_ON(ret);
 out:
	return ret;
}

static void __exit synth_event_gen_test_exit(void)
{
	/* Disable the event or you can't remove it */
	WARN_ON(trace_array_set_clr_event(gen_synth_test->tr,
					  "synthetic",
					  "gen_synth_test", false));

	/* Now give the file and instance back */
	put_event_file(gen_synth_test);

	/* Now unregister and free the synthetic event */
	WARN_ON(delete_synth_event("gen_synth_test"));

	/* Disable the event or you can't remove it */
	WARN_ON(trace_array_set_clr_event(empty_synth_test->tr,
					  "synthetic",
					  "empty_synth_test", false));

	/* Now give the file and instance back */
	put_event_file(empty_synth_test);

	/* Now unregister and free the synthetic event */
	WARN_ON(delete_synth_event("empty_synth_test"));

	/* Disable the event or you can't remove it */
	WARN_ON(trace_array_set_clr_event(create_synth_test->tr,
					  "synthetic",
					  "create_synth_test", false));

	/* Now give the file and instance back */
	put_event_file(create_synth_test);

	/* Now unregister and free the synthetic event */
	WARN_ON(delete_synth_event("create_synth_test"));
}

module_init(synth_event_gen_test_init)
module_exit(synth_event_gen_test_exit)

MODULE_AUTHOR("Tom Zanussi");
MODULE_DESCRIPTION("synthetic event generation test");
MODULE_LICENSE("GPL v2");
