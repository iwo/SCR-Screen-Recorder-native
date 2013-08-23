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
    "uniform mat4 colorTransform;"
    "varying vec2 tc; \n"
    "void main() {\n"
    "  gl_FragColor.rgba = colorTransform * texture2D(textureSampler, tc); \n"
    "}\n";


static const char sFragmentShaderOES[] =
    "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "uniform samplerExternalOES textureSampler; \n"
    "uniform mat4 colorTransform;"
    "varying vec2 tc; \n"
    "void main() {\n"
    "  gl_FragColor.rgba = colorTransform * texture2D(textureSampler, tc); \n"
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
    ProcessState::self()->startThreadPool();
    printf("ready\n");
    fflush(stdout);

    signal(SIGPIPE, sigpipeHandler);
    signal(SIGUSR1, sigusr1Handler);
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    mainThread = pthread_self();
    commandThread = mainThread; // will be changed when command thread is started

    getOutputName();
    getRotation();
    getAudioSetting();
    getResolution();
    getPadding();
    getFrameRate();
    getUseGl();
    getColorFormat();
    getVideoBitrate();
    getAudioSamplingRate();

    ALOGI("SETTINGS rotation: %d, micAudio: %s, resolution: %d x %d, padding: %d x %d, frameRate: %d, mode: %s, colorFix: %s",
          rotation, micAudio ? "true" : "false", reqWidth, reqHeight, paddingWidth, paddingHeight, frameRate, useGl ? "GPU" : "CPU", colorMatrix == rgbaMatrix ? "false" : "true");

    printf("configured\n");
    fflush(stdout);

    setupOutput();
    setupInput();
    adjustRotation();
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

    while (mrRunning && !finished) {
        if (restrictFrameRate) {
            waitForNextFrame();
        }
        renderFrame();
    }

    if (!stopping) {
        stop(0, "finished");
    }

    interruptCommandThread();

    return errorCode;
}

void getOutputName() {
    if (fgets(outputName, 512, stdin) == NULL) {
        ALOGV("cancelled");
        exit(200);
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

void getPadding() {
    char width[16];
    char height[16];
    fgets(width, 16, stdin);
    fgets(height, 16, stdin);
    paddingWidth = atoi(width);
    paddingHeight = atoi(height);
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
        } else if (mode[0] == 'O') { //OES
            #if SCR_SDK_VERSION >= 18
            useOes = true;
            #endif
        }
    }
}

void getColorFormat() {
    colorMatrix = rgbaMatrix;
    char mode[8];
    if (fgets(mode, 8, stdin) != NULL) {
        if (mode[0] == 'B') { //BGRA
            colorMatrix = bgraMatrix;
        }
    }
}

void getVideoBitrate() {
    char bitrate[16];
    fgets(bitrate, 16, stdin);
    videoBitrate = atoi(bitrate);

    if (videoBitrate == 0) {
        videoBitrate = 10000000;
    }
}

