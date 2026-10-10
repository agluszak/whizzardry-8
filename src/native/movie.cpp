/* Native Bink replacement: bounded SGP file/SLF I/O, FFmpeg decoding and
   timed RGB555 frames. Recovered IntroScreen still owns transitions/input. */
#include "movie.h"
#include "wiz8/filesystem.h"
#include "wiz8/slf.h"
#include <wiz8/filesystem.h>
#include "compat/surfaces.h"
#include "native/movie_audio.h"
#include "surrender/srGERD.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/surface2d.h"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
void check(int result, const char* operation)
{
    if (result >= 0)
        return;
    char error[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(result, error, sizeof(error));
    throw std::runtime_error(std::string(operation) + ": " + error);
}
} // namespace
struct W8NativeVideo::State
{
    std::unique_ptr<wiz8::File> file;
    uint64_t start = 0, length = 0, position = 0;
    AVIOContext* io = nullptr;
    AVFormatContext* format = nullptr;
    AVCodecContext *video = nullptr, *audio = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* decoded = nullptr;
    SwsContext* scale = nullptr;
    SwrContext* resample = nullptr;
    int video_index = -1, audio_index = -1;
    double origin = 0, frame_duration = 0, last_video_time = 0, end_time = 0;
    unsigned video_frames = 0;
    uint64_t audio_frames = 0;
    bool eof = false, started = false;
    uint64_t began = 0; // w8_clock_us
    std::unique_ptr<w8_native::MovieAudio> voice;
    std::deque<Frame> queued;
    Frame current{};
    srColorSurface* presentation_surface = nullptr;
    stSurface2D* presentation_tiles = nullptr;
    ~State()
    {
        if (presentation_tiles)
            presentation_tiles->release();
        if (presentation_surface)
            presentation_surface->release();
        voice.reset();
        swr_free(&resample);
        sws_freeContext(scale);
        av_frame_free(&decoded);
        av_packet_free(&packet);
        avcodec_free_context(&audio);
        avcodec_free_context(&video);
        avformat_close_input(&format);
        if (io)
            av_freep(&io->buffer); // libavformat may replace the original AVIO buffer.
        avio_context_free(&io);
        file.reset();
    }
    static int read(void* opaque, uint8_t* bytes, int size)
    {
        auto& source = *static_cast<State*>(opaque);
        if (size < 0)
            return AVERROR(EINVAL);
        if (!size)
            return 0;
        if (!source.file || source.position > source.length)
            return AVERROR(EIO);
        if (source.position == source.length)
            return AVERROR_EOF;
        try
        {
            const auto result = source.file->read(
                bytes, std::min<uint64_t>(size, source.length - source.position));
            source.position += result.bytes;
            return result.bytes ? int(result.bytes) : result.eof ? AVERROR_EOF : AVERROR(EIO);
        }
        catch (...)
        {
            return AVERROR(EIO);
        }
    }
    static int64_t seek(void* opaque, int64_t offset, int whence)
    {
        auto& source = *static_cast<State*>(opaque);
        const auto limit = uint64_t(std::numeric_limits<int64_t>::max());
        if (source.length > limit || source.position > source.length ||
            source.start > limit - source.length)
            return AVERROR(EINVAL);
        if (whence & AVSEEK_SIZE)
            return int64_t(source.length);
        whence &= ~AVSEEK_FORCE;
        const int64_t base = whence == SEEK_SET   ? 0
                             : whence == SEEK_CUR ? int64_t(source.position)
                             : whence == SEEK_END ? int64_t(source.length)
                                                  : -1;
        if (base < 0 || offset < -base || offset > int64_t(source.length) - base)
            return AVERROR(EINVAL);
        const uint64_t target = base + offset;
        try
        {
            if (!source.file ||
                source.file->seek(int64_t(source.start + target), wiz8::SeekOrigin::begin) !=
                    int64_t(source.start + target))
                return AVERROR(EIO);
            source.position = target;
            return int64_t(target);
        }
        catch (...)
        {
            return AVERROR(EIO);
        }
    }
    AVCodecContext* codec(int index)
    {
        auto parameters = format->streams[index]->codecpar;
        auto decoder = avcodec_find_decoder(parameters->codec_id);
        if (!decoder)
            throw std::runtime_error("Movie codec is unavailable");
        auto context = avcodec_alloc_context3(decoder);
        if (!context)
            throw std::bad_alloc();
        // Store before operations that can fail so State owns every allocation.
        if (index == video_index)
            video = context;
        else
            audio = context;
        check(avcodec_parameters_to_context(context, parameters), "Movie parameters");
        context->pkt_timebase = format->streams[index]->time_base;
        check(avcodec_open2(context, decoder, nullptr), "Movie codec");
        return context;
    }
    void open(const char* path)
    {
        if (!path)
            throw std::runtime_error("Movie path is null");
        file = wiz8::open_file(path);
        length = file->size();
        if (!file)
            throw std::runtime_error("Cannot open movie stream");
        start = file->tell();
        const auto size = uint64_t(file->size());
        if (start > size || length > size - start)
            throw std::runtime_error("Movie stream exceeds its file");
        auto buffer = static_cast<unsigned char*>(av_malloc(32768));
        if (!buffer)
            throw std::bad_alloc();
        io = avio_alloc_context(buffer, 32768, 0, this, read, nullptr, seek);
        if (!io)
        {
            av_free(buffer);
            throw std::bad_alloc();
        }
        format = avformat_alloc_context();
        if (!format)
            throw std::bad_alloc();
        format->pb = io;
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        check(avformat_open_input(&format, nullptr, nullptr, nullptr), "Movie container");
        check(avformat_find_stream_info(format, nullptr), "Movie streams");
        video_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        check(video_index, "Movie video stream");
        codec(video_index);
        if (video->width <= 0 || video->width > 640 || video->height <= 0 || video->height > 480)
            throw std::runtime_error("Movie exceeds the 640x480 presentation surface");
        auto rate = av_guess_frame_rate(format, format->streams[video_index], nullptr);
        if (!rate.num || !rate.den)
            throw std::runtime_error("Movie has no frame rate");
        frame_duration = av_q2d(av_inv_q(rate));
        if (format->start_time != AV_NOPTS_VALUE)
            origin = double(format->start_time) / AV_TIME_BASE;
        audio_index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audio_index >= 0)
        {
            codec(audio_index);
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            check(swr_alloc_set_opts2(&resample, &stereo, AV_SAMPLE_FMT_FLT, 44100,
                                      &audio->ch_layout, audio->sample_fmt, audio->sample_rate, 0,
                                      nullptr),
                  "Movie resampler");
            check(swr_init(resample), "Movie resampler initialization");
            if (w8_native::movie_audio_available())
                voice = std::make_unique<w8_native::MovieAudio>(44100, 2);
        }
        packet = av_packet_alloc();
        decoded = av_frame_alloc();
        if (!packet || !decoded)
            throw std::bad_alloc();
    }
    void audio_frame(const AVFrame* frame)
    {
        int capacity = swr_get_out_samples(resample, frame ? frame->nb_samples : 0);
        check(capacity, "Movie audio capacity");
        std::vector<float> samples(size_t(capacity) * 2);
        uint8_t* output = reinterpret_cast<uint8_t*>(samples.data());
        int count = swr_convert(resample, &output, capacity,
                                frame ? const_cast<const uint8_t**>(frame->extended_data) : nullptr,
                                frame ? frame->nb_samples : 0);
        check(count, "Movie audio conversion");
        if (voice && count)
            voice->append(samples.data(), count);
        audio_frames += count;
    }
    void video_frame()
    {
        double time =
            decoded->best_effort_timestamp == AV_NOPTS_VALUE
                ? video_frames * frame_duration
                : decoded->best_effort_timestamp * av_q2d(format->streams[video_index]->time_base) -
                      origin;
        Frame frame{decoded->width, decoded->height, std::max(0.0, time), frame_duration, {}};
        frame.pixels.resize(size_t(frame.width) * frame.height);
        scale =
            sws_getCachedContext(scale, frame.width, frame.height, AVPixelFormat(decoded->format),
                                 frame.width, frame.height, AV_PIX_FMT_RGB555LE,
                                 SWS_BILINEAR | SWS_BITEXACT, nullptr, nullptr, nullptr);
        if (!scale)
            throw std::runtime_error("Cannot create movie RGB555 converter");
        uint8_t* output[] = {reinterpret_cast<uint8_t*>(frame.pixels.data()), nullptr, nullptr,
                             nullptr};
        int pitch[] = {frame.width * 2, 0, 0, 0};
        check(sws_scale(scale, decoded->data, decoded->linesize, 0, frame.height, output, pitch),
              "Movie pixel conversion");
        last_video_time = frame.time;
        end_time = frame.time + frame.duration;
        ++video_frames;
        queued.push_back(std::move(frame));
    }
    void decode(AVCodecContext* context, const AVPacket* input)
    {
        check(avcodec_send_packet(context, input), "Movie packet");
        while (true)
        {
            int result = avcodec_receive_frame(context, decoded);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
                break;
            check(result, "Movie decoding");
            if (context == video)
                video_frame();
            else
                audio_frame(decoded);
            av_frame_unref(decoded);
        }
    }
    void fill(double until)
    {
        while (!eof && (queued.empty() || last_video_time < until))
        {
            int result = av_read_frame(format, packet);
            if (result == AVERROR_EOF)
            {
                decode(video, nullptr);
                if (audio)
                {
                    decode(audio, nullptr);
                    audio_frame(nullptr);
                    end_time = std::max(end_time, audio_frames / 44100.0);
                }
                if (voice)
                    voice->finish();
                eof = true;
                break;
            }
            check(result, "Movie demux");
            if (packet->stream_index == video_index)
                decode(video, packet);
            else if (packet->stream_index == audio_index)
                decode(audio, packet);
            av_packet_unref(packet);
            if (queued.size() > 64)
                throw std::runtime_error("Movie decoder exceeded frame lookahead");
        }
    }
};
W8NativeVideo::W8NativeVideo() : state(std::make_unique<State>())
{
}
W8NativeVideo::~W8NativeVideo() = default;
void W8NativeVideo::open(const char* path)
{
    state = std::make_unique<State>();
    auto next = std::make_unique<State>();
    next->open(path);
    next->fill(0.25);
    state = std::move(next);
}
W8NativeVideo::Result W8NativeVideo::update(double elapsed)
{
    // Keep at most a quarter-second of video/audio ahead of the frame being
    // shown. A stalled caller catches up one frame at a time, as the recovered
    // Bink owner did.
    double next = state->queued.empty() ? state->last_video_time : state->queued.front().time;
    state->fill(next + 0.25);
    if (!state->started)
    {
        if (state->voice)
            state->voice->start();
        state->started = true;
    }
    if (!state->queued.empty() && state->queued.front().time <= elapsed)
    {
        state->current = std::move(state->queued.front());
        state->queued.pop_front();
        return FrameReady;
    }
    if (state->eof && state->queued.empty() && elapsed >= state->end_time &&
        (!state->voice || state->voice->drained()))
        return Done;
    return Waiting;
}
const W8NativeVideo::Frame& W8NativeVideo::frame() const
{
    return state->current;
}
unsigned W8NativeVideo::decoded_frames() const
{
    return state->video_frames;
}
uint64_t W8NativeVideo::decoded_audio_frames() const
{
    return state->audio_frames;
}

