#include "ffmpeg_audio.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

#define OUTPUT_RATE 44100
#define OUTPUT_CHANNELS 2

struct FfmpegAudio {
    AVFormatContext *format;
    AVCodecContext *codec;
    SwrContext *resampler;
    AVPacket *packet;
    AVFrame *frame;
    int stream_index;
    int flush_sent;
    int decoder_finished;
    int resampler_finished;
    int64_t duration_ms;

    int16_t *pcm;
    int pcm_capacity;
    int pcm_frames;
    int pcm_position;
};

static void set_error(char *dst, unsigned long size, const char *message)
{
    if (dst && size)
        snprintf(dst, size, "%s", message ? message : "Unknown error");
}

static void set_av_error(char *dst, unsigned long size,
                         const char *operation, int error)
{
    char detail[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(error, detail, sizeof(detail));

    if (dst && size)
        snprintf(dst, size, "%s: %s", operation, detail);
}

static int setup_resampler(FfmpegAudio *audio)
{
    AVChannelLayout input_layout = {0};
    AVChannelLayout output_layout = {0};
    int result;

    if (audio->frame->ch_layout.nb_channels > 0) {
        result = av_channel_layout_copy(
            &input_layout, &audio->frame->ch_layout);
        if (result < 0)
            return result;
    } else if (audio->codec->ch_layout.nb_channels > 0) {
        result = av_channel_layout_copy(
            &input_layout, &audio->codec->ch_layout);
        if (result < 0)
            return result;
    } else {
        av_channel_layout_default(&input_layout, 2);
    }

    av_channel_layout_default(&output_layout, OUTPUT_CHANNELS);

    result = swr_alloc_set_opts2(
        &audio->resampler,
        &output_layout,
        AV_SAMPLE_FMT_S16,
        OUTPUT_RATE,
        &input_layout,
        (enum AVSampleFormat)audio->frame->format,
        audio->frame->sample_rate > 0
            ? audio->frame->sample_rate : audio->codec->sample_rate,
        0,
        NULL);

    av_channel_layout_uninit(&input_layout);
    av_channel_layout_uninit(&output_layout);

    if (result < 0)
        return result;

    return swr_init(audio->resampler);
}

static int ensure_pcm_capacity(FfmpegAudio *audio, int frames)
{
    if (frames <= audio->pcm_capacity)
        return 0;

    if (frames <= 0 || frames > INT_MAX / (OUTPUT_CHANNELS * (int)sizeof(int16_t)))
        return AVERROR(EINVAL);

    int16_t *replacement = av_realloc_array(
        audio->pcm, (size_t)frames * OUTPUT_CHANNELS, sizeof(int16_t));

    if (!replacement)
        return AVERROR(ENOMEM);

    audio->pcm = replacement;
    audio->pcm_capacity = frames;
    return 0;
}

/* Decode and convert one frame, or flush the resampler at end of stream. */
static int produce_pcm(FfmpegAudio *audio)
{
    int result;

    audio->pcm_frames = 0;
    audio->pcm_position = 0;

    for (;;) {
        if (audio->decoder_finished) {
            if (audio->resampler_finished || !audio->resampler)
                return 0;

            int capacity = swr_get_out_samples(audio->resampler, 0);
            if (capacity < 1)
                capacity = 32;

            result = ensure_pcm_capacity(audio, capacity);
            if (result < 0)
                return result;

            uint8_t *output_planes[1] = {
                (uint8_t *)audio->pcm
            };

            result = swr_convert(audio->resampler, output_planes, capacity,
                                 NULL, 0);
            if (result < 0)
                return result;

            if (result == 0) {
                audio->resampler_finished = 1;
                return 0;
            }

            audio->pcm_frames = result;
            return result;
        }

        result = avcodec_receive_frame(audio->codec, audio->frame);

        if (result == 0) {
            if (!audio->resampler) {
                result = setup_resampler(audio);
                if (result < 0) {
                    av_frame_unref(audio->frame);
                    return result;
                }
            }

            int capacity = swr_get_out_samples(
                audio->resampler, audio->frame->nb_samples);
            if (capacity < 1)
                capacity = 32;

            result = ensure_pcm_capacity(audio, capacity);
            if (result < 0) {
                av_frame_unref(audio->frame);
                return result;
            }

            uint8_t *output_planes[1] = {
                (uint8_t *)audio->pcm
            };

            result = swr_convert(
                audio->resampler,
                output_planes,
                capacity,
                (const uint8_t **)audio->frame->extended_data,
                audio->frame->nb_samples);

            av_frame_unref(audio->frame);

            if (result < 0)
                return result;

            audio->pcm_frames = result;
            if (result > 0)
                return result;

            continue;
        }

        if (result == AVERROR_EOF) {
            audio->decoder_finished = 1;
            continue;
        }

        if (result != AVERROR(EAGAIN))
            return result;

        if (audio->flush_sent) {
            /* The decoder should eventually return AVERROR_EOF. */
            return AVERROR(EIO);
        }

        for (;;) {
            result = av_read_frame(audio->format, audio->packet);

            if (result == AVERROR_EOF) {
                result = avcodec_send_packet(audio->codec, NULL);
                if (result < 0 && result != AVERROR_EOF)
                    return result;

                audio->flush_sent = 1;
                break;
            }

            if (result < 0)
                return result;

            if (audio->packet->stream_index != audio->stream_index) {
                av_packet_unref(audio->packet);
                continue;
            }

            result = avcodec_send_packet(audio->codec, audio->packet);
            av_packet_unref(audio->packet);

            if (result == AVERROR(EAGAIN))
                break;

            if (result < 0)
                return result;

            break;
        }
    }
}

FfmpegAudio *ffmpeg_audio_open(const char *filename,
                               char *error_text,
                               unsigned long error_text_size)
{
    FfmpegAudio *audio = calloc(1, sizeof(*audio));
    if (!audio) {
        set_error(error_text, error_text_size, "Out of memory");
        return NULL;
    }

    int result = avformat_open_input(&audio->format, filename, NULL, NULL);
    if (result < 0) {
        set_av_error(error_text, error_text_size,
                     "Cannot open media file", result);
        ffmpeg_audio_close(audio);
        return NULL;
    }

    result = avformat_find_stream_info(audio->format, NULL);
    if (result < 0) {
        set_av_error(error_text, error_text_size,
                     "Cannot read media information", result);
        ffmpeg_audio_close(audio);
        return NULL;
    }

    result = av_find_best_stream(audio->format, AVMEDIA_TYPE_AUDIO,
                                 -1, -1, NULL, 0);
    if (result < 0) {
        set_av_error(error_text, error_text_size,
                     "No usable audio stream found", result);
        ffmpeg_audio_close(audio);
        return NULL;
    }

    audio->stream_index = result;
    AVStream *stream = audio->format->streams[audio->stream_index];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);

    if (!decoder) {
        set_error(error_text, error_text_size,
                  "No decoder available for this audio codec");
        ffmpeg_audio_close(audio);
        return NULL;
    }

    audio->codec = avcodec_alloc_context3(decoder);
    if (!audio->codec) {
        set_error(error_text, error_text_size,
                  "Cannot allocate audio decoder");
        ffmpeg_audio_close(audio);
        return NULL;
    }

    result = avcodec_parameters_to_context(audio->codec, stream->codecpar);
    if (result < 0) {
        set_av_error(error_text, error_text_size,
                     "Cannot configure audio decoder", result);
        ffmpeg_audio_close(audio);
        return NULL;
    }

    result = avcodec_open2(audio->codec, decoder, NULL);
    if (result < 0) {
        set_av_error(error_text, error_text_size,
                     "Cannot start audio decoder", result);
        ffmpeg_audio_close(audio);
        return NULL;
    }

    audio->packet = av_packet_alloc();
    audio->frame = av_frame_alloc();

    if (!audio->packet || !audio->frame) {
        set_error(error_text, error_text_size,
                  "Cannot allocate decoder buffers");
        ffmpeg_audio_close(audio);
        return NULL;
    }

    if (stream->duration != AV_NOPTS_VALUE && stream->duration >= 0) {
        audio->duration_ms = av_rescale_q(
            stream->duration, stream->time_base, (AVRational){1, 1000});
    } else if (audio->format->duration != AV_NOPTS_VALUE &&
               audio->format->duration >= 0) {
        audio->duration_ms = audio->format->duration / (AV_TIME_BASE / 1000);
    }

    set_error(error_text, error_text_size, "");
    return audio;
}

