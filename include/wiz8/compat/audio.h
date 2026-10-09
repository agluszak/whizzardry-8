#pragma once
/* Native implementations of the Miles calls used by the recovered sound
   manager. Handles retain their source identities; no SDK object layout is
   exposed or serialized. */
#include "soundman.h"
using U32 = UINT32;
using S32 = INT32;
using F32 = FLOAT;
using C8 = CHAR8;
using HPROVIDER = UINT32;
using HPROENUM = UINT32;
using H3DPOBJECT = H3DSAMPLE;
using M3DRESULT = INT32;
#define HPROENUM_FIRST 0
#define M3D_NOERR 0
#define SMP_DONE 2
#define SMP_STOPPED 8
#define YES 1
#define NO 0
#define SMP_PLAYING 4
#define DIG_MIXER_CHANNELS 1
#define DIG_USE_WAVEOUT 15
extern "C"
{
    void AIL_3D_provider_attribute(HPROVIDER lib, C8 const* name, void* val);
    S32 AIL_3D_sample_playback_rate(H3DSAMPLE S);
    U32 AIL_3D_sample_status(H3DSAMPLE S);
    S32 AIL_3D_sample_volume(H3DSAMPLE S);
    H3DSAMPLE AIL_allocate_3D_sample_handle(HPROVIDER lib);
    HSAMPLE AIL_allocate_sample_handle(HDIGDRIVER dig);
    void AIL_close_3D_provider(HPROVIDER lib);
    void AIL_close_digital_driver(HDIGDRIVER dig);
    void AIL_close_stream(HSTREAM stream);
    void AIL_digital_configuration(HDIGDRIVER dig, S32* rate, S32* format, char* string);
    S32 AIL_enumerate_3D_providers(HPROENUM* next, HPROVIDER* dest, C8** name);
    void AIL_init_sample(HSAMPLE S);
    char* AIL_last_error(void);
    void* AIL_mem_alloc_lock(U32 size);
    void AIL_mem_free_lock(void* ptr);
    H3DPOBJECT AIL_open_3D_listener(HPROVIDER lib);
    M3DRESULT AIL_open_3D_provider(HPROVIDER lib);
    HDIGDRIVER AIL_open_digital_driver(U32 frequency, S32 bits, S32 channel, U32 flags);
    HSTREAM AIL_open_stream(HDIGDRIVER dig, char const* filename, S32 stream_mem);
    void AIL_release_3D_sample_handle(H3DSAMPLE S);
    void AIL_release_sample_handle(HSAMPLE S);
    void AIL_sample_ms_position(HSAMPLE S, S32* total_milliseconds, S32* current_milliseconds);
    S32 AIL_sample_playback_rate(HSAMPLE S);
    U32 AIL_sample_status(HSAMPLE S);
    S32 AIL_sample_volume(HSAMPLE S);
    S32 AIL_service_stream(HSTREAM stream, S32 fillup);
    void AIL_set_3D_orientation(H3DPOBJECT obj, F32 X_face, F32 Y_face, F32 Z_face, F32 X_up,
                                F32 Y_up, F32 Z_up);
    void AIL_set_3D_position(H3DPOBJECT obj, F32 X, F32 Y, F32 Z);
    void AIL_set_3D_provider_preference(HPROVIDER lib, C8 const* name, void const* val);
    void AIL_set_3D_sample_distances(H3DSAMPLE S, F32 max_dist, F32 min_dist);
    void AIL_set_3D_sample_loop_count(H3DSAMPLE S, U32 loops);
    void AIL_set_3D_sample_playback_rate(H3DSAMPLE S, S32 playback_rate);
    void AIL_set_3D_sample_volume(H3DSAMPLE S, S32 volume);
    void AIL_set_3D_velocity_vector(H3DPOBJECT obj, F32 dX_per_ms, F32 dY_per_ms, F32 dZ_per_ms);
    S32 AIL_set_named_sample_file(HSAMPLE S, C8 const* file_type_suffix, void const* file_image,
                                  S32 file_size, S32 block);
    S32 AIL_set_preference(U32 number, S32 value);
    void AIL_set_sample_loop_count(HSAMPLE S, S32 loop_count);
    void AIL_set_sample_pan(HSAMPLE S, S32 pan);
    void AIL_set_sample_playback_rate(HSAMPLE S, S32 playback_rate);
    void AIL_set_sample_volume(HSAMPLE S, S32 volume);
    void AIL_set_stream_loop_count(HSTREAM stream, S32 count);
    void AIL_set_stream_pan(HSTREAM stream, S32 pan);
    void AIL_set_stream_playback_rate(HSTREAM stream, S32 rate);
    void AIL_set_stream_volume(HSTREAM stream, S32 volume);
    void AIL_start_3D_sample(H3DSAMPLE S);
    void AIL_start_sample(HSAMPLE S);
    void AIL_start_stream(HSTREAM stream);
    S32 AIL_startup(void);
    void AIL_stop_3D_sample(H3DSAMPLE S);
    void AIL_stop_sample(HSAMPLE S);
    void AIL_stream_ms_position(HSTREAM S, S32* total_milliseconds, S32* current_milliseconds);
    S32 AIL_stream_playback_rate(HSTREAM stream);
    S32 AIL_stream_status(HSTREAM stream);
    S32 AIL_stream_volume(HSTREAM stream);
    S32 W8AudioSetSpatialFile(H3DSAMPLE sample, const void* data, UINT32 size);
}
