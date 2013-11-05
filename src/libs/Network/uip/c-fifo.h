#ifndef _CFIFO_H_
#define _CFIFO_H_

#ifdef __cplusplus
extern "C" {
#endif

char *fifo_pop();
void fifo_push(char *);
int fifo_size();

#ifdef __cplusplus
}
#endif

#endif