void getAudioSamplingRate() {
    char sampling[16];
    fgets(sampling, 16, stdin);
    audioSamplingRate = atoi(sampling);

    if (audioSamplingRate == 0) {
        audioSamplingRate = 16000;
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
    inputStride = inputWidth;
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
    if (screenshot.update(display) != NO_ERROR) {
        stop(217, "screenshot.update() failed");
    }
    #else
    if (screenshot.update() != NO_ERROR) {
        stop(217, "screenshot.update() failed");
    }
    #endif // SCR_SDK_VERSION

    if ((screenshot.getWidth() < screenshot.getHeight()) != (reqWidth < reqHeight)) {
        ALOGI("swapping dimensions");
        int tmp = reqWidth;
        reqWidth = reqHeight;
        reqHeight = tmp;
    }
    screenshotUpdate(reqWidth, reqHeight);
    inputWidth = screenshot.getWidth();
    inputHeight = screenshot.getHeight();
    inputStride = screenshot.getStride();
    if (useOes) {
        screenshot.release();
    }
    ALOGV("Screenshot width: %d, height: %d, stride: %d, format %d, size: %d", inputWidth, inputHeight, inputStride, screenshot.getFormat(), screenshot.getSize());

#endif // SCR_FB

    if (inputWidth > inputHeight) {
        videoWidth = inputWidth + 2 * paddingWidth;
        videoHeight = inputHeight + 2 * paddingHeight;
        rotateView = false;
    } else {
        videoWidth = inputHeight + 2 * paddingWidth;
        videoHeight = inputWidth + 2 * paddingHeight;
        rotateView = true;
    }
}

void adjustRotation() {
    if (rotateView) {
        rotation = (rotation + 90) % 360;
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

    if (useOes) {
        mProgram = createProgram(sVertexShader, sFragmentShaderOES);
    } else {
        mProgram = createProgram(sVertexShader, sFragmentShader);
    }
    if (!mProgram) {
        stop(212, "Could not create GL program.");
    }

    mvPositionHandle = glGetAttribLocation(mProgram, "vPosition");
    mTexCoordHandle = glGetAttribLocation(mProgram, "texCoord");
    checkGlError("glGetAttribLocation");

    mvTransformHandle = glGetUniformLocation(mProgram, "vTransform");
    checkGlError("glGetUniformLocation");

    mColorTransformHandle = glGetUniformLocation(mProgram, "colorTransform");
    checkGlError("glGetUniformLocation");

    if (useOes) {
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 1);
        checkGlError("glBindTexture");

        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        checkGlError("glTexParameteri");
    } else {
        glDeleteTextures(1, &mTexture);
        glGenTextures(1, &mTexture);
        glBindTexture(GL_TEXTURE_2D, mTexture);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        checkGlError("texture setup");

        int texWidth = getTexSize(inputStride);
        int texHeight = getTexSize(inputHeight);

        mPixels = (uint32_t*)malloc(4 * texWidth * texHeight);
        if (mPixels == (uint32_t*)NULL) {
            stop(211, "malloc failed");
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, mPixels);
        checkGlError("glTexImage2D", true);
        
        GLfloat wTexPortion = inputStride/(float)texWidth;
        GLfloat hTexPortion = inputHeight/(float)texHeight;

        texCoordinates[3] = wTexPortion;
        texCoordinates[7] = hTexPortion;
        texCoordinates[9] = wTexPortion;
        texCoordinates[10] = hTexPortion;
    }

    GLfloat wVideoPortion = (GLfloat) (videoWidth - 2 * paddingWidth) / (GLfloat) videoWidth;
    GLfloat hVideoPortion = (GLfloat) (videoHeight - 2 * paddingHeight) / (GLfloat) videoHeight;
    vertices[0] *= hVideoPortion;
    vertices[3] *= hVideoPortion;
    vertices[6] *= hVideoPortion;
    vertices[9] *= hVideoPortion;
    vertices[1] *= wVideoPortion;
    vertices[4] *= wVideoPortion;
    vertices[7] *= wVideoPortion;
    vertices[10]*= wVideoPortion;

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
    char rot[8];
    if (fgets(rot, 8, stdin) == NULL) {
        stop(219, "No rotation specified");
    }
    rotation = atoi(rot);
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
    if (mr->initCheck() != NO_ERROR) {
        stop(231, "Error starting MediaRecorder");
    }
    sp<SCRListener> listener = new SCRListener();
    mr->setListener(listener);
    mr->setVideoSource(VIDEO_SOURCE_GRALLOC_BUFFER);
    if (micAudio) {
        mr->setAudioSource(AUDIO_SOURCE_MIC);
    }
    mr->setOutputFormat(OUTPUT_FORMAT_MPEG_4);
    mr->setVideoEncoder(VIDEO_ENCODER_H264);
    if (micAudio) {
        mr->setAudioEncoder(AUDIO_ENCODER_AAC);
        mr->setParameters(String8::format("audio-param-sampling-rate=%d", audioSamplingRate));
        mr->setParameters(String8("audio-param-encoding-bitrate=128000"));
    }
    mr->setOutputFile(outputFd, 0, 0);
    mr->setVideoSize(videoWidth, videoHeight);
    mr->setVideoFrameRate(frameRate);
    mr->setParameters(String8::format("video-param-rotation-angle-degrees=%d", rotation));
    mr->setParameters(String8::format("video-param-encoding-bitrate=%d", videoBitrate));
    #ifdef SCR_FREE
    mr->setParameters(String8::format("max-duration=200000"));
    #endif
    mr->prepare();

    ALOGV("Starting MediaRecorder...");
    if (mr->start() != OK) {
        stop(213, "Error starting MediaRecorder");
    } else {
        mrRunning = true;
    }

    //TODO: check media recorder status
    #if SCR_SDK_VERSION >= 18
    sp<IGraphicBufferProducer> iST = mr->querySurfaceMediaSourceFromMediaServer();
    #else
    sp<ISurfaceTexture> iST = mr->querySurfaceMediaSourceFromMediaServer();
    #endif // SCR_SDK_VERSION
    mSTC = new Surface(iST);
    mANW = mSTC;

    #if SCR_SDK_VERSION < 17
    if (!useGl) {
        if (native_window_api_connect(mANW.get(), NATIVE_WINDOW_API_CPU) != NO_ERROR) {
            stop(224, "native_window_api_connect");
        }
        if (native_window_set_buffers_format(mANW.get(), PIXEL_FORMAT_RGBA_8888) != NO_ERROR) {
            stop(225, "native_window_set_buffers_format");
        }
    }
    #endif
}


void listenForCommand() {
    if (pthread_create(&commandThread, NULL, &commandThreadStart, NULL) != 0){
        stop(216, "Can't start command thread");
    }
}

void* commandThreadStart(void* args) {
    char command [16];
    memset(command,'\0', 16);
    read(fileno(stdin), command, 15);
    finished = true;
    commandThread = mainThread; // reset command thread id to indicate that it's stopped
    return NULL;
}

void interruptCommandThread() {
    if (!pthread_equal(commandThread, mainThread)) {
        ALOGV("Interrupting command thread");
        pthread_kill(commandThread, SIGUSR1);
    }
}

void renderFrame() {
    if (useGl) {
        renderFrameGl();
    } else {
        renderFrameCPU();
    }
}

void renderFrameCPU() {
    updateInput();

    ANativeWindowBuffer* anb;
    int rv;

    #if SCR_SDK_VERSION > 16
    rv = native_window_set_buffers_user_dimensions(mANW.get(), videoWidth, videoHeight);
    #else
    rv = native_window_set_buffers_dimensions(mANW.get(), videoWidth, videoHeight);
    #endif

    if (rv != NO_ERROR) {
        stop(241, "native_window_set_buffers_user_dimensions");
    }

    #if SCR_SDK_VERSION > 16
    native_window_set_scaling_mode(mANW.get(), NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
    #endif

    #if SCR_SDK_VERSION > 16
    rv = native_window_dequeue_buffer_and_wait(mANW.get(), &anb);
    #else
    rv = mANW->dequeueBuffer(mANW.get(), &anb);
    #endif

    if (rv != NO_ERROR) {
        if (stopping) return;
        stop(242, "mANW->dequeueBuffer");
    }

    if (anb == NULL) {
        if (stopping) return;
        stop(243, "anb == NULL");
    }

    sp<GraphicBuffer> buf(new GraphicBuffer(anb, false));

    #if SCR_SDK_VERSION < 17
    mANW->lockBuffer(mANW.get(), buf->getNativeBuffer());
    #endif

    // Fill the buffer
    uint32_t* bufPixels = NULL;
    uint32_t* screen = (uint32_t*) inputBase;
    buf->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&bufPixels));

    int stride = buf->stride;

    if (rotateView) {
        if (colorMatrix == rgbaMatrix) {
            for (int y = paddingHeight; y < videoHeight - paddingHeight; y++) {
                for (int x = paddingWidth; x < videoWidth - paddingWidth; x++) {
                    bufPixels[y * stride + x] = screen[(x - paddingWidth) * inputStride + videoHeight - paddingHeight - y - 1];
                }
            }
        } else {
            for (int y = paddingHeight; y < videoHeight - paddingHeight; y++) {
                for (int x = paddingWidth; x < videoWidth - paddingWidth; x++) {
                    uint32_t color = screen[(x - paddingWidth) * inputStride + videoHeight - paddingHeight - y - 1];
                    bufPixels[y * stride + x] = (color & 0xFF00FF00) | ((color >> 16) & 0x000000FF) | ((color << 16) & 0x00FF0000);
                }
            }
        }

    } else {
        if (videoWidth == stride && colorMatrix == rgbaMatrix) {
            memcpy(bufPixels, screen, videoWidth * videoHeight * 4);
        } else {
            //TODO: test on some device with this screen orientation
            if (colorMatrix == rgbaMatrix) {
                for (int y = paddingHeight; y < videoHeight - paddingHeight; y++) {
                    for (int x = paddingWidth; x < videoWidth - paddingWidth; x++) {
                        bufPixels[y * stride + x] = screen[(y - paddingHeight) * inputStride + (x - paddingWidth)];
                    }
                }
            } else {
                for (int y = paddingHeight; y < videoHeight - paddingHeight; y++) {
                    for (int x = paddingWidth; x < videoWidth - paddingWidth; x++) {
                        uint32_t color = screen[(y - paddingHeight) * inputStride + (x - paddingWidth)];
                        bufPixels[y * stride + x] = (color & 0xFF00FF00) | ((color >> 16) & 0x000000FF) | ((color << 16) & 0x00FF0000);
                    }
                }
            }
        }
    }

    buf->unlock();

    #if SCR_SDK_VERSION > 16
    rv = mANW->queueBuffer(mANW.get(), buf->getNativeBuffer(), -1);
    #else
    rv = mANW->queueBuffer(mANW.get(), buf->getNativeBuffer());
    #endif

    if (rv != NO_ERROR) {
        if (stopping) return;
        stop(245, "mANW->queueBuffer");
    }
}

