#include "shell.h"

int main(int argc, char* argv[]) {
    setupSELinux();
    signal(SIGPIPE, sigPipeHandler);
    signal(SIGCHLD, sigChldHandler);
    set_sched_policy(0, SP_FOREGROUND);
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    if (argc == 2 && strncmp(argv[1], "umount", 6) == 0) {
        int ret = crashUnmountAudioHAL(NULL);
        restoreSELinux();
        return ret;
    }

    //TODO: handle test mode

    shellSetState("READY");
    char commandBuffer[MAX_COMMAND_LENGTH];
    workerPid = -1;

    while (true) {
        char* cmd = fgets(commandBuffer, MAX_COMMAND_LENGTH, stdin);
        if (cmd == NULL) {
            //TODO: handle error
            break;
        }
        if (strlen(cmd) > 0) {
            cmd[strlen(cmd) - 1] = '\0'; // remove \n
        }

        if (strncmp(cmd, "start ", 6) == 0) {

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
            if (workerPid > 0) {
                kill(workerPid, SIGINT);
                //TODO: make sure worker exits timely or kill it
            } else {
                ALOGE("no worker process to stop");
            }
        } else if (strncmp(cmd, "logcat ", 7) == 0) {
            runLogcat(cmd + 7);
        } else if (strncmp(cmd, "mount_audio ", 12) == 0) {
            commandForResult(cmd, mountAudioHAL(cmd + 12));
        } else if (strncmp(cmd, "unmount_audio", 13) == 0) {
            commandForResult(cmd, unmountAudioHAL());
        } else if (strncmp(cmd, "kill_kill ", 10) == 0) {
            commandForResult(cmd, killStrPid(cmd + 10, SIGKILL));
        } else if (strncmp(cmd, "kill_term ", 10) == 0) {
            commandForResult(cmd, killStrPid(cmd + 10, SIGTERM));
        } else if (strncmp(cmd, "quit", 4) == 0) {
            break;
        } else {
            ALOGE("unknown command %s", cmd);
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
    int status;
    pid_t pid = waitpid(-1, &status, 0);
    if (pid < 0) {
        ALOGV("no child process info");
    } else if (pid == workerPid) {
        workerPid = -1;
        if (WIFEXITED(status)) {
            int exitStatus = WEXITSTATUS(status);
            if (exitStatus == 0) {
                shellSetState("FINISHED");
            } else {
                shellSetError(exitStatus);
            }
            ALOGV("worker stopped normally, return %d", exitStatus);
        } else if (WIFSIGNALED(status)) {
            shellSetError(128 + WTERMSIG(status));
            ALOGE("worker stopped by signal", strsignal(WTERMSIG(status)));
        } else {
            shellSetError(194);
            ALOGE("worker stopped abnormally");
        }
        shellSetState("READY");
    } else if (pid == logcatPid) {
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            commandSuccess("logcat");
        } else {
            commandError("logcat", WEXITSTATUS(status));
        }
    } else {
        ALOGE("unknown process exit %d", pid);
    }
}

void runLogcat(char *path) {
    logcatPid = fork();
    if (logcatPid == 0) {
        execlp("logcat", "logcat", "-d", "-f", path, "*:V", NULL);
        commandError("logcat", "exec error");
    } else if (logcatPid < 0) {
        commandError("logcat", "fork error");
    }
}

void commandForResult(const char *command, int exitValue) {
    if (exitValue == 0) {
        commandSuccess(command);
    } else {
        commandError(command, exitValue);
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

inline void commandSuccess(const char* command) {
    ALOGV("command success %s", command);
    printf("command success %s\n", command);
    fflush(stdout);
}

inline void commandError(const char* command, const char* error) {
    ALOGV("command error %s %s", command, error);
    printf("command error %s %s\n", command, error);
    fflush(stdout);
}

inline void commandError(const char* command, int errorCode) {
    ALOGV("command error %s %d", command, errorCode);
    printf("command error %s %d\n", command, errorCode);
    fflush(stdout);
}
