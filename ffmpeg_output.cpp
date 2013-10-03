#include "ffmpeg_output.h"

void setupOutput() {
    int ret;

    load_ff_components();
    setupOutputContext();
    setupVideoStream();

    setupFrame();
    setupAudioOutput();

    setupOutputFile();

    startAudioInput();

    startTimeMs = getTimeMs();

    mrRunning = true;
}


void load_ff_components() {
    extern AVCodec ff_mpeg4_encoder;
    avcodec_register(&ff_mpeg4_encoder);

    extern AVCodec ff_aac_encoder;
        avcodec_register(&ff_aac_encoder);

    extern AVOutputFormat ff_mp4_muxer;
    av_register_output_format(&ff_mp4_muxer);

    extern URLProtocol ff_file_protocol;
    ffurl_register_protocol(&ff_file_protocol, sizeof(ff_file_protocol));
}


void setupOutputContext() {
    avformat_alloc_output_context2(&oc, NULL, NULL, outputName);
    if (!oc) {
        fprintf(stderr, "Can't alloc output context\n");
        exit(1);
    }
}


void setupVideoStream() {
    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);

    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    videoStream = avformat_new_stream(oc, codec);
    if (!videoStream) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }

    AVCodecContext *c = videoStream->codec;

    /* put sample parameters */
    c->bit_rate = videoBitrate;
    /* resolution must be a multiple of two */
    c->width = videoWidth;
    c->height = videoHeight;
    /* frames per second */
    c->time_base= (AVRational){1,10};
    c->gop_size = 10; /* emit one intra frame every ten frames */
    c->max_b_frames=1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->thread_count = 4;
    c->mb_decision = 2;

    //int rot = rotateView ? (rotation + 270) % 360 : rotation;
    int rot = rotation;
    if (rot) {
        char value[16];
        sprintf(value, "%d", rot);
        av_dict_set(&videoStream->metadata, "rotate", value, 0);
    }

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }
}


void setupFrame() {
    int ret;
    AVCodecContext *c = videoStream->codec;

    frame = avcodec_alloc_frame();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame->format = c->pix_fmt;
    frame->width  = c->width;
    frame->height = c->height;

    ret = av_image_alloc(frame->data, frame->linesize, c->width, c->height,
                         c->pix_fmt, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate raw picture buffer\n");
        exit(1);
    }
}


void setupOutputFile() {
    int ret;
    ret = avio_open(&oc->pb, outputName, AVIO_FLAG_WRITE);
    if (ret < 0) {
        fprintf(stderr, "Could not open '%s'\n", outputName);
        exit(1);
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(oc, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        exit(1);
    }
}


void startAudioInput() {
    int ret;
    audioRecord = new AudioRecord(AUDIO_SOURCE_MIC, audioSamplingRate, AUDIO_FORMAT_PCM_16_BIT, AUDIO_CHANNEL_IN_MONO, 4096, &audioRecordCallback);
    ret = audioRecord->start();

    if (ret != OK) {
        fprintf(stderr, "Can't start audio source\n");
        exit(0);
    }
}


void setupAudioOutput() {
    int ret;

    AVCodec *audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audioCodec) {
        fprintf(stderr, "AAC Codec not found\n");
        exit(1);
    }

    audioStream = avformat_new_stream(oc, audioCodec);
    if (!audioStream) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }

    AVCodecContext *c = audioStream->codec;
    c->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    c->bit_rate    = 64000;
    c->sample_rate = audioSamplingRate;
    c->channels    = 1;
    c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    /* open it */
    ret = avcodec_open2(c, audioCodec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec");
        exit(1);
    }

    if (c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)
        audioFrameSize = 10000;
    else
        audioFrameSize = c->frame_size;

    fprintf(stderr, "audioFrameSize=%d\n\n", audioFrameSize);

    audioSamples = (float*) av_malloc(audioFrameSize *
                        av_get_bytes_per_sample(c->sample_fmt) *
                        c->channels);
    if (!audioSamples) {
        fprintf(stderr, "Could not allocate audio samples buffer\n");
        exit(1);
    }

    audioStream->time_base= (AVRational){1,audioSamplingRate};
}



void audioRecordCallback(int event, void* user, void *info) {
    if (event != 0) return;

    AudioRecord::Buffer *buffer = (AudioRecord::Buffer*) info;

    for (unsigned int i = 0; i < buffer->frameCount; i++) {
        inSamples[inSamplesEnd++] = (float)buffer->i16[i] / 30000.0;
        inSamplesEnd %= IN_SAMPLES_SIZE;
        if (inSamplesEnd == inSamplesStart) {
            fprintf(stderr, "OVERRUN <<<<<<<<<<<<<<<<<<<<\n");
        }
    }
    fprintf(stderr, "more data %d\n", buffer->frameCount);
}

int availableSamplesCount() {
    return (IN_SAMPLES_SIZE + inSamplesEnd - inSamplesStart) % IN_SAMPLES_SIZE;
}

static int getAudioFrame()
{
    int samplesWritten = 0;

    while (samplesWritten < audioFrameSize && availableSamplesCount() > 0) {
        audioSamples[samplesWritten++] = inSamples[inSamplesStart++];
        inSamplesStart %= IN_SAMPLES_SIZE;
    }
    return samplesWritten;
}

