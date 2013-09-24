#include "ffmpeg_output.h"

void setupOutput() {
    int ret;
    av_register_all();
    //extern AVCodec ff_mpeg1video_encoder;
    //avcodec_register(&ff_mpeg1video_encoder);

    codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    avformat_alloc_output_context2(&oc, NULL, NULL, outputName);
    if (!oc) {
        fprintf(stderr, "Can't alloc output context\n");
        exit(1);
    }
    fmt = oc->oformat;

    videoStream = avformat_new_stream(oc, codec);
    if (!videoStream) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }

    c = videoStream->codec;

    /* put sample parameters */
    c->bit_rate = videoBitrate;
    /* resolution must be a multiple of two */
    c->width = videoHeight;
    c->height = videoWidth;
    /* frames per second */
    c->time_base= (AVRational){1,25};
    c->gop_size = 10; /* emit one intra frame every ten frames */
    c->max_b_frames=1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->thread_count = 4;
    c->mb_decision = 2;

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    inframe = avcodec_alloc_frame();
    if (!inframe) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    inframe->format = AV_PIX_FMT_RGB32;
    inframe->width  = c->width;
    inframe->height = c->height;

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

    ptsOffset = getTimeMs();

    mrRunning = true;
}

int64_t getTimeMs() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000l + now.tv_nsec / 1000000l;
}

void renderFrame() {
    updateInput();
    int ret, x, y, got_output;
    av_init_packet(&pkt);
    pkt.data = NULL;    // packet data will be allocated by the encoder
    pkt.size = 0;

    AVPixelFormat inFormat = useBGRA ? PIX_FMT_RGB32 : PIX_FMT_BGR32; // I have no idea why it needs to be inverted

    avpicture_fill((AVPicture*)inframe, (uint8_t*)inputBase, inFormat, inputStride, inputHeight);

    frame_count++;

    frame->pts = av_rescale_q(getTimeMs() - ptsOffset, (AVRational){1,1000}, videoStream->time_base);

    struct SwsContext* swsContext = sws_getContext(inframe->width, inframe->height, inFormat, c->width, c->height, PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    sws_scale(swsContext, inframe->data, inframe->linesize, 0, c->height, frame->data, frame->linesize);

    /* encode the image */
    ret = avcodec_encode_video2(c, &pkt, frame, &got_output);
    if (ret < 0) {
        fprintf(stderr, "Error encoding frame\n");
        exit(1);
    }

    if (got_output) {
        fprintf(stderr, "Write frame %3d (size=%5d)\n", frame_count, pkt.size);
        fflush(stderr);

        if (c->coded_frame->key_frame)
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
}

void closeOutput(bool fromMainThread) {
    int ret, got_output;
    /*for (got_output = 1; got_output; frame_count++) {

        ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
        if (ret < 0) {
            fprintf(stderr, "2 Error encoding frame\n");
            exit(1);
        }

        if (got_output) {
            fprintf(stderr, "Write frame %3d (size=%5d)\n", frame_count, pkt.size);
            fflush(stderr);


            av_free_packet(&pkt);
        }
    }*/

    av_write_trailer(oc);

    avcodec_close(c);
    av_freep(&frame->data[0]);
    avcodec_free_frame(&frame);

    avio_close(oc->pb);

    /* free the stream */
    avformat_free_context(oc);

    mrRunning = false;
}