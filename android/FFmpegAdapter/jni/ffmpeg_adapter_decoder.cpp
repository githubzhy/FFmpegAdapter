#include "ffmpeg_adapter_decoder.h"
#include "ffmpeg_adapter_encoder.h"

#define TAG ("ffmpeg_adapter_decoder")

#define ENABLE_VIDEO 1

ffmpeg_adapter_decoder::ffmpeg_adapter_decoder() {
  m_p_video_stream = NULL;
  m_p_audio_stream = NULL;

  m_p_fmt_ctx = NULL;

  m_p_converter = NULL;

  m_video_codec_id = AV_CODEC_ID_NONE;
  m_audio_codec_id = AV_CODEC_ID_NONE;

  m_output_audio_format = AV_SAMPLE_FMT_S16;
  m_output_audio_channels = 2;
  m_output_audio_frame_size = 1024;
  m_output_audio_samplerate = DEFAULT_AUDIO_SAMPLE_RATE;

  m_output_video_width = 480;
  m_output_video_height = 480;
  m_output_video_format = AV_PIX_FMT_YUV420P;

  m_video_timestamp = 0;
  m_audio_timestamp = 0;

  m_file_eof = true;

  m_started = 0;
}

ffmpeg_adapter_decoder::~ffmpeg_adapter_decoder() {
  end();
}

int ffmpeg_adapter_decoder::is_started() {
  return m_started;
}

