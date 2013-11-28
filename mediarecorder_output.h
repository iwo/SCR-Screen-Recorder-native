#ifndef SCREENREC_MEDIARECORDER_OUTPUT_H
#define SCREENREC_MEDIARECORDER_OUTPUT_H

#include "screenrec.h"

#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <cutils/log.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <media/AudioRecord.h>

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

class AbstractMediaRecorderOutput : public ScrOutput {
public:
    AbstractMediaRecorderOutput() : mr(NULL), mSTC(NULL), mANW(NULL) {}
    virtual ~AbstractMediaRecorderOutput() {}
    virtual void setupOutput();
    virtual void renderFrame() = 0;
    virtual void closeOutput(bool fromMainThread);

protected:
    // MediaRecorder
    sp<MediaRecorder> mr;
    sp<Surface> mSTC;
    sp<ANativeWindow> mANW;

    void setupMediaRecorder();
    void checkAudioSource();
    void tearDownMediaRecorder(bool async);
    static void* stoppingThreadStart(void* args);
    void stopMediaRecorder();
    void stopMediaRecorderAsync();
};


class GLMediaRecorderOutput : public AbstractMediaRecorderOutput {
public:
    GLMediaRecorderOutput() :
        mEglDisplay(EGL_NO_DISPLAY), mEglSurface(EGL_NO_SURFACE), mEglContext(EGL_NO_CONTEXT) {
        memset(vertices, 0, sizeof(vertices));
        memset(texCoordinates, 0, sizeof(texCoordinates));
    }
    virtual ~GLMediaRecorderOutput() {}
    virtual void setupOutput();
    virtual void renderFrame();
    virtual void closeOutput(bool fromMainThread);

private:
    // EGL
    EGLDisplay mEglDisplay;
    EGLSurface mEglSurface;
    EGLContext mEglContext;
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
    static GLfloat flipAndRotateMatrix[16];
    static GLfloat flipMatrix[16];

    GLfloat *colorMatrix;
    static GLfloat rgbaMatrix[16];
    static GLfloat bgraMatrix[16];

    GLfloat vertices[12];
    GLfloat texCoordinates[12];

    void setupEgl();
    void setupGl();
    void tearDownEgl();
    int getTexSize(int size);

    // OpenGL helpers
    void checkGlError(const char* op, bool critical);
    void checkGlError(const char* op);
    GLuint loadShader(GLenum shaderType, const char* pSource);
    GLuint createProgram(const char* pVertexSource, const char* pFragmentSource);
};


class CPUMediaRecorderOutput : public AbstractMediaRecorderOutput {
public:
    CPUMediaRecorderOutput() {}
    virtual ~CPUMediaRecorderOutput() {}
    virtual void setupOutput();
    virtual void renderFrame();
    virtual void closeOutput(bool fromMainThread);

private:
    void fillBuffer(sp<GraphicBuffer> buf);
    void copyRotateYUVBuf(uint8_t* yuvPixels, uint8_t* screen, int stride);
    void copyRotateBuf(uint32_t* bufPixels, uint32_t* screen, int stride);
    void copyBuf(uint32_t* bufPixels, uint32_t* screen, int stride);
    inline uint32_t convertColor(uint32_t color);
};


class SCRListener : public MediaRecorderListener
{
public:
    SCRListener() : firstError(true) {};
    void notify(int msg, int ext1, int ext2);
private:
    volatile bool firstError;
};

#endif