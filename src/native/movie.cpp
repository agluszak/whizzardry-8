/* Native Bink replacement: FFmpeg decoding and timed RGB555 frames.
   IntroScreen owns transitions and input. */
#include "wiz8/movie.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <wiz8/filesystem.h>

#include "asset_paths.h"
#include "wiz8/audio/movie_audio.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/renderer.h"
#include "wiz8/runtime_test_hooks.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <deque>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void check(int result, const char* operation)
{
    if (result >= 0) return;
    char error[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(result, error, sizeof(error));
    throw std::runtime_error(std::string(operation) + ": " + error);
}
} // namespace
struct W8NativeVideo::State {
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
    uint64_t began = 0; // MonotonicMicroseconds
    std::unique_ptr<w8_native::MovieAudio> voice;
    std::deque<Frame> queued;
    Frame current{};
    std::shared_ptr<SDL_Surface> presentation_surface;
    std::unique_ptr<UiCanvasTexture> presentation_overlay;
    ~State()
    {
        voice.reset();
        swr_free(&resample);
        sws_freeContext(scale);
        av_frame_free(&decoded);
        av_packet_free(&packet);
        avcodec_free_context(&audio);
        avcodec_free_context(&video);
        avformat_close_input(&format);
    }
    AVCodecContext* codec(int index)
    {
        auto parameters = format->streams[index]->codecpar;
        auto decoder = avcodec_find_decoder(parameters->codec_id);
        if (!decoder) throw std::runtime_error("Movie codec is unavailable");
        auto context = avcodec_alloc_context3(decoder);
        if (!context) throw std::bad_alloc();
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
        if (!path) throw std::runtime_error("Movie path is null");
        // FFmpeg's file protocol takes UTF-8, including on Windows.
        const auto url = "file:" + wiz8::path_to_utf8(wiz8::asset_path(path));
        check(avformat_open_input(&format, url.c_str(), nullptr, nullptr), "Movie container");
        check(avformat_find_stream_info(format, nullptr), "Movie streams");
        video_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        check(video_index, "Movie video stream");
        codec(video_index);
        if (video->width <= 0 || video->width > 640 || video->height <= 0 || video->height > 480)
            throw std::runtime_error("Movie exceeds the 640x480 presentation surface");
        auto rate = av_guess_frame_rate(format, format->streams[video_index], nullptr);
        if (!rate.num || !rate.den) throw std::runtime_error("Movie has no frame rate");
        frame_duration = av_q2d(av_inv_q(rate));
        if (format->start_time != AV_NOPTS_VALUE)
            origin = double(format->start_time) / AV_TIME_BASE;
        audio_index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audio_index >= 0) {
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
        if (!packet || !decoded) throw std::bad_alloc();
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
        if (voice && count) voice->append(samples.data(), count);
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
        if (!scale) throw std::runtime_error("Cannot create movie RGB555 converter");
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
        while (true) {
            int result = avcodec_receive_frame(context, decoded);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
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
        while (!eof && (queued.empty() || last_video_time < until)) {
            int result = av_read_frame(format, packet);
            if (result == AVERROR_EOF) {
                decode(video, nullptr);
                if (audio) {
                    decode(audio, nullptr);
                    audio_frame(nullptr);
                    end_time = std::max(end_time, audio_frames / 44100.0);
                }
                if (voice) voice->finish();
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
W8NativeVideo::W8NativeVideo() : state(std::make_unique<State>()) {}
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
    if (!state->started) {
        if (state->voice) state->voice->start();
        state->started = true;
    }
    if (!state->queued.empty() && state->queued.front().time <= elapsed) {
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
    const uint64_t now = MonotonicMicroseconds();
    if (!state->started) state->began = now;
    return update((now - state->began) / 1e6);
}

bool W8NativeVideo::play(const char* path, SDL_Surface& surface)
{
    target = &surface;
    WIZ8_TEST_HOOK(if (g_runtime_test_hooks.skip_movies) {
        state = std::make_unique<State>();
        return false;
    })
    try {
        open(path);
        return true;
    } catch (const std::exception& failure) {
        fprintf(stderr, "Wizardry movie: %s\n", failure.what());
        return false;
    }
}

bool W8NativeVideo::advance()
{
    try {
        const auto result = update_now();
        if (result == FrameReady) {
            show();
            WIZ8_TEST_HOOK(++g_runtime_test_hooks.movie_frames_presented;)
        }
        return result == Done;
    } catch (const std::exception& failure) {
        fprintf(stderr, "Wizardry movie: %s\n", failure.what());
        return true;
    }
}

/* Copy the current frame into the RGB555 target and present the target. */
void W8NativeVideo::show()
{
    const auto& frame = state->current;
    const auto* format = target ? SDL_GetPixelFormatDetails(target->format) : nullptr;
    if (!format || !target->pixels || target->w < frame.width || target->h < frame.height ||
        SDL_BYTESPERPIXEL(target->format) != 2 || format->Rmask != 0x7c00 ||
        format->Gmask != 0x3e0 || format->Bmask != 0x1f ||
        !SDL_ConvertPixels(frame.width, frame.height, SDL_PIXELFORMAT_XRGB1555, frame.pixels.data(),
                           frame.width * 2, target->format, target->pixels, target->pitch))
        throw std::runtime_error("Movie surface does not support RGB555 output");
    if (!g_renderer || !g_ui_canvas) return;
    const auto& description = *target;
    if (!state->presentation_surface || state->presentation_surface.get() != target) {
        ++target->refcount;
        state->presentation_surface = std::shared_ptr<SDL_Surface>(target, SDL_DestroySurface);
        state->presentation_overlay =
            std::make_unique<UiCanvasTexture>(state->presentation_surface);
        state->presentation_overlay->setAlphaTestEnabled(false);
    }
    state->presentation_overlay->updateRectangle(g_renderer.get(), 0, 0, description.w,
                                                 description.h);
    g_renderer->beginFrame();
    state->presentation_overlay->draw(g_renderer.get());
    g_renderer->endFrame();
}
