#ifndef SCREENREC_FFMPEG_OUTPUT_H
#define SCREENREC_FFMPEG_OUTPUT_H

#include "screenrec.h"

#include <math.h>

#include <media/AudioRecord.h>
#include <media/AudioSystem.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/url.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
}

using namespace android;

class FFmpegOutput : public ScrOutput {
public:
    FFmpegOutput()
        : oc(NULL),
          startTimeMs(0),
          videoStream(NULL),
          videoFrame(NULL),
          audioStream(NULL),
          audioFrameSize(0),
          outSamples(NULL),
          sampleCount(0),
          audioRecord(NULL),
          inSamplesSize(0),
          inSamples(NULL),
          inSamplesStart(0),
          inSamplesEnd(0) {
        pthread_mutex_init(&frameReadyMutex, NULL);
        pthread_mutex_init(&frameEncMutex, NULL);
        pthread_mutex_init(&outputWriteMutex, NULL);
        pthread_mutex_init(&inSamplesMutex, NULL);
        frames[0] = NULL;
        frames[1] = NULL;
    }
    virtual ~FFmpegOutput() {}
    virtual void setupOutput();
    virtual void renderFrame();
    virtual void closeOutput(bool fromMainThread);
    void audioRecordCallback(int event, void* user, void *info);

private:

    AVFormatContext *oc;
    int64_t startTimeMs;

    AVStream *videoStream;
    AVFrame *videoFrame;
    AVFrame *frames[2];

    AVStream *audioStream;
    int audioFrameSize;
    float *outSamples;
    int64_t sampleCount;

    #if SCR_SDK_VERSION >= 16
    sp<AudioRecord> audioRecord;
    #else
    AudioRecord *audioRecord;
    #endif
    int inSamplesSize;
    float *inSamples;
    int inSamplesStart, inSamplesEnd;

    pthread_t encodingThread;
    pthread_mutex_t frameReadyMutex;
    pthread_mutex_t frameEncMutex;
    pthread_mutex_t outputWriteMutex;
    pthread_mutex_t inSamplesMutex;

    static void* encodingThreadStart(void* args);
    void encodeAndSaveVideoFrame(AVFrame *frame);

    void loadFFmpegComponents();
    void setupOutputContext();
    void setupVideoStream();
    AVFrame * createFrame();
    void setupFrames();
    void setupAudioOutput();
    void setupOutputFile();
    void startAudioInput();
    inline int availableSamplesCount();
    void getAudioFrame();
    void writeAudioFrame();
    void writeVideoFrame();
    void copyRotateYUVBuf(uint8_t** yuvPixels, uint8_t* screen, int* stride);
    void copyYUVBuf(uint8_t** yuvPixels, uint8_t* screen, int* stride);
};

static void staticAudioRecordCallback(int event, void* user, void *info);

#endif
