#ifndef INCLUDE_squeue_h__
#define INCLUDE_squeue_h__
#include <time.h>

typedef struct squeue squeue;
typedef struct squeue_event {
	time_t when;
	void *data;
	struct squeue_event *prev_event, *next_event;
} squeue_event;

/**
 * Creates a scheduling queue optimized for handling events within
 * the given timeframe. Callers should take care to create a queue
 * of a decent but not overly large size, as too small or too large
 * a queue will impact performance negatively. A queue can hold any
 * number of events. A good value for "horizon" would be the max
 * seconds into the future one expects to schedule things, although
 * with few scheduled items in that timeframe you'd be better off
 * using a more narrow horizon.
 *
 * @param horizon The desired event horizon
 * @return A pointer to a scheduling queue
 */
extern squeue *squeue_create(unsigned int size);

/**
 * Adds an event to the scheduling queue. Callers should take
 * care to save the returned pointer if they intend to remove
 * the event from the scheduling queue later.
 *
 * @param sq The scheduling queue to add to
 * @param when The unix timestamp when this event is to occur
 * @param data Pointer to any kind of data
 * @return The complete scheduled event
 */
extern squeue_event *squeue_add(squeue *sq, time_t when, void *data);

/**
 * Returns the next scheduled event from the scheduling queue
 * without removing it from the queue.
 * @param sq The scheduling queue to peek into
 */
extern squeue_event *squeue_peek(squeue *sq);

/**
 * Pops the next scheduled event from the scheduling queue and
 * returns it. This is equivalent to squeue_peek() + squeue_pop()
 *
 * @param sq The scheduling queue to pop from
 */
extern squeue_event *squeue_pop(squeue *sq);

extern int squeue_remove(squeue *sq, squeue_event *evt);
extern int squeue_destroy_event(squeue *sq, squeue_event *evt);
#endif
