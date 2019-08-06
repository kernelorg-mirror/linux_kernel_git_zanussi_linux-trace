=============
Event Tracing
=============

:Author: Theodore Ts'o
:Updated: Li Zefan and Tom Zanussi

1. Introduction
===============

Tracepoints (see Documentation/trace/tracepoints.rst) can be used
without creating custom kernel modules to register probe functions
using the event tracing infrastructure.

Not all tracepoints can be traced using the event tracing system;
the kernel developer must provide code snippets which define how the
tracing information is saved into the tracing buffer, and how the
tracing information should be printed.

2. Using Event Tracing
======================

2.1 Via the 'set_event' interface
---------------------------------

The events which are available for tracing can be found in the file
/sys/kernel/debug/tracing/available_events.

To enable a particular event, such as 'sched_wakeup', simply echo it
to /sys/kernel/debug/tracing/set_event. For example::

	# echo sched_wakeup >> /sys/kernel/debug/tracing/set_event

.. Note:: '>>' is necessary, otherwise it will firstly disable all the events.

To disable an event, echo the event name to the set_event file prefixed
with an exclamation point::

	# echo '!sched_wakeup' >> /sys/kernel/debug/tracing/set_event

To disable all events, echo an empty line to the set_event file::

	# echo > /sys/kernel/debug/tracing/set_event

To enable all events, echo ``*:*`` or ``*:`` to the set_event file::

	# echo *:* > /sys/kernel/debug/tracing/set_event

The events are organized into subsystems, such as ext4, irq, sched,
etc., and a full event name looks like this: <subsystem>:<event>.  The
subsystem name is optional, but it is displayed in the available_events
file.  All of the events in a subsystem can be specified via the syntax
``<subsystem>:*``; for example, to enable all irq events, you can use the
command::

	# echo 'irq:*' > /sys/kernel/debug/tracing/set_event

2.2 Via the 'enable' toggle
---------------------------

The events available are also listed in /sys/kernel/debug/tracing/events/ hierarchy
of directories.

To enable event 'sched_wakeup'::

	# echo 1 > /sys/kernel/debug/tracing/events/sched/sched_wakeup/enable

To disable it::

	# echo 0 > /sys/kernel/debug/tracing/events/sched/sched_wakeup/enable

To enable all events in sched subsystem::

	# echo 1 > /sys/kernel/debug/tracing/events/sched/enable

To enable all events::

	# echo 1 > /sys/kernel/debug/tracing/events/enable

When reading one of these enable files, there are four results:

 - 0 - all events this file affects are disabled
 - 1 - all events this file affects are enabled
 - X - there is a mixture of events enabled and disabled
 - ? - this file does not affect any event

2.3 Boot option
---------------

In order to facilitate early boot debugging, use boot option::

	trace_event=[event-list]

event-list is a comma separated list of events. See section 2.1 for event
format.

3. Defining an event-enabled tracepoint
=======================================

See The example provided in samples/trace_events

4. Event formats
================

Each trace event has a 'format' file associated with it that contains
a description of each field in a logged event.  This information can
be used to parse the binary trace stream, and is also the place to
find the field names that can be used in event filters (see section 5).

It also displays the format string that will be used to print the
event in text mode, along with the event name and ID used for
profiling.

Every event has a set of ``common`` fields associated with it; these are
the fields prefixed with ``common_``.  The other fields vary between
events and correspond to the fields defined in the TRACE_EVENT
definition for that event.

Each field in the format has the form::

     field:field-type field-name; offset:N; size:N;

where offset is the offset of the field in the trace record and size
is the size of the data item, in bytes.

