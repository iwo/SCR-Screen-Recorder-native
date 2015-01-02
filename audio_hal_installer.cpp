#include "audio_hal_installer.h"

bool cmdMatch(int pid, const char *name) {
    char cmdline[1024];
    int fd, r;

    sprintf(cmdline, "/proc/%d/cmdline", pid);
    fd = open(cmdline, O_RDONLY);
    if (fd == 0)
        return false;
    r = read(fd, cmdline, 1023);
    close(fd);
    if (r <= 0) {
        return false;
    }
    cmdline[r] = 0;

    if (!strncmp(cmdline, name, r))
        return true;
    return false;
}

int getProcessPid(const char *name) {
    DIR *d;
    struct dirent *de;

    d = opendir("/proc");
    if(d == 0)
        return -1;

    while((de = readdir(d)) != 0){
        if (!isdigit(de->d_name[0]))
            continue;
        int pid = atoi(de->d_name);
        if (pid != 0 && cmdMatch(pid, name)) {
            closedir(d);
            return pid;
        }
    }
    closedir(d);
    return -1;
}

bool waitForProcessStop(int pid, int waitTime, int maxWait) {
    int totalWait = 0;
    while (totalWait < maxWait) {
        if (kill(pid, 0) == -1) {
            return true;
        }
        usleep(waitTime);
        totalWait += waitTime;
    }
    return false;
}

int getMediaServerPid() {
    return getProcessPid("/system/bin/mediaserver");
}

bool moveFile(const char* src, const char* dst) {
    if (!rename(src, dst)) {
        ALOGV("\t%s => %s", src, dst);
        return true;
    }
    ALOGE("\t%s => %s error: %s", src, dst, strerror(errno));
    return false;
}

bool fileExists(const char* path) {
    if (!access(path, R_OK))
        return true;

    if (errno != ENOENT) {
        ALOGE("error accessing a file %s %s", path, strerror(errno));
    }
    return false;
}

bool copyFile(const char* src, const char* dst) {
    char buf[BUFSIZ];
    ssize_t size;
    bool success = true;

    int srcFd = open(src, O_RDONLY, 0);
    if (srcFd < 0) {
        ALOGE("Can't open source file %s error: %s", src, strerror(errno));
        return false;
    }
    int dstFd = open(dst, O_WRONLY | O_CREAT, 0644);
    if (dstFd < 0) {
        ALOGE("Can't open destination file %s error: %s", dst, strerror(errno));
        close(srcFd);
        return false;
    }

    while ((size = read(srcFd, buf, BUFSIZ)) > 0) {
        write(dstFd, buf, size);
    }

    if (size == -1) {
        ALOGE("Error copying %s to %s error: %s", src, dst, strerror(errno));
        success = false;
    } else {
        ALOGV("\tcopy %s to %s", src, dst);
    }
    close(srcFd);
    close(dstFd);
    chmod(dst, 0644);
    return success;
}

bool removeFile(const char* path) {
    if (unlink(path)) {
        ALOGE("error deleting file %s error: %s", path, strerror(errno));
        return false;
    }
    return true;
}

bool symlinkRwFiles(const char *baseDir) {
    char confPath [1024];
    char logPath [1024];
    sprintf(confPath, "%.950s/scr_audio.conf", baseDir);
    sprintf(logPath, "%.950s/scr_audio.log", baseDir);

    if (fileExists("/system/lib/hw/scr_audio.conf")) {
        removeFile("/system/lib/hw/scr_audio.conf");
    }

    if (fileExists("/system/lib/hw/scr_audio.log")) {
        removeFile("/system/lib/hw/scr_audio.log");
    }

    ALOGV("Creating rw files symlinks");
    if (symlink(confPath, "/system/lib/hw/scr_audio.conf") || symlink(logPath, "/system/lib/hw/scr_audio.log")) {
        ALOGE("Error creating symlink %s", strerror(errno));
        return false;
    }
    return true;
}

