#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <windows.h>
#include <map>
#include <mutex>
#include <stddef.h>
#include <vector>

struct ShmInfo
{
    HANDLE hMap;
    void *pMem;
    size_t size;
    int key;
};

static std::map<int, ShmInfo> g_shmMap;

struct SemInfo
{
    std::vector<HANDLE> handles;
    std::vector<int> values;
};

namespace IpcBridge
{
    void initBridges();
}

/* A POSIX message queue whose reader is a cabinet daemon the loader does not
 * run.  Undrained, it fills in seconds and mq_send() then answers EAGAIN, so
 * registering a consumer makes the loader stand in for that reader. */
extern "C"
{
    typedef void (*IpcQueueConsumer)(const char *name, const void *data, size_t length,
                                     unsigned priority);

    /* Pass NULL to remove. Consumers may be registered before the guest opens
     * the queue; the name is matched when a message is sent. */
    void ipcSetQueueConsumer(const char *name, IpcQueueConsumer consumer);
}

/*
 * The i386 Linux layout of the third semop()/semctl() argument.  Only the
 * fields the guest fills are named; the union member it passes for semctl is
 * always the first word.
 */
struct LinuxSembuf
{
    unsigned short sem_num;
    short sem_op;
    short sem_flg;
};

struct LinuxMqAttr
{
    long flags;
    long maxmsg;
    long msgsize;
    long curmsgs;
};

extern "C"
{
    int bridgeShmget(int key, size_t size, int shmflg);
    void *bridgeShmat(int shmid, const void *shmaddr, int shmflg);
    int bridgeShmctl(int shmid, int cmd, void *buf);
    int bridgeShmdt(const void *shmaddr);

    int bridgeFtok(const char *pathname, int projectId);
    int bridgeSemget(int key, int semaphoreCount, int flags);
    int bridgeSemop(int semaphoreId, struct LinuxSembuf *operations, unsigned int count);
    int bridgeSemctl(int semaphoreId, int semaphoreNumber, int command, ...);

    int bridgeMqOpen(const char *name, int flags, ...);
    int bridgeMqClose(int descriptor);
    int bridgeMqUnlink(const char *name);
    int bridgeMqSend(int descriptor, const char *message, size_t length, unsigned priority);
    int bridgeMqReceive(int descriptor, char *buffer, size_t length, unsigned *priority);
    int bridgeMqGetattr(int descriptor, struct LinuxMqAttr *attribute);
    int bridgeMqNotify(int descriptor, const void *notification);
};
