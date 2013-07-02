#define LOG_NDEBUG 0
#define LOG_TAG "screenrec"

#include "screenrec.h"

using namespace android;

static const char sVertexShader[] =
    "attribute vec4 vPosition;\n"
    "attribute vec2 texCoord; \n"
    "uniform mat4 vTransform;"
    "varying vec2 tc; \n"
    "void main() {\n"
    "  tc = texCoord;\n"
    "  gl_Position = vTransform * vPosition;\n"
    "}\n";

static const char sFragmentShader[] =
    "precision mediump float;\n"
    "uniform sampler2D textureSampler; \n"
    "varying vec2 tc; \n"
    "void main() {\n"
    //"  gl_FragColor = vec4(0.0, 1.0, 0, 1.0);\n"
#ifdef SCR_FB
    "  gl_FragColor.bgra = texture2D(textureSampler, tc); \n"
#else
    "  gl_FragColor.rgba = texture2D(textureSampler, tc); \n"
#endif // SCR_FB
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
    printf("ready\n");
    fflush(stdout);

    signal(SIGPIPE, sigpipeHandler);
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    getOutputName();
    getRotation();
    getAudioSetting();
    getResolution();
    getFrameRate();
    getUseGl();

    printf("configured\n");
    fflush(stdout);
    setupOutput();
    setupInput();
    if (useGl)
        setupEgl();
    setupMediaRecorder();
    if (useGl)
        setupGl();
    listenForCommand();

    printf("recording\n");
    fflush(stdout);
    ALOGV("Setup finished. Starting rendering loop.");

    timespec frameStart;
    timespec frameEnd;
    targetFrameTime = 1000000 / frameRate;

#ifdef SCR_FREE
    int framesLeft = frameRate * 60 * 4;
#endif

    while (mrRunning && !finished) {
        if (restrictFrameRate) {
            waitForNextFrame();
        }

        if (useGl) {
            renderFrameGl();
        } else {
            renderFrame();
        }
#ifdef SCR_FREE
        if (--framesLeft == 0) {
            stop(220, "Maximum recording time reached");
        }
#endif
    }

    stop(0, "finished");

    return 0;
}

void getOutputName() {
    if (fgets(outputName, 512, stdin) == NULL) {
        stop(200, "No output file specified");
    }
    trim(outputName);
}

void getResolution() {
    char width[16];
    char height[16];
    fgets(width, 16, stdin);
    fgets(height, 16, stdin);
    reqWidth = atoi(width);
    reqHeight = atoi(height);
}

void getFrameRate() {
    char fps[16];
    fgets(fps, 16, stdin);
    frameRate = atoi(fps);
    if (frameRate == -1) {
        restrictFrameRate = false;
        frameRate = FRAME_RATE;
    } else if (frameRate <= 0 || frameRate > 100) {
        frameRate = FRAME_RATE;
    }
}

void getUseGl() {
    char mode[8];
    if (fgets(mode, 8, stdin) != NULL) {
        if (mode[0] == 'C') { //CPU
            useGl = false;
        }
    }
}

void setupOutput() {
    outputFd = open(outputName, O_RDWR | O_CREAT, 0744);
    if (outputFd < 0) {
        stop(201, "Could not open the output file");
    }
}

void trim(char* str) {
    while (*str) {
        if (*str == '\n') {
            *str = '\0';
        }
        str++;
    }
}

void setupInput() {
#ifdef SCR_FB
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

    mapsize = size * 4; // For triple buffering 3 should be enough, setting to 4 for padding
    fbMapBase = mmap(0, mapsize, PROT_READ, MAP_SHARED, fbFd, 0);
    if (fbMapBase == MAP_FAILED) {
        stop(204, "mmap failed");
    }
    inputBase = (void const *)((char const *)fbMapBase + offset);
#else
    #if SCR_SDK_VERSION >= 17
    display = SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain);
    if (display == NULL) {
        stop(205, "Can't access display");
    }
    #endif // SCR_SDK_VERSION
    updateInput();
    inputWidth = screenshot.getWidth();
    inputHeight = screenshot.getHeight();
    ALOGV("Screenshot width: %d, height: %d, format %d, size: %d", inputWidth, inputHeight, screenshot.getFormat(), screenshot.getSize());
#endif // SCR_FB

    if (inputWidth > inputHeight) {
        videoWidth = inputWidth;
        videoHeight = inputHeight;
        rotateView = false;
    } else {
        videoWidth = inputHeight;
        videoHeight = inputWidth;
        rotateView = true;
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

    mEglSurface = eglCreateWindowSurface(mEglDisplay, mEglconfig, mANW.get(), NULL);
    if (eglGetError() != EGL_SUCCESS || mEglSurface == EGL_NO_SURFACE) {
        stop(214, "eglCreateWindowSurface() failed");
    };

    eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext);
    if (eglGetError() != EGL_SUCCESS ) {
        stop(215, "eglMakeCurrent() failed");
    };

    transformMatrix = rotateView ? flipAndRotateMatrix : flipMatrix;

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

    mvTransformHandle = glGetUniformLocation(mProgram, "vTransform");
    checkGlError("glGetUniformLocation");

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

