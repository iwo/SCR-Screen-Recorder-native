#ifndef SCREENREC_H
#define SCREENREC_H


#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <cutils/log.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/prctl.h>

#if SCR_SDK_VERSION < 16
#include <system/audio.h>
#define ALOGV(...) LOGV(__VA_ARGS__)
#define ALOGD(...) LOGD(__VA_ARGS__)
#define ALOGI(...) LOGI(__VA_ARGS__)
#define ALOGW(...) LOGW(__VA_ARGS__)
#define ALOGE(...) LOGE(__VA_ARGS__)
#endif

#include <media/mediarecorder.h>
#include <binder/ProcessState.h>
#if SCR_SDK_VERSION >= 16
#include <gui/Surface.h>
#else
#include <surfaceflinger/Surface.h>
#endif // SCR_SDK_VERSION

#if SCR_SDK_VERSION >= 18
#include <ui/GraphicBuffer.h>
#else
#include <gui/SurfaceTextureClient.h>
#endif

#ifdef SCR_FB

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#else

#include <binder/IMemory.h>
#if SCR_SDK_VERSION >= 16
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#else
#include <surfaceflinger/SurfaceComposerClient.h>
#endif // SCR_SDK_VERSION
#endif //SCR_FB

#ifdef SCR_FB
#define FRAME_RATE 30
#else
#define FRAME_RATE 15
#endif //SCR_FB

using namespace android;

// EGL
EGLDisplay mEglDisplay = EGL_NO_DISPLAY;
EGLSurface mEglSurface = EGL_NO_SURFACE;
EGLContext mEglContext = EGL_NO_CONTEXT;
EGLConfig mEglconfig;

// OpenGL
GLuint mProgram;
GLuint mvPositionHandle;
GLuint mvTransformHandle;
GLuint mColorTransformHandle;
GLuint mTexCoordHandle;
GLuint mTexture;
uint32_t *mPixels;

GLfloat *transformMatrix;

GLfloat flipAndRotateMatrix[] = {
    0.0, 1.0, 0.0, 0.0,
    1.0, 0.0, 0.0, 0.0,
    0.0, 0.0,-1.0, 0.0,
    0.0, 0.0, 0.0, 1};

GLfloat flipMatrix[] = {
    1.0, 0.0, 0.0, 0.0,
    0.0,-1.0, 0.0, 0.0,
    0.0, 0.0,-1.0, 0.0,
    0.0, 0.0, 0.0, 1};

GLfloat *colorMatrix;

GLfloat rgbaMatrix[] = {
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0};

GLfloat bgraMatrix[] = {
    0.0, 0.0, 1.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    1.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 1.0};

GLfloat vertices[] =  {
    -1.0, -1.0, 0.0,
     1.0, -1.0, 0.0,
    -1.0,  1.0, 0.0,
     1.0,  1.0, 0.0};

GLfloat texCoordinates[] = {
    0.0, 0.0, 0.0,
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0,
    1.0 ,1.0, 0.0 };

// Output
int outputFd = -1;
int videoWidth, videoHeight;

// input
#ifdef SCR_FB
int fbFd = -1;
struct fb_var_screeninfo fbInfo;
void const* fbMapBase = MAP_FAILED;
#else
ScreenshotClient screenshot;
#if SCR_SDK_VERSION >= 17
sp<IBinder> display;
#endif // SCR_SDK_VERSION 17
#if SCR_SDK_VERSION >= 18
sp<GLConsumer> glConsumer;
#endif // SCR_SDK_VERSION 18
#endif //SCR_FB

void const* inputBase;
int inputWidth, inputHeight, inputStride;
bool rotateView = true;

// MediaRecorder
sp<MediaRecorder> mr = NULL;
sp<Surface> mSTC = NULL;
sp<ANativeWindow> mANW = NULL;
bool mrRunning = false;

// global state
bool finished = false;
bool stopping = false;
int errorCode = 0;
int targetFrameTime = 0;

// input
char outputName [512];
int rotation;
bool micAudio = false;
int reqWidth = 0;
int reqHeight = 0;
int paddingWidth = 0;
int paddingHeight = 0;
int frameRate = 0;
bool restrictFrameRate = true;
bool useGl = true;
bool useOes = false;
int videoBitrate;
int audioSamplingRate;


// pthreads
pthread_t mainThread;
pthread_t stoppingThread;
pthread_t commandThread;

// frame timers
long uLastFrame = -1;

int main(int argc, char* argv[]);
void setupOutput();
void trim(char* str);
void setupInput();
void adjustRotation();
void setupEgl();
void setupGl();
int getTexSize(int size);
void getRotation();
void getAudioSetting();
void getOutputName();
void getResolution();
void getPadding();
void getFrameRate();
void getUseGl();
void getColorFormat();
void getVideoBitrate();
void getAudioSamplingRate();
void setupMediaRecorder();
void* commandThreadStart(void* args);
void listenForCommand();
void renderFrame();
void renderFrameGl();
void renderFrameCPU();
void updateInput();
void stop(int error, const char* message);
void stop(int error, bool fromMainThread, const char* message);
void tearDownMediaRecorder(bool async);
void* stoppingThreadStart(void* args);
void stopMediaRecorder();
void stopMediaRecorderAsync();
void tearDownEgl();
void closeOutput();
void closeInput();
void waitForNextFrame();
void sigpipeHandler(int param);
void sigusr1Handler(int param);
void screenshotUpdate(int reqWidth, int reqHeight);
const char* getThreadName();

// OpenGL helpers
void checkGlError(const char* op, bool critical);
void checkGlError(const char* op);
GLuint loadShader(GLenum shaderType, const char* pSource);
GLuint createProgram(const char* pVertexSource, const char* pFragmentSource);

class SCRListener : public MediaRecorderListener
{
public:
    SCRListener() : firstError(true) {};
    void notify(int msg, int ext1, int ext2);
private:
    volatile bool firstError;
};

#endif