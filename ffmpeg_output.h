#ifndef SCREENREC_FFMPEG_OUTPUT_H
#define SCREENREC_FFMPEG_OUTPUT_H

#include "screenrec.h"

#include <math.h>

extern "C" {
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

AVCodec *codec;
AVCodecContext *c= NULL;
AVOutputFormat *fmt;
AVFormatContext *oc;
AVStream *videoStream;

AVFrame *frame, *inframe;
AVPacket pkt;
uint8_t endcode[] = { 0, 0, 1, 0xb7 };
int frame_count = 1;
int64_t ptsOffset = 0;

int64_t getTimeMs();

#endif