For example, here's the information displayed for the 'sched_wakeup'
event::

	# cat /sys/kernel/debug/tracing/events/sched/sched_wakeup/format

	name: sched_wakeup
	ID: 60
	format:
		field:unsigned short common_type;	offset:0;	size:2;
		field:unsigned char common_flags;	offset:2;	size:1;
		field:unsigned char common_preempt_count;	offset:3;	size:1;
		field:int common_pid;	offset:4;	size:4;
		field:int common_tgid;	offset:8;	size:4;

		field:char comm[TASK_COMM_LEN];	offset:12;	size:16;
		field:pid_t pid;	offset:28;	size:4;
		field:int prio;	offset:32;	size:4;
		field:int success;	offset:36;	size:4;
		field:int cpu;	offset:40;	size:4;

	print fmt: "task %s:%d [%d] success=%d [%03d]", REC->comm, REC->pid,
		   REC->prio, REC->success, REC->cpu

This event contains 10 fields, the first 5 common and the remaining 5
event-specific.  All the fields for this event are numeric, except for
'comm' which is a string, a distinction important for event filtering.

5. Event filtering
==================

Trace events can be filtered in the kernel by associating boolean
'filter expressions' with them.  As soon as an event is logged into
the trace buffer, its fields are checked against the filter expression
associated with that event type.  An event with field values that
'match' the filter will appear in the trace output, and an event whose
values don't match will be discarded.  An event with no filter
associated with it matches everything, and is the default when no
filter has been set for an event.

5.1 Expression syntax
---------------------

A filter expression consists of one or more 'predicates' that can be
combined using the logical operators '&&' and '||'.  A predicate is
simply a clause that compares the value of a field contained within a
logged event with a constant value and returns either 0 or 1 depending
on whether the field value matched (1) or didn't match (0)::

	  field-name relational-operator value

Parentheses can be used to provide arbitrary logical groupings and
double-quotes can be used to prevent the shell from interpreting
operators as shell metacharacters.

The field-names available for use in filters can be found in the
'format' files for trace events (see section 4).

The relational-operators depend on the type of the field being tested:

The operators available for numeric fields are:

==, !=, <, <=, >, >=, &

And for string fields they are:

==, !=, ~

