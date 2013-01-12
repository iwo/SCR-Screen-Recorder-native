#define LOG_NDEBUG 0
#define LOG_TAG "screenrec"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <cutils/log.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <media/mediarecorder.h>
#include <gui/SurfaceTextureClient.h>

using namespace android;

void stop(int error, const char* message);

static const char sVertexShader[] =
    "attribute vec4 vPosition;\n"
    "attribute vec2 texCoord; \n"
    "varying vec2 tc; \n"
    "void main() {\n"
    "  tc = texCoord;\n"
    "  gl_Position = mat4(0,1,0,0,  1,0,0,0, 0,0,-1,0,  0,0,0,1) * vPosition;\n"
    "}\n";

static const char sFragmentShader[] =
    "precision mediump float;\n"
    "uniform sampler2D textureSampler; \n"
    "varying vec2 tc; \n"
    "void main() {\n"
    //"  gl_FragColor = vec4(0.0, 1.0, 0, 1.0);\n"
    "  gl_FragColor.bgra = texture2D(textureSampler, tc); \n"
    "}\n";

static void checkGlError(const char* op, bool critical) {
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        ALOGI("after %s() glError (0x%x)\n", op, error);
        if (critical) {
            stop(-1, op);
        }
    }
}

static void checkGlError(const char* op) {
    checkGlError(op, false);
}

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    ALOGE("Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    ALOGE("Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

static EGLint eglConfigAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_RECORDABLE_ANDROID, EGL_TRUE,
            EGL_NONE };

static EGLint eglContextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE };

EGLDisplay mEglDisplay = EGL_NO_DISPLAY;
EGLSurface mEglSurface = EGL_NO_SURFACE;
EGLContext mEglContext = EGL_NO_CONTEXT;
EGLConfig mEglconfig;

GLuint mProgram;
GLuint mvPositionHandle;
GLuint mTexCoordHandle;
GLuint mTexture;
uint32_t *mPixels;

int outputFd = -1;
int videoWidth, videoHeight;

int fbFd = -1;
struct fb_var_screeninfo fbInfo;
void const* fbBase;
int fbWidth, fbHeight;

int texWidth, texHeight;

sp<MediaRecorder> mr = NULL;
sp<SurfaceTextureClient> mSTC = NULL;
sp<ANativeWindow> mANW = NULL;

void setupEgl() {
    ALOGV("setupEgl()");
    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglGetError() != EGL_SUCCESS || mEglDisplay == EGL_NO_DISPLAY) {
        stop(-1, "eglGetDisplay() failed");
    }

    EGLint majorVersion;
    EGLint minorVersion;
    eglInitialize(mEglDisplay, &majorVersion, &minorVersion);
    if (eglGetError() != EGL_SUCCESS) {
        stop(-1, "eglInitialize() failed");
    }

    EGLint numConfigs = 0;
    eglChooseConfig(mEglDisplay, eglConfigAttribs, &mEglconfig, 1, &numConfigs);
    if (eglGetError() != EGL_SUCCESS  || numConfigs < 1) {
        stop(-1, "eglChooseConfig() failed");
    }
    mEglContext = eglCreateContext(mEglDisplay, mEglconfig, EGL_NO_CONTEXT, eglContextAttribs);
    if (eglGetError() != EGL_SUCCESS || mEglContext == EGL_NO_CONTEXT) {
        stop(-1, "eglGetDisplay() failed");
    }
    ALOGV("EGL initialized");
}

void tearDownEgl() {
    if (mEglContext != EGL_NO_CONTEXT) {
        eglDestroyContext(mEglDisplay, mEglContext);
        mEglContext = EGL_NO_CONTEXT;
    }
    if (mEglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(mEglDisplay, mEglSurface);
        mEglSurface = EGL_NO_SURFACE;
    }
    if (mEglDisplay != EGL_NO_DISPLAY) {
        eglTerminate(mEglDisplay);
        mEglDisplay = EGL_NO_DISPLAY;
    }
    if (eglGetError() != EGL_SUCCESS) {
        ALOGE("tearDownEgl() failed");
    }
}

int getTexSize(int size) {
    int texSize = 2;
    while (texSize < size) {
        texSize = texSize * 2;
    }
    return texSize;
}

void setupGl() {
    ALOGV("setup GL");

    glDeleteTextures(1, &mTexture);
    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    checkGlError("texture setup");

    texWidth = getTexSize(fbWidth);
    texHeight = getTexSize(fbHeight);

    mPixels = (uint32_t*)malloc(4 * texWidth * texHeight);
    if (mPixels == (uint32_t*)NULL) {
        stop(-1, "malloc failed");
    }

    mProgram = createProgram(sVertexShader, sFragmentShader);
    if (!mProgram) {
        stop(-1, "Could not create GL program.");
    }

    mvPositionHandle = glGetAttribLocation(mProgram, "vPosition");
    mTexCoordHandle = glGetAttribLocation(mProgram, "texCoord");
    checkGlError("glGetAttribLocation");

    glTexImage2D(GL_TEXTURE_2D,
                        0,
                        GL_RGBA,
                        texWidth,
                        texHeight,
                        0,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        mPixels);
    checkGlError("glTexImage2D", true);

    glViewport(0, 0, videoWidth, videoHeight);
    checkGlError("glViewport");
}

