#include <stdint.h>

#include "rangeros/input_queue.h"

void input_queue_init(struct input_queue *q)
{
    q->head = 0;
    q->count = 0;
    q->dropped = 0;
}

int input_queue_push(struct input_queue *q, uint8_t byte)
{
    if (q->count == INPUT_QUEUE_CAPACITY) {
        q->dropped++;
        return 0;
    }
    q->buf[(q->head + q->count) % INPUT_QUEUE_CAPACITY] = byte;
    q->count++;
    return 1;
}

int input_queue_pop(struct input_queue *q, uint8_t *byte)
{
    if (q->count == 0)
        return 0;
    *byte = q->buf[q->head];
    q->head = (q->head + 1) % INPUT_QUEUE_CAPACITY;
    q->count--;
    return 1;
}

uint32_t input_queue_count(const struct input_queue *q)
{
    return q->count;
}
