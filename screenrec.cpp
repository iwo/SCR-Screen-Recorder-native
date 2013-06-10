#define LOG_NDEBUG 0
#define LOG_TAG "screenrec"

#include "screenrec.h"

using namespace android;

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
#ifdef FB
    "  gl_FragColor.bgra = texture2D(textureSampler, tc); \n"
#else
    "  gl_FragColor.rgba = texture2D(textureSampler, tc); \n"
#endif //FB
    "}\n";

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




int main(int argc, char* argv[]) {
    printf("Screen Recorder started\n");

    setupOutput();
    setupInput();
    setupEgl();
    setupMediaRecorder();
    setupGl();
    listenForCommand();

    timespec frameStart;
    timespec frameEnd;
    int targetFrameTime = 1000000 / FRAME_RATE;

    while (mrRunning && !finished) {
        clock_gettime(CLOCK_MONOTONIC, &frameStart);
        renderFrame();
        clock_gettime(CLOCK_MONOTONIC, &frameEnd);
        int frameTime = udiff(frameStart, frameEnd);

        if (frameTime < targetFrameTime) {
            usleep(targetFrameTime - frameTime);
        }
    }

    stop(0, "finished");

    return 0;
}

void setupOutput() {
    char outputName [512];

    if (fgets(outputName, 512, stdin) == NULL) {
        stop(200, "No output file specified");
    }
    trimName(outputName);

    outputFd = open(outputName, O_RDWR | O_CREAT, 0744);
    if (outputFd < 0) {
        stop(201, "Could not open the output file");
    }
}

void trimName(char* str) {
    while (*str) {
        if (*str == '\n') {
            *str = '\0';
        }
        str++;
    }
}

void setupInput() {
#ifdef FB
    ALOGV("Setting up FB mmap");
    const char* fbpath = "/dev/graphics/fb0";
    fbFd = open(fbpath, O_RDONLY);

    if (fbFd < 0) {
        stop(202, "Error opening FB device");
    }

    if (ioctl(fbFd, FBIOGET_VSCREENINFO, &fbInfo) != 0) {
        stop(203, "FB ioctl failed");
    }

    int bytespp = fbInfo.bits_per_pixel / 8;

    size_t mapsize, size;
    size_t offset = (fbInfo.xoffset + fbInfo.yoffset * fbInfo.xres) * bytespp;
    inputWidth = fbInfo.xres;
    inputHeight = fbInfo.yres;
    ALOGV("FB width: %d hieght: %d bytespp: %d", inputWidth, inputHeight, bytespp);

    size = inputWidth * inputHeight * bytespp;

    mapsize = offset + size;
    void const* mapbase = MAP_FAILED;
    mapbase = mmap(0, mapsize, PROT_READ, MAP_PRIVATE, fbFd, 0);
    if (mapbase == MAP_FAILED) {
        stop(204, "mmap failed");
    }
    inputBase = (void const *)((char const *)mapbase + offset);
#else
    display = SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain);
    if (display == NULL) {
        stop(205, "Can't access display");
    }
    if (screenshot.update(display) != NO_ERROR) {
        stop(206, "screenshot.update() failed");
    }
    inputBase = screenshot.getPixels();
    inputWidth = screenshot.getWidth();
    inputHeight = screenshot.getHeight();
    ALOGV("Screenshot width: %d, height: %d, format %d, size: %d", inputWidth, inputHeight, screenshot.getFormat(), screenshot.getSize());
#endif // FB

    if (inputWidth > inputHeight) {
        videoWidth = inputWidth;
        videoHeight = inputHeight;
    } else {
        videoWidth = inputHeight;
        videoHeight = inputWidth;
    }
}


void setupEgl() {
    ALOGV("setupEgl()");
    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglGetError() != EGL_SUCCESS || mEglDisplay == EGL_NO_DISPLAY) {
        stop(207, "eglGetDisplay() failed");
    }

    EGLint majorVersion;
    EGLint minorVersion;
    eglInitialize(mEglDisplay, &majorVersion, &minorVersion);
    if (eglGetError() != EGL_SUCCESS) {
        stop(208, "eglInitialize() failed");
    }

    EGLint numConfigs = 0;
    eglChooseConfig(mEglDisplay, eglConfigAttribs, &mEglconfig, 1, &numConfigs);
    if (eglGetError() != EGL_SUCCESS  || numConfigs < 1) {
        stop(209, "eglChooseConfig() failed");
    }
    mEglContext = eglCreateContext(mEglDisplay, mEglconfig, EGL_NO_CONTEXT, eglContextAttribs);
    if (eglGetError() != EGL_SUCCESS || mEglContext == EGL_NO_CONTEXT) {
        stop(210, "eglGetDisplay() failed");
    }
    ALOGV("EGL initialized");
}

