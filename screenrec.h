#ifndef SCREENREC_H
#define SCREENREC_H

#define LOG_NDEBUG 0
#define LOG_TAG "screenrec"

#include <pthread.h>

#if SCR_SDK_VERSION < 16
#include <system/audio.h>

#define ALOGV(...) LOGV(__VA_ARGS__)
#define ALOGD(...) LOGD(__VA_ARGS__)
#define ALOGI(...) LOGI(__VA_ARGS__)
#define ALOGW(...) LOGW(__VA_ARGS__)
#define ALOGE(...) LOGE(__VA_ARGS__)
#endif

#ifdef SCR_FB
#define FRAME_RATE 30
#else
#define FRAME_RATE 15
#endif //SCR_FB

// Configuration parameters
extern char outputName [512];
extern int rotation;
extern bool micAudio;
extern int reqWidth;
extern int reqHeight;
extern int paddingWidth;
extern int paddingHeight;
extern int frameRate;
extern bool restrictFrameRate;
extern bool useGl;
extern bool useOes;
extern int videoBitrate;
extern int audioSamplingRate;
extern bool useBGRA;


// Output
extern int outputFd;
extern int videoWidth, videoHeight;

// Capture
extern void const* inputBase;
extern int inputWidth, inputHeight, inputStride;
extern bool rotateView;

// global state
extern bool finished;
extern bool stopping;
extern int errorCode;
extern int targetFrameTime;
extern bool mrRunning;

extern pthread_t stoppingThread;

void setupOutput();
void setupInput();
void renderFrame();
void updateInput();
void updateTexImage(); // check if it can't be removed after moving updateInput() invocation
void stop(int error, const char* message);
void stop(int error, bool fromMainThread, const char* message);
void closeOutput(bool fromMainThread);
void closeInput();

#endif