// SPDX-License-Identifier: GPL-2.0
/*
 * trace_events_live - trace event live tracing support
 *
 * Copyright (C) 2019 Tom Zanussi <zanussi@kernel.org>
 */

#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/rculist.h>
#include <linux/tracefs.h>
#include <linux/trace_events.h>

#include "tracing_map.h"
#include "trace.h"
#include "trace_dynevent.h"

static u64 live_field_string(struct ftrace_event_field *field,
			     struct ring_buffer_event *rbe,
			     void *event)
{
	char *addr = (char *)(event + field->offset);

	return (u64)(unsigned long)addr;
}

static u64 live_field_dynstring(struct ftrace_event_field *field,
				struct ring_buffer_event *rbe,
				void *event)
{
	u32 str_item = *(u32 *)(event + field->offset);
	int str_loc = str_item & 0xffff;
	char *addr = (char *)(event + str_loc);

	return (u64)(unsigned long)addr;
}

static u64 live_field_pstring(struct ftrace_event_field *field,
			      struct ring_buffer_event *rbe,
			      void *event)
{
	char **addr = (char **)(event + field->offset);

	return (u64)(unsigned long)*addr;
}

#define DEFINE_LIVE_FIELD_FN(type)					\
	static u64 live_field_##type(struct ftrace_event_field *field,	\
				     struct ring_buffer_event *rbe,	\
				     void *event)			\
{									\
	type *addr = (type *)(event + field->offset);			\
									\
	return (u64)(unsigned long)*addr;				\
}

DEFINE_LIVE_FIELD_FN(s64);
DEFINE_LIVE_FIELD_FN(u64);
DEFINE_LIVE_FIELD_FN(s32);
DEFINE_LIVE_FIELD_FN(u32);
DEFINE_LIVE_FIELD_FN(s16);
DEFINE_LIVE_FIELD_FN(u16);
DEFINE_LIVE_FIELD_FN(s8);
DEFINE_LIVE_FIELD_FN(u8);

static u64 live_field_cpu(struct ftrace_event_field *field,
			  struct ring_buffer_event *rbe,
			  void *event)
{
	int cpu = raw_smp_processor_id();

	return cpu;
}

static u64 live_field_timestamp(struct ftrace_event_field *field,
				struct ring_buffer_event *rbe,
				void *event)
{
	u64 ts = ring_buffer_event_time_stamp(rbe);

	return ts;
}

static live_field_fn_t select_value_fn(int field_size, int field_is_signed)
{
	live_field_fn_t fn = NULL;

	switch (field_size) {
	case 8:
		if (field_is_signed)
			fn = live_field_s64;
		else
			fn = live_field_u64;
		break;
	case 4:
		if (field_is_signed)
			fn = live_field_s32;
		else
			fn = live_field_u32;
		break;
	case 2:
		if (field_is_signed)
			fn = live_field_s16;
		else
			fn = live_field_u16;
		break;
	case 1:
		if (field_is_signed)
			fn = live_field_s8;
		else
			fn = live_field_u8;
		break;
	}

	return fn;
}

static live_field_fn_t find_accessor_fn(struct ftrace_event_field *field)
{
	live_field_fn_t fn;

	if (is_string_field(field)) {
		if (field->filter_type == FILTER_STATIC_STRING)
			fn = live_field_string;
		else if (field->filter_type == FILTER_DYN_STRING)
			fn = live_field_dynstring;
		else
			fn = live_field_pstring;
	} else if (field->filter_type == FILTER_CPU)
		fn = live_field_cpu;
	else
		fn = select_value_fn(field->size, field->is_signed);

	return fn;
}

static struct live_field *
__create_timestamp_field_accessor(struct trace_event_file *file)
{
	struct live_field *live_field = NULL;
	int ret;

	live_field = kzalloc(sizeof(*live_field), GFP_KERNEL);
	if (!live_field) {
		live_field = ERR_PTR(-ENOMEM);
		goto out;
	}

	live_field->fn = live_field_timestamp;

	ret = tracing_set_clock(file->tr, "global");
	if (ret) {
		live_field = ERR_PTR(ret);
		goto out;
	}

	tracing_set_time_stamp_abs(file->tr, true);
 out:
	return live_field;
}

