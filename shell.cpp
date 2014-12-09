#include "shell.h"

int main(int argc, char* argv[]) {
    setupSELinux();
    signal(SIGPIPE, sigPipeHandler);
    signal(SIGCHLD, sigChldHandler);
    set_sched_policy(0, SP_FOREGROUND);
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    if (argc == 2 && strncmp(argv[1], "unmount_audio", 13) == 0) {
        int ret = unmountAudioHAL();
        restoreSELinux();
        return ret;
    }

    if (argc == 3 && strncmp(argv[1], "mount_audio", 11) == 0) {
        int ret = mountAudioHAL(argv[2]);
        restoreSELinux();
        return ret;
    }

    if (argc == 2 && strncmp(argv[1], "umount", 6) == 0) {
        int ret = crashUnmountAudioHAL(NULL);
        restoreSELinux();
        return ret;
    }

    getSuVersion();

    //TODO: handle test mode

    shellSetState("READY");
    char commandBuffer[MAX_COMMAND_LENGTH];
    workerPid = -1;

    while (true) {
        ALOGV("#");
        char* cmd = fgets(commandBuffer, MAX_COMMAND_LENGTH, stdin);
        if (cmd == NULL) {
            //TODO: handle error
            break;
        }
        if (strlen(cmd) > 0) {
            cmd[strlen(cmd) - 1] = '\0'; // remove \n
        }

        if (strncmp(cmd, "start ", 6) == 0) {
            ALOGV("start");
            if (workerPid != -1) {
                ALOGE("Looks like we have some runaway worker process %d", workerPid);
            }

            workerPid = fork();
            if (workerPid == 0) {
                // child
                shellSetState("STARTING");
                return startRecording(cmd + 6);
            } else if (workerPid > 0) {
                // parent
            } else {
                // error
                //TODO: report error
                break;
            }
        } else if (strncmp(cmd, "stop", 4) == 0) {
            ALOGV("stop");
            if (workerPid > 0) {
                kill(workerPid, SIGINT);
            } else {
                ALOGE("no worker process to stop");
            }
        } else if (strncmp(cmd, "force_stop", 10) == 0) {
            ALOGV("force stop");
            if (workerPid > 0) {
                kill(workerPid, SIGKILL);
            } else {
                ALOGE("no worker process to stop");
            }
        } else if (strncmp(cmd, "quit", 4) == 0) {
            break;
        } else {
            // commands
            int requestId;
            int argsPos;

            if (sscanf(cmd, "%*64s %d%*[ ]%n", &requestId, &argsPos) != 1) {
                ALOGE("Error parsing command %s", cmd);
                continue;
            }

            if (strncmp(cmd, "logcat ", 7) == 0) {
                ALOGV("%s", cmd);
                logcatRequestId = requestId;
                runLogcat(cmd + argsPos);
            } else if (strncmp(cmd, "mount_audio_master ", 19) == 0) {
                ALOGV("soft-install audio async");
                mountMasterRequestId = requestId;
                runMountMaster(argv[0], "mount_audio", cmd + argsPos);
            } else if (strncmp(cmd, "mount_audio ", 12) == 0) {
                ALOGV("soft-install audio");
                commandResult("mount_audio", requestId, mountAudioHAL(cmd + 12));
            } else if (strncmp(cmd, "unmount_audio_master ", 21) == 0) {
                ALOGV("soft-uninstall audio async");
                mountMasterRequestId = requestId;
                runMountMaster(argv[0], "unmount_audio", NULL);
            } else if (strncmp(cmd, "unmount_audio", 13) == 0) {
                ALOGV("soft-uninstall audio");
                commandResult(cmd, requestId, unmountAudioHAL());
            } else if (strncmp(cmd, "kill_kill ", 10) == 0) {
                ALOGV("%s", cmd);
                commandResult(cmd, requestId, killStrPid(cmd + argsPos, SIGKILL));
            } else if (strncmp(cmd, "kill_term ", 10) == 0) {
                ALOGV("%s", cmd);
                commandResult(cmd, requestId, killStrPid(cmd + argsPos, SIGTERM));
                ALOGV("%s", cmd);
            } else {
                ALOGE("unknown command %s", cmd);
            }
        }
    }

    shellSetState("DONE");
    restoreSELinux();
    return 0;
}

void setupSELinux() {
#if SCR_SDK_VERSION >= 18
    selinuxEnforcing = security_getenforce();
    ALOGV("SELinux enforcing %d", selinuxEnforcing);
    char *con;
    if (getpidcon(getpid(), &con) < 0) {
        ALOGV("Can't fetch context\n");
    } else {
        ALOGV("Running in SELinux context %s\n", con);
        freecon(con);
    }
    if (selinuxEnforcing > 0) {
        ALOGW("Disabling SELinux enforcing");
        security_setenforce(0);
    }
    #endif
}