bool moveOriginalModules() {
    DIR *d;
    struct dirent *de;
    char src[512];
    char dst[512];

    ALOGV("Moving original audio drivers");

    d = opendir("/system/lib/hw/");
    if(d == 0)
        return -1;

    while((de = readdir(d)) != 0){
        if (strncmp(de->d_name, "audio.primary.", 13))
            continue;
        sprintf(src, "/system/lib/hw/audio.primary.%s", de->d_name + 14);
        sprintf(dst, "/system/lib/hw/audio.original_primary.%s", de->d_name + 14);
        if (fileExists(dst)) {
            ALOGV("%s file already exists. Skipping.", dst);
        } else {
            if (!moveFile(src, dst)) {
                closedir(d);
                return false;
            }
        }
    }
    closedir(d);
    return true;
}

bool restoreOriginalModules() {
    DIR *d;
    struct dirent *de;
    char src[512];
    char dst[512];
    bool success = true;

    ALOGV("Restoring original audio drivers");

    d = opendir("/system/lib/hw/");
    if(d == 0)
        return -1;

    while((de = readdir(d)) != 0){
        if (strncmp(de->d_name, "audio.original_primary.", 22))
            continue;
        sprintf(src, "/system/lib/hw/audio.original_primary.%s", de->d_name + 23);
        sprintf(dst, "/system/lib/hw/audio.primary.%s", de->d_name + 23);
        if (fileExists(dst)) {
            removeFile(dst);
            chmod(dst, 0644);
        }
        success = moveFile(src, dst) && success;
        chmod(dst, 0644);
    }
    closedir(d);
    return success;
}

int waitForMediaServerPid() {
    int retryCount = 0;
    int pid = -1;

    while (pid == -1 && retryCount++ < 100) {
        usleep(100000);
        pid = getMediaServerPid();
    }
    return pid;
}

void backupAudioPolicyFile(const char* src, const char* dst) {
    if (fileExists(src) && !fileExists(dst)) {
        ALOGV("Moving original audio policy file %s to %s", src, dst);
        moveFile(src, dst);
    }
}

void restoreAudioPolicyFile(const char* src, const char* dst) {
    if (!fileExists(src)) {
        return;
    }
    if (fileExists(dst)) {
        removeFile(dst);
    }
    ALOGV("Restoring original audio policy file %s to %s", src, dst);
    moveFile(src, dst);
    chmod(dst, 0644);
}

void stopMediaServer() {
    ALOGV("Restarting ms");
    int pid = getMediaServerPid();
    if (pid == -1) {
        ALOGV("No ms running!");
    } else {
        kill(pid, SIGTERM);
        if (!waitForProcessStop(pid, 500000, 5000000)) {
             kill(pid, SIGKILL);
             waitForProcessStop(pid, 100000, 1000000);
        }
    }
}

bool isProcessWriting(int pid, dev_t device, const char *fd) {
    char procFd[32];
    sprintf(procFd, "/proc/%d/fd/%s", pid, fd);
    struct stat st, lst;

    if (stat(procFd, &st) != 0) {
        ALOGE("Can't access %s : %s", procFd, strerror(errno));
        return false;
    }
    if (lstat(procFd, &lst) != 0) {
        ALOGE("Can't access link %s : %s", procFd, strerror(errno));
        return false;
    }
    if (st.st_dev == device && (lst.st_mode & S_IWUSR)) {
        char *path = realpath(procFd, NULL);
        if (path == NULL) {
            ALOGE("Error resolving path %s %s", procFd, strerror(errno));
        } else {
            ALOGW("%d writting to %s", pid, path);
            free(path);
        }
        return true;
    }
    return false;
}

bool isProcessWriting(int pid, dev_t device) {
    DIR *d;
    struct dirent *de;
    char procFd[32];

    sprintf(procFd, "/proc/%d/fd", pid);

    d = opendir(procFd);
    if (d == 0) {
        ALOGW("Error opening %s %s", procFd, strerror(errno));
        return false;
    }

    while ((de = readdir(d)) != 0) {
        if (isProcessWriting(pid, device, de->d_name)) {
            closedir(d);
            return true;
        }
    }
    closedir(d);
    return false;
}