static void
__destroy_timestamp_field_accessor(struct trace_event_file *file,
				   struct live_field *live_field)
{
	tracing_set_time_stamp_abs(file->tr, false);

	kfree(live_field);
}

static struct live_field *
__create_live_field_accessor(struct trace_event_file *file, char *field_name)
{
	struct ftrace_event_field *field = NULL;
	struct live_field *live_field = NULL;
	live_field_fn_t fn;

	live_field = kzalloc(sizeof(*live_field), GFP_KERNEL);
	if (!live_field) {
		live_field = ERR_PTR(-ENOMEM);
		goto out;
	}

	field = trace_find_event_field(file->event_call, field_name);
	if (!field) {
		live_field = ERR_PTR(-EINVAL);
		goto out;
	}

	fn = find_accessor_fn(field);
	if (!fn) {
		live_field = ERR_PTR(-EINVAL);
		goto out;
	}

	live_field->field = field;
	live_field->fn = fn;
 out:
	return live_field;
}

/**
 * generate_synth_event - Generate a synthetic event
 * @file: The trace_event_file representing the synthetic event
 * @vals: Array of values
 * @n_vals: The number of values in vals
 *
 * Generate a synthetic event using the values passed in as 'vals'.
 *
 * The 'vals' array is just an array of 'n_vals' u64.  The number of
 * vals must match the number of field in the synthetic event, and
 * must be in the same order as the synthetic event fields.
 *
 * All vals should be cast to u64, and string vals are just pointers
 * to strings, cast to u64.  Strings will be copied into space
 * reserved in the event for the string, using these pointers.
 *
 * Return: 0 on success, err otherwise.
 */
int generate_synth_event(struct trace_event_file *file, u64 *vals,
			 unsigned int n_vals)
{
	struct trace_event_buffer fbuffer;
	struct synth_trace_event *entry;
	struct ring_buffer *buffer;
	struct synth_event *event;
	unsigned int i, n_u64;
	int fields_size = 0;
	int ret = 0;

	event = file->event_call->data;

	if (n_vals != event->n_fields)
		return -EINVAL;

	if (trace_trigger_soft_disabled(file))
		return -EINVAL;

	fields_size = event->n_u64 * sizeof(u64);

	/*
	 * Avoid ring buffer recursion detection, as this event
	 * is being performed within another event.
	 */
	buffer = file->tr->trace_buffer.buffer;
	ring_buffer_nest_start(buffer);

	entry = trace_event_buffer_reserve(&fbuffer, file,
					   sizeof(*entry) + fields_size);
	if (!entry) {
		ret = -EINVAL;
		goto out;
	}

	for (i = 0, n_u64 = 0; i < event->n_fields; i++) {
		if (event->fields[i]->is_string) {
			char *str_val = (char *)(long)vals[i];
			char *str_field = (char *)&entry->fields[n_u64];

			strscpy(str_field, str_val, STR_VAR_LEN_MAX);
			n_u64 += STR_VAR_LEN_MAX / sizeof(u64);
		} else {
			entry->fields[n_u64] = vals[i];
			n_u64++;
		}
	}

	trace_event_buffer_commit(&fbuffer);
out:
	ring_buffer_nest_end(buffer);

	return ret;
}
EXPORT_SYMBOL_GPL(generate_synth_event);

static struct event_trigger_data *
register_live_handler(struct trace_event_file *file,
		      struct event_trigger_ops *ops)
{
	struct event_trigger_data *data = NULL;
	struct event_command *cmd = NULL;
	int ret = 0;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto free;
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto free;
	}

	cmd->trigger_type = ETT_USER;
	cmd->flags = EVENT_CMD_FL_NEEDS_REC;
	data->cmd_ops = cmd;
	data->ops = ops;

	mutex_lock(&event_mutex);
	ret = register_trigger(NULL, ops, data, file);
	mutex_unlock(&event_mutex);
	if (ret <= 0) /* register_trigger returns # registered */
		goto free;
 out:
	return data;
 free:
	kfree(cmd);
	kfree(data);

	data = ERR_PTR(ret);

	goto out;
}

