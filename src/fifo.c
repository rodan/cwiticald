
#include <stdlib.h>
#include "fifo.h"

fifo_t *create_fifo(const size_t size)
{
    uint8_t *buffer = (uint8_t *) malloc(size);

    if (buffer == NULL) {
        return NULL;
    }

    fifo_t *fifo = (fifo_t *) malloc(sizeof(fifo_t));

    if (fifo == NULL) {
        free(buffer);
        return NULL;
    }

    fifo->buffer = buffer;
    fifo->head = 0;
    fifo->tail = 0;
    fifo->size = size;

    return fifo;
}

size_t fifo_push_byte(fifo_t * fifo, const uint8_t byte)
{
    //CHECK_FIFO_NULL(fifo);

    // check if fifo is full
    if ((fifo->head == (fifo->size - 1) && fifo->tail == 0)
        || (fifo->head == (fifo->tail - 1))) {
        return 0;
    }

    fifo->buffer[fifo->head] = byte;
    fifo->head++;
    if (fifo->head == fifo->size) {
        fifo->head = 0;
    }

    return 1;
}

size_t fifo_push(fifo_t * fifo, uint8_t * bytes, const size_t count)
{
    //CHECK_FIFO_NULL(fifo);
    size_t i;

    for (i = 0; i < count; i++) {
        if (fifo_push_byte(fifo, bytes[i]) == 0) {
            return i;
        }
    }

    return count;
}

size_t fifo_pop_byte(fifo_t * fifo, uint8_t * byte)
{
    //CHECK_FIFO_NULL(fifo);

    // check if fifo is empty
    if (fifo->head == fifo->tail) {
        return 0;
    }

    *byte = fifo->buffer[fifo->tail];

    fifo->tail++;
    if (fifo->tail == fifo->size) {
        fifo->tail = 0;
    }

    return 1;
}

size_t fifo_pop(fifo_t * fifo, uint8_t * bytes, size_t count)
{
    //CHECK_FIFO_NULL(fifo);
    size_t i;

    for (i = 0; i < count; i++) {
        if (fifo_pop_byte(fifo, bytes + i) == 0) {
            return i;
        }
    }

    return count;
}

uint8_t fifo_check_full(fifo_t * fifo)
{
    if ((fifo->head == (fifo->size - 1) && fifo->tail == 0)
        || (fifo->head == (fifo->tail - 1))) {
        return 1;
    } else {
        return 0;
    }
}

uint8_t fifo_check_empty(fifo_t * fifo)
{
    if (fifo->head == fifo->tail) {
        return 1;
    } else {
        return 0;
    }
}

size_t fifo_check_fill(fifo_t * fifo)
{
    if (fifo->head == fifo->tail) {
        return 0;
    } else if ((fifo->head == (fifo->size - 1) && fifo->tail == 0)
               || (fifo->head == (fifo->tail - 1))) {
        return fifo->size;
    } else if (fifo->head < fifo->tail) {
        return (fifo->head) + (fifo->size - fifo->tail);
    } else {
        return fifo->head - fifo->tail;
    }
}