void setupGl() {
    ALOGV("setup GL");

    glDeleteTextures(1, &mTexture);
    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    checkGlError("texture setup");

    texWidth = getTexSize(inputWidth);
    texHeight = getTexSize(inputHeight);

    mPixels = (uint32_t*)malloc(4 * texWidth * texHeight);
    if (mPixels == (uint32_t*)NULL) {
        stop(211, "malloc failed");
    }

    mProgram = createProgram(sVertexShader, sFragmentShader);
    if (!mProgram) {
        stop(212, "Could not create GL program.");
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

int getTexSize(int size) {
    int texSize = 2;
    while (texSize < size) {
        texSize = texSize * 2;
    }
    return texSize;
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
    mr->setParameters(String8("video-param-encoding-bitrate=10000000"));
    mr->prepare();

    ALOGV("Starting MediaRecorder...");
    if (mr->start() != OK) {
        stop(213, "Error starting MediaRecorder");
    } else {
        mrRunning = true;
    }

    //TODO: check media recorder status

    sp<ISurfaceTexture> iST = mr->querySurfaceMediaSourceFromMediaServer();
    mSTC = new SurfaceTextureClient(iST);
    mANW = mSTC;

    mEglSurface = eglCreateWindowSurface(mEglDisplay, mEglconfig, mANW.get(), NULL);
    if (eglGetError() != EGL_SUCCESS || mEglSurface == EGL_NO_SURFACE) {
        stop(214, "eglCreateWindowSurface() failed");
    };

    eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext);
    if (eglGetError() != EGL_SUCCESS ) {
        stop(215, "eglMakeCurrent() failed");
    };
}


void listenForCommand() {
    if (pthread_create(&commandThread, NULL, &commandThreadStart, NULL) != 0){
        stop(216, "Can't start command thread");
    }
}

void* commandThreadStart(void* args) {
    char command [16];
    fgets(command, 16, stdin);
    finished = true;
    return NULL;
}

void renderFrame() {
    updateInput();

    glClearColor(0, 0.9, 0.7, 0.6);
    glClear(GL_COLOR_BUFFER_BIT);

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inputWidth, inputHeight, GL_RGBA, GL_UNSIGNED_BYTE, inputBase);
    checkGlError("glTexSubImage2D");

    GLfloat wFillPortion = inputWidth/(float)texWidth;
    GLfloat hFillPortion = inputHeight/(float)texHeight;

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

    if (mrRunning) {
        eglSwapBuffers(mEglDisplay, mEglSurface);
        if (eglGetError() != EGL_SUCCESS) {
            ALOGW("eglSwapBuffers failed");
        }
    }
}

void updateInput() {
#ifdef FB
    //TODO update framebuffer offset
#else
    if (screenshot.update(display) != NO_ERROR) {
        stop(217, "screenshot.update() failed");
    }
    inputBase = screenshot.getPixels();
#endif
}


void stop(int error, const char* message) {
    pthread_t threadId = pthread_self();
    const char* thread;
    if (threadId == commandThread) {
        thread = "command";
    } else if (threadId == stoppingThread) {
        thread = "stopping";
    } else {
        thread = "main";
    }
    printf("stop requested %s %d from thread %s\n", message, error, thread);
    fflush(stdout);

    if (error == 0) {
        ALOGV("%s - stopping\n", message);
    } else {
        ALOGE("%s - stopping\n", message);
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

    tearDownMediaRecorder();
    tearDownEgl();
    closeOutput();
    closeInput();

    if (errorCode != 0) {
        exit(errorCode);
    }
}


void tearDownMediaRecorder() {
    if (mr.get() != NULL) {
        if (mrRunning) {
            // MediaRecorder needs to be stopped from separate thread as couple frames may need to be rendered before mr->stop() returns.
            if (pthread_create(&stoppingThread, NULL, &stoppingThreadStart, NULL) != 0){
                printf("Can't create stopping thread, stopping synchronously");
                stoppingThreadStart(NULL);
            }
            while (mrRunning) {
                renderFrame();
            }
            pthread_join(stoppingThread, NULL);
        }
        mr.clear();
    }
    if (mSTC.get() != NULL) {
        mSTC.clear();
    }
    if (mANW.get() != NULL) {
        mANW.clear();
    }
}

void* stoppingThreadStart(void* args) {
    ALOGV("stoppingThreadStart");

    if (mr.get() != NULL) {
        ALOGV("Stopping MediaRecorder");
        mr->stop();
        mrRunning = false;
        ALOGV("MediaRecorder Stopped");
    }
    
    return NULL;
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

void closeOutput() {
    if (outputFd >= 0) {
         close(outputFd);
         outputFd = -1;
    }
}

void closeInput() {
#ifdef FB
    if (fbFd >= 0) {
        close(fbFd);
    fbFd = -1;
    }
#endif //FB
}


// OpenGL helpers

void checkGlError(const char* op, bool critical) {
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        ALOGI("after %s() glError (0x%x)\n", op, error);
        if (critical) {
            stop(218, op);
        }
    }
}

void checkGlError(const char* op) {
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

// time helpers

int udiff(timespec start, timespec end)
{
	int nsec;

	if ((end.tv_nsec-start.tv_nsec)<0) {
		nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		nsec = end.tv_nsec-start.tv_nsec;
	}
	return nsec / 1000;
}

