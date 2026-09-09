#include "Bridge.h"
#include "ltc.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SH_CAPACITY 2048
struct SHCapture {
    LTCDecoder *decoder;
    AudioQueueRef queue;
    double rate;
    int channel, channels;
    ltc_off_t offset;
    SHFrame ring[SH_CAPACITY];
    unsigned read, write;
    pthread_mutex_t lock;
    atomic_int running, overflows, error;
    double last_audio;
    AudioUnit duplex;
    float *input_scratch;
    UInt32 max_frames;
    int outputs;
    _Atomic(float) *gains;
    AudioDeviceID route_device;
};

double sh_now(void) {
    return (double)AudioConvertHostTimeToNanos(AudioGetCurrentHostTime()) / 1e9;
}
static int property(AudioObjectID id, AudioObjectPropertySelector selector,
                    AudioObjectPropertyScope scope, void *out, UInt32 *size) {
    AudioObjectPropertyAddress a = {selector, scope, kAudioObjectPropertyElementMain};
    return AudioObjectGetPropertyData(id, &a, 0, NULL, size, out);
}
static int channel_count(AudioDeviceID id, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress a = {kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain};
    UInt32 bytes = 0;
    if (AudioObjectGetPropertyDataSize(id, &a, 0, NULL, &bytes) || bytes < sizeof(AudioBufferList)) return 0;
    AudioBufferList *list = malloc(bytes);
    if (!list) return 0;
    int count = 0;
    if (!AudioObjectGetPropertyData(id, &a, 0, NULL, &bytes, list))
        for (UInt32 i = 0; i < list->mNumberBuffers; i++) count += list->mBuffers[i].mNumberChannels;
    free(list); return count;
}
int sh_devices(SHDevice *out, int capacity) {
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &size)) return 0;
    AudioDeviceID *ids = malloc(size);
    if (!ids) return 0;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, ids)) {
        free(ids); return 0;
    }
    int count = 0;
    for (unsigned i = 0; i < size / sizeof(AudioDeviceID) && count < capacity; i++) {
        a.mSelector = kAudioDevicePropertyStreamConfiguration;
        a.mScope = kAudioObjectPropertyScopeInput;
        UInt32 bytes = 0;
        if (AudioObjectGetPropertyDataSize(ids[i], &a, 0, NULL, &bytes)) continue;
        AudioBufferList *list = malloc(bytes);
        if (!list) continue;
        if (AudioObjectGetPropertyData(ids[i], &a, 0, NULL, &bytes, list)) { free(list); continue; }
        int channels = 0;
        for (UInt32 b = 0; b < list->mNumberBuffers; b++) channels += list->mBuffers[b].mNumberChannels;
        free(list);
        if (!channels) continue;
        SHDevice d = {0}; d.id = ids[i]; d.channels = channels;
        d.outputs = channel_count(ids[i], kAudioObjectPropertyScopeOutput);
        UInt32 n = sizeof(double);
        if (property(ids[i], kAudioDevicePropertyNominalSampleRate,
                     kAudioObjectPropertyScopeGlobal, &d.sample_rate, &n)) continue;
        CFStringRef name = NULL, uid = NULL;
        n = sizeof(name);
        property(ids[i], kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, &name, &n);
        n = sizeof(uid);
        property(ids[i], kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, &uid, &n);
        if (name) { CFStringGetCString(name, d.name, sizeof(d.name), kCFStringEncodingUTF8); CFRelease(name); }
        if (uid) { CFStringGetCString(uid, d.uid, sizeof(d.uid), kCFStringEncodingUTF8); CFRelease(uid); }
        if (d.uid[0] && d.sample_rate > 0) out[count++] = d;
    }
    free(ids); return count;
}
SHCapture *sh_create(double rate, int channel, int channels) {
    if (rate < 8000 || channels < 1 || channel < 0 || channel >= channels) return NULL;
    SHCapture *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->rate = rate; s->channel = channel; s->channels = channels;
    s->decoder = ltc_decoder_create((int)(rate / 25), 64);
    if (!s->decoder) { free(s); return NULL; }
    pthread_mutex_init(&s->lock, NULL);
    atomic_init(&s->running, 0); atomic_init(&s->overflows, 0); atomic_init(&s->error, 0);
    return s;
}
void sh_feed(SHCapture *s, const float *samples, int count, double block_start) {
    // Fixed scratch buffer: no heap allocations in the audio callback.
    float mono[1024];
    for (int pos = 0; pos < count; ) {
        int n = count - pos; if (n > 1024) n = 1024;
        for (int j = 0; j < n; j++) mono[j] = samples[(pos + j) * s->channels + s->channel];
        ltc_decoder_write_float(s->decoder, mono, n, s->offset);
        LTCFrameExt f;
        while (ltc_decoder_read(s->decoder, &f)) {
            SMPTETimecode tc; ltc_frame_to_time(&tc, &f.ltc, 0);
            // Reject impossible BCD values. Timecode has no checksum.
            if (tc.hours > 23 || tc.mins > 59 || tc.secs > 59 || tc.frame > 29) continue;
            double length = (double)(f.off_end - f.off_start + 1);
            if (length <= 0) continue;
            double fps = s->rate / length;
            if (fps < 15 || fps > 40) continue;
            SHFrame frame = {
                .time = block_start + ((double)(f.off_end - s->offset) + pos + 1) / s->rate,
                .measured_fps = fps,
                .hours = tc.hours, .minutes = tc.mins, .seconds = tc.secs, .frames = tc.frame,
                .drop_frame = f.ltc.dfbit, .reverse = f.reverse != 0
            };
            pthread_mutex_lock(&s->lock);
            if (s->write - s->read == SH_CAPACITY) { s->read++; atomic_fetch_add(&s->overflows, 1); }
            s->ring[s->write++ % SH_CAPACITY] = frame;
            pthread_mutex_unlock(&s->lock);
        }
        s->offset += n; pos += n;
    }
    pthread_mutex_lock(&s->lock);
    s->last_audio = block_start + count / s->rate;
    pthread_mutex_unlock(&s->lock);
}
static void callback(void *user, AudioQueueRef q, AudioQueueBufferRef buf,
                     const AudioTimeStamp *stamp, UInt32 packets,
                     const AudioStreamPacketDescription *descriptions) {
    SHCapture *s = user;
    if (!atomic_load(&s->running)) return;
    int frames = buf->mAudioDataByteSize / (sizeof(float) * s->channels);
    double start = sh_now() - frames / s->rate;
    if (stamp && (stamp->mFlags & kAudioTimeStampHostTimeValid))
        start = (double)AudioConvertHostTimeToNanos(stamp->mHostTime) / 1e9;
    sh_feed(s, buf->mAudioData, frames, start);
    if (atomic_load(&s->running)) {
        OSStatus err = AudioQueueEnqueueBuffer(q, buf, 0, NULL);
        if (err) atomic_store(&s->error, err);
    }
}
int sh_start(SHCapture *s, const char *uid) {
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = s->rate; fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBitsPerChannel = 32; fmt.mChannelsPerFrame = s->channels;
    fmt.mFramesPerPacket = 1; fmt.mBytesPerFrame = fmt.mBytesPerPacket = 4 * s->channels;
    OSStatus err = AudioQueueNewInput(&fmt, callback, s, NULL, NULL, 0, &s->queue);
    if (err) return err;
    CFStringRef device = CFStringCreateWithCString(NULL, uid, kCFStringEncodingUTF8);
    err = AudioQueueSetProperty(s->queue, kAudioQueueProperty_CurrentDevice, &device, sizeof(device));
    CFRelease(device);
    if (err) { sh_stop(s); return err; }
    for (int i = 0; i < 3; i++) {
        AudioQueueBufferRef buf;
        err = AudioQueueAllocateBuffer(s->queue, 512 * fmt.mBytesPerFrame, &buf);
        if (!err) err = AudioQueueEnqueueBuffer(s->queue, buf, 0, NULL);
        if (err) { sh_stop(s); return err; }
    }
    atomic_store(&s->running, 1);
    err = AudioQueueStart(s->queue, NULL);
    if (err) sh_stop(s);
    return err;
}
void sh_stop(SHCapture *s) {
    if (!s) return;
    atomic_store(&s->running, 0);
    if (s->duplex) {
        AudioOutputUnitStop(s->duplex);
        AudioUnitUninitialize(s->duplex);
        AudioComponentInstanceDispose(s->duplex);
        s->duplex = NULL;
    }
    free(s->input_scratch); s->input_scratch = NULL;
    if (s->queue) {
        AudioQueueStop(s->queue, true);
        AudioQueueDispose(s->queue, true);
        s->queue = NULL;
    }
}
void sh_destroy(SHCapture *s) {
    if (!s) return;
    sh_stop(s); free(s->gains); ltc_decoder_free(s->decoder); pthread_mutex_destroy(&s->lock); free(s);
}
int sh_read(SHCapture *s, SHFrame *out, int capacity) {
    int n = 0;
    pthread_mutex_lock(&s->lock);
    while (s->read < s->write && n < capacity) out[n++] = s->ring[s->read++ % SH_CAPACITY];
    pthread_mutex_unlock(&s->lock); return n;
}
int sh_overflows(SHCapture *s) { return atomic_load(&s->overflows); }
int sh_error(SHCapture *s) { return atomic_load(&s->error); }
double sh_last_audio(SHCapture *s) {
    pthread_mutex_lock(&s->lock); double t = s->last_audio; pthread_mutex_unlock(&s->lock); return t;
}

