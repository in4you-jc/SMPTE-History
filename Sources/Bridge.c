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
};

double sh_now(void) {
    return (double)AudioConvertHostTimeToNanos(AudioGetCurrentHostTime()) / 1e9;
}
static int property(AudioObjectID id, AudioObjectPropertySelector selector,
                    AudioObjectPropertyScope scope, void *out, UInt32 *size) {
    AudioObjectPropertyAddress a = {selector, scope, kAudioObjectPropertyElementMain};
    return AudioObjectGetPropertyData(id, &a, 0, NULL, size, out);
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
    if (s->queue) {
        AudioQueueStop(s->queue, true);
        AudioQueueDispose(s->queue, true);
        s->queue = NULL;
    }
}
void sh_destroy(SHCapture *s) {
    if (!s) return;
    sh_stop(s); ltc_decoder_free(s->decoder); pthread_mutex_destroy(&s->lock); free(s);
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
