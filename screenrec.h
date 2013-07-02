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
#include <gui/SurfaceTextureClient.h>

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
#endif // SCR_SDK_VERSION
#endif //SCR_FB

void const* inputBase;
int inputWidth, inputHeight;
int texWidth, texHeight;
bool rotateView = true;

// MediaRecorder
sp<MediaRecorder> mr = NULL;
sp<SurfaceTextureClient> mSTC = NULL;
sp<ANativeWindow> mANW = NULL;
bool mrRunning = false;

// global state
bool finished = false;
bool stopping = false;
int errorCode = 0;
int targetFrameTime = 0;

// input
char outputName [512];
char rotation [8];
bool micAudio = false;
int reqWidth = 0;
int reqHeight = 0;
int frameRate = 0;
bool restrictFrameRate = true;
bool useGl = true;



// pthreads
pthread_t stoppingThread;
pthread_t commandThread;

// frame timers
long uLastFrame = -1;

int main(int argc, char* argv[]);
void setupOutput();
void trim(char* str);
void setupInput();
void setupEgl();
void setupGl();
int getTexSize(int size);
void getRotation();
void getAudioSetting();
void getOutputName();
void getResolution();
void getFrameRate();
void setupMediaRecorder();
void* commandThreadStart(void* args);
void listenForCommand();
void renderFrame();
void renderFrameGl();
void updateInput();
void stop(int error, const char* message);
void tearDownMediaRecorder();
void* stoppingThreadStart(void* args);
void tearDownEgl();
void closeOutput();
void closeInput();
void waitForNextFrame();
void sigpipeHandler(int param);

// OpenGL helpers
void checkGlError(const char* op, bool critical);
void checkGlError(const char* op);
GLuint loadShader(GLenum shaderType, const char* pSource);
GLuint createProgram(const char* pVertexSource, const char* pFragmentSource);

#endif