int sh_configure_outputs(SHCapture *s, int count) {
    if (!s || atomic_load(&s->running) || count < 1) return -50;
    _Atomic(float) *gains = calloc((size_t)count, sizeof(*gains));
    if (!gains) return -108;
    for (int i = 0; i < count; i++) atomic_init(&gains[i], 0.0f);
    free(s->gains); s->gains = gains; s->outputs = count;
    return 0;
}
void sh_set_output_gain(SHCapture *s, int channel, float gain) {
    if (!s || channel < 0 || channel >= s->outputs) return;
    if (!isfinite(gain)) gain = 0;
    atomic_store_explicit(&s->gains[channel], fminf(1, fmaxf(0, gain)), memory_order_relaxed);
}
void sh_mix_outputs(SHCapture *s, const float *input, float *output, int frames) {
    // Physical channel order, not a stereo/downmix layout. A muted output is
    // exactly zero. No allocations, timecode synthesis or UI dependency here.
    for (int ch = 0; ch < s->outputs; ch++) {
        const float gain = atomic_load_explicit(&s->gains[ch], memory_order_relaxed);
        for (int i = 0; i < frames; i++) {
            float value = input[i * s->channels + s->channel];
            if (!isfinite(value)) value = 0;
            output[i * s->outputs + ch] = fmaxf(-1, fminf(1, value)) * gain;
        }
    }
}
static OSStatus duplex_render(void *user, AudioUnitRenderActionFlags *flags,
    const AudioTimeStamp *stamp, UInt32 bus, UInt32 frames, AudioBufferList *out) {
    SHCapture *s = user;
    for (UInt32 b = 0; b < out->mNumberBuffers; b++)
        if (out->mBuffers[b].mData) memset(out->mBuffers[b].mData, 0, out->mBuffers[b].mDataByteSize);
    if (!atomic_load(&s->running) || atomic_load(&s->error)) return noErr;
    if (frames > s->max_frames || out->mNumberBuffers != 1 ||
        out->mBuffers[0].mNumberChannels != (UInt32)s->outputs ||
        !out->mBuffers[0].mData || out->mBuffers[0].mDataByteSize < frames * sizeof(float) * s->outputs) {
        atomic_store(&s->error, kAudioUnitErr_FormatNotSupported); return noErr;
    }
    AudioBufferList input = {.mNumberBuffers = 1};
    input.mBuffers[0] = (AudioBuffer){.mNumberChannels = s->channels,
        .mDataByteSize = frames * sizeof(float) * s->channels, .mData = s->input_scratch};
    AudioUnitRenderActionFlags input_flags = 0;
    OSStatus err = AudioUnitRender(s->duplex, &input_flags, stamp, 1, frames, &input);
    if (err) { atomic_store(&s->error, err); return noErr; }
    if (input_flags & kAudioUnitRenderAction_OutputIsSilence)
        memset(s->input_scratch, 0, frames * sizeof(float) * s->channels);
    sh_mix_outputs(s, s->input_scratch, out->mBuffers[0].mData, frames);
    *flags &= ~kAudioUnitRenderAction_OutputIsSilence;
    double start = sh_now() - frames / s->rate;
    if (stamp->mFlags & kAudioTimeStampHostTimeValid)
        start = (double)AudioConvertHostTimeToNanos(stamp->mHostTime) / 1e9;
    sh_feed(s, s->input_scratch, frames, start);
    return noErr;
}
int sh_start_duplex(SHCapture *s, uint32_t device_id) {
    if (!s || !s->outputs || s->duplex || s->queue) return -50;
    // Recheck topology before opening. Never silently fall back to speakers.
    if (channel_count(device_id, kAudioObjectPropertyScopeInput) != s->channels ||
        channel_count(device_id, kAudioObjectPropertyScopeOutput) != s->outputs) return -50;
    s->route_device = device_id;
    AudioComponentDescription desc = {.componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput, .componentManufacturer = kAudioUnitManufacturer_Apple};
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (!component) return kAudioUnitErr_FailedInitialization;
    OSStatus err = AudioComponentInstanceNew(component, &s->duplex);
    if (err) return err;
    UInt32 enabled = 1;
#define SET(prop, scope, bus, ptr, size) do { \
    err = AudioUnitSetProperty(s->duplex, prop, scope, bus, ptr, size); \
    if (err) goto failed; \
} while (0)
    SET(kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enabled, sizeof(enabled));
    SET(kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enabled, sizeof(enabled));
    SET(kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device_id, sizeof(device_id));
    AudioStreamBasicDescription fmt = {.mSampleRate = s->rate, .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBitsPerChannel = 32, .mFramesPerPacket = 1, .mChannelsPerFrame = s->channels,
        .mBytesPerFrame = 4 * s->channels, .mBytesPerPacket = 4 * s->channels};
    SET(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &fmt, sizeof(fmt));
    fmt.mChannelsPerFrame = s->outputs; fmt.mBytesPerFrame = fmt.mBytesPerPacket = 4 * s->outputs;
    SET(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
    // Explicit identity output map: do not let CoreAudio apply speaker mixing.
    SInt32 *map = malloc(sizeof(SInt32) * s->outputs);
    if (!map) { err = -108; goto failed; }
    for (int i = 0; i < s->outputs; i++) map[i] = i;
    err = AudioUnitSetProperty(s->duplex, kAudioOutputUnitProperty_ChannelMap,
        kAudioUnitScope_Input, 0, map, sizeof(SInt32) * s->outputs);
    free(map); if (err) goto failed;
    s->max_frames = 4096;
    SET(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &s->max_frames, sizeof(s->max_frames));
    UInt32 size = sizeof(s->max_frames);
    err = AudioUnitGetProperty(s->duplex, kAudioUnitProperty_MaximumFramesPerSlice,
        kAudioUnitScope_Global, 0, &s->max_frames, &size);
    if (err) goto failed;
    s->input_scratch = calloc((size_t)s->max_frames * s->channels, sizeof(float));
    if (!s->input_scratch) { err = -108; goto failed; }
    AURenderCallbackStruct cb = {.inputProc = duplex_render, .inputProcRefCon = s};
    SET(kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    err = AudioUnitInitialize(s->duplex); if (err) goto failed;
    atomic_store(&s->running, 1);
    err = AudioOutputUnitStart(s->duplex); if (err) goto failed;
    return noErr;
failed:
    sh_stop(s); return err;
#undef SET
}
int sh_validate_device(SHCapture *s) {
    if (!s || !s->duplex) return 0;
    UInt32 alive = 0, size = sizeof(alive);
    double rate = 0;
    int bad = property(s->route_device, kAudioDevicePropertyDeviceIsAlive,
        kAudioObjectPropertyScopeGlobal, &alive, &size) || !alive;
    size = sizeof(rate);
    bad |= property(s->route_device, kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal, &rate, &size) || fabs(rate - s->rate) > 0.01;
    bad |= channel_count(s->route_device, kAudioObjectPropertyScopeInput) != s->channels;
    bad |= channel_count(s->route_device, kAudioObjectPropertyScopeOutput) != s->outputs;
    if (bad) atomic_store(&s->error, kAudioHardwareBadDeviceError);
    return bad;
}
