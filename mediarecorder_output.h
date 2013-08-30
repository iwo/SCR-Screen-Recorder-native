#ifndef SCREENREC_MEDIARECORDER_OUTPUT_H
#define SCREENREC_MEDIARECORDER_OUTPUT_H

#include "screenrec.h"

#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <cutils/log.h>

#include <media/mediarecorder.h>
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

// MediaRecorder
sp<MediaRecorder> mr = NULL;
sp<Surface> mSTC = NULL;
sp<ANativeWindow> mANW = NULL;

void setupEgl();
void setupGl();
int getTexSize(int size);
void setupMediaRecorder();
void renderFrameGl();
void renderFrameCPU();

void tearDownMediaRecorder(bool async);
void* stoppingThreadStart(void* args);
void stopMediaRecorder();
void stopMediaRecorderAsync();
void tearDownEgl();


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