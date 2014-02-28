#include "audio_hal_installer.h"

static const char *versionFilePath = "/system/lib/hw/scr_module_version";

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
    int pid = -1;

    d = opendir("/proc");
    if(d == 0)
        return -1;

    while((de = readdir(d)) != 0){
        if (!isdigit(de->d_name[0]))
            continue;
        pid = atoi(de->d_name);
        if (pid != 0 && cmdMatch(pid, name))
            break;
    }
    closedir(d);
    return pid > 0 ? pid : -1;
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
        ALOGV("\t%s => %s\n", src, dst);
        return true;
    }
    ALOGE("\t%s => %s error: %s\n", src, dst, strerror(errno));
    return false;
}

bool fileExists(const char* path) {
    if (!access(path, R_OK))
        return true;

    if (errno != ENOENT) {
        ALOGE("error accessing a file %s %s\n", path, strerror(errno));
    }
    return false;
}

bool copyFile(const char* src, const char* dst) {
    char buf[BUFSIZ];
    ssize_t size;
    bool success = true;

    int srcFd = open(src, O_RDONLY, 0);
    if (srcFd < 0) {
        ALOGE("Can't open source file %s error: %s\n", src, strerror(errno));
        return false;
    }
    int dstFd = open(dst, O_WRONLY | O_CREAT, 0644);
    if (dstFd < 0) {
        ALOGE("Can't open destination file %s error: %s\n", dst, strerror(errno));
        close(srcFd);
        return false;
    }

    while ((size = read(srcFd, buf, BUFSIZ)) > 0) {
        write(dstFd, buf, size);
    }

    if (size == -1) {
        ALOGE("Error copying %s to %s error: %s\n", src, dst, strerror(errno));
        success = false;
    } else {
        ALOGV("\tcopy %s to %s\n", src, dst);
    }
    close(srcFd);
    close(dstFd);
    chmod(dst, 0644);
    return success;
}

bool removeFile(const char* path) {
    if (unlink(path)) {
        ALOGE("error deleting file %s error: %s\n", path, strerror(errno));
        return false;
    }
    return true;
}

