#ifndef _SIGNAL_H
#define _SIGNAL_H

#define SIGINT     2
#define SIGILL     4
#define SIGFPE     8
#define SIGSEGV   11
#define SIGTERM   15
#define SIGBREAK  21
#define SIGABRT   22

typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);
int raise(int sig);

#endif