int killWritingProcesses(const char *path) {
    DIR *d;
    struct dirent *de;
    struct stat st;

    if (stat(path, &st) != 0) {
        ALOGE("Can't access %s : %s", path, strerror(errno));
        return -1;
    }

    dev_t device = st.st_dev;

    d = opendir("/proc");
    if (d == 0)
        return -1;

    while ((de = readdir(d)) != 0) {
        if (!isdigit(de->d_name[0]))
            continue;
        int pid = atoi(de->d_name);
        if (isProcessWriting(pid, device)) {
            if (pid == getpid()) {
                ALOGE("We're still writing!");
            } else {
                char cmdline[1024];
                int fd, r = 0;

                sprintf(cmdline, "/proc/%d/cmdline", pid);
                fd = open(cmdline, O_RDONLY);
                if (fd != 0) {
                    r = read(fd, cmdline, 1023);
                    close(fd);
                }
                cmdline[r > 0 ? r : 0] = '\0';
                ALOGW("Killing %d %s", pid, cmdline);
                kill(pid, SIGKILL);
            }
        }
    }
    closedir(d);
    return -1;
}

bool remountReadOnly() {
    ALOGV("Flushing filesystem changes");
    sync();

    ALOGV("Mounting system partition in read-only mode");
    if (mount(NULL, "/system", NULL, MS_REMOUNT | MS_RDONLY, 0)) {
        bool busy = (errno == EBUSY);
        ALOGW("Error mounting read-only /system filesystem. error: %s", strerror(errno));
        if (busy) {
            killWritingProcesses("/system/");
            if (mount(NULL, "/system", NULL, MS_REMOUNT | MS_RDONLY, 0)) {
                ALOGE("Fatal error mounting read-only /system filesystem. error: %s", strerror(errno));
                return false;
            }
        }
    }

    return true;
}

int installAudioHAL(const char *baseDir) {
    ALOGV("Installing SCR audio HAL");
    char modulePath [1024];
    char policyPath [1024];
    char uninstallPath [1024];

    sprintf(modulePath, "%.950s/audio.scr_primary.default.so", baseDir);
    sprintf(policyPath, "%.950s/audio_policy.conf", baseDir);
    sprintf(uninstallPath, "%.950s/deinstaller.sh", baseDir);

    ALOGV("Mounting system partition in read-write mode");
    if (mount(NULL, "/system", NULL, MS_REMOUNT | MS_NOATIME, 0)) {
        ALOGE("Error mounting /system filesystem. error: %s", strerror(errno));
        return 167;
    }

    ALOGV("Copying uninstall script");
    if (!copyFile(uninstallPath, "/system/lib/hw/uninstall_scr.sh")){
        remountReadOnly();
        return 174;
    }
    chmod("/system/lib/hw/uninstall_scr.sh", 0655);

    if (!symlinkRwFiles(baseDir)) {
        remountReadOnly();
        return 173;
    }

    if (!moveOriginalModules()) {
        restoreOriginalModules();
        remountReadOnly();
        return 168;
    }

    ALOGV("Copying SCR audio driver");
    if (!copyFile(modulePath, "/system/lib/hw/audio.primary.default.so")){
        restoreOriginalModules();
        remountReadOnly();
        return 169;
    }

    if (fileExists(policyPath)) {
        ALOGV("Installing audio policy file");
        backupAudioPolicyFile("/system/etc/audio_policy.conf", "/system/etc/audio_policy.conf.back");
        backupAudioPolicyFile("/vendor/etc/audio_policy.conf", "/vendor/etc/audio_policy.conf.back");
        if (!copyFile(policyPath, "/system/etc/audio_policy.conf")) {
            restoreAudioPolicyFile("/system/etc/audio_policy.conf.back", "/system/etc/audio_policy.conf");
            restoreAudioPolicyFile("/vendor/etc/audio_policy.conf.back", "/vendor/etc/audio_policy.conf");
            remountReadOnly();
            return 170;
        }
    }

    bool readOnly = remountReadOnly();

    stopMediaServer();
    int pid = waitForMediaServerPid();

    if (pid == -1) {
        ALOGE("ms process not showing up!");
        uninstallAudioHAL();
        return 171;
    }

    ALOGV("Waiting 3s to see if %d won't die", pid);
    if (waitForProcessStop(pid, 250000, 3000000)) {
        ALOGE("ms process died!");
        uninstallAudioHAL();
        return 172;
    }

    ALOGV("Installed!");
    return readOnly ? 0 : 202;
}

