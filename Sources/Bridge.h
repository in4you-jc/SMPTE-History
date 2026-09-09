#pragma once
#include <stdint.h>

typedef struct SHCapture SHCapture;
typedef struct {
    uint32_t id;
    int channels;
    int outputs;
    double sample_rate;
    char name[256];
    char uid[256];
} SHDevice;
typedef struct {
    double time;              // monotonic seconds at frame end
    double measured_fps;      // sample rate / decoded frame length
    int hours, minutes, seconds, frames;
    int drop_frame, reverse;
} SHFrame;
int sh_devices(SHDevice *out, int capacity);
double sh_now(void);
SHCapture *sh_create(double sample_rate, int channel, int channels);
int sh_start(SHCapture *, const char *uid);
// Configure before starting; one device clock for input and all outputs.
int sh_configure_outputs(SHCapture *, int count);
int sh_start_duplex(SHCapture *, uint32_t device_id);
int sh_validate_device(SHCapture *);
float sh_take_input_peak(SHCapture *);
float sh_take_output_peak(SHCapture *, int channel);
void sh_set_output_gain(SHCapture *, int channel, float gain);
void sh_mix_outputs(SHCapture *, const float *input, float *output, int frames);
void sh_stop(SHCapture *);
void sh_destroy(SHCapture *);
int sh_read(SHCapture *, SHFrame *out, int capacity);
int sh_overflows(SHCapture *);
int sh_error(SHCapture *);
double sh_last_audio(SHCapture *);
// Same decoder path for live audio and offline verification. Single producer.
void sh_feed(SHCapture *, const float *interleaved, int frames, double block_start);
