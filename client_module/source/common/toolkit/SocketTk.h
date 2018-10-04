#ifndef OPEN_SOCKETTK_H_
#define OPEN_SOCKETTK_H_

#include <common/Common.h>
#include <common/net/sock/Socket.h>
#include <common/toolkit/Time.h>
#include <linux/poll.h>

#define SOCKETTK_ENDPOINTSTR_LEN       24 // size for _endpointAddrToStringNoAlloc()


// forward declarations
struct PollState;
typedef struct PollState PollState;

struct ExternalHelperd;



extern bool SocketTk_initOnce(void);
extern void SocketTk_uninitOnce(void);

extern int SocketTk_poll(PollState* state, int timeoutMS);

extern bool SocketTk_getHostByName(struct ExternalHelperd* helperd, const char* hostname,
   struct in_addr* outIPAddr);
extern bool SocketTk_getHostByAddrStr(const char* hostAddr, struct in_addr* outIPAddr);
extern struct in_addr SocketTk_in_aton(const char* hostAddr);

extern char* SocketTk_ipaddrToStr(struct in_addr* ipaddress);
extern char* SocketTk_endpointAddrToString(struct in_addr* ipaddress, unsigned short port);
extern void SocketTk_endpointAddrToStringNoAlloc(char* buf, unsigned bufLen,
   struct in_addr* ipaddress, unsigned short port);


struct PollState
{
   struct list_head list;
};

static inline void PollState_init(PollState* state)
{
   INIT_LIST_HEAD(&state->list);
}

static inline void PollState_addSocket(PollState* state, Socket* socket, short events)
{
   list_add_tail(&socket->poll._list, &state->list);
   socket->poll._events = events;
   socket->poll.revents = 0;
}

#endif /*OPEN_SOCKETTK_H_*/
