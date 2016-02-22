#ifndef AMQ_H_
#define AMQ_H_

typedef void (*amq_msg_func)(const std::wstring& message);
typedef void (*amq_poll_func)();

int amq_init(const std::wstring& uri,
             amq_msg_func msg_func,
             amq_poll_func poll_func);
void amq_send(const std::wstring& message);
void amq_status(const std::wstring& message);
void amq_waitforfinish();
void amq_mainloop();
void amq_stop();
int amq_shutdown();

#endif
