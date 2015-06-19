#ifndef __FIFO_H_
#define __FIFO_H_

#include <inttypes.h>
#include <stddef.h>

typedef struct {
    uint8_t *buffer;
    size_t head;
    size_t tail;
    size_t size;
} fifo_t;

//#define CHECK_FIFO_NULL(fifo) MAC_FUNC(if (fifo == NULL) return 0;)
fifo_t *create_fifo(const size_t size);
size_t fifo_push_byte(fifo_t * fifo, const uint8_t byte);
size_t fifo_push(fifo_t * fifo, uint8_t * buff, const size_t count);
size_t fifo_pop_byte(fifo_t * fifo, uint8_t * byte);
size_t fifo_pop(fifo_t * fifo, uint8_t * buff, const size_t count);
uint8_t fifo_check_full(fifo_t * fifo);
uint8_t fifo_check_empty(fifo_t * fifo);
size_t fifo_check_fill(fifo_t * fifo);

#endif