int ffmpeg_adapter_decoder::start(const char* filename) {
  int res = -1;

  AVCodecContext* p_video_ctx = NULL;
  AVCodecContext* p_audio_ctx = NULL;
  AVCodec* p_audio_codec = NULL;
  AVCodec* p_video_codec = NULL;
  AVDictionary* options = NULL;
  int index = 0;
  int nb_streams = 0;

  LOGD("%s %d E. ", __func__, __LINE__);

  if (NULL == filename || strlen(filename) <= 0){
    res = ErrorParametersInvalid;
    LOGE("%s %d E, invalid parama for input file name ", __func__, __LINE__);
    goto EXIT;
  }
  if (m_output_audio_channels <= 0 || m_output_audio_channels > 2){
    res = ErrorParametersInvalid;
    LOGE("%s %d E, invalid parama for output channels %d ", __func__, __LINE__, m_output_audio_channels);
    goto EXIT;
  }

  if (m_output_audio_samplerate <= 0){
    res = ErrorParametersInvalid;
    LOGE("%s %d E, invalid parama for output sample rate %d ", __func__, __LINE__, m_output_audio_samplerate);
    goto EXIT;
  }

  if (m_output_audio_format <= AV_SAMPLE_FMT_NONE || m_output_audio_format >= AV_SAMPLE_FMT_NB){
    res = ErrorParametersInvalid;
    LOGE("%s %d E, invalid parama for output format %d ", __func__, __LINE__, m_output_audio_format);
    goto EXIT;
  }
  if (m_output_video_width <= 0 || m_output_video_height <= 0){
    res = ErrorParametersInvalid;
    LOGE("%s %d E, invalid parama for output width %d, height %d ", __func__, __LINE__, m_output_video_width, m_output_video_height);
    goto EXIT;
  }

  if (AV_PIX_FMT_NONE >= m_output_video_format || m_output_video_format >= AV_PIX_FMT_NB){
    res = ErrorParametersInvalid;
    LOGE("%s %d E, invalid parama for output video format %d ", __func__, __LINE__, m_output_video_format);
    goto EXIT;
  }

  av_register_all();
  avcodec_register_all();

  res = avformat_open_input(&m_p_fmt_ctx, filename, NULL, NULL);
  if (res < 0) {
    LOGE("Could not open find stream info (error '%s')\n",
      get_error_text(res));
    goto EXIT;
  }

  if (options) {
    av_dict_free(&options);
    options = NULL;
  }

  /** Get information on the input file (number of streams etc.). */
  res = avformat_find_stream_info(m_p_fmt_ctx, NULL);
  if (res < 0) {
    LOGE("Could not open find stream info (error '%s')\n", get_error_text(res));
    goto EXIT;
  }

  // Dump information about file onto standard error
  av_dump_format(m_p_fmt_ctx, 0, filename, 0);

  nb_streams = m_p_fmt_ctx->nb_streams;
  for (index = 0; index < nb_streams; index++) {
    AVStream* st = m_p_fmt_ctx->streams[index];
    // Get a pointer to the codec context for the video or audio stream
    AVCodecContext* c = st->codec;
    if (!m_p_video_stream && c->codec_type == AVMEDIA_TYPE_VIDEO) {
#ifdef ENABLE_VIDEO
      m_p_video_stream = st;
      p_video_ctx = c;
#endif
    } else if (!m_p_audio_stream && c->codec_type == AVMEDIA_TYPE_AUDIO) {
      m_p_audio_stream = st;
      p_audio_ctx = c;
    }
  }

  if (!m_p_video_stream && !m_p_audio_stream) {
    LOGE("Did not find a video or audio stream inside %s", filename);
    res = ErrorNoStreamFound;
    goto EXIT;
  }

  if (m_p_video_stream) {
    // Find the decoder for the video stream
    p_video_codec = avcodec_find_decoder(p_video_ctx->codec_id);
    if (!p_video_codec) {
      LOGE("avcodec_find_decoder() error: Unsupported video format or codec not found: %d. ", p_video_ctx->codec_id);
    }

    /* if (m_p_video_codec->capabilities & CODEC_CAP_TRUNCATED) {
       p_video_ctx->flags |= CODEC_FLAG_TRUNCATED;
       }*/


    // Open video codec
    if (p_video_codec && (res = avcodec_open2(p_video_ctx, p_video_codec, NULL)) < 0) {
      LOGE("avcodec_open2() error %d: Could not open video codec.", res);
    }

    // Hack to correct wrong frame rates that seem to be generated by some codecs
    if (p_video_ctx->time_base.num > 1000 && p_video_ctx->time_base.den == 1) {
      p_video_ctx->time_base.den = 1000;
    }

    LOGE("width= %d, height=%d video format=%d: ", p_video_ctx->width, p_video_ctx->height, p_video_ctx->pix_fmt);
  }

  if (m_p_audio_stream) {
    // Find the decoder for the audio stream
    p_audio_codec = avcodec_find_decoder(p_audio_ctx->codec_id);
    if (!p_audio_codec) {
      res = ErrorNoDecoderFound;
      LOGE("avcodec_find_decoder() error: Unsupported audio format or codec not found: %d", p_audio_ctx->codec_id);
      goto EXIT;
    }

    // Open audio codec
    res = avcodec_open2(p_audio_ctx, p_audio_codec, NULL);
    if (res < 0){
      LOGE("avcodec_open2() error %d Could not open audio codec.", res);
      goto EXIT;
    }
  }

  m_p_converter = new ffmpeg_adapter_converter();
  if (!m_p_converter) {
    res = ErrorAllocFailed;
    goto EXIT;
  }

  if (m_p_audio_stream && m_p_audio_stream->codec) {
    res = set_output_audio_parameters(
      m_output_audio_channels,
      m_output_audio_frame_size,
      m_output_audio_samplerate,
      m_output_audio_format);
    if (res)
      goto EXIT;
  }

  if (m_p_video_stream && m_p_video_stream->codec) {
    res = set_output_video_parameters(
      m_output_video_width,
      m_output_video_height,
      m_output_video_format);
    if (res)
      goto EXIT;
  }

  res = 0;
EXIT:
  if (res) {
    end();
  } else {
    m_file_eof = false;
    m_started = 1;
  }

  LOGD("%s %d X. res=%d ", __func__, __LINE__, res);

  return res;
}

void ffmpeg_adapter_decoder::end() {
  m_started = 0;

  if (m_p_converter) {
    delete m_p_converter;
    m_p_converter = NULL;
  }

  if (m_p_fmt_ctx) {
    if (m_p_video_stream && m_p_video_stream->codec) {
      avcodec_close(m_p_video_stream->codec);
    }

    if (m_p_audio_stream && m_p_audio_stream->codec) {
      avcodec_close(m_p_audio_stream->codec);
    }

    avformat_close_input(&m_p_fmt_ctx);
    m_p_fmt_ctx = NULL;
  }

  m_file_eof = true;
}

bool ffmpeg_adapter_decoder::is_eof() {
  return m_file_eof &&
    (m_p_converter ?
    m_p_converter->get_audio_fifo_size() <= 0 :
    1);
}