void getRotation() {
    if (fgets(rotation, 8, stdin) == NULL) {
        stop(219, "No rotation specified");
    }
    trim(rotation);
}

void getAudioSetting() {
    char audio[8];
    if (fgets(audio, 8, stdin) == NULL) {
        stop(221, "No audio setting specified");
    }
    if (audio[0] == 'm') {
        micAudio = true;
    } else {
        micAudio = false;
    }
}

// Set up the MediaRecorder which runs in the same process as mediaserver
void setupMediaRecorder() {
    mr = new MediaRecorder();
    mr->setVideoSource(VIDEO_SOURCE_GRALLOC_BUFFER);
    if (micAudio) {
        mr->setAudioSource(AUDIO_SOURCE_MIC);
    }
    mr->setOutputFormat(OUTPUT_FORMAT_MPEG_4);
    mr->setVideoEncoder(VIDEO_ENCODER_H264);
    if (micAudio) {
        mr->setAudioEncoder(AUDIO_ENCODER_AAC);
        mr->setParameters(String8("audio-param-sampling-rate=16000"));
        mr->setParameters(String8("audio-param-encoding-bitrate=128000"));
    }
    mr->setOutputFile(outputFd, 0, 0);
    mr->setVideoSize(videoWidth, videoHeight);
    mr->setVideoFrameRate(frameRate);
    mr->setParameters(String8("video-param-rotation-angle-degrees=") + String8(rotation));
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

    ANativeWindowBuffer* anb;
    if (native_window_set_buffers_user_dimensions(mANW.get(), videoWidth, videoHeight) != NO_ERROR) {
        stop(241, "native_window_set_buffers_user_dimensions");
    }

    native_window_set_scaling_mode(mANW.get(), NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);

    if (native_window_dequeue_buffer_and_wait(mANW.get(), &anb) != NO_ERROR) {
        if (stopping) return;
        stop(242, "mANW->dequeueBuffer");
    }

    if (anb == NULL) {
        if (stopping) return;
        stop(243, "anb == NULL");
    }

    sp<GraphicBuffer> buf(new GraphicBuffer(anb, false));

    // Fill the buffer
    uint32_t* bufPixels = NULL;
    uint32_t* screen = (uint32_t*) inputBase;
    buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&bufPixels));

    if (rotateView) {
        for (int y = 0; y < videoHeight; y++) {
            for (int x = 0; x < videoWidth; x++) {
                bufPixels[y * videoWidth + x] = screen[x * videoHeight + videoHeight - y - 1];
            }
        }
    } else {
        memcpy(bufPixels, screen, videoWidth * videoHeight * 4);
    }

    buf->unlock();

    if (mANW->queueBuffer(mANW.get(), buf->getNativeBuffer(), -1) != NO_ERROR) {
        if (stopping) return;
        stop(245, "mANW->queueBuffer");
    }
}

void renderFrameGl() {
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

    glUniformMatrix4fv(mvTransformHandle, 1, GL_FALSE, transformMatrix);
    checkGlError("glUniformMatrix4fv");

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
#ifdef SCR_FB
    // it's still flickering, maybe ioctl(fd, FBIO_WAITFORVSYNC, &crt); would help
    if (ioctl(fbFd, FBIOGET_VSCREENINFO, &fbInfo) != 0) {
        stop(223, "FB ioctl failed");
    }
    int bytespp = fbInfo.bits_per_pixel / 8;
    size_t offset = (fbInfo.xoffset + fbInfo.yoffset * fbInfo.xres) * bytespp;
    inputBase = (void const *)((char const *)fbMapBase + offset);
#else
    #if SCR_SDK_VERSION >= 17
    if (screenshot.update(display, reqWidth, reqHeight) != NO_ERROR) {
        stop(217, "screenshot.update() failed");
    }
    #else
    if (screenshot.update(reqWidth, reqHeight) != NO_ERROR) {
        stop(217, "screenshot.update() failed");
    }
    #endif // SCR_SDK_VERSION
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
    printf("%d - stop requested from thread %s\n", error, thread);
    fflush(stdout);

    if (error == 0) {
        ALOGV("%s - stopping\n", message);
    } else {
        ALOGE("%d - stopping\n", error);
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
    if (useGl)
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
#ifdef SCR_FB
    if (fbFd >= 0) {
        close(fbFd);
    fbFd = -1;
    }
#endif // SCR_FB
}

void waitForNextFrame() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long usec = now.tv_nsec / 1000;

    if (uLastFrame == -1) {
        uLastFrame = usec;
        return;
    }

    long time = usec - uLastFrame;
    if (time < 0) {
        time += 1000000;
    }

    uLastFrame = usec;

    if (time < targetFrameTime) {
        usleep(targetFrameTime - time);
    }
}

void sigpipeHandler(int param) {
    ALOGI("SIGPIPE received");
    exit(222);
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