void renderFrameGl() {
    updateInput();

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!useOes) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inputStride, inputHeight, GL_RGBA, GL_UNSIGNED_BYTE, inputBase);
        checkGlError("glTexSubImage2D");
    }

    glUseProgram(mProgram);
    checkGlError("glUseProgram");

    glUniformMatrix4fv(mvTransformHandle, 1, GL_FALSE, transformMatrix);
    checkGlError("glUniformMatrix4fv");

    glUniformMatrix4fv(mColorTransformHandle, 1, GL_FALSE, colorMatrix);
    checkGlError("glUniformMatrix4fv");

    glVertexAttribPointer(mvPositionHandle, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(mvPositionHandle);
    glVertexAttribPointer(mTexCoordHandle, 3, GL_FLOAT, GL_FALSE, 0, texCoordinates);
    glEnableVertexAttribArray(mTexCoordHandle);
    checkGlError("vertexAttrib");

    #if SCR_SDK_VERSION >= 18 && !defined SCR_FB
    if (useOes) {
        if (glConsumer->updateTexImage() != NO_ERROR) {
            if (!stopping) {
                stop(226, "texture update failed");
            }
        }
    }
    #endif

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

    if (useOes) {
        #if SCR_SDK_VERSION >= 18
        if (glConsumer.get() != NULL) {
            glConsumer.clear();
        }
        glConsumer = new GLConsumer(1);
        glConsumer->setName(String8("scr_consumer"));
             if (ScreenshotClient::capture(display, glConsumer->getBufferQueue(),
                reqWidth, reqHeight, 0, -1) != NO_ERROR) {
            stop(227, "capture failed");
        }
        #endif // SCR_SDK_VERSION >= 18
    } else {
        screenshotUpdate(reqWidth, reqHeight);
        inputBase = screenshot.getPixels();
    }
#endif
}