W8NativeVideo::Result W8NativeVideo::update_now()
{
    const uint64_t now = w8_clock_us();
    if (!state->started)
        state->began = now;
    return update((now - state->began) / 1e6);
}

void W8NativeVideo::present(CpuSurface* target)
{
    if (!g_gerd || !g_surface_node)
        return;
    SurfaceLock description{};
    description.width = target->surface->w;
    description.height = target->surface->h;
    if (!state->presentation_surface)
    {
        state->presentation_surface = new srColorSurface(srPixelConvert::SURFACE_RGB555,
                                                         description.width, description.height);
        state->presentation_tiles = new stSurface2D(
            state->presentation_surface, description.width, description.height, nullptr, 128);
        state->presentation_tiles->setAlphaTestEnabled(false);
    }
    description = LockCpuSurface(*target);
    if (!description.pixels)
        throw std::runtime_error("Cannot lock movie presentation surface");
    for (int y = 0; y < description.height; ++y)
        state->presentation_surface->setPixelRowRaw(
            static_cast<unsigned char*>(description.pixels) + y * description.pitch, y, 0,
            description.width);
    // stSurface2D uploads from its source surface, not updateRectangle's ABI
    // pixel argument.
    state->presentation_tiles->updateRectangle(g_gerd, description.pixels, description.pitch, 0,
                                               0, description.width, description.height);
    UnlockCpuSurface(*target);
    if (g_gerd->beginFrame() != srGERD::ERROR_NONE)
        throw std::runtime_error("Cannot begin movie presentation");
    g_gerd->setClearColor(0, 0, 0, 1);
    g_gerd->clear(srFlags<srGERD::e_buffer>(srGERD::BUFFER_COLOR | srGERD::BUFFER_DEPTH));
    g_gerd->matrixMode(srGERD::MATRIX_MODELVIEW);
    g_gerd->loadIdentity();
    state->presentation_tiles->DrawTiles(g_gerd);
    g_gerd->endFrame();
}
