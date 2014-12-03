#ifndef AUDIO_HAL_INSTALLER_H
#define AUDIO_HAL_INSTALLER_H

#include "screenrec.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>

#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <dirent.h>
#include <signal.h>

#include <pwd.h>

int installAudioHAL();
int uninstallAudioHAL();
int mountAudioHAL(const char *baseDir);
int unmountAudioHAL();
int crashUnmountAudioHAL(const char* executablePath);

#endif