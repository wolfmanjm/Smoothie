#include "CommandQueue.h"

#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#include "Kernel.h"
#include "libs/SerialMessage.h"
#include "CallbackStream.h"

static CommandQueue *command_queue_instance;
CommandQueue *CommandQueue::instance = NULL;


CommandQueue::CommandQueue()
{
    command_queue_instance = this;
    stream_map[0]= &(StreamOutput::NullStream);
}

CommandQueue* CommandQueue::getInstance()
{
    if(instance == 0) instance= new CommandQueue();
    return instance;
}

extern "C" {

    int network_add_command(const char *cmd, int cb_id)
    {
        return command_queue_instance->add(cmd, cb_id);
    }

    void register_callback(cb_t cb, int id)
    {
        command_queue_instance->registerCallback(cb, id);
    }

}

int CommandQueue::add(const char *cmd, int cb_id)
{
    cmd_t c= {strdup(cmd), (uint8_t)cb_id};
    q.push(c);
    return q.size();
}

// pops the next command off the queue and submits it.
bool CommandQueue::pop()
{
    if (q.size() == 0) return false;

    cmd_t c= q.pop();
    char *cmd= c.str;

    struct SerialMessage message;
    message.message = cmd;
    message.stream = stream_map[c.id];

    free(cmd);
    THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
    message.stream->puts(NULL); // indicates command is done

    return true;
}

void CommandQueue::registerCallback(cb_t cb, int id)
{
    stream_map[id]= new CallbackStream(cb);
}

void CommandQueue::registerCallback(cb_t cb, int id, void *user)
{
    stream_map[id]= new CallbackStream(cb, user);
}