// Set up the MediaRecorder which runs in the same process as mediaserver
void setupMediaRecorder() {
    mr = new MediaRecorder();
    mr->setVideoSource(VIDEO_SOURCE_GRALLOC_BUFFER);
    mr->setOutputFormat(OUTPUT_FORMAT_MPEG_4);
    mr->setVideoEncoder(VIDEO_ENCODER_H264);
    mr->setOutputFile(outputFd, 0, 0);
    mr->setVideoSize(videoWidth, videoHeight);
    mr->setVideoFrameRate(30);
    mr->setParameters(String8("video-param-rotation-angle-degrees=90"));
    mr->setParameters(String8("video-param-encoding-bitrate=1000000"));
    mr->prepare();

    ALOGV("Starting MediaRecorder...");
    if (mr->start() != OK) {
        stop(-1, "Error starting MediaRecorder");
    }

    //TODO: check media recorder status

    sp<ISurfaceTexture> iST = mr->querySurfaceMediaSourceFromMediaServer();
    mSTC = new SurfaceTextureClient(iST);
    mANW = mSTC;

    mEglSurface = eglCreateWindowSurface(mEglDisplay, mEglconfig, mANW.get(), NULL);
    if (eglGetError() != EGL_SUCCESS || mEglSurface == EGL_NO_SURFACE) {
        stop(-1, "eglCreateWindowSurface() failed");
    };

    eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext);
    if (eglGetError() != EGL_SUCCESS ) {
        stop(-1, "eglMakeCurrent() failed");
    };
}

void tearDownMediaRecorder() {
    if (mEglSurface != EGL_NO_SURFACE) {
        if (mEglDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        eglDestroySurface(mEglDisplay, mEglSurface);
        mEglSurface = EGL_NO_SURFACE;
    }
    if (mr != NULL) {
        ALOGV("Stopping MediaRecorder");
        mr->stop();
        ALOGV("Stopped");
        mr.clear();
    }
    if (mSTC != NULL) {
        mSTC.clear();
    }
    if (mANW != NULL) {
        mANW.clear();
    }
}

void setupFb() {
    ALOGV("Setting up FB mmap");
    const char* fbpath = "/dev/graphics/fb0";
    fbFd = open(fbpath, O_RDONLY);

    if (fbFd < 0) {
        stop(-1, "Error opening FB device");
    }

    if (ioctl(fbFd, FBIOGET_VSCREENINFO, &fbInfo) != 0) {
        stop(-1, "FB ioctl failed");
    }

    int bytespp = fbInfo.bits_per_pixel / 8;

    size_t mapsize, size;
    size_t offset = (fbInfo.xoffset + fbInfo.yoffset * fbInfo.xres) * bytespp;
    fbWidth = fbInfo.xres;
    fbHeight = fbInfo.yres;
    ALOGV("FB width: %d hieght: %d bytespp: %d", fbWidth, fbHeight, bytespp);

    size = fbWidth * fbHeight * bytespp;

    mapsize = offset + size;
    void const* mapbase = MAP_FAILED;
    mapbase = mmap(0, mapsize, PROT_READ, MAP_PRIVATE, fbFd, 0);
    if (mapbase == MAP_FAILED) {
        stop(-1, "mmap failed");
    }
    fbBase = (void const *)((char const *)mapbase + offset);

    if (fbWidth > fbHeight) {
        videoWidth = fbWidth;
        videoHeight = fbHeight;
    } else {
        videoWidth = fbHeight;
        videoHeight = fbWidth;
    }
}

void stop(int error, const char* message) {
    printf("%s - Stopping\n", message);
    if (error == 0) {
        ALOGV("%s - Stopping\n", message);
    } else {
        ALOGE("%s - Stopping\n", message);
    }

    tearDownMediaRecorder();

    tearDownEgl();

    if (outputFd >= 0) {
         close(outputFd);
    }
    if (fbFd >= 0) {
        close(fbFd);
    }

    exit(error);
}

void renderFrame() {
    glClearColor(0, 0.9, 0.7, 0.6);
    glClear(GL_COLOR_BUFFER_BIT);

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fbWidth, fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, fbBase);
    checkGlError("glTexSubImage2D");

    GLfloat wFillPortion = fbWidth/(float)texWidth;
    GLfloat hFillPortion = fbHeight/(float)texHeight;

    GLfloat vertices[] =    {-1.0,-1.0,0.0,   1.0,-1.0,0.0,  -1.0,1.0,0.0,  1.0,1.0,0.0};
    GLfloat coordinates[] = {0.0,0.0,0.0,   wFillPortion,0.0,0.0,  0.0,hFillPortion,0.0,  wFillPortion,hFillPortion,0.0 };

    glUseProgram(mProgram);
    checkGlError("glUseProgram");

    glVertexAttribPointer(mvPositionHandle, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(mvPositionHandle);
    glVertexAttribPointer(mTexCoordHandle, 3, GL_FLOAT, GL_FALSE, 0, coordinates);
    glEnableVertexAttribArray(mTexCoordHandle);
    checkGlError("vertexAttrib");

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    checkGlError("glDrawArrays");

    eglSwapBuffers(mEglDisplay, mEglSurface);
    if (eglGetError() != EGL_SUCCESS) {
        stop(-1, "eglSwapBuffers failed");
    }
}

int main(int argc, char* argv[]) {
    printf("Screen Recorder started\n");

    setupFb();

    if (argc < 2) {
        stop(-1, "Usage: screenrec <filename>");
    }

    outputFd = open(argv[1], O_RDWR | O_CREAT, 0744);
    if (outputFd < 0) {
        stop(-1, "Could not open the output file");;
    }

    setupEgl();

    setupMediaRecorder();

    setupGl();

    int frames = 100;
    while (frames--) {
        renderFrame();
    }

    stop(0, "finished");
}



