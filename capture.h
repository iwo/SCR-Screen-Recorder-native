#ifndef SCREENREC_CAPTURE_H
#define SCREENREC_CAPTURE_H

#include "screenrec.h"

#include <stdio.h>
#include <fcntl.h>

#if SCR_SDK_VERSION >= 18
#include <ui/GraphicBuffer.h>
#include <gui/GLConsumer.h>
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

// allow up to 10 consecutive screenshot update errors before stopping
#define MAX_UPDATE_ERRORS 10

using namespace android;

// external
void const* inputBase;
int inputWidth, inputHeight, inputStride;
bool rotateView;

// input
#ifdef SCR_FB
int fbFd = -1;
struct fb_var_screeninfo fbInfo;
void const* fbMapBase = MAP_FAILED;
#else
ScreenshotClient *screenshot;
#if SCR_SDK_VERSION >= 17
sp<IBinder> display;
#endif // SCR_SDK_VERSION 17
#if SCR_SDK_VERSION >= 18
sp<GLConsumer> glConsumer;
#endif // SCR_SDK_VERSION 18
#if SCR_SDK_VERSION == 19
sp<BufferQueue> bufferQueue;
#endif // SCR_SDK_VERSION 19
#if SCR_SDK_VERSION >= 21
sp<IGraphicBufferProducer> producer;
sp<IGraphicBufferConsumer> consumer;
#endif // SCR_SDK_VERSION 20
#endif //SCR_FB

int updateErrors = 0;

void swapPadding();
status_t screenshotUpdate(int reqWidth, int reqHeight);

#endif