The glob (~) accepts a wild card character (\*,?) and character classes
([). For example::

  prev_comm ~ "*sh"
  prev_comm ~ "sh*"
  prev_comm ~ "*sh*"
  prev_comm ~ "ba*sh"

5.2 Setting filters
-------------------

A filter for an individual event is set by writing a filter expression
to the 'filter' file for the given event.

For example::

	# cd /sys/kernel/debug/tracing/events/sched/sched_wakeup
	# echo "common_preempt_count > 4" > filter

A slightly more involved example::

	# cd /sys/kernel/debug/tracing/events/signal/signal_generate
	# echo "((sig >= 10 && sig < 15) || sig == 17) && comm != bash" > filter

If there is an error in the expression, you'll get an 'Invalid
argument' error when setting it, and the erroneous string along with
an error message can be seen by looking at the filter e.g.::

	# cd /sys/kernel/debug/tracing/events/signal/signal_generate
	# echo "((sig >= 10 && sig < 15) || dsig == 17) && comm != bash" > filter
	-bash: echo: write error: Invalid argument
	# cat filter
	((sig >= 10 && sig < 15) || dsig == 17) && comm != bash
	^
	parse_error: Field not found

Currently the caret ('^') for an error always appears at the beginning of
the filter string; the error message should still be useful though
even without more accurate position info.

5.3 Clearing filters
--------------------

To clear the filter for an event, write a '0' to the event's filter
file.

To clear the filters for all events in a subsystem, write a '0' to the
subsystem's filter file.

5.3 Subsystem filters
---------------------

For convenience, filters for every event in a subsystem can be set or
cleared as a group by writing a filter expression into the filter file
at the root of the subsystem.  Note however, that if a filter for any
event within the subsystem lacks a field specified in the subsystem
filter, or if the filter can't be applied for any other reason, the
filter for that event will retain its previous setting.  This can
result in an unintended mixture of filters which could lead to
confusing (to the user who might think different filters are in
effect) trace output.  Only filters that reference just the common
fields can be guaranteed to propagate successfully to all events.

Here are a few subsystem filter examples that also illustrate the
above points:

Clear the filters on all events in the sched subsystem::

	# cd /sys/kernel/debug/tracing/events/sched
	# echo 0 > filter
	# cat sched_switch/filter
	none
	# cat sched_wakeup/filter
	none

Set a filter using only common fields for all events in the sched
subsystem (all events end up with the same filter)::

	# cd /sys/kernel/debug/tracing/events/sched
	# echo common_pid == 0 > filter
	# cat sched_switch/filter
	common_pid == 0
	# cat sched_wakeup/filter
	common_pid == 0

Attempt to set a filter using a non-common field for all events in the
sched subsystem (all events but those that have a prev_pid field retain
their old filters)::

	# cd /sys/kernel/debug/tracing/events/sched
	# echo prev_pid == 0 > filter
	# cat sched_switch/filter
	prev_pid == 0
	# cat sched_wakeup/filter
	common_pid == 0

5.4 PID filtering
-----------------

The set_event_pid file in the same directory as the top events directory
exists, will filter all events from tracing any task that does not have the
PID listed in the set_event_pid file.
::

	# cd /sys/kernel/debug/tracing
	# echo $$ > set_event_pid
	# echo 1 > events/enable

Will only trace events for the current task.

To add more PIDs without losing the PIDs already included, use '>>'.
::

	# echo 123 244 1 >> set_event_pid


6. Event triggers
=================

Trace events can be made to conditionally invoke trigger 'commands'
which can take various forms and are described in detail below;
examples would be enabling or disabling other trace events or invoking
a stack trace whenever the trace event is hit.  Whenever a trace event
with attached triggers is invoked, the set of trigger commands
associated with that event is invoked.  Any given trigger can
additionally have an event filter of the same form as described in
section 5 (Event filtering) associated with it - the command will only
be invoked if the event being invoked passes the associated filter.
If no filter is associated with the trigger, it always passes.

Triggers are added to and removed from a particular event by writing
trigger expressions to the 'trigger' file for the given event.

A given event can have any number of triggers associated with it,
subject to any restrictions that individual commands may have in that
regard.

Event triggers are implemented on top of "soft" mode, which means that
whenever a trace event has one or more triggers associated with it,
the event is activated even if it isn't actually enabled, but is
disabled in a "soft" mode.  That is, the tracepoint will be called,
but just will not be traced, unless of course it's actually enabled.
This scheme allows triggers to be invoked even for events that aren't
enabled, and also allows the current event filter implementation to be
used for conditionally invoking triggers.

The syntax for event triggers is roughly based on the syntax for
set_ftrace_filter 'ftrace filter commands' (see the 'Filter commands'
section of Documentation/trace/ftrace.rst), but there are major
differences and the implementation isn't currently tied to it in any
way, so beware about making generalizations between the two.

Note: Writing into trace_marker (See Documentation/trace/ftrace.rst)
     can also enable triggers that are written into
     /sys/kernel/tracing/events/ftrace/print/trigger

6.1 Expression syntax
---------------------

Triggers are added by echoing the command to the 'trigger' file::

  # echo 'command[:count] [if filter]' > trigger

Triggers are removed by echoing the same command but starting with '!'
to the 'trigger' file::

  # echo '!command[:count] [if filter]' > trigger

The [if filter] part isn't used in matching commands when removing, so
leaving that off in a '!' command will accomplish the same thing as
having it in.

The filter syntax is the same as that described in the 'Event
filtering' section above.

For ease of use, writing to the trigger file using '>' currently just
adds or removes a single trigger and there's no explicit '>>' support
('>' actually behaves like '>>') or truncation support to remove all
triggers (you have to use '!' for each one added.)

6.2 Supported trigger commands
------------------------------

The following commands are supported:

- enable_event/disable_event

  These commands can enable or disable another trace event whenever
  the triggering event is hit.  When these commands are registered,
  the other trace event is activated, but disabled in a "soft" mode.
  That is, the tracepoint will be called, but just will not be traced.
  The event tracepoint stays in this mode as long as there's a trigger
  in effect that can trigger it.

  For example, the following trigger causes kmalloc events to be
  traced when a read system call is entered, and the :1 at the end
  specifies that this enablement happens only once::

	  # echo 'enable_event:kmem:kmalloc:1' > \
	      /sys/kernel/debug/tracing/events/syscalls/sys_enter_read/trigger

  The following trigger causes kmalloc events to stop being traced
  when a read system call exits.  This disablement happens on every
  read system call exit::

	  # echo 'disable_event:kmem:kmalloc' > \
	      /sys/kernel/debug/tracing/events/syscalls/sys_exit_read/trigger

  The format is::

      enable_event:<system>:<event>[:count]
      disable_event:<system>:<event>[:count]

  To remove the above commands::

	  # echo '!enable_event:kmem:kmalloc:1' > \
	      /sys/kernel/debug/tracing/events/syscalls/sys_enter_read/trigger

	  # echo '!disable_event:kmem:kmalloc' > \
	      /sys/kernel/debug/tracing/events/syscalls/sys_exit_read/trigger

  Note that there can be any number of enable/disable_event triggers
  per triggering event, but there can only be one trigger per
  triggered event. e.g. sys_enter_read can have triggers enabling both
  kmem:kmalloc and sched:sched_switch, but can't have two kmem:kmalloc
  versions such as kmem:kmalloc and kmem:kmalloc:1 or 'kmem:kmalloc if
  bytes_req == 256' and 'kmem:kmalloc if bytes_alloc == 256' (they
  could be combined into a single filter on kmem:kmalloc though).