AVFrame* ffmpeg_adapter_decoder::get_audio_frame() {
  int res = -1;
  int got_frame = 0;
  int done = 0;
  int decoded_length = 0;
  AVPacket pkt = { 0 };
  AVFrame* temp_frame = NULL;
  AVFrame* output_frame = NULL;

  LOGD("%s %d E. ", __func__, __LINE__);

  if (!m_p_fmt_ctx || !m_p_audio_stream || !m_p_audio_stream->codec || !m_p_converter) {
    res = ErrorNoContext;
    goto EXIT;
  }

  if (is_eof()) {
    res = AVERROR_EOF;
    goto EXIT;
  }

  temp_frame = av_frame_alloc();
  if (!temp_frame) {
    res = ErrorAllocFailed;
    goto EXIT;
  }

  do
  {
    av_init_packet(&pkt);

    res = av_read_frame(m_p_fmt_ctx, &pkt);
    if (res == AVERROR_EOF) {
      m_file_eof = true;
    } else if (res) {
      av_free_packet(&pkt);
      goto EXIT;
    }

    if (pkt.stream_index != m_p_audio_stream->index) {
      av_free_packet(&pkt);
      continue;
    }

    //if audio is EOF then just output.
    if (!m_file_eof) {
      // Decode audio frames
      decoded_length = avcodec_decode_audio4(m_p_audio_stream->codec, temp_frame, &got_frame, &pkt);
      if (decoded_length <= 0 || !got_frame) {
        av_frame_unref(temp_frame);
        av_free_packet(&pkt);
        continue;
      }

      res = m_p_converter->input_audio(temp_frame);
      av_free_packet(&pkt);
      if (res)
        goto EXIT;

      //TODO -- to split the audio timestamp
      if (!m_audio_timestamp && m_p_audio_stream->time_base.den) {
        m_audio_timestamp = static_cast<long>(
          (double)(av_frame_get_best_effort_timestamp(temp_frame) * m_p_audio_stream->time_base.num) /
          (double)m_p_audio_stream->time_base.den *
          1000000L);
      }

      av_frame_unref(temp_frame);
    }

    res = m_p_converter->output_audio(&output_frame, m_file_eof);
    if (res == ErrorBufferNotReady) {
      //need more data
      continue;
    } else if (res) {
      //error occurred
      goto EXIT;
    } else {
      //data is ready
      done = true;

      m_audio_timestamp += static_cast<long>(
        (double)m_output_audio_frame_size /
        (double)m_output_audio_samplerate *
        1000000L);
    }
  } while (!done && !m_file_eof);

  res = 0;
EXIT:
  if (res) {

  }

  if (temp_frame) {
    RELEASE_FRAME(temp_frame);
    temp_frame = NULL;
  }

  LOGD("%s %d X. res = %d", __func__, __LINE__, res);

  return output_frame;
}

AVFrame* ffmpeg_adapter_decoder::get_video_frame() {
  int res = -1;
  int got_frame = 0;
  int decoded_length = 0;
  AVPacket pkt = { 0 };
  AVFrame* temp_frame = NULL;
  AVFrame* result_frame = NULL;

  LOGD("%s %d E \n", __func__, __LINE__);

  if (is_eof()) {
    res = AVERROR_EOF;
    goto EXIT;
  }

  if (!m_p_fmt_ctx) {
    res = ErrorNoContext;
    goto EXIT;
  }

  if (!m_p_video_stream || !m_p_video_stream->codec || !m_p_converter) {
    res = ErrorNoContext;
    goto EXIT;
  }

  //if this parameters are not pre-set, then use default value.
  if (m_output_video_width <= 0 || m_output_video_height <= 0) {
    m_output_video_width = m_p_video_stream->codec->width;
    m_output_video_height = m_p_video_stream->codec->height;
  }

  //if this parameters are not pre-set, then use default value.
  if (m_output_video_format <= -1) {
    m_output_video_format = m_p_video_stream->codec->pix_fmt;
  }

  temp_frame = av_frame_alloc();
  if (!temp_frame) {
    res = ErrorAllocFailed;
    goto EXIT;
  }

  do
  {
    av_init_packet(&pkt);

    res = av_read_frame(m_p_fmt_ctx, &pkt);
    if (res < 0) {
      av_free_packet(&pkt);
      break;
    }

    if (pkt.stream_index != m_p_video_stream->index) {
      av_free_packet(&pkt);
      continue;
    }

    // Decode audio frame
    decoded_length = avcodec_decode_video2(m_p_video_stream->codec, temp_frame, &got_frame, &pkt);
    if (decoded_length <= 0 ||
      !got_frame ||
      temp_frame->height <= 0 ||
      temp_frame->width <= 0
      /*|| !result_frame->key_frame*/) {
      // On error, trash the whole packet
      av_frame_unref(temp_frame);
      av_free_packet(&pkt);
      continue;
    }

    res = m_p_converter->convert_video(&result_frame, temp_frame);
    av_free_packet(&pkt);
    CHK_RES(res);

    LOGD("%s %d src w=%d h=%d format=%d, dst w=%d h=%d format=%d \n",
      __func__, __LINE__,
      temp_frame->width,
      temp_frame->height,
      temp_frame->format,
      result_frame->width,
      result_frame->height,
      result_frame->format);

    if (m_p_video_stream->time_base.den) {
      m_video_timestamp = static_cast<long>(
        (double)(av_frame_get_best_effort_timestamp(temp_frame) * m_p_video_stream->time_base.num) /
        (double)m_p_video_stream->time_base.den *
        1000000L);
    }

    av_frame_unref(temp_frame);
  } while (!got_frame);

  if (res) {
    goto EXIT;
  }

  res = 0;
EXIT:
  if (temp_frame) {
    RELEASE_FRAME(temp_frame);
    temp_frame = NULL;
  }

  //av_free_packet(&pkt);

  m_file_eof = (AVERROR_EOF == res);

  LOGD("%s %d X res=%d \n", __func__, __LINE__, res);

  return result_frame;
}