void destroy_trigger_data(void *trigger_data)
{
	struct event_trigger_data *data = trigger_data;
	struct event_command *cmd_ops;

	if (!data)
		return;

	cmd_ops = data->cmd_ops;
	trigger_data_free(data);
	kfree(cmd_ops);
}
EXPORT_SYMBOL_GPL(destroy_trigger_data);

/**
 * create_live_handler - Create an in-kernel handler for a trace event
 * @subsys_name: The subsystem name of the trace event
 * @event_name: The name of the trace event
 * @trigger_ops: The event_trigger_ops used to call the handler
 *
 * Allocate and initialize a new event trigger handler for an existing
 * trace event.
 *
 * The handler function that will be called for each event hit should
 * be embedded in the event_trigger_ops object passed in
 * (event_trigger_ops.func) along with the other event_trigger_ops
 * fields.
 *
 * When the handler function is called on each event hit, an array of
 * live_field 'accessors' is passed in as the private_data of the
 * event_trigger_data passed to the handler.  A given event field's
 * data can be accessed via the live_field struct pointer
 * corresponding to that field, specifically by calling the
 * live_field->fn() member, which will return the field data as a u64
 * that can be cast into the actual field type.
 *
 * The event_trigger_ops.init function can be used to create the field
 * accessors that will be passed in to the handler.  The
 * create_live_field_accessors() function can be used to create them
 * all automatically and/or add_live_field_accessor() can be used to
 * add them manually.
 *
 * The handler should be removed using destroy_live_handler().
 *
 * Return: A pointer to the event_trigger_data for use in further
 *         calls, ERR_PTR otherwise.
 */
void *create_live_handler(char *subsys_name,
			  char *event_name,
			  void *trigger_ops)
{
	struct trace_array *tr = top_trace_array();
	struct event_trigger_data *data;
	struct trace_event_file *file;

	file = event_file(tr, subsys_name, event_name);
	if (IS_ERR(file)) {
		data = ERR_PTR(PTR_ERR(file)); // zzzz
		goto out;
	}

	data = register_live_handler(file, trigger_ops);
 out:
	return data;
}
EXPORT_SYMBOL_GPL(create_live_handler);

void destroy_live_handler(char *subsys_name,
			  char *event_name,
			  void *data)
{
	struct trace_array *tr = top_trace_array();
	struct trace_event_file *file;

	file = event_file(tr, subsys_name, event_name);
	if (IS_ERR(file))
		return;

	mutex_lock(&event_mutex);
	unregister_trigger(NULL, NULL, data, file);
	mutex_unlock(&event_mutex);
}
EXPORT_SYMBOL_GPL(destroy_live_handler);

/**
 * add_live_field_accessor - Add a new field accessor
 * @live_accessors: The live_accessors object to add the accessor to
 * @field_name: The name of the field to add a live accessor for
 *
 * Create and add a live field accessor for the specified field in a
 * trace event.
 *
 * A given field's accessor can be retrieved using
 * find_live_field_accessor() with the field's name.
 *
 * All field accessors should be destroyed together using
 * destroy_live_field_accessors().
 *
 * Return: 0 on success, error otherwise.
 */
int add_live_field_accessor(struct live_accessors *live_accessors,
			    char *field_name)
{
	struct live_field *live_field = NULL;
	struct trace_event_file *file;
	int ret = 0;

	if (live_accessors->n >= MAX_ACCESSORS) {
		ret = -EINVAL;
		goto out;
	}

	file = live_accessors->file;

	if (strcmp(field_name, "common_timestamp") == 0) {
		live_field = __create_timestamp_field_accessor(file);
		if (IS_ERR(live_field))
			ret = PTR_ERR(live_field);
		else
			live_accessors->timestamp_accessor = live_field;
		goto out;
	}

	live_field = __create_live_field_accessor(file, field_name);
	if (IS_ERR(live_field)) {
		ret = PTR_ERR(live_field);
		goto out;
	}

	live_accessors->accessors[live_accessors->n++] = live_field;
 out:
	return ret;
}
EXPORT_SYMBOL_GPL(add_live_field_accessor);

