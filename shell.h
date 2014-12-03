#ifndef SCREENREC_SHELL_H
#define SCREENREC_SHELL_H

#include "screenrec.h"
#include "audio_hal_installer.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>
#include <cutils/log.h>
#include <cutils/sched_policy.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/prctl.h>
#include <stdlib.h>

#if SCR_SDK_VERSION >= 16
#include <selinux/selinux.h>
#endif

#define MAX_COMMAND_LENGTH 1024

// shell state
int selinuxEnforcing;
pid_t workerPid = -1;
pid_t logcatPid = -1;


// shell methods
void setupSELinux();
void restoreSELinux();

void sigPipeHandler(int param);
void sigChldHandler(int param);
void runLogcat(char *path);
void commandForResult(const char *command, int exitValue);
int killStrPid(const char *strPid, int sig);

inline void commandSuccess(const char* command);
inline void commandError(const char* command, const char* error);
inline void commandError(const char* command, int errorCode);
#endif