int ffmpeg_audio_read(FfmpegAudio *audio, int16_t *output, int max_frames)
{
    if (!audio || !output || max_frames <= 0)
        return -1;

    int copied = 0;

    while (copied < max_frames) {
        if (audio->pcm_position < audio->pcm_frames) {
            int available = audio->pcm_frames - audio->pcm_position;
            int wanted = max_frames - copied;
            int count = available < wanted ? available : wanted;

            memcpy(output + (size_t)copied * OUTPUT_CHANNELS,
                   audio->pcm + (size_t)audio->pcm_position * OUTPUT_CHANNELS,
                   (size_t)count * OUTPUT_CHANNELS * sizeof(int16_t));

            audio->pcm_position += count;
            copied += count;
            continue;
        }

        int result = produce_pcm(audio);
        if (result < 0)
            return copied > 0 ? copied : -1;

        if (result == 0)
            break;
    }

    return copied;
}
int ffmpeg_audio_seek(FfmpegAudio *audio, int64_t position_ms)
{
    if (!audio || !audio->format || !audio->codec ||
        audio->stream_index < 0)
        return -1;

    if (position_ms < 0)
        position_ms = 0;

    if (audio->duration_ms > 0 && position_ms > audio->duration_ms)
        position_ms = audio->duration_ms;

    AVStream *stream = audio->format->streams[audio->stream_index];
    int64_t timestamp = av_rescale_q(
        position_ms,
        (AVRational){1, 1000},
        stream->time_base
    );

    if (av_seek_frame(audio->format, audio->stream_index,
                      timestamp, AVSEEK_FLAG_BACKWARD) < 0)
        return -1;

    avcodec_flush_buffers(audio->codec);

    swr_free(&audio->resampler);

    audio->flush_sent = 0;
    audio->decoder_finished = 0;
    audio->resampler_finished = 0;
    audio->pcm_frames = 0;
    audio->pcm_position = 0;

    return 0;
}
int64_t ffmpeg_audio_duration_ms(const FfmpegAudio *audio)
{
    return audio ? audio->duration_ms : 0;
}

void ffmpeg_audio_close(FfmpegAudio *audio)
{
    if (!audio)
        return;

    av_freep(&audio->pcm);
    av_frame_free(&audio->frame);
    av_packet_free(&audio->packet);
    swr_free(&audio->resampler);
    avcodec_free_context(&audio->codec);
    avformat_close_input(&audio->format);
    free(audio);
}
