//////////////////////////////////////////////////////////////////////////////
// this program takes every frame from a video an produces an image from it //
//////////////////////////////////////////////////////////////////////////////

extern "C" {
    #include <stdio.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/avutil.h>
}

static void save_color_frame(uint8_t *buf, int wrap, int xsize, int ysize, char *filename){
    FILE *f;
    int i;
    f = fopen(filename,"wb");
    if (f == NULL) {
        printf("cant open the file \n");
    }
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    // fprintf(f, "P3\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize * wrap; i++){
        fwrite(buf + i, 1, 1, f);
    }
    fclose(f);
}

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame){
    int response = avcodec_send_packet(pCodecContext, pPacket);
    if (response < 0) {
        printf("Error while sending a packet to the decoder\n");
        return response;
    }

    while (response >= 0) {
        response = avcodec_receive_frame(pCodecContext, pFrame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            printf("Error while receiving a frame from the decoder");
            return response;
        }

        if (response >= 0) {
            char frame_filename[1024];
            snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", pCodecContext->frame_number);
            if (pFrame->format != AV_PIX_FMT_YUV420P) {
                printf("Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB");
            }
            // converting color format from yuv to rgb
            SwsContext* swsCtx = sws_getContext(pFrame->width, pFrame->height, pCodecContext->pix_fmt, pFrame->width, pFrame->height, AV_PIX_FMT_GRAY8, SWS_BILINEAR, NULL, NULL, NULL);
            if (!swsCtx){
                printf("could not initialize the swsContext\n");
            }
            uint8_t* realData = (uint8_t*)malloc(pFrame->width * pFrame->height); 
            uint8_t* theData[4] = {realData, NULL, NULL, NULL};
            int destLineSize[4] = {pFrame->width, 0, 0, 0};
            int theHeight = sws_scale(swsCtx, pFrame->data, pFrame->linesize, 0, pFrame->height, theData, destLineSize);
            sws_freeContext(swsCtx);
            // save a grayscale frame into a .pgm file
            save_color_frame(realData, destLineSize[0], pFrame->width, pFrame->height, frame_filename);
        }
    }
    return 0;
}


int video_to_file(char* media){
    // char* media = "media/video.mpeg";
    
    printf("initializing all the containers, codecs and protocols.\n");
    AVFormatContext *pFormatContext = avformat_alloc_context();
    if (!pFormatContext) {
        printf("ERROR could not allocate memory for Format Context\n");
        return -1;
    }

    if (avformat_open_input(&pFormatContext, media, NULL, NULL) != 0) {
        printf("ERROR could not open the file\n");
        return -1;
    }

    printf("\nformat %s, duration %lld us, bit_rate %lld\n\n", pFormatContext->iformat->name, pFormatContext->duration, pFormatContext->bit_rate);

    if (avformat_find_stream_info(pFormatContext,  NULL) < 0) {
        printf("ERROR could not get the stream info\n");
        return -1;
    }

    const AVCodec *pCodec = NULL;
    const AVCodecParameters *pCodecParameters =  NULL;
    int video_stream_index = -1;

    for (int i = 0; i < pFormatContext->nb_streams; i++){
        AVCodecParameters *pLocalCodecParameters =  NULL;
        pLocalCodecParameters = pFormatContext->streams[i]->codecpar;

        const AVCodec *pLocalCodec = NULL;
        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec == NULL) {
            printf("ERROR unsupported codec!\n");
            continue;
        }

        // when the stream is a video we store its index, codec parameters and codec
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_stream_index == -1) {
                video_stream_index = i;
                pCodec = pLocalCodec;
                pCodecParameters = pLocalCodecParameters;
            }
            printf("Video Codec: resolution %d x %d\n", pLocalCodecParameters->width, pLocalCodecParameters->height);
        } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("Audio Codec: %d channels, sample rate %d\n", pLocalCodecParameters->channels, pLocalCodecParameters->sample_rate);
        }
        printf("\tCodec %s ID %d bit_rate %lld\n\n", pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
    }

    if (video_stream_index == -1) {
        printf("this file does not contain a video stream!");
        return -1;
    }

    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
    if (!pCodecContext)
    {
        printf("failed to allocated memory for AVCodecContext\n");
        return -1;
    }

    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0)
    {
        printf("failed to copy codec params to codec context\n");
        return -1;
    }

    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0)
    {
        printf("failed to open codec through avcodec_open2\n");
        return -1;
    }

    AVFrame *pFrame = av_frame_alloc();
    if (!pFrame)
    {
        printf("failed to allocated memory for AVFrame\n");
        return -1;
    }

    AVPacket *pPacket = av_packet_alloc();
    if (!pPacket)
    {
        printf("failed to allocated memory for AVPacket\n");
        return -1;
    }

    int response = 0;
    int how_many_packets_to_process = 2;
    while (av_read_frame(pFormatContext, pPacket) >= 0)
    {
        if (pPacket->stream_index == video_stream_index) {
            printf("AVPacket->pts %d\n", pPacket->pts);
            response = decode_packet(pPacket, pCodecContext, pFrame);
            if (response < 0) break;
            if (--how_many_packets_to_process <= 0) break;
        }
        av_packet_unref(pPacket);
    }

    printf("\n\nreleasing all the resources\n\n");

    avformat_close_input(&pFormatContext);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);

    return 0;
}