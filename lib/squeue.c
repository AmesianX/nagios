/*
 * Simple scheduling queue for equal-priority events.
 * This librar handle events in a LIFO manner. That suits the
 * primary application (Nagios) very well, since events
 * scheduled less in advance are generally meant to be run
 * more often, and are therefore more important.
 *
 * "add" and "remove" are always O(1).
 *
 * "peek" and "pop" are O(n) worst case, where 'n' is the size
 * of the scheduling queue. The probability of hitting it
 * increases the fewer elements there are in the list. That's
 * acceptable, since it means we aren't scheduling so much that
 * we need to worry about performance.
 *
 * "find" is not implemented, but would be expensive. Callers
 * must maintain a pointer to the event if they want to remove
 * it.
 *
 * There is no "grow" function. Adding one wouldn't be terribly
 * difficult but it would be inefficient to run and with a small
 * amount of forward planning it shouldn't be needed for this
 * application.
 */

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include "squeue.h"

/*
 * The *current list points to events scheduled to be run the next time
 * this second % squeue->num_buckets comes around.
 *
 * The "later" list contains all events that end up in this bucket
 * but are scheduled to run so late that time(NULL) will wrap around
 * squeue->num_buckets before we reach it. It's best to avoid that,
 * since moving all events can be a fairly timeconsuming operation.
 * Larger queues will have less problems with that, obviously.
 */
typedef struct squeue_bucket {
	squeue_event *current;
	squeue_event *later;
} squeue_bucket;

struct squeue {
	unsigned int num_events; /* events in this queue */
	unsigned int runs_later;
	unsigned int promotions;
	unsigned int num_buckets;
	time_t read_offset; /* where we read from last time */
	squeue_bucket *buckets;
};

static void sq_stats(squeue *sq, const char *from)
{
	printf("sq_stats from %s\n", from);
	if (!sq) {
		printf("sq is NULL\n");
		return;
	}
	printf("  num_events: %u\n", sq->num_events);
	printf("  runs_later: %u\n", sq->runs_later);
	printf("  promotions: %u\n", sq->promotions);
	printf("  num_buckets: %u\n", sq->num_buckets);
}

squeue *squeue_create(unsigned int horizon)
{
	squeue *sq;

	/* there is no 'grow', and empty scheduling queues are useless */
	if (!horizon)
		return NULL;
	sq = calloc(1, sizeof(*sq));
	if (!sq)
		return NULL;

	sq->buckets = calloc(horizon, sizeof(*sq->buckets));
	if (!sq->buckets) {
		free(sq);
		return NULL;
	}
	sq->num_buckets = horizon;
	sq->read_offset = time(NULL);
	return sq;
}

#define sq_slot(sq, when) (when % sq->num_buckets)
#define sq_bucket(sq, when) (sq->buckets[sq_slot(sq, when)])
static int is_current(squeue *sq, time_t when)
{
	int current = 0;

	if (when < time(NULL) || (when - time(NULL)) < sq->num_buckets)
		current = 1;
	return current;
}

/* promotes events from 'later' to 'current' */
static void sq_promote(squeue *sq, squeue_bucket *bucket)
{
	squeue_event *evt, *prev = NULL, *next;
	time_t lowest_later = ~0;

	/* never promote items while current is non-empty */
	if (bucket->current || !bucket->later)
		return;

	/*
	 * The first event in 'later' queue always has the lowest
	 * timestamp in that queue, so we can avoid trying to
	 * promote events if we know none of them will be moved
	 * to the 'current' queue.
	 */
	if (bucket->later->when > time(NULL) + sq->num_buckets)
		return;

	for (evt = bucket->later; evt; evt = next) {
		next = evt->next_event;

		if (!is_current(sq, evt->when - 1)) {
			prev = evt;
			continue;
		}

		sq->runs_later--;

		if (evt->when < lowest_later)
			lowest_later = evt->when;

		if (prev) {
			prev->next_event = next;
		} else {
			bucket->later = next;
		}
		if (next)
			next->prev_event = prev;

		evt->next_event = bucket->current;
		bucket->current = evt;
	}

}

static void sq_add_event(squeue *sq, squeue_event *evt)
{
	squeue_bucket *bucket = &sq_bucket(sq, evt->when);
	if (is_current(sq, evt->when)) {
		evt->next_event = bucket->current;
		bucket->current = evt;
	} else {
		/*
		 * when adding to the 'later' queue, we make sure
		 * an entry with the lowest value comes first, so
		 * we know we can skip traversing it if its turn
		 * won't come again until we've done another circuit
		 * of all events
		 */
		if (!bucket->later || evt->when >= bucket->later->when) {
			evt->next_event = bucket->later;
			bucket->later = evt;
		} else {
			evt->next_event = bucket->later->next_event;
			evt->prev_event = bucket->later;
			bucket->later->next_event = evt;
		}
		sq->runs_later++;
	}

	/* now set the prev_event link for next_event */
	if (evt->next_event)
		evt->next_event->prev_event = evt;

	sq->num_events++;
	if (evt->when < sq->read_offset)
		sq->read_offset = evt->when;
}

squeue_event *squeue_add(squeue *sq, time_t when, void *data)
{
	squeue_event *evt;

	if (!sq || !sq->buckets)
		return NULL;

	/* we can't schedule events in the past */
	if (when < time(NULL))
		when = time(NULL);

	evt = calloc(1, sizeof(*evt));
	if (!evt)
		return NULL;
	evt->data = data;
	evt->when = when;

	sq_add_event(sq, evt);
	return evt;
}

squeue_event *squeue_add_weighted(time_t when, void *data, int weight, int max_offset)
{
	/* not yet implemented */
	return NULL;
}

squeue_event *squeue_peek(squeue *sq)
{
	squeue_event *best_later = NULL;
	unsigned int i;

	if (!sq || !sq->buckets || !sq->num_events)
		return NULL;

	for (i = sq->read_offset; i < sq->read_offset + sq->num_buckets; i++) {
		squeue_bucket *bucket = &sq->buckets[sq_slot(sq, i)];

		sq_promote(sq, bucket);

		if (bucket->current) {
			return bucket->current;
		}
		if (bucket->later) {
			if (!best_later || best_later->when > bucket->later->when) {
				best_later = bucket->later;
			}
		}
	}

	return best_later;
}

squeue_event *squeue_pop(squeue *sq)
{
	squeue_event *evt;

	evt = squeue_peek(sq);
	if (!evt)
		return NULL;

	squeue_remove(sq, evt);

	return evt;
}

int squeue_remove(squeue *sq, squeue_event *evt)
{
	squeue_event *prev = NULL, *next = NULL;

	if (!sq || !sq->buckets || !sq->num_events || !evt) {
		return -1;
	}

	prev = evt->prev_event;
	next = evt->next_event;

	sq->num_events--;

	/* middle or end of linked list has a shortcut */
	if (next)
		next->prev_event = prev;
	if (prev) {
		prev->next_event = next;
	} else {
		if (is_current(sq, evt->when)) {
			sq_bucket(sq, evt->when).current = next;
		} else {
			sq_bucket(sq, evt->when).later = next;
		}
	}

	return 0;
}

/*
 * same as 'remove' but also free()'s the event.
 * Callers are responsible for free()'ing the data.
 */
int squeue_destroy_event(squeue *sq, squeue_event *evt)
{
	int ret;

	ret = squeue_remove(sq, evt);
	if (evt)
		free(evt);

	return ret;
}

unsigned int squeue_num_events(squeue *sq)
{
	if (!sq)
		return 0;

	return sq->num_events;
}
