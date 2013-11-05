// c accessibllity to the c++ fifo class
#include "fifo.h"
#include "c-fifo.h"

static Fifo<char *> fifo;

char *fifo_pop()
{
    return fifo.pop();
}

void fifo_push(char *str)
{
    fifo.push(str);
}

int fifo_size()
{
    return fifo.size();
}
