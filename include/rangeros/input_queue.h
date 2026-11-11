#ifndef RANGEROS_INPUT_QUEUE_H
#define RANGEROS_INPUT_QUEUE_H

#include <stdint.h>

#define INPUT_QUEUE_CAPACITY 128

/*
 * Fixed-capacity FIFO of input bytes (SPEC section 4). When full, push
 * drops the NEW byte and increments dropped; queued bytes are never
 * overwritten. count tells empty (0) from full (CAPACITY).
 *
 * Pure logic, no locking: the kernel pushes from the keyboard IRQ (IF=0)
 * and pops/inspects inside irq_save()/irq_restore(). Also built on the
 * host for tests/host/test_input_queue.c.
 */
struct input_queue {
    uint8_t buf[INPUT_QUEUE_CAPACITY];
    uint32_t head;    /* index of the oldest byte */
    uint32_t count;   /* bytes queued */
    uint32_t dropped; /* bytes rejected because the queue was full */
};

void input_queue_init(struct input_queue *q);
/* 1 if queued, 0 if dropped. */
int input_queue_push(struct input_queue *q, uint8_t byte);
/* 1 and *byte set if a byte was removed, 0 if empty. */
int input_queue_pop(struct input_queue *q, uint8_t *byte);
uint32_t input_queue_count(const struct input_queue *q);

#endif
