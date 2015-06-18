#ifndef SCREENREC_H
#define SCREENREC_H

#define LOG_NDEBUG 0
#define LOG_TAG "screenrec"

#include <pthread.h>
#include <cutils/log.h>
#include <errno.h>

#if SCR_SDK_VERSION < 16
#include <system/audio.h>

#define ALOGV(...) LOGV(__VA_ARGS__)
#define ALOGD(...) LOGD(__VA_ARGS__)
#define ALOGI(...) LOGI(__VA_ARGS__)
#define ALOGW(...) LOGW(__VA_ARGS__)
#define ALOGE(...) LOGE(__VA_ARGS__)
#endif

#define FRAME_RATE 15

// constants corresponding to AudioSource enum
#define SCR_AUDIO_MUTE 'x'
#define SCR_AUDIO_MIC 'm'
#define SCR_AUDIO_INTERNAL 'i'
#define SCR_AUDIO_MIX '+'

// Configuration parameters
extern char *outputName;
extern int rotation;
extern char audioSource;
extern int reqWidth;
extern int reqHeight;
extern int paddingWidth;
extern int paddingHeight;
extern int frameRate;
extern bool restrictFrameRate;
extern bool useGl;
extern bool useOes;
extern bool useFb;
extern int videoBitrate;
extern int audioSamplingRate;
extern int audioChannels;
extern bool useBGRA;
extern bool useYUV_P;
extern bool useYUV_SP;
extern int videoEncoder;
extern bool allowVerticalFrames;


// Output
extern int outputFd;
extern int videoWidth, videoHeight;

// Capture
extern void const* inputBase;
extern int inputWidth, inputHeight, inputStride;
extern bool rotateView;

// global state
extern bool stopping;
extern bool mrRunning;
extern int frameCount;

extern pthread_t stoppingThread;

int startRecording(char *config);

void setupInput();

void updateInput();
void updateTexImage(); // check if it can't be removed after moving updateInput() invocation
void stop(int error, const char* message);
void stop(int error, bool fromMainThread, const char* message);
void closeInput();
int64_t getTimeMs();
void trim(char* str);
bool fixOutputName();

class ScrOutput {
public:
    ScrOutput() {}
    virtual ~ScrOutput() {}
    virtual void setupOutput() = 0;
    virtual void renderFrame() = 0;
    virtual void closeOutput(bool fromMainThread) = 0;
};


void shellSetState(const char* state);
void shellSetError(int errorCode);

#endif