status_t writeAudioFrame() {
    AVCodecContext *c;
    AVPacket pkt;
    AVFrame *frame = avcodec_alloc_frame();
    int got_packet, ret;

    av_init_packet(&pkt);
    pkt.data = NULL;    // packet data will be allocated by the encoder
    pkt.size = 0;
    c = audioStream->codec;

    frame->nb_samples = getAudioFrame();
    if (frame->nb_samples == 0) {
        avcodec_free_frame(&frame);
        return 1;
    }

    frame->pts = totalSamples;
    totalSamples += frame->nb_samples;

    avcodec_fill_audio_frame(frame, c->channels, c->sample_fmt, (uint8_t *)audioSamples, audioFrameSize *
                             av_get_bytes_per_sample(c->sample_fmt) *
                             c->channels, 1);

    ret = avcodec_encode_audio2(c, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame");
        exit(1);
    }

    if (!got_packet)
        return 2;

    fprintf(stderr, "AUDIO frame (size=%5d)\n", pkt.size);
    fflush(stderr);

    pkt.stream_index = audioStream->index;

    /* Write the compressed frame to the media file. */
    ret = av_interleaved_write_frame(oc, &pkt);
    if (ret != 0) {
        fprintf(stderr, "Error while writing audio frame");
        exit(1);
    }

    avcodec_free_frame(&frame);
    return NO_ERROR;
}

int64_t getTimeMs() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000l + now.tv_nsec / 1000000l;
}

void copyRotateYUVBuf(uint8_t** yuvPixels, uint8_t* screen, int* stride) {
    for (int x = paddingWidth; x < videoWidth - paddingWidth; x++) {
        for (int y = videoHeight - paddingHeight - 1; y >= paddingHeight; y--) {
            int idx = ((x - paddingWidth) * inputStride + videoHeight - paddingHeight - y - 1) * 4;
            uint8_t r,g,b;
            if (useBGRA) {
                b = screen[idx];
                g = screen[idx + 1];
                r = screen[idx + 2];
            } else {
                r = screen[idx];
                g = screen[idx + 1];
                b = screen[idx + 2];
            }
            uint16_t Y = ( (  66 * r + 129 * g +  25 * b + 128) >> 8) +  16;
            yuvPixels[0][y * stride[0] + x] = Y;
            if (y % 2 == 0 && x % 2 == 0) {
                uint16_t U = ( ( -38 * r -  74 * g + 112 * b + 128) >> 8) + 128;
                uint16_t V = ( ( 112 * r -  94 * g -  18 * b + 128) >> 8) + 128;
                yuvPixels[1][y * stride[1] / 2 + x / 2 ] = U;
                yuvPixels[2][y * stride[2] / 2 + x / 2 ] = V;
            }
        }
    }
}

void renderFrame() {
    updateInput();
    int ret, x, y, got_output;
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;    // packet data will be allocated by the encoder
    pkt.size = 0;

    frameCount++;
    long pts = frame->pts = av_rescale_q(getTimeMs() - startTimeMs, (AVRational){1,1000}, videoStream->time_base);

    copyRotateYUVBuf(frame->data, (uint8_t*)inputBase, frame->linesize);

    /* encode the image */
    ret = avcodec_encode_video2(videoStream->codec, &pkt, frame, &got_output);
    if (ret < 0) {
        fprintf(stderr, "Error encoding frame\n");
        exit(1);
    }

    if (got_output) {
        fprintf(stderr, "VIDEO frame %3d (size=%5d)\n", frameCount, pkt.size);
        fflush(stderr);

        if (videoStream->codec->coded_frame->key_frame)
            pkt.flags |= AV_PKT_FLAG_KEY;

        pkt.stream_index = videoStream->index;

        /* Write the compressed frame to the media file. */
        ret = av_interleaved_write_frame(oc, &pkt);
        if (ret != 0) {
            fprintf(stderr, "Error while writing video frame");
            exit(1);
        }

        av_free_packet(&pkt);
    }

    long audioPts = av_rescale_q(audioStream->pts.val, audioStream->time_base, videoStream->time_base);
    fprintf(stderr, "%ld < %ld  (%d)\n", audioPts, pts, availableSamplesCount());
    fflush(stderr);
    while (audioPts < pts && availableSamplesCount() >= audioFrameSize) {
        fprintf(stderr, "%ld < %ld  (%d) write\n", audioPts, pts, availableSamplesCount());
        fflush(stderr);
        if (writeAudioFrame() != NO_ERROR) {
            break;
        }
        audioPts = av_rescale_q(audioStream->pts.val, audioStream->time_base, videoStream->time_base);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

void closeOutput(bool fromMainThread) {
    int ret, got_output;
    /*for (got_output = 1; got_output; frameCount++) {

        ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
        if (ret < 0) {
            fprintf(stderr, "2 Error encoding frame\n");
            exit(1);
        }

        if (got_output) {
            fprintf(stderr, "Write frame %3d (size=%5d)\n", frameCount, pkt.size);
            fflush(stderr);


            av_free_packet(&pkt);
        }
    }*/

    fprintf(stderr, "avg fps %lld\n", frameCount * 1000 / (getTimeMs() - startTimeMs));
    fflush(stderr);

    av_write_trailer(oc);

    avcodec_close(videoStream->codec);
    av_freep(&frame->data[0]);
    avcodec_free_frame(&frame);

    avio_close(oc->pb);

    /* free the stream */
    avformat_free_context(oc);

    audioRecord->stop();

    mrRunning = false;
}