- stacktrace

  This command dumps a stacktrace in the trace buffer whenever the
  triggering event occurs.

  For example, the following trigger dumps a stacktrace every time the
  kmalloc tracepoint is hit::

	  # echo 'stacktrace' > \
		/sys/kernel/debug/tracing/events/kmem/kmalloc/trigger

  The following trigger dumps a stacktrace the first 5 times a kmalloc
  request happens with a size >= 64K::

	  # echo 'stacktrace:5 if bytes_req >= 65536' > \
		/sys/kernel/debug/tracing/events/kmem/kmalloc/trigger

  The format is::

      stacktrace[:count]

  To remove the above commands::

	  # echo '!stacktrace' > \
		/sys/kernel/debug/tracing/events/kmem/kmalloc/trigger

	  # echo '!stacktrace:5 if bytes_req >= 65536' > \
		/sys/kernel/debug/tracing/events/kmem/kmalloc/trigger

  The latter can also be removed more simply by the following (without
  the filter)::

	  # echo '!stacktrace:5' > \
		/sys/kernel/debug/tracing/events/kmem/kmalloc/trigger

  Note that there can be only one stacktrace trigger per triggering
  event.

- snapshot

  This command causes a snapshot to be triggered whenever the
  triggering event occurs.

  The following command creates a snapshot every time a block request
  queue is unplugged with a depth > 1.  If you were tracing a set of
  events or functions at the time, the snapshot trace buffer would
  capture those events when the trigger event occurred::

	  # echo 'snapshot if nr_rq > 1' > \
		/sys/kernel/debug/tracing/events/block/block_unplug/trigger

  To only snapshot once::

	  # echo 'snapshot:1 if nr_rq > 1' > \
		/sys/kernel/debug/tracing/events/block/block_unplug/trigger

  To remove the above commands::

	  # echo '!snapshot if nr_rq > 1' > \
		/sys/kernel/debug/tracing/events/block/block_unplug/trigger

	  # echo '!snapshot:1 if nr_rq > 1' > \
		/sys/kernel/debug/tracing/events/block/block_unplug/trigger

  Note that there can be only one snapshot trigger per triggering
  event.

