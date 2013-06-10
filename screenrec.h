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

using namespace android;

// GL helpers
GLuint loadShader(GLenum shaderType, const char* pSource);
GLuint createProgram(const char* pVertexSource, const char* pFragmentSource);

// EGL
EGLDisplay mEglDisplay = EGL_NO_DISPLAY;
EGLSurface mEglSurface = EGL_NO_SURFACE;
EGLContext mEglContext = EGL_NO_CONTEXT;
EGLConfig mEglconfig;

// OpenGL
GLuint mProgram;
GLuint mvPositionHandle;
GLuint mTexCoordHandle;
GLuint mTexture;
uint32_t *mPixels;

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

// MediaRecorder
sp<MediaRecorder> mr = NULL;
sp<SurfaceTextureClient> mSTC = NULL;
sp<ANativeWindow> mANW = NULL;
bool mrRunning = false;

// global state
bool finished = false;
bool stopping = false;
int errorCode = 0;

// pthreads
pthread_t stoppingThread;
pthread_t commandThread;


int main(int argc, char* argv[]);
void setupOutput();
void trimName(char* str);
void setupInput();
void setupEgl();
void setupGl();
int getTexSize(int size);
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

// OpenGL helpers
void checkGlError(const char* op, bool critical);
void checkGlError(const char* op);
GLuint loadShader(GLenum shaderType, const char* pSource);
GLuint createProgram(const char* pVertexSource, const char* pFragmentSource);

// time helpers
int udiff(timespec start, timespec end);

#endif