#ifndef SCREENREC_FFMPEG_OUTPUT_H
#define SCREENREC_FFMPEG_OUTPUT_H

#include "screenrec.h"

#include <math.h>

#include <media/AudioRecord.h>
#include <media/AudioSystem.h>

using namespace android;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/url.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
}

AVFormatContext *oc;
int64_t startTimeMs = 0;

AVStream *videoStream;
AVFrame *frame;
int frameCount = 0;

AVStream *audioStream;
int audioFrameSize;
float *outSamples;
int64_t sampleCount = 0;

AudioRecord *audioRecord;
int inSamplesSize;
float *inSamples;
int inSamplesStart, inSamplesEnd;

void load_ff_components();
void setupOutputContext();
void setupVideoStream();
void setupFrame();
void setupAudioOutput();
void setupOutputFile();
void startAudioInput();
void audioRecordCallback(int event, void* user, void *info);
void writeAudioFrame();
void writeVideoFrame();
void copyRotateYUVBuf(uint8_t** yuvPixels, uint8_t* screen, int* stride);
int64_t getTimeMs();

#endif