long ffmpeg_adapter_decoder::get_video_timestamp() {
  return m_video_timestamp;
}

long ffmpeg_adapter_decoder::get_audio_timestamp() {
  return m_audio_timestamp;
}

long ffmpeg_adapter_decoder::get_video_duration() {
  if (!m_p_video_stream) {
    return 0;
  }

  return m_p_video_stream->duration;
}

long ffmpeg_adapter_decoder::get_audio_duration() {
  if (!m_p_audio_stream) {
    return 0;
  }

  if (!m_p_audio_stream->time_base.den) {
    return 0;
  }

  return (double)m_p_audio_stream->duration / m_p_audio_stream->time_base.den;
}

int ffmpeg_adapter_decoder::seek_to(long timestamp) {
  int res = -1;
  LOGD("%s%d E timestamp=%ld", __func__, __LINE__, timestamp);

  if (!m_p_fmt_ctx) {
    res = ErrorNoContext;
    goto EXIT;
  }

  if (m_p_fmt_ctx->start_time != AV_NOPTS_VALUE)
    timestamp += m_p_fmt_ctx->start_time;

  res = av_seek_frame(m_p_fmt_ctx, -1, timestamp, AVSEEK_FLAG_BACKWARD); //TODO add timestamp
  if (res < 0) {
    LOGE("%s %d Error: %s", __func__, __LINE__, get_error_text(res));
    goto EXIT;
  }

  m_audio_timestamp = 0;

  m_file_eof = false;

  res = 0;
EXIT:
  if (res) {

  } else {
    m_file_eof = false;
  }

  LOGD("%s%d X res=%d", __func__, __LINE__, res);

  return res;
}

int ffmpeg_adapter_decoder::set_output_video_resolution(int width, int height) {
  return set_output_video_parameters(width, height, m_output_video_format);
}

int ffmpeg_adapter_decoder::set_output_video_format(int format) {
  return set_output_video_parameters(m_output_video_width, m_output_video_height, format);
}

int ffmpeg_adapter_decoder::set_output_video_parameters(int width, int height, int format) {
  int res = -1;

  m_output_video_width = width;
  m_output_video_height = height;
  m_output_video_format = format;

  if (width <= 0 || height <= 0 || (width % 16) || format <= -1) {
    res = ErrorParametersInvalid;
    goto EXIT;
  }

  if (m_p_converter && m_p_video_stream && m_p_video_stream->codec) {
    res = m_p_converter->init_video(
      m_p_video_stream->codec->width,
      m_p_video_stream->codec->height,
      m_p_video_stream->codec->pix_fmt,
      m_output_video_width,
      m_output_video_height,
      m_output_video_format);
    if (res)
      goto EXIT;
  }

  res = 0;
EXIT:
  if (res) {
    LOGE("%s %d Error res = %d", __func__, __LINE__, res);
  }

  return res;
}