static void destroy_live_field_accessor(struct live_field *live_field)
{
	kfree(live_field);
}

void destroy_live_field_accessors(struct live_accessors *live_accessors)
{
	unsigned int i;

	if (!live_accessors)
		return;

	for (i = 0; i < MAX_ACCESSORS; i++)
		destroy_live_field_accessor(live_accessors->accessors[i]);

	if (live_accessors->timestamp_accessor)
		__destroy_timestamp_field_accessor(live_accessors->file,
						   live_accessors->timestamp_accessor);

	kfree(live_accessors);
}
EXPORT_SYMBOL_GPL(destroy_live_field_accessors);

/**
 * find_live_field_accessor - Find a field accessor
 * @live_accessors: The live_accessors object to search
 * @field_name: The name of the field to find a live accessor for
 *
 * Find the live field accessor corresponding to the specified field
 * in a trace event.
 *
 * Return: the live_field object on success, NULL otherwise.
 */
struct live_field *find_live_field_accessor(struct live_accessors *live_accessors,
					    char *field_name)
{
	struct live_field *field = NULL;
	unsigned int i;

	for (i = 0; i < MAX_ACCESSORS; i++) {
		field = live_accessors->accessors[i];
		if (!field)
			break;

		if (strcmp(field->field->name, field_name) == 0)
			break;

		field = NULL;
	}

	return field;
}
EXPORT_SYMBOL_GPL(find_live_field_accessor);

struct live_accessors *__create_live_field_accessors(char *subsys_name,
						     char *event_name)
{
	struct trace_array *tr = top_trace_array();
	struct live_accessors *live_accessors;
	struct trace_event_file *file;
	int ret = 0;

	live_accessors = kzalloc(sizeof(*live_accessors), GFP_KERNEL);
	if (!live_accessors) {
		ret = -ENOMEM;
		goto out;
	}

	file = event_file(tr, subsys_name, event_name);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto free;
	}

	live_accessors->file = file;
 out:
	return live_accessors;
 free:
	destroy_live_field_accessors(live_accessors);
	live_accessors = ERR_PTR(ret);
	goto out;
}
EXPORT_SYMBOL_GPL(__create_live_field_accessors);

/**
 * create_live_field_accessors - Auto-create field accessors for an event
 * @subsys_name: The subsystem name of the trace event
 * @event_name: The name of the trace event
 *
 * Automatically create and add a live field accessor for each field
 * in a trace event.
 *
 * Each field will have an accessor created for it and added to the
 * live_accessors array for the event.  This includes the common event
 * fields, but doesn't include the common_timestamp (which can be
 * added manually using add_live_field_accessor() if needed).
 *
 * A given field's accessor can be retrieved using
 * find_live_field_accessor() with the field's name.
 *
 * The field accessors should be destroyed using
 * destroy_live_field_accessors().
 *
 * Return: A pointer to the live_accessors object for use in further
 *         calls, ERR_PTR otherwise.
 */
struct live_accessors *create_live_field_accessors(char *subsys_name,
						   char *event_name)
{
	struct live_accessors *live_accessors;
	struct ftrace_event_field *field;
	struct list_head *head;
	int ret = 0;

	live_accessors = __create_live_field_accessors(subsys_name, event_name);
	if (IS_ERR(live_accessors)) {
		ret = PTR_ERR(live_accessors);
		goto out;
	}

	head = trace_get_common_fields();
	list_for_each_entry(field, head, link) {
		ret = add_live_field_accessor(live_accessors, (char *)field->name);
		if (ret)
			goto free;
	}

	head = trace_get_fields(live_accessors->file->event_call);
	list_for_each_entry(field, head, link) {
		ret = add_live_field_accessor(live_accessors, (char *)field->name);
		if (ret)
			goto free;
	}

	goto out;
 free:
	destroy_live_field_accessors(live_accessors);
 out:
	if (ret)
		return ERR_PTR(ret);

	return live_accessors;
}
EXPORT_SYMBOL_GPL(create_live_field_accessors);

int destroy_live_event_handlers(char *subsys_name, char *event_name)
{
	int ret = 0;

	return ret;
}
EXPORT_SYMBOL_GPL(destroy_live_event_handlers);

