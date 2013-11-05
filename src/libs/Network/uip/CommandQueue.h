#ifndef _COMMANDQUEUE_H_
#define _COMMANDQUEUE_H_

typedef int (*cb_t)(const char *);

#ifdef __cplusplus

#include "fifo.h"
#include <string>
#include <map>

#include "NetworkStream.h"

class CommandQueue
{
public:
    CommandQueue();
    ~CommandQueue();
    bool pop();
    int add(const char* cmd, int cb_id);
    int size() {return q.size();}
    static CommandQueue* getInstance() { if(instance == 0) instance= new CommandQueue(); return instance; }
    void registerCallback(cb_t cb, int id);

private:
    typedef struct {char* str; uint8_t id;} cmd_t;
    Fifo<cmd_t> q;
    std::map<int, NetworkStream*> stream_map;
    static CommandQueue *instance;
};

#else

extern int network_add_command(const char * cmd, int cb_id);
extern void register_callback(cb_t cb, int cb_id);
#endif

#endif