bool moveOriginalModules() {
    DIR *d;
    struct dirent *de;
    char src[512];
    char dst[512];

    ALOGV("Moving original audio drivers\n");

    d = opendir("/system/lib/hw/");
    if(d == 0)
        return -1;

    while((de = readdir(d)) != 0){
        if (strncmp(de->d_name, "audio.primary.", 13))
            continue;
        sprintf(src, "/system/lib/hw/audio.primary.%s", de->d_name + 14);
        sprintf(dst, "/system/lib/hw/audio.original_primary.%s", de->d_name + 14);
        if (fileExists(dst)) {
            ALOGV("%s file already exists. Skipping.\n", dst);
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

    ALOGV("Restoring original audio drivers\n");

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
        ALOGV("Moving original audio policy file %s to %s\n", src, dst);
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
    ALOGV("Restoring original audio policy file %s to %s\n", src, dst);
    moveFile(src, dst);
    chmod(dst, 0644);
}

void stopMediaServer() {
    ALOGV("Restarting Media Server\n");
    int pid = getMediaServerPid();
    if (pid == -1) {
        ALOGV("No mediaserver running!\n");
    } else {
        kill(pid, SIGTERM);
        if (!waitForProcessStop(pid, 500000, 5000000)) {
             kill(pid, SIGKILL);
             waitForProcessStop(pid, 100000, 1000000);
        }
    }
}

void addVersionFile(int version) {
    FILE *f = fopen(versionFilePath, "w");

    if (f == NULL) {
        ALOGE("Can't create version file");
        return;
    }

    fprintf(f, "%d", version);
    fflush(f);
    fclose(f);
    chmod(versionFilePath, 0644);
}

int installAudioHAL() {
    ALOGV("Installing SCR audio HAL\n");
    char baseDir [1024];
    char modulePath [1024];
    char policyPath [1024];

    if (fgets(baseDir, 1023, stdin) == NULL) {
        ALOGE("No base directory specified");
        return 173;
    }
    trim(baseDir);

    sprintf(modulePath, "%.950s/audio.primary.default.so", baseDir);
    sprintf(policyPath, "%.950s/audio_policy.conf", baseDir);

    int version = 0;
    char versionString [16];
    if (fgets(versionString, 15, stdin) != NULL) {
        version = atoi(versionString);
    } else {
        ALOGW("No driver version specified");
    }

    ALOGV("Mounting system partition in read-write mode\n");
    if (mount(NULL, "/system", NULL, MS_REMOUNT, 0)) {
        ALOGE("Error mounting /system filesystem. error: %s\n", strerror(errno));
        return 167;
    }

    if (!moveOriginalModules()) {
        restoreOriginalModules();
        return 168;
    }

    ALOGV("Copying SCR audio driver\n");
    if (!copyFile(modulePath, "/system/lib/hw/audio.primary.default.so")){
        restoreOriginalModules();
        return 169;
    }

    if (fileExists(policyPath)) {
        ALOGV("Installing audio policy file\n");
        backupAudioPolicyFile("/system/etc/audio_policy.conf", "/system/etc/audio_policy.conf.back");
        backupAudioPolicyFile("/vendor/etc/audio_policy.conf", "/vendor/etc/audio_policy.conf.back");
        if (!copyFile(policyPath, "/system/etc/audio_policy.conf")) {
            restoreAudioPolicyFile("/system/etc/audio_policy.conf.back", "/system/etc/audio_policy.conf");
            restoreAudioPolicyFile("/vendor/etc/audio_policy.conf.back", "/vendor/etc/audio_policy.conf");
            return 170;
        }
    }

    stopMediaServer();
    int pid = waitForMediaServerPid();

    if (pid == -1) {
        ALOGE("Media Server process not showing up!\n");
        uninstallAudioHAL();
        return 171;
    }

    ALOGV("Waiting 10s to see if mediaserver process %d won't die\n", pid);
    if (waitForProcessStop(pid, 1000000, 10000000)) {
        ALOGE("Meida Server process died!\n");
        uninstallAudioHAL();
        return 172;
    }

    addVersionFile(version);

    ALOGV("Installed!\n");
    return 0;
}

int uninstallAudioHAL() {
    ALOGV("Uninstalling SCR audio HAL\n");

    ALOGV("Mounting system partition in read-write mode\n");
    if (mount(NULL, "/system", NULL, MS_REMOUNT, 0)) {
        ALOGE("Error mounting /system filesystem\n");
        return 167;
    }

    removeFile(versionFilePath);

    restoreOriginalModules();

    restoreAudioPolicyFile("/system/etc/audio_policy.conf.back", "/system/etc/audio_policy.conf");
    restoreAudioPolicyFile("/vendor/etc/audio_policy.conf.back", "/vendor/etc/audio_policy.conf");

    stopMediaServer();

    ALOGV("Uninstalled!\n");
    return 0;
}

int mountAudioHAL() {
    ALOGV("Soft-Installing SCR audio HAL\n");
    char baseDir [1024];
    char modulesPath [1024];
    char policyPath [1024];

    if (fgets(baseDir, 1023, stdin) == NULL) {
        ALOGE("No base directory specified");
        return 173;
    }
    trim(baseDir);

    sprintf(modulesPath, "%.950s/hw", baseDir);
    sprintf(policyPath, "%.950s/audio_policy.conf", baseDir);

    ALOGV("Linking modules dir");
    if (mount(modulesPath, "/system/lib/hw", NULL, MS_BIND, 0)) {
        ALOGE("Error linking modules directory. error: %s", strerror(errno));
        return 174;
    }

    if (fileExists(policyPath)) {
        ALOGV("Linking audio policy file\n");
        if (fileExists("/vendor/etc/audio_policy.conf")) {
            if (mount(policyPath, "/vendor/etc/audio_policy.conf", NULL, MS_BIND, 0)) {
                ALOGE("Error linking policy file. error: %s", strerror(errno));
                unmountAudioHAL();
                return 175;
            }
        }
        if (fileExists("/system/etc/audio_policy.conf")) {
            if (mount(policyPath, "/system/etc/audio_policy.conf", NULL, MS_BIND, 0)) {
                ALOGE("Error linking policy file. error: %s", strerror(errno));
                unmountAudioHAL();
                return 176;
            }
        }
    }

    stopMediaServer();
    int pid = waitForMediaServerPid();

    if (pid == -1) {
        ALOGE("Media Server process not showing up!\n");
        unmountAudioHAL();
        return 177;
    }

    ALOGV("Waiting 10s to see if mediaserver process %d won't die\n", pid);
    if (waitForProcessStop(pid, 1000000, 10000000)) {
        ALOGE("Meida Server process died!\n");
        unmountAudioHAL();
        return 178;
    }

    ALOGV("Soft-Installed!\n");

    return 0;
}

int unmountAudioHAL() {
    umount("/system/lib/hw");
    umount("/system/etc/audio_policy.conf");
    umount("/vendor/etc/audio_policy.conf");
    return 0;
}
