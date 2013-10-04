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
AVFrame *videoFrame;
AVFrame *frames[2];
int frameCount = 0;

AVStream *audioStream;
int audioFrameSize;
float *outSamples;
int64_t sampleCount = 0;

AudioRecord *audioRecord;
int inSamplesSize;
float *inSamples;
int inSamplesStart, inSamplesEnd;

pthread_t encodingThread;
pthread_mutex_t frameReadyMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t frameEncMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t outputWriteMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t inSamplesMutex = PTHREAD_MUTEX_INITIALIZER;

void* encodingThreadStart(void* args);
void encodeAndSaveVideoFrame(AVFrame *frame);

void load_ff_components();
void setupOutputContext();
void setupVideoStream();
AVFrame * createFrame();
void setupFrames();
void setupAudioOutput();
void setupOutputFile();
void startAudioInput();
void audioRecordCallback(int event, void* user, void *info);
void writeAudioFrame();
void writeVideoFrame();
void copyRotateYUVBuf(uint8_t** yuvPixels, uint8_t* screen, int* stride);
void copyYUVBuf(uint8_t** yuvPixels, uint8_t* screen, int* stride);
int64_t getTimeMs();

#define PERF_INIT( name ) \
    int64_t perf_time_##name = 0; \
    int perf_count_##name = 0; \
    int64_t perf_start_##name = 0;

#define PERF_START( name ) \
    perf_start_##name = perf_getTimeUs();

#define PERF_END( name ) \
    int64_t perf_current_time_##name = perf_getTimeUs() - perf_start_##name; \
    perf_time_##name += perf_current_time_##name; \
    perf_count_##name++;

#define PERF_STATS( name ) \
    fprintf(stderr, "PERF "#name"\tavg=%6lld\tfps limit %lld\n", perf_time_##name / perf_count_##name, 1000000l/(perf_time_##name / perf_count_##name)); \
    fflush(stderr);


int64_t perf_getTimeUs() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000l + now.tv_nsec / 1000l;
}

PERF_INIT(screenshot)
PERF_INIT(video_enc)
PERF_INIT(audio_out)
PERF_INIT(audio_in)
PERF_INIT(transform)



#endif