void screenshotUpdate(int reqWidth, int reqHeight) {
    #ifndef SCR_FB
    #if SCR_SDK_VERSION >= 18
        screenshot.release();
    #endif
    #if SCR_SDK_VERSION >= 17
    if (screenshot.update(display, reqWidth, reqHeight) != NO_ERROR) {
        stop(217, "update failed");
    }
    #else
    if (screenshot.update(reqWidth, reqHeight) != NO_ERROR) {
        stop(217, "update failed");
    }
    #endif // SCR_SDK_VERSION
    #endif // ndef SCR_FB
}

void stop(int error, const char* message) {
    stop(error, true, message);
}

void stop(int error, bool fromMainThread, const char* message) {

    fprintf(stderr, "%d - stop requested from thread %s\n", error, getThreadName());
    fflush(stderr);

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

    tearDownMediaRecorder(fromMainThread);
    if (useGl)
        tearDownEgl();
    closeOutput();
    closeInput();

    interruptCommandThread();

    if (fromMainThread) {
        exit(errorCode);
    }
}


void tearDownMediaRecorder(bool async) {
    if (mr.get() != NULL) {
        if (mrRunning) {
            if (async) {
                stopMediaRecorderAsync();
            } else {
                stopMediaRecorder();
            }
        }
        mr.clear();
    }
    if (mSTC.get() != NULL) {
        mSTC.clear();
    }
    if (mANW.get() != NULL) {
        #if SCR_SDK_VERSION < 17
        if (!useGl) {
            native_window_api_disconnect(mANW.get(), NATIVE_WINDOW_API_CPU);
        }
        #endif
        mANW.clear();
    }
}

