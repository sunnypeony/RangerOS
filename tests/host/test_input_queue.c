/* Host unit test for lib/input_queue.c: empty, full, drop-new, FIFO
 * order, wrap-around and the dropped counter. */
#include <stdint.h>
#include <stdio.h>

#include "rangeros/input_queue.h"

static int failures;
static int checks;

#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            printf("FAIL line %d: %s\n", __LINE__, #cond);              \
        }                                                               \
    } while (0)

int main(void)
{
    struct input_queue q;
    uint8_t b = 0xEE;

    /* empty */
    input_queue_init(&q);
    CHECK(input_queue_count(&q) == 0);
    CHECK(input_queue_pop(&q, &b) == 0);
    CHECK(b == 0xEE); /* untouched on empty pop */
    CHECK(q.dropped == 0);

    /* fill to capacity */
    for (int i = 0; i < INPUT_QUEUE_CAPACITY; i++)
        CHECK(input_queue_push(&q, (uint8_t)i) == 1);
    CHECK(input_queue_count(&q) == INPUT_QUEUE_CAPACITY);

    /* full: new bytes dropped and counted, old ones kept */
    CHECK(input_queue_push(&q, 0xAA) == 0);
    CHECK(input_queue_push(&q, 0xBB) == 0);
    CHECK(q.dropped == 2);
    CHECK(input_queue_count(&q) == INPUT_QUEUE_CAPACITY);
    for (int i = 0; i < INPUT_QUEUE_CAPACITY; i++) {
        CHECK(input_queue_pop(&q, &b) == 1);
        CHECK(b == (uint8_t)i);
    }
    CHECK(input_queue_count(&q) == 0);
    CHECK(input_queue_pop(&q, &b) == 0);
    CHECK(q.dropped == 2); /* dropped is cumulative */

    /* wrap-around: many push/pop pairs move head past the end repeatedly */
    input_queue_init(&q);
    uint8_t next_in = 0, next_out = 0;
    for (int round = 0; round < 1000; round++) {
        int n = 1 + round % 7;
        for (int i = 0; i < n; i++)
            CHECK(input_queue_push(&q, next_in++) == 1);
        for (int i = 0; i < n; i++) {
            CHECK(input_queue_pop(&q, &b) == 1);
            CHECK(b == next_out++);
        }
    }
    CHECK(q.head != 0); /* head really wrapped somewhere mid-buffer */
    CHECK(q.dropped == 0);

    /* full while wrapped: head mid-buffer, fill, overflow, drain in order */
    input_queue_init(&q);
    for (int i = 0; i < 77; i++) {
        input_queue_push(&q, 0);
        input_queue_pop(&q, &b);
    }
    CHECK(q.head == 77);
    for (int i = 0; i < INPUT_QUEUE_CAPACITY; i++)
        CHECK(input_queue_push(&q, (uint8_t)(200 + i)) == 1);
    CHECK(input_queue_push(&q, 0x11) == 0);
    CHECK(q.dropped == 1);
    for (int i = 0; i < INPUT_QUEUE_CAPACITY; i++) {
        CHECK(input_queue_pop(&q, &b) == 1);
        CHECK(b == (uint8_t)(200 + i));
    }
    CHECK(input_queue_count(&q) == 0);

    /* space frees up after a pop */
    for (int i = 0; i < INPUT_QUEUE_CAPACITY; i++)
        input_queue_push(&q, 1);
    CHECK(input_queue_push(&q, 2) == 0);
    CHECK(input_queue_pop(&q, &b) == 1);
    CHECK(input_queue_push(&q, 3) == 1);
    CHECK(q.dropped == 2);

    printf("HOST_INPUT_QUEUE %s (%d checks, %d failed)\n", failures ? "FAIL" : "PASS", checks,
           failures);
    return failures ? 1 : 0;
}