void restoreSELinux() {
#if SCR_SDK_VERSION >= 18
    if (selinuxEnforcing > 0) {
        SLOGW("Restoring SELinux enforcing to %d", selinuxEnforcing);
        security_setenforce(selinuxEnforcing);
    }
    #endif
}

void sigPipeHandler(int param __unused) {
    ALOGE("SIGPIPE received");
    exit(222);
}

void sigChldHandler(int param __unused) {
    int status, exitValue;
    const char *cmd = "unknown process";
    pid_t pid = waitpid(-1, &status, 0);

    if (WIFEXITED(status)) {
        exitValue = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exitValue = 128 + WTERMSIG(status);
    } else {
        exitValue = -1;
    }

    if (pid < 0) {
        ALOGV("no child process info");
    } else if (pid == workerPid) {
        workerPid = -1;
        cmd = "worker";
        if (exitValue == 0) {
            shellSetState("FINISHED");
        } else {
            shellSetError(exitValue);
        }
        shellSetState("READY");
    } else if (pid == logcatPid) {
        logcatPid = -1;
        cmd = "logcat";
        commandResult("logcat", logcatRequestId, exitValue);
    } else if (pid == suPid) {
        suPid = -1;
        cmd = "su";
        char suResult[128];
        int resultSize = read(suPipe[0], suResult, 127);
        if (resultSize >= 0) {
            suResult[resultSize] = '\0';
            printf("su version %s\n", suResult);
            fflush(stdout);
        }
        close(suPipe[0]);
    } else if (pid == mountMasterPid) {
        mountMasterPid = -1;
        if (strcmp(mountMasterCmd, "mount_audio") == 0) {
            cmd = "mount_audio_master";
        } else {
            cmd = "unmount_audio_master";
        }
        commandResult(cmd, mountMasterRequestId, exitValue);
    } else {
        ALOGE("unknown process exit %d", pid);
    }
    if (exitValue == 0) {
        ALOGV("%s finished", cmd);
    } else {
        ALOGE("%s exit value: %d", cmd, exitValue);
    }
}

void runLogcat(char *path) {
    ALOGV("dump logcat to %s", path);
    logcatPid = fork();
    if (logcatPid == 0) {
        execlp("logcat", "logcat", "-d", "-v", "threadtime", "-f", path, "*:V", NULL);
        commandResult("logcat", logcatRequestId, -2);
    } else if (logcatPid < 0) {
        commandResult("logcat", logcatRequestId, -3);
    }
}

void runMountMaster(const char *executable, const char *command, const char *basePath) {
    mountMasterPid = fork();
    mountMasterCmd = command;
    if (mountMasterPid == 0) {
        execlp("su", "su", "--mount-master", "--context", "u:r:init:s0", "-c", executable, command, basePath, NULL);
        commandResult("mount_master", mountMasterRequestId, -2);
    } else if (mountMasterPid < 0) {
        commandResult("mount_master", mountMasterRequestId, -3);
    }
}

void getSuVersion() {
    if (pipe(suPipe) < 0) {
        ALOGE("Error creating pipe!");
        return;
    }
    suPid = fork();
    if (suPid == 0) {
        fclose(stdin);
        if (dup2(suPipe[1], STDOUT_FILENO) == -1) {
            ALOGE("redirecting stdout");
            exit(-1);
        }
        execlp("su", "su", "-v", NULL);
        ALOGE("su exec failed");
        exit(-1);
    } else if (suPid > 0) {
        close(suPipe[1]);
    } else {
        ALOGE("su version fork error");
    }
}

int killStrPid(const char *strPid, int sig) {
    int pid = atoi(strPid);
    if (pid < 1) {
        return -1;
    }
    return kill(pid, sig);
}

void shellSetState(const char* state) {
    ALOGV("%s", state);
    printf("state %s\n", state);
    fflush(stdout);
}

void shellSetError(int errorCode) {
    ALOGE("error %d", errorCode);
    printf("error %d\n", errorCode);
    fflush(stdout);
}

inline void commandResult(const char *command, int requestId, int result) {
    if (result == 0) {
        ALOGV("command result %s %d", command, result);
    } else {
        ALOGW("command result %s %d", command, result);
    }
    printf("command result |%d|%d|%s\n", requestId, result, command);
    fflush(stdout);
}