void stopMediaRecorderAsync() {
    // MediaRecorder needs to be stopped from separate thread as couple frames may need to be rendered before mr->stop() returns.
    if (pthread_create(&stoppingThread, NULL, &stoppingThreadStart, NULL) != 0){
        ALOGE("Can't create stopping thread, stopping synchronously");
        stopMediaRecorder();
    }
    while (mrRunning) {
        renderFrame();
    }
    pthread_join(stoppingThread, NULL);
}

void stopMediaRecorder() {
    if (mr.get() != NULL) {
        ALOGV("Stopping MediaRecorder");
        mr->stop();
        mrRunning = false;
        ALOGV("MediaRecorder Stopped");
    }
}

void* stoppingThreadStart(void* args) {
    ALOGV("stoppingThreadStart");
    stopMediaRecorder();
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

void SCRListener::notify(int msg, int ext1, int ext2)
{
    ALOGI("SCRListener %d %d %d, track: %d value: %d\n", msg, ext1, ext2, (ext1 >> 24), (ext1 && 0x0000FFFF));

    int error = 0;

    if (msg == MEDIA_RECORDER_EVENT_ERROR) {
        ALOGE("MEDIA_RECORDER_EVENT_ERROR");
        error = 227;
    } else if (msg == MEDIA_RECORDER_TRACK_EVENT_ERROR) {
        ALOGE("MEDIA_RECORDER_TRACK_EVENT_ERROR");
        error = 228;
    } else if (msg == MEDIA_RECORDER_EVENT_INFO && ext1 == MEDIA_RECORDER_INFO_MAX_FILESIZE_REACHED) {
        ALOGE("MEDIA_RECORDER_INFO_MAX_FILESIZE_REACHED");
        error = 229;
    } else if (msg == MEDIA_RECORDER_EVENT_INFO && ext1 == MEDIA_RECORDER_INFO_MAX_DURATION_REACHED) {
        ALOGE("MEDIA_RECORDER_INFO_MAX_DURATION_REACHED");
        error = 230;
    }

    if (error != 0 && firstError) { //TODO: add proper thread handling instead of this hack
        firstError = false;

        usleep(500); // wait a moment for mediaserver to finish it's stuff

        stop(error, false, "stopping from listener");

        ALOGV("SCRListener thread completed");
    }
}

void sigpipeHandler(int param) {
    ALOGI("SIGPIPE received");
    exit(222);
}

void sigusr1Handler(int param) {
    ALOGV("SIGUSR1 received");
    pthread_exit(0);
}

const char* getThreadName() {
    pthread_t threadId = pthread_self();
    if (pthread_equal(threadId, mainThread)) {
        return "main";
    }
    if (pthread_equal(threadId, commandThread)) {
        return "command";
    }
    if (pthread_equal(threadId, stoppingThread)) {
        return "stopping";
    }

    return "other";
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
