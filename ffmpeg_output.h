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
AVStream *videoStream;
AVStream *audioStream;

AudioRecord *audioRecord;

int audioFrameSize;
float *audioSamples;

#define IN_SAMPLES_SIZE (8 * 1024)
float inSamples [IN_SAMPLES_SIZE];
int inSamplesStart, inSamplesEnd;

int64_t totalSamples = 0;

AVFrame *frame;

int frameCount = 0;
int64_t startTimeMs = 0;

int64_t getTimeMs();

void load_ff_components();
void setupOutputContext();
void setupVideoStream();
void setupFrame();
void setupAudioOutput();
void setupOutputFile();
void startAudioInput();
void audioRecordCallback(int event, void* user, void *info);
status_t writeAudioFrame();

#endif