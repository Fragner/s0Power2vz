#ifndef MYCONFIG_C
#define MYCONFIG_C

#include <stdio.h>              /* standard library functions for file input and output */
#include <stdlib.h>             /* standard library for the C programming language, */
#include <string.h>             /* functions implementing operations on strings  */
#include <unistd.h>             /* provides access to the POSIX operating system API */
#include <sys/stat.h>           /* declares the stat() functions; umask */
#include <fcntl.h>              /* file descriptors */
#include <syslog.h>             /* send messages to the system logger */
#include <errno.h>              /* macros to report error conditions through error codes */
#include <signal.h>             /* signal processing */
#include <stddef.h>             /* defines the macros NULL and offsetof as well as the types ptrdiff_t, wchar_t, and size_t */
#include <libconfig.h>          /* reading, manipulating, and writing structured configuration files */

#include <sys/ioctl.h>		/* */
#include <stdbool.h>

#include "myConfig.h"
#include "constants.h"
#include "s0Power2vz.h"

int pidFilehandle=0;
const int CMAXINPUTS = 5; //5 inputs supportet
  
/**********************/
extern const char *vzserver;
extern const char *vzpath;
extern const char *vzuuid[64];
extern int vzport;
extern int m_updateTime;
extern int m_debug;

void signal_handler(int sig) {
	switch(sig)
	{
		case SIGHUP:
		syslog(LOG_WARNING, "Received SIGHUP signal.");
		break;
		case SIGINT:
		case SIGTERM:
		syslog(LOG_INFO, "Daemon exiting");
		daemonShutdown();
		exit(EXIT_SUCCESS);
		break;
		default:
		syslog(LOG_WARNING, "Unhandled signal %s", strsignal(sig));
		break;
	}
}

void daemonShutdown() {
	close(pidFilehandle);
	char pid_file[22];
	sprintf ( pid_file, "/tmp/%s.pid", DAEMON_NAME );
	remove(pid_file);
}

void daemonize(char *rundir, char *pidfile) {
	int pid, sid, i;
	char str[10];
	struct sigaction newSigAction;
	sigset_t newSigSet;

	if (getppid() == 1)
	{
		return;
	}

	sigemptyset(&newSigSet);
	sigaddset(&newSigSet, SIGCHLD);
	sigaddset(&newSigSet, SIGTSTP);
	sigaddset(&newSigSet, SIGTTOU);
	sigaddset(&newSigSet, SIGTTIN);
	sigprocmask(SIG_BLOCK, &newSigSet, NULL);

	newSigAction.sa_handler = signal_handler;
	sigemptyset(&newSigAction.sa_mask);
	newSigAction.sa_flags = 0;

	sigaction(SIGHUP, &newSigAction, NULL);
	sigaction(SIGTERM, &newSigAction, NULL);
	sigaction(SIGINT, &newSigAction, NULL);

	pid = fork();
	if (pid < 0)
	{
		exit(EXIT_FAILURE);
	}
	if (pid > 0)
	{
		printf("Child process created: %d\n", pid);
		exit(EXIT_SUCCESS);
	}
	umask(027);

	sid = setsid();
	if (sid < 0)
	{
		exit(EXIT_FAILURE);
	}

	for (i = getdtablesize(); i >= 0; --i)
	{
		close(i);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	chdir(rundir);
	pidFilehandle = open(pidfile, O_RDWR|O_CREAT, 0600);

	if (pidFilehandle == -1 )
	{
		syslog(LOG_INFO, "Could not open PID lock file %s, exiting", pidfile);
		exit(EXIT_FAILURE);
	}

	if (lockf(pidFilehandle,F_TLOCK,0) == -1)
	{
		syslog(LOG_INFO, "Could not lock PID lock file %s, exiting", pidfile);
		exit(EXIT_FAILURE);
	}
	sprintf(str,"%d\n",getpid());
	write(pidFilehandle, str, strlen(str));
}

void cfile(int inputs) {
	config_t cfg;
	config_init(&cfg);
	int chdir(const char *path);
	chdir ("/etc");

	if(!config_read_file(&cfg, DAEMON_NAME".cfg"))
	{
		syslog(LOG_INFO, "Config error > /etc/%s - %s\n", config_error_file(&cfg),config_error_text(&cfg));
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}

	if (!config_lookup_string(&cfg, "vzserver", &vzserver))
	{
		syslog(LOG_INFO, "Missing 'VzServer' setting in configuration file.");
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	else
	syslog(LOG_INFO, "VzServer:%s", vzserver);

	if (!config_lookup_int(&cfg, "vzport", &vzport))
	{
		syslog(LOG_INFO, "Missing 'VzPort' setting in configuration file.");
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	else
    syslog(LOG_INFO, "VzPort:%d", vzport);

	if (!config_lookup_string(&cfg, "vzpath", &vzpath))
	{
		syslog(LOG_INFO, "Missing 'VzPath' setting in configuration file.");
		config_destroy(&cfg);
		daemonShutdown();
		exit(EXIT_FAILURE);
	}
	else
    syslog(LOG_INFO, "VzPath:%s", vzpath);

	if (inputs > CMAXINPUTS) {
		syslog(LOG_INFO, "too many inputs (%i) defined, only %i inputs (GPIOS) supported.", inputs, CMAXINPUTS);
		inputs = CMAXINPUTS;
	}      
	for (int i=0; i<inputs; i++) {
		char gpio[6];
		sprintf ( gpio, "GPIO%01d", i );
		if ( config_lookup_string( &cfg, gpio, &vzuuid[i]) == CONFIG_TRUE )
		syslog ( LOG_INFO, "%s = %s", gpio, vzuuid[i] );
	}

	if (config_lookup_int(&cfg, "updateTime", &m_updateTime)){
		syslog(LOG_INFO, "m_updateTime:%d", m_updateTime);
	} else syslog(LOG_INFO, "m_updateTime:%d not found", m_updateTime);

	if (config_lookup_int(&cfg, "debug", &m_debug)) {
		syslog(LOG_INFO, "m_debug: %d", m_debug);	
	} else syslog(LOG_INFO, "m_debug: %d not found --> use default value", m_debug);	
}

#endif