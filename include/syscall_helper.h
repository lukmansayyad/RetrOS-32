#ifndef __SYSCALL_HELPER_H
#define __SYSCALL_HELPER_H

enum {
    SYSCALL_ALLOC,
    SYSCALL_SCRPUT,
    SYSCALL_PRTPUT,
    SYSCALL_EXIT,
    SYSCALL_SLEEP,
    SYSCALL_GFX_WINDOW,
    SYSCALL_GFX_PUT_CHAR,
    SYSCALL_GFX_GET_TIME,
    SYSCALL_GFX_DRAW,
    SYSCALL_GFX_SET_TITLE,
    SYSCALL_OPEN,
    SYSCALL_READ,
    SYSCALL_WRITE,
    SYSCALL_MALLOC,
    SYSCALL_FREE,
    SYSCALL_CLOSE,
    SYSCALL_GFX_SET_HEADER,
    /* NET system calls */
    SYSCALL_NET_SOCK_CLOSE,
    SYSCALL_NET_SOCK_BIND,
    SYSCALL_NET_SOCK_ACCEPT,
    SYSCALL_NET_SOCK_CONNECT,
    SYSCALL_NET_SOCK_LISTEN,
    SYSCALL_NET_SOCK_RECV,
    SYSCALL_NET_SOCK_RECVFROM,
    SYSCALL_NET_SOCK_RECV_TIMEOUT,
    SYSCALL_NET_SOCK_SEND,
    SYSCALL_NET_SOCK_SENDTO,
    SYSCALL_NET_SOCK_SOCKET,
    SYSCALL_NET_DNS_LOOKUP,
    /* IPC system calls */
    SYSCALL_IPC_OPEN,
    SYSCALL_IPC_CLOSE,
    SYSCALL_IPC_SEND,
    SYSCALL_IPC_RECEIVE,

    /* Thread system calls */
    SYSCALL_CREATE_THREAD,
    SYSCALL_YIELD,
    SYSCALL_JOIN_THREAD,
    SYSCALL_AWAIT_PROCESS,
};

#endif /* __SYSCALL_HELPER_H */