- traceon/traceoff

  These commands turn tracing on and off when the specified events are
  hit. The parameter determines how many times the tracing system is
  turned on and off. If unspecified, there is no limit.

  The following command turns tracing off the first time a block
  request queue is unplugged with a depth > 1.  If you were tracing a
  set of events or functions at the time, you could then examine the
  trace buffer to see the sequence of events that led up to the
  trigger event::

	  # echo 'traceoff:1 if nr_rq > 1' > \
		/sys/kernel/debug/tracing/events/block/block_unplug/trigger

  To always disable tracing when nr_rq  > 1::

	  # echo 'traceoff if nr_rq > 1' > \
		/sys/kernel/debug/tracing/events/block/block_unplug/trigger

  To remove the above commands::

	  # echo '!traceoff:1 if nr_rq > 1' > \
		/sys/kernel/debug/tracing/events/block/block_unplug/trigger

	  # echo '!traceoff if nr_rq > 1' > \
		/sys/kernel/debug/tracing/events/block/block_unplug/trigger

  Note that there can be only one traceon or traceoff trigger per
  triggering event.

- hist

  This command aggregates event hits into a hash table keyed on one or
  more trace event format fields (or stacktrace) and a set of running
  totals derived from one or more trace event format fields and/or
  event counts (hitcount).

  See Documentation/trace/histogram.rst for details and examples.

6.3 In-kernel trace event API
-----------------------------

In most cases, the command-line interface to trace events is more than
sufficient.  Sometimes, however, applications might find the need for
more complex relationships than can be expressed through a simple
series of linked command-line expressions, or putting together sets of
commands may be simply too cumbersome.  An example might be an
application that needs to 'listen' to the trace stream in order to
maintain an in-kernel state machine detecting, for instance, when an
illegal kernel state occurs in the scheduler.

The trace event subsystem provides an in-kernel API allowing modules
or other kernel code to access the trace stream in real-time, and
simultaneously generate user-defined 'synthetic' events at will, which
can be used to either augment the existing trace stream and/or signal
that a particular important state has occured.

The API provided for these purposes is describe below and allows the
following:

  - creating and defining custom handlers for existing trace events
  - dynamically creating synthetic event definitions
  - generating synthetic events from custom handlers

6.3.1 Creating and defining custom handlers for existing trace events
---------------------------------------------------------------------

To create a live event handler for an existing event in a kernel
module, the create_live_handler() function should be used.  The name
of the event and its subsystem should be specified, along with an
event_trigger_ops object.  The event_trigger_ops object specifies the
name of the callback function that will be called on every hit of the
named event.  For example, to have the 'sched_switch_event_trigger()'
callback called on every sched_switch event, the following code could
be used:

  static struct event_trigger_ops event_live_trigger_ops = {
      .func			= sched_switch_event_trigger,
      .print			= event_live_trigger_print,
      .init			= event_live_trigger_init,
      .free			= event_live_trigger_free,
  };

  data = create_live_handler("sched", "sched_switch", &event_live_trigger_ops);

In order to access the event's trace data on an event hit, field
accessors can be used.  When the callback function is invoked, an
array of live_field 'accessors' is passed in as the private_data of
the event_trigger_data passed to the handler.  A given event field's
data can be accessed via the live_field struct pointer corresponding
to that field, specifically by calling the live_field->fn() member,
which will return the field data as a u64 that can be cast into the
actual field type (is_string_field() can be used to determine and deal
with string field types).

For example, the following code will print out each field value for
various types:

  for (i = 0; i < MAX_ACCESSORS; i++) {
      field = live_accessors->accessors[i];
      if (!field)
          break;

      val = field->fn(field->field, rbe, rec);

      if (is_string_field(field->field))
          printk("\tval=%s (%s)\n", (char *)(long)val, field->field->name);
      else
          printk("\tval=%llu (%s)\n", val, field->field->name);
  }

  val = prev_comm_field->fn(prev_comm_field->field, rbe, rec);
  printk("prev_comm: %s\n", (char *)val);
  val = prev_pid_field->fn(prev_pid_field->field, rbe, rec);
  printk("prev_pid: %d\n", (pid_t)val);

  print_timestamp(live_accessors->timestamp_accessor, rec, rbe);

In order for this to work, a set of field accessors needs to be
created before the event is registered.  This is best done in the
event_trigger_ops.init() callback, which is called from the
create_live_handler() code.

create_live_field_accessors() automatically creates and add a live
field accessor for each field in a trace event.