int ffmpeg_adapter_decoder::get_audio_channels() {
  if (m_p_audio_stream && m_p_audio_stream->codec)
    return m_p_audio_stream->codec->channels;
  else
    return 0;
}

int ffmpeg_adapter_decoder::get_audio_channel_layout() {
  if (m_p_audio_stream && m_p_audio_stream->codec)
    return m_p_audio_stream->codec->channel_layout;
  else
    return 0;
}

int ffmpeg_adapter_decoder::get_audio_format() {
  if (m_p_audio_stream && m_p_audio_stream->codec)
    return m_p_audio_stream->codec->sample_fmt;
  else
    return 0;
}

int ffmpeg_adapter_decoder::get_audio_samplerate() {
  if (m_p_audio_stream && m_p_audio_stream->codec)
    return m_p_audio_stream->codec->sample_rate;
  else
    return 0;
}

int ffmpeg_adapter_decoder::get_audio_bitrate() {
  if (m_p_audio_stream && m_p_audio_stream->codec)
    return m_p_audio_stream->codec->bit_rate;
  else
    return 0;
}

int ffmpeg_adapter_decoder::get_audio_framesize() {
  if (m_p_audio_stream && m_p_audio_stream->codec)
    return m_p_audio_stream->codec->frame_size;
  else
    return 0;
}

int ffmpeg_adapter_decoder::get_output_audio_buffer_size() {
  if (!m_p_converter)
    return 0;

  return m_p_converter->get_output_audio_buffer_size();
}

int ffmpeg_adapter_decoder::set_output_audio_frame_size(int frame_size) {
  return set_output_audio_parameters(m_output_audio_channels,
    frame_size,
    m_output_audio_samplerate,
    m_output_audio_format);
}

int ffmpeg_adapter_decoder::set_output_audio_channels(int channels) {
  return set_output_audio_parameters(channels,
    m_output_audio_frame_size,
    m_output_audio_samplerate,
    m_output_audio_format);
}

int ffmpeg_adapter_decoder::set_output_samplerate(int samplerate) {
  return set_output_audio_parameters(m_output_audio_channels,
    m_output_audio_frame_size,
    samplerate,
    m_output_audio_format);
}

int ffmpeg_adapter_decoder::set_output_audio_format(int format) {
  return set_output_audio_parameters(m_output_audio_channels,
    m_output_audio_frame_size,
    m_output_audio_samplerate,
    format);
}

int ffmpeg_adapter_decoder::set_output_audio_parameters(int channels, int frame_size, int samplerate, int format){
  int res = -1;

  //set first
  m_output_audio_channels = channels;
  m_output_audio_frame_size = frame_size;
  m_output_audio_samplerate = samplerate;
  m_output_audio_format = format;

  if (channels <= -1 || frame_size <= -1 || samplerate <= -1 || format <= -1) {
    res = ErrorParametersInvalid;
    goto EXIT;
  }

  if (m_p_converter && m_p_audio_stream && m_p_audio_stream->codec) {
    res = m_p_converter->init_audio(
      m_p_audio_stream->codec->sample_rate, m_p_audio_stream->codec->channels, m_p_audio_stream->codec->sample_fmt,
      m_output_audio_samplerate, m_output_audio_channels, m_output_audio_format, m_output_audio_frame_size);
    if (res)
      goto EXIT;
  }

  res = 0;
EXIT:
  if (res) {
    //need to destroy convert?
    LOGE("%s %d Error res = %d", __func__, __LINE__, res);
  }

  return res;
}

//
//
//// Allocate video frame and an AVFrame structure for the RGB image
//if ((picture = avcodec_alloc_frame()) == null) {
//  throw new Exception("avcodec_alloc_frame() error: Could not allocate raw picture frame.");
//}
//if ((picture_rgb = avcodec_alloc_frame()) == null) {
//  throw new Exception("avcodec_alloc_frame() error: Could not allocate RGB picture frame.");
//}
//
//int width = getImageWidth()  > 0 ? getImageWidth() : video_c.width();
//int height = getImageHeight() > 0 ? getImageHeight() : video_c.height();
//
//
//
//// Assign appropriate parts of buffer to image planes in picture_rgb
//// Note that picture_rgb is an AVFrame, but AVFrame is a superset of AVPicture
//avpicture_fill(new AVPicture(picture_rgb), buffer_rgb, fmt, width, height);
//