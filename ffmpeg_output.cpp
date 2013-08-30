#include "ffmpeg_output.h"

void setupOutput() {
    int ret;
    avcodec_register_all();

    codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    /* put sample parameters */
    c->bit_rate = 2000000;
    /* resolution must be a multiple of two */
    c->width = videoWidth;
    c->height = videoHeight;
    /* frames per second */
    c->time_base= (AVRational){1,25};
    c->gop_size = 10; /* emit one intra frame every ten frames */
    c->max_b_frames=1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    f = fopen(outputName, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", outputName);
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

    ret = av_image_alloc(inframe->data, inframe->linesize, inframe->width, inframe->height,
                         AV_PIX_FMT_RGB32, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate raw picture buffer\n");
        exit(1);
    }

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

    mrRunning = true;
}

void renderFrame() {
    int ret, x, y, got_output;
    av_init_packet(&pkt);
    pkt.data = NULL;    // packet data will be allocated by the encoder
    pkt.size = 0;

    fflush(stdout);
    /* prepare a dummy image */
    for(y=0; y < inframe->height; y++) {
        for(x=0; x < inframe->width; x++) {
            inframe->data[0][y * inframe->linesize[0] + x * 4 + 2] = x % 255;  // R
            inframe->data[0][y * inframe->linesize[0] + x * 4 + 1] = y % 255;  // G
            inframe->data[0][y * inframe->linesize[0] + x * 4] = frame_count % 255;      // B
        }
    }

    frame->pts = frame_count++;

    struct SwsContext* swsContext = sws_getContext(inframe->width, inframe->height, PIX_FMT_RGB32, c->width, c->height, PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
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
        fwrite(pkt.data, 1, pkt.size, f);
        av_free_packet(&pkt);
    }
}

void closeOutput(bool fromMainThread) {
    int ret, got_output;
    for (got_output = 1; got_output; frame_count++) {

        ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
        if (ret < 0) {
            fprintf(stderr, "Error encoding frame\n");
            exit(1);
        }

        if (got_output) {
            fprintf(stderr, "Write frame %3d (size=%5d)\n", frame_count, pkt.size);
            fflush(stderr);
            fwrite(pkt.data, 1, pkt.size, f);
            av_free_packet(&pkt);
        }
    }

    /* add sequence end code to have a real mpeg file */
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_close(c);
    av_free(c);
    av_freep(&frame->data[0]);
    avcodec_free_frame(&frame);

    mrRunning = false;
}