#include "export_encoder.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

const BatchFormat batch_formats[] = {
    {"FLAC", "flac", "flac", "flac", 44100, 0},
    {"WAV", "wav", "pcm_s16le", "wav", 44100, 0},
    {"Opus", "opus", "libopus", "opus", 48000, 128000},
    {"Ogg Vorbis", "ogg", "libvorbis", "ogg", 44100, 160000},
    {"M4A (AAC)", "m4a", "aac", "ipod", 44100, 256000}
};
const guint batch_format_count = G_N_ELEMENTS(batch_formats);

struct ExportEncoder {
    AVFormatContext *output;
    AVCodecContext *codec;
    AVStream *stream;
    AVPacket *packet;
    AVAudioFifo *fifo;
    SwrContext *swr;
    int64_t pts;
    int block;
};

static gboolean check(int result, const char *operation, GError **error)
{
    if (result >= 0) return TRUE;
    char detail[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(result, detail, sizeof detail);
    g_set_error(error, g_quark_from_static_string("modulez-encoder"), result,
                "%s: %s", operation, detail);
    return FALSE;
}

void export_encoder_free(ExportEncoder *enc)
{
    if (!enc) return;
    if (enc->output) {
        avio_closep(&enc->output->pb);
        avformat_free_context(enc->output);
    }
    avcodec_free_context(&enc->codec);
    av_packet_free(&enc->packet);
    swr_free(&enc->swr);
    if (enc->fifo) av_audio_fifo_free(enc->fifo);
    g_free(enc);
}

ExportEncoder *export_encoder_open(const char *path, const BatchFormat *format,
                                   const char *title, const char *artist, GError **error)
{
    ExportEncoder *enc = g_new0(ExportEncoder, 1);
    const AVCodec *codec = avcodec_find_encoder_by_name(format->codec);
    if (!check(codec ? 0 : AVERROR_ENCODER_NOT_FOUND, format->codec, error)) goto fail;
    if (!check(avformat_alloc_output_context2(&enc->output, NULL, format->container, path),
               "Create output container", error)) goto fail;
    enc->stream = avformat_new_stream(enc->output, NULL);
    enc->codec = avcodec_alloc_context3(codec);
    enc->packet = av_packet_alloc();
    if (!check(enc->stream && enc->codec && enc->packet ? 0 : AVERROR(ENOMEM),
               "Allocate encoder", error)) goto fail;
    AVCodecContext *ctx = enc->codec;
    ctx->sample_fmt = codec->sample_fmts[0];
    ctx->sample_rate = format->rate;
    ctx->bit_rate = format->bitrate;
    ctx->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    ctx->time_base = (AVRational){1, ctx->sample_rate};
    if (enc->output->oformat->flags & AVFMT_GLOBALHEADER) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (!check(avcodec_open2(ctx, codec, NULL), "Open encoder", error) ||
        !check(avcodec_parameters_from_context(enc->stream->codecpar, ctx), "Set audio parameters", error))
        goto fail;
    enc->block = ctx->frame_size ? ctx->frame_size : 4096;
    enc->stream->time_base = ctx->time_base;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (!check(swr_alloc_set_opts2(&enc->swr, &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate,
                                   &stereo, AV_SAMPLE_FMT_S16, 44100, 0, NULL), "Configure resampler", error) ||
        !check(swr_init(enc->swr), "Start resampler", error)) goto fail;
    enc->fifo = av_audio_fifo_alloc(ctx->sample_fmt, 2, enc->block);
    if (!check(enc->fifo ? 0 : AVERROR(ENOMEM), "Allocate sample buffer", error)) goto fail;
    if (title) av_dict_set(&enc->output->metadata, "title", title, 0);
    if (artist) av_dict_set(&enc->output->metadata, "artist", artist, 0);
    if (!g_strcmp0(format->container, "wav"))
        av_opt_set(enc->output->priv_data, "rf64", "auto", 0);
    if (!check(avio_open(&enc->output->pb, path, AVIO_FLAG_WRITE), "Open output", error) ||
        !check(avformat_write_header(enc->output, NULL), "Write header", error)) goto fail;
    return enc;
fail:
    export_encoder_free(enc);
    return NULL;
}

static gboolean send_frame(ExportEncoder *enc, AVFrame *frame, GError **error)
{
    if (!check(avcodec_send_frame(enc->codec, frame), "Encode audio", error)) return FALSE;
    for (;;) {
        int rc = avcodec_receive_packet(enc->codec, enc->packet);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return TRUE;
        if (!check(rc, "Receive audio packet", error)) return FALSE;
        av_packet_rescale_ts(enc->packet, enc->codec->time_base, enc->stream->time_base);
        enc->packet->stream_index = enc->stream->index;
        if (!check(av_interleaved_write_frame(enc->output, enc->packet), "Write audio packet", error))
            return FALSE;
    }
}

static gboolean drain_samples(ExportEncoder *enc, gboolean final, GError **error)
{
    int available;
    while ((available = av_audio_fifo_size(enc->fifo)) >= enc->block || (final && available > 0)) {
        AVFrame *frame = av_frame_alloc();
        if (!check(frame ? 0 : AVERROR(ENOMEM), "Allocate audio frame", error)) return FALSE;
        frame->format = enc->codec->sample_fmt;
        frame->sample_rate = enc->codec->sample_rate;
        frame->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        frame->nb_samples = MIN(available, enc->block);
        frame->pts = enc->pts;
        enc->pts += frame->nb_samples;
        gboolean ok = check(av_frame_get_buffer(frame, 0), "Allocate samples", error) &&
            check(av_audio_fifo_read(enc->fifo, (void **)frame->data, frame->nb_samples),
                  "Read samples", error) && send_frame(enc, frame, error);
        av_frame_free(&frame);
        if (!ok) return FALSE;
    }
    return TRUE;
}

gboolean export_encoder_write(ExportEncoder *enc, const int16_t *pcm, int frames, GError **error)
{
    const uint8_t *input[] = {(const uint8_t *)pcm};
    int capacity = MAX(1, swr_get_out_samples(enc->swr, frames));
    uint8_t *samples[2] = {NULL, NULL};
    if (!check(av_samples_alloc(samples, NULL, 2, capacity, enc->codec->sample_fmt, 0),
               "Allocate converted samples", error)) return FALSE;
    int got = swr_convert(enc->swr, samples, capacity, pcm ? input : NULL, frames);
    gboolean ok = check(got, "Convert samples", error) &&
        check(av_audio_fifo_write(enc->fifo, (void **)samples, got), "Buffer audio", error);
    av_freep(&samples[0]);
    return ok && drain_samples(enc, FALSE, error) && check(enc->output->pb->error, "Write audio", error);
}

gboolean export_encoder_finish(ExportEncoder *enc, GError **error)
{
    if (!export_encoder_write(enc, NULL, 0, error) || !drain_samples(enc, TRUE, error) ||
        !send_frame(enc, NULL, error)) return FALSE;
    return check(av_write_trailer(enc->output), "Finish output", error) &&
           check(avio_closep(&enc->output->pb), "Close output", error);
}