/**
 * create_empty_synth_event - Create a synth event to be populated with fields
 * @name: The name of the synthetic event to create
 *
 * Allocate and initialize a new synth_event struct for a new
 * synthetic event.
 *
 * The new synthetic event should be populated with fields using one
 * or more calls to add_synth_field() and then finalized and
 * registered using finalize_synth_event().
 *
 * The new synth event should be deleted using delete_synth_event() if
 * registration was successful using finalize_synth_event().  If not,
 * free_synth_event() should be used.
 *
 * Return: A pointer to the synth_event struct representing the new
 *         synth event, ERR_PTR otherwise.
 */
struct synth_event *create_empty_synth_event(const char *name)
{
	struct synth_event *event;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event) {
		event = ERR_PTR(-ENOMEM);
		goto out;
	}

	event->name = kstrdup(name, GFP_KERNEL);
	if (!event->name) {
		kfree(event);
		event = ERR_PTR(-ENOMEM);
		goto out;
	}

	dyn_event_init(&event->devent, &synth_event_ops);
 out:
	return event;
}
EXPORT_SYMBOL_GPL(create_empty_synth_event);

/**
 * finalize_synth_event - Finalize and register a new synth event
 * @event: A pointer to the synth_event struct representing the new event
 *
 * Register a new synth event only if an event with the same name
 * doesn't already exist.
 *
 * Return: 0 on success, ERR otherwise.
 */
int finalize_synth_event(struct synth_event *event)
{
	int ret;

	mutex_lock(&event_mutex);

	if (find_synth_event(event->name)) {
		ret = -EEXIST;
		goto out;
	}

	ret = register_synth_event(event);
	if (!ret)
		dyn_event_add(&event->devent);
	else
		free_synth_event(event);
 out:
	mutex_unlock(&event_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(finalize_synth_event);

static int update_fields(struct synth_event *event, struct synth_field *field)
{
	struct synth_field **old_fields;
	unsigned int i, n_fields;

	old_fields = event->fields;

	n_fields = event->n_fields + 1;

	event->fields = kcalloc(n_fields, sizeof(*event->fields), GFP_KERNEL);
	if (!event->fields)
		return -ENOMEM;
/* if field_name contains [n] it's an array */
	for (i = 0; i < n_fields - 1; i++)
		event->fields[i] = old_fields[i];

	event->fields[n_fields - 1] = field;

	event->n_fields = n_fields;

	return 0;
}


/**
 * add_synth_field - Add a new field to a synthetic event
 * @event: A pointer to the synth_event struct representing the new event
 * @field_type: The type of the new field to add
 * @field_name: The name of the new field to add
 *
 * Add a new field to a synthetic event object.  Field ordering is in
 * the same order the fields are added.
 *
 * See synth_field_size() for available types. If field_name contains
 * [n] the field is considered to be an array.
 *
 * Return: 0 if successful, error otherwise.
 */
int add_synth_field(struct synth_event *event, const char *field_type,
		    const char *field_name)
{
	struct synth_field *field;
	const char *array;
	int len, ret = 0;

	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field)
		return -ENOMEM;

	len = strlen(field_name);
	array = strchr(field_name, '[');
	if (array)
		len -= strlen(array);

	field->name = kmemdup_nul(field_name, len, GFP_KERNEL);
	if (!field->name) {
		ret = -ENOMEM;
		goto free;
	}

	len = strlen(field_type) + 1;
	if (array)
		len += strlen(array);

	field->type = kzalloc(len, GFP_KERNEL);
	if (!field->type) {
		ret = -ENOMEM;
		goto free;
	}

	strcat(field->type, field_type);
	if (array)
		strcat(field->type, array);

	field->size = synth_field_size(field->type);
	if (!field->size) {
		ret = -EINVAL;
		goto free;
	}

	if (synth_field_is_string(field->type))
		field->is_string = true;

	field->is_signed = synth_field_signed(field->type);

	ret = update_fields(event, field);
 out:
	return ret;
 free:
	free_synth_field(field);
	goto out;
}
EXPORT_SYMBOL_GPL(add_synth_field);
