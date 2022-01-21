#ifndef MYCONFIG_H
#define MYCONFIG_H

/* prototypes */
void signal_handler(int sig);
void daemonShutdown();
void daemonize(char *rundir, char *pidfile);
void cfile(int inputs);  

#endif