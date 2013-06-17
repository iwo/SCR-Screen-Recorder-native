#ifndef SCREENREC_H
#define SCREENREC_H


#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <cutils/log.h>
#include <sys/time.h>

#include <media/mediarecorder.h>
#include <gui/SurfaceTextureClient.h>

//#define FB

#define FREE

#ifdef FB

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#else

#include <binder/IMemory.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>

#endif //FB

#ifdef FB
#define FRAME_RATE 30
#else
#define FRAME_RATE 15
#endif //FB
#define TARGET_FRAME_TIME 1000000 / FRAME_RATE

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
#ifdef FB
int fbFd = -1;
struct fb_var_screeninfo fbInfo;
#else
ScreenshotClient screenshot;
sp<IBinder> display;
#endif //FB

void const* inputBase;
int inputWidth, inputHeight;
int texWidth, texHeight;
char rotation [8];

// MediaRecorder
sp<MediaRecorder> mr = NULL;
sp<SurfaceTextureClient> mSTC = NULL;
sp<ANativeWindow> mANW = NULL;
bool mrRunning = false;

// global state
bool finished = false;
bool stopping = false;
int errorCode = 0;
bool micAudio = false;

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
void setupMediaRecorder();
void* commandThreadStart(void* args);
void listenForCommand();
void renderFrame();
void updateInput();
void stop(int error, const char* message);
void tearDownMediaRecorder();
void* stoppingThreadStart(void* args);
void tearDownEgl();
void closeOutput();
void closeInput();
void waitForNextFrame();

// OpenGL helpers
void checkGlError(const char* op, bool critical);
void checkGlError(const char* op);
GLuint loadShader(GLenum shaderType, const char* pSource);
GLuint createProgram(const char* pVertexSource, const char* pFragmentSource);

#endif