Each field will have an accessor created for it and added to the
live_accessors array for the event.  This includes the common event
fields, but doesn't include the common_timestamp (which can be added
manually using add_live_field_accessor() if needed).

If you don't need or want accessors for every field, you can add them
individually using add_live_field_accessor() for each one.

A given field's accessor, for use in the handler, can be retrieved
using find_live_field_accessor() with the field's name.

For example, this call would create a set of live field accessors, one
for each field of the sched_switch event:

  live_accessors = create_live_field_accessors("sched", "sched_switch");

To access a couple specific sched_switch fields (the same fields we
used above in the handler):

  prev_pid_field = find_live_field_accessor(live_accessors, "prev_pid");
  prev_comm_field = find_live_field_accessor(live_accessors, "prev_comm");

To add a common_timestamp accessor, use this call:

  ret = add_live_field_accessor(live_accessors, "common_timestamp");

Note that the common_timestamp accessor is special and unlike all
other accessors, can be accessed using the dedicated
live_accessors->timestamp_accessor member.

6.3.2 Dyamically creating synthetic event definitions
-----------------------------------------------------

To create a new synthetic event from a kernel module, an empty
synthetic event should first be created using
create_empty_synthetic_event().  The name of the event should be
supplied to this function.  For example, to create a new "livetest"
synthetic event:

  struct synth_event *se = create_empty_synth_event("livetest");

Once a synthetic event object has been created, it can then be
populated with fields.  Fields are added one by one using
add_synth_field(), supplying the new synthetic event object, a field
type, and a field name.  For example, to add a new int field named
"intfield", the following call should be made:

  ret = add_synth_field(se, "int", "intfield");

See synth_field_size() for available types. If field_name contains [n]
the field is considered to be an array.

Once all the fields have been added, the event should be finalized and
registered by calling the finalize_synth_event() function:

  ret = finalize_synth_event(se);

At this point, the event object is ready to be used for generating new
events.

6.3.3 Generating synthetic events from custom handlers
------------------------------------------------------

To generate a synthetic event, the generate_synth_event() function is
used.  It's passed the trace_event_file representing the synthetic
event (which can be retrieved using find_event_file() using the
synthetic event name and "synthetic" as the system name, along with
top_trace_array() as the trace array), along with an array of u64, one
for each synthetic event field.

The 'vals' array is just an array of u64, the number of which must
match the number of field in the synthetic event, and which must be in
the same order as the synthetic event fields.

All vals should be cast to u64, and string vals are just pointers to
strings, cast to u64.  Strings will be copied into space reserved in
the event for the string, using these pointers.

So for a synthetic event that was created using the following:

  se = create_empty_synth_event("schedtest");
  add_synth_field(se, "pid_t", "next_pid_field");
  add_synth_field(se, "char[16]", "next_comm_field");
  add_synth_field(se, "u64", "ts_ns");
  add_synth_field(se, "u64", "ts_ms");
  add_synth_field(se, "unsigned int", "cpu");
  add_synth_field(se, "char[64]", "my_string_field");
  add_synth_field(se, "int", "my_int_field");
  finalize_synth_event(se);

  schedtest_event_file = find_event_file(top_trace_array(),
                                         "synthetic", "schedtest");

Code to generate a synthetic event might then look like this:

  u64 vals[7];

  ts_ns = get_timestamp(live_accessors->timestamp_accessor, rec, rbe, true);
  ts_ms = get_timestamp(live_accessors->timestamp_accessor, rec, rbe, false);
  cpu = smp_processor_id();
  next_comm_val = next_comm_field->fn(next_comm_field->field, rbe, rec);
  next_pid_val = next_pid_field->fn(next_pid_field->field, rbe, rec);

  vals[0] = next_pid_val;
  vals[1] = next_comm_val;
  vals[2] = ts_ns;
  vals[3] = ts_ms;
  vals[4] = cpu;
  vals[5] = (u64)"thneed";
  vals[6] = 398;

  generate_synth_event(schedtest_event_file, vals, sizeof(vals));
