#include "main.h"

using namespace android;

int startRecording(char *config) {
    ProcessState::self()->startThreadPool();
    signal(SIGINT, sigIntHandler);

    parseConfig(config);

    setupInput();
    adjustRotation();

    printf("rotateView %d verticalInput %d rotation %d\n", rotateView, inputHeight > inputWidth ? 1 : 0, rotation);
    fflush(stdout);

    if (videoEncoder >= 0) {
        if (useGl) {
            output = new GLMediaRecorderOutput();
        } else {
            output = new CPUMediaRecorderOutput();
        }
    } else {
        #ifdef SCR_FFMPEG
            output = new FFmpegOutput();
        #else
            stop(199, "encoder not supported");
        #endif
    }
    output->setupOutput();

    shellSetState("RECORDING");

    startTime = getTimeMs();

    while (mrRunning && !finished) {
        if (restrictFrameRate) {
            waitForNextFrame();
        }
        frameCount++;
        output->renderFrame();
    }

    int recordingTime = getTimeMs() - startTime;
    float fps = -1.0f;
    if (recordingTime > 0) {
        fps = 1000.0f * frameCount / recordingTime;
    }
    printf("fps %f\n", fps);
    fflush(stdout);

    if (testMode) {
        if (errorCode != 0) {
            fps = 0.0f;
        }
        //TODO: use params instead of argv
        //fprintf(stderr, "%ld, %4sx%s, %6s, %s, %2s, %4.1f\n", (long int)time(NULL), argv[1], argv[2], argv[3], argv[4], argv[5], fps);
        fflush(stderr);
    }

    if (!stopping) {
        stop(0, "finished");
    }

    fixFilePermissions();
    ALOGV("main thread completed");

    return errorCode;
}

void parseConfig(const char* config) {
    if ((outputName = strchr(config, '/')) == NULL) {
        stop(196, true, "no output name");
    }
    char mode[8];
    char colorFormat[8];
    int vertical;

    int scanned = sscanf(config, "%d %c %d %d %d %d %d %7s %7s %d %d %d %d",
            &rotation, &audioSource, &reqWidth, &reqHeight, &paddingWidth, &paddingHeight, &frameRate, mode,
            colorFormat, &videoBitrate, &audioSamplingRate, &videoEncoder, &vertical);

    if (scanned != 13) {
        stop(195, true, "params parse error");
    }

    if (frameRate == -1) {
        restrictFrameRate = false;
        frameRate = FRAME_RATE;
    } else if (frameRate <= 0 || frameRate > 100) {
        frameRate = FRAME_RATE;
    }

    initializeTransformation(mode);

    if (colorFormat[0] == 'B') { //BGRA
        useBGRA = true;
    }

    if (videoBitrate == 0) {
        videoBitrate = 10000000;
    }

    if (audioSamplingRate == 0) {
        audioSamplingRate = 16000;
    }

    allowVerticalFrames = (vertical > 0);

    if (videoEncoder < 0) {
        useOes = false;
    }

    ALOGI("SETTINGS rotation: %d, audioSource: %c, resolution: %d x %d, padding: %d x %d, frameRate: %d, mode: %s, colorFix: %d, videoEncoder: %d, verticalFrames: %d",
            rotation, audioSource, reqWidth, reqHeight, paddingWidth, paddingHeight, frameRate, useGl ? "GPU" : "CPU", useBGRA, videoEncoder, allowVerticalFrames);
}

void initializeTransformation(char *transformation) {
    if (transformation[0] == 'C') { //CPU
        useGl = false;
    } else if (transformation[0] == 'O') { //OES
        #if SCR_SDK_VERSION >= 18
        useOes = true;
        #endif
    } else if (transformation[0] == 'S') { // YUV_SP
        useGl = false;
        useYUV_SP = true;
    } else if (transformation[0] == 'P') { // YUV_P
        useGl = false;
        useYUV_P = true;
    }
}

void sigIntHandler(int param __unused) {
    shellSetState("STOPPING");
    finished = true;
}

void stop(int error, const char* message) {
    stop(error, true, message);
}

void stop(int error, bool fromMainThread, const char* message) {
    if (error != 0) {
        shellSetError(error);
    }

    //fprintf(stderr, "%d - stop requested from thread %s\n", error, getThreadName());
    //fflush(stderr);

    if (error == 0) {
        ALOGV("%s - stopping\n", message);
    } else {
        ALOGE("%d - stopping\n", error);
    }

    if (stopping) {
        if (errorCode == 0 && error != 0) {
            errorCode = error;
        }
        ALOGV("Already stopping");
        return;
    }

    stopping = true;
    errorCode = error;

    if (output != NULL) {
        output->closeOutput(fromMainThread);
    }
    closeInput();

    if (fromMainThread) {
        fixFilePermissions();
        ALOGV("exiting main thread");
        exit(errorCode);
    }
}

void fixFilePermissions() {
    // on SD Card this will be ignored and in other locations this will give read access for everyone (Gallery etc.)
    if (chmod(outputName, 0664) < 0) {
        ALOGW("can't change file mode %s (%s)", outputName, strerror(errno));
    }
}

const char* getThreadName() {
    pthread_t threadId = pthread_self();
    if (pthread_equal(threadId, mainThread)) {
        return "main";
    }
    if (pthread_equal(threadId, stoppingThread)) {
        return "stopping";
    }

    return "other";
}

void waitForNextFrame() {
    int64_t now = getTimeMs();
    int64_t sleepTime = startTime + (frameCount * 1000l / frameRate) - now;
    if (sleepTime > 0) {
        usleep(sleepTime * 1000);
    }
}

int64_t getTimeMs() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000l + now.tv_nsec / 1000000l;
}