int uninstallAudioHAL() {
    ALOGV("Uninstalling SCR audio HAL");

    ALOGV("Mounting system partition in read-write mode");
    if (mount(NULL, "/system", NULL, MS_REMOUNT | MS_NOATIME, 0)) {
        ALOGE("Error mounting /system filesystem");
        return 167;
    }

    restoreOriginalModules();

    restoreAudioPolicyFile("/system/etc/audio_policy.conf.back", "/system/etc/audio_policy.conf");
    restoreAudioPolicyFile("/vendor/etc/audio_policy.conf.back", "/vendor/etc/audio_policy.conf");

    removeFile("/system/lib/hw/scr_audio.log");
    removeFile("/system/lib/hw/scr_audio.conf");
    removeFile("/system/lib/hw/uninstall_scr.sh");

    stopMediaServer();

    bool readOnly = remountReadOnly();

    ALOGV("Uninstalled!");
    return readOnly ? 0 : 202;
}

int mountAudioHAL(const char *baseDir) {
    ALOGV("Soft-Installing SCR audio HAL");
    char vendorPolicyPath [1024];
    char systemPolicyPath [1024];

    sprintf(vendorPolicyPath, "%.950s/vendor_audio_policy.conf", baseDir);
    sprintf(systemPolicyPath, "%.950s/system_audio_policy.conf", baseDir);

    ALOGV("Linking modules dir");
    if (mount(baseDir, "/system/lib/hw", NULL, MS_BIND, 0)) {
        ALOGE("Error linking modules directory. error: %s", strerror(errno));
        return 174;
    }

    if (fileExists("/vendor/etc/audio_policy.conf") && fileExists(vendorPolicyPath)) {
        ALOGV("Linking vendor audio policy file");
        if (mount(vendorPolicyPath, "/vendor/etc/audio_policy.conf", NULL, MS_BIND, 0)) {
            ALOGE("Error linking policy file. error: %s", strerror(errno));
            return 175;
        }
    }

    if (fileExists("/system/etc/audio_policy.conf") && fileExists(systemPolicyPath)) {
        ALOGV("Linking system audio policy file");
        if (mount(systemPolicyPath, "/system/etc/audio_policy.conf", NULL, MS_BIND, 0)) {
            ALOGE("Error linking policy file. error: %s", strerror(errno));
            return 176;
        }
    }

    ALOGV("Soft-Installed!");

    return 0;
}

void forceUnmount(const char *path) {
    if (umount2(path, MNT_DETACH)) {
        ALOGE("Can't uninstall %s error: %s", path, strerror(errno));
    }
}

int unmountAudioHAL() {
    forceUnmount("/system/lib/hw");
    forceUnmount("/system/etc/audio_policy.conf");
    if (fileExists("/vendor/etc/audio_policy.conf")) {
        forceUnmount("/vendor/etc/audio_policy.conf");
    }
    return 0;
}

bool crashUnmount(const char *path) {
    if (umount2(path, MNT_DETACH) == 0) {
        ALOGV("Uninstalled: %s", path);
        return true;
    }
    return false;
}

void forkUmountProcess(const char* executablePath) {
    ALOGV("Emergency uninstall");
    char umountCommand[1024];
    sprintf(umountCommand, "%s umount", executablePath);
    pid_t pid = fork();
    int status = -1;
    if (pid == -1) {
        ALOGW("Error forking process: %s ", strerror(errno));
    } else if (pid == 0) {
        if (execlp("su", "su", "--mount-master", "-c", umountCommand, NULL) == -1) {
            ALOGW("Error executing command: %s ", strerror(errno));
        }
    } else {
        waitpid(pid, &status, 0);
        ALOGV("Uninstall process returned %d", status);
    }
}

int crashUnmountAudioHAL(const char* executablePath) {
    bool uninstalled = false;
    uninstalled |= crashUnmount("/system/lib/hw");
    uninstalled |= crashUnmount("/system/etc/audio_policy.conf");
    if (fileExists("/vendor/etc/audio_policy.conf")) {
        uninstalled |= crashUnmount("/vendor/etc/audio_policy.conf");
    }
    if (uninstalled && executablePath != NULL) {
        forkUmountProcess(executablePath);
    }
    return 0;
}
