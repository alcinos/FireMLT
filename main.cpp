/*
 * http://ffmpeg.org/doxygen/trunk/index.html
 *
 * Main components
 *
 * Format (Container) - a wrapper, providing sync, metadata and muxing for the
 * streams. Stream - a continuous stream (audio or video) of data over time.
 * Codec - defines how data are enCOded (from Frame to Packet)
 *        and DECoded (from Packet to Frame).
 * Packet - are the data (kind of slices of the stream data) to be decoded as
 * raw frames. Frame - a decoded raw frame (to be encoded or filtered).
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <SDL_audio.h>
#include <SDL_render.h>
#include <SDL_syswm.h>
#include <SDL_thread.h>
}

#include <arrayfire.h>
// print out the steps and errors
static void logging(const char *fmt, ...);
// decode packets into frames
static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext,
                         AVFrame *pFrame);

using namespace af;
int main(int argc, const char *argv[]) {
  af::setDevice(0);
  af::info();
  logging("initializing all the containers, codecs and protocols.");

  // AVFormatContext holds the header information from the format (Container)
  // Allocating memory for this component
  // http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
  AVFormatContext *pFormatContext = avformat_alloc_context();
  if (!pFormatContext) {
    logging("ERROR could not allocate memory for Format Context");
    return -1;
  }

  logging("opening the input file (%s) and loading format (container) header",
          argv[1]);
  // Open the file and read its header. The codecs are not opened.
  // The function arguments are:
  // AVFormatContext (the component we allocated memory for),
  // url (filename),
  // AVInputFormat (if you pass NULL it'll do the auto detect)
  // and AVDictionary (which are options to the demuxer)
  // http://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga31d601155e9035d5b0e7efedc894ee49
  if (avformat_open_input(&pFormatContext, argv[1], NULL, NULL) != 0) {
    logging("ERROR could not open the file");
    return -1;
  }

  // now we have access to some information about our file
  // since we read its header we can say what format (container) it's
  // and some other information related to the format itself.
  logging("format %s, duration %lld us, bit_rate %lld",
          pFormatContext->iformat->name, pFormatContext->duration,
          pFormatContext->bit_rate);

  logging("finding stream info from format");
  // read Packets from the Format to get stream information
  // this function populates pFormatContext->streams
  // (of size equals to pFormatContext->nb_streams)
  // the arguments are:
  // the AVFormatContext
  // and options contains options for codec corresponding to i-th stream.
  // On return each dictionary will be filled with options that were not found.
  // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#gad42172e27cddafb81096939783b157bb
  if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
    logging("ERROR could not get the stream info");
    return -1;
  }

  // the component that knows how to enCOde and DECode the stream
  // it's the codec (audio or video)
  // http://ffmpeg.org/doxygen/trunk/structAVCodec.html
  AVCodec *pCodec = NULL;
  // this component describes the properties of a codec used by the stream i
  // https://ffmpeg.org/doxygen/trunk/structAVCodecParameters.html
  AVCodecParameters *pCodecParameters = NULL;
  int video_stream_index = -1;

  // loop though all the streams and print its main information
  for (int i = 0; i < pFormatContext->nb_streams; i++) {
    AVCodecParameters *pLocalCodecParameters = NULL;
    pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
    logging("AVStream->time_base before open coded %d/%d",
            pFormatContext->streams[i]->time_base.num,
            pFormatContext->streams[i]->time_base.den);
    logging("AVStream->r_frame_rate before open coded %d/%d",
            pFormatContext->streams[i]->r_frame_rate.num,
            pFormatContext->streams[i]->r_frame_rate.den);
    logging("AVStream->start_time %" PRId64,
            pFormatContext->streams[i]->start_time);
    logging("AVStream->duration %" PRId64,
            pFormatContext->streams[i]->duration);

    logging("finding the proper decoder (CODEC)");

    AVCodec *pLocalCodec = NULL;

    // finds the registered decoder for a codec ID
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga19a0ca553277f019dd5b0fec6e1f9dca
    pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

    if (pLocalCodec == NULL) {
      logging("ERROR unsupported codec!");
      return -1;
    }

    // when the stream is a video we store its index, codec parameters and codec
    if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_index = i;
      pCodec = pLocalCodec;
      pCodecParameters = pLocalCodecParameters;

      logging("Video Codec: resolution %d x %d", pLocalCodecParameters->width,
              pLocalCodecParameters->height);
    } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
      logging("Audio Codec: %d channels, sample rate %d",
              pLocalCodecParameters->channels,
              pLocalCodecParameters->sample_rate);
    }

    // print its name, id and bitrate
    logging("\tCodec %s ID %d bit_rate %lld", pLocalCodec->name,
            pLocalCodec->id, pCodecParameters->bit_rate);
  }
  // https://ffmpeg.org/doxygen/trunk/structAVCodecContext.html
  AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
  if (!pCodecContext) {
    logging("failed to allocated memory for AVCodecContext");
    return -1;
  }

  // Fill the codec context based on the values from the supplied codec
  // parameters
  // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#gac7b282f51540ca7a99416a3ba6ee0d16
  if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0) {
    logging("failed to copy codec params to codec context");
    return -1;
  }

  // Initialize the AVCodecContext to use the given AVCodec.
  // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#ga11f785a188d7d9df71621001465b0f1d
  if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
    logging("failed to open codec through avcodec_open2");
    return -1;
  }
  static struct SwsContext *swsCtx;
  swsCtx = sws_getCachedContext(
      NULL, pCodecContext->width, pCodecContext->height, pCodecContext->pix_fmt,
      pCodecContext->width, pCodecContext->height, AV_PIX_FMT_RGB24,
      SWS_BILINEAR, NULL, NULL, NULL);

  // https://ffmpeg.org/doxygen/trunk/structAVFrame.html
  AVFrame *pFrame = av_frame_alloc();
  AVFrame *pFrameRGB = av_frame_alloc();
  if (!pFrame) {
    logging("failed to allocated memory for AVFrame");
    return -1;
  }
  int numBytes = av_image_get_buffer_size(
      AV_PIX_FMT_RGB24, pCodecContext->width, pCodecContext->height, 8);
  uint8_t *frameBuffer = (uint8_t *)av_malloc(numBytes);
  av_image_fill_arrays(&pFrameRGB->data[0], &pFrameRGB->linesize[0],
                       frameBuffer, AV_PIX_FMT_RGB24, pCodecContext->width,
                       pCodecContext->height, 1);

  // https://ffmpeg.org/doxygen/trunk/structAVPacket.html
  AVPacket *pPacket = av_packet_alloc();
  if (!pPacket) {
    logging("failed to allocated memory for AVPacket");
    return -1;
  }
  SDL_Window *window = SDL_CreateWindow(
      "player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      pCodecContext->width, pCodecContext->height, 0);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
  ;
  SDL_Texture *texture = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
      pCodecContext->width, pCodecContext->height);

  int response = 0;
  int how_many_packets_to_process = 800;

  // fill the Packet with data from the Stream
  // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga4fdb3084415a82e3810de6ee60e46a61

  SDL_Event evt;
  array conv_kernel = constant(-1., 3, 3, f32);
  conv_kernel(1, 1) = 8;
  af_print(conv_kernel);
  while (av_read_frame(pFormatContext, pPacket) >= 0) {
    // if it's the video stream
    if (pPacket->stream_index == video_stream_index) {
      logging("AVPacket->pts %" PRId64, pPacket->pts);
      response = decode_packet(pPacket, pCodecContext, pFrame);
      if (response == 0) {
        sws_scale(swsCtx, pFrame->data, pFrame->linesize, 0,
                  pCodecContext->height, pFrameRGB->data, pFrameRGB->linesize);

        int w = pCodecContext->width;
        int h = pCodecContext->height;
        array frame(3, w, h, pFrameRGB->data[0]);
        printf("dims of frame before: %d %d %d\n", frame.dims(0), frame.dims(1),
               frame.dims(2));
        // arrayfire wants the colors in last dimension
        frame = reorder(frame, 1, 2, 0);
        printf("dims of frame: %d %d %d\n", frame.dims(0), frame.dims(1),
               frame.dims(2));

        array small =
            scale(frame, 0.5, 0.5, (unsigned)(0.5 * w), (unsigned)(0.5 * h));
        printf("dims of small: %d %d %d\n", small.dims(0), small.dims(1),
               small.dims(2));

        // original version
        frame(seq(0, small.dims(0) - 1), seq(0, small.dims(1) - 1), span) =
            small;

        // flipped version
        int x = small.dims(0);
        int y = 0;
        frame(seq(x, x + small.dims(0) - 1), seq(y, y + small.dims(1) - 1),
              span) = flip(small, 0);

        // edge detector, for fun
        x = 0;
        y = small.dims(1);
        ;
        array smallf = small.as(f32);
        array grey = smallf(span, span, 0) + smallf(span, span, 1) +
                     smallf(span, span, 2);
        grey /= 3;
        grey = convolve2(grey, conv_kernel);
        frame(seq(x, x + small.dims(0) - 1), seq(y, y + small.dims(1) - 1), 0) =
            grey;
        frame(seq(x, x + small.dims(0) - 1), seq(y, y + small.dims(1) - 1), 1) =
            grey;
        frame(seq(x, x + small.dims(0) - 1), seq(y, y + small.dims(1) - 1), 2) =
            grey;

        // send back to cpu
        frame = reorder(frame, 2, 0, 1);
        frame.host(pFrameRGB->data[0]);

        SDL_UpdateTexture(texture, NULL, pFrameRGB->data[0],
                          pFrameRGB->linesize[0]);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
      }

      av_frame_unref(pFrame);
      if (response < 0)
        break;
      // stop it, otherwise we'll be saving hundreds of frames
      if (--how_many_packets_to_process <= 0)
        break;
      SDL_PollEvent(&evt);
      switch (evt.type) {
      case SDL_QUIT:
        goto cleanup;
        break;
      default:
        break;
      }
    }
    // https://ffmpeg.org/doxygen/trunk/group__lavc__packet.html#ga63d5a489b419bd5d45cfd09091cbcbc2
    av_packet_unref(pPacket);
  }

cleanup:
  logging("releasing all the resources");
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  avformat_close_input(&pFormatContext);
  avformat_free_context(pFormatContext);
  av_packet_free(&pPacket);
  av_frame_free(&pFrame);
  avcodec_free_context(&pCodecContext);
  return 0;
}

static void logging(const char *fmt, ...) {
  va_list args;
  fprintf(stderr, "LOG: ");
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext,
                         AVFrame *pFrame) {
  // Supply raw packet data as input to a decoder
  // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
  int response = avcodec_send_packet(pCodecContext, pPacket);

  if (response < 0) {
    logging("Error while sending a packet to the decoder");
    return response;
  }

  while (response >= 0) {
    // Return decoded output data (into a frame) from a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
    response = avcodec_receive_frame(pCodecContext, pFrame);
    if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
      break;
    } else if (response < 0) {
      logging("Error while receiving a frame from the decoder");
      return response;
    }

    if (response >= 0) {
      logging("Frame %d (type=%c, size=%d bytes) pts %d key_frame %d [DTS %d]",
              pCodecContext->frame_number,
              av_get_picture_type_char(pFrame->pict_type), pFrame->pkt_size,
              pFrame->pts, pFrame->key_frame, pFrame->coded_picture_number);

      char frame_filename[1024];
      snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame",
               pCodecContext->frame_number);
      return 0;
    }
  }
  return 0;
}
