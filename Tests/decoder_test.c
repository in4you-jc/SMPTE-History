#include "Bridge.h"
#include "ltc.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_rate(double rate, double fps, int df, int invert) {
    LTCEncoder *enc = ltc_encoder_create(rate, fps,
        fps == 25 ? LTC_TV_625_50 : (fps < 25 ? LTC_TV_FILM_24 : LTC_TV_525_60), 0);
    assert(enc);
    SMPTETimecode tc = {0}; tc.hours = 1; tc.mins = 2; tc.secs = 3;
    ltc_encoder_set_timecode(enc, &tc);
    SHCapture *capture = sh_create(rate, 1, 2);
    SHCapture *wrong = sh_create(rate, 0, 2);
    unsigned char *buf = malloc(ltc_encoder_get_buffersize(enc));
    float *stereo = malloc(ltc_encoder_get_buffersize(enc) * 2 * sizeof(float));
    long long offset = 0;
    int received = 0, after_gap = 0;
    double previous = 0, sum = 0;
    SMPTETimecode expected_labels[200];
    double expected_ends[200];
    for (int f = 0; f < 200; f++) {
        ltc_encoder_get_timecode(enc, &expected_labels[f]);
        ltc_encoder_encode_frame(enc);
        int n = ltc_encoder_copy_buffer(enc, buf);
        for (int j = 0; j < n; j++) {
            stereo[2*j] = 0;
            stereo[2*j+1] = (f >= 70 && f < 90) ? 0 :
                ((float)buf[j] - 128) / 128 * (invert ? -0.5f : 0.5f);
        }
        // Irregular blocks exercise decoder state across callback boundaries.
        for (int p = 0; p < n; ) {
            int chunk = n - p; if (chunk > 317) chunk = 317;
            sh_feed(capture, stereo + p * 2, chunk, (double)(offset+p) / rate);
            sh_feed(wrong, stereo + p * 2, chunk, (double)(offset+p) / rate);
            p += chunk;
        }
        offset += n;
        expected_ends[f] = (double)offset / rate;
        SHFrame decoded[16];
        int count = sh_read(capture, decoded, 16);
        assert(sh_read(wrong, decoded + 8, 8) == 0);
        for (int k = 0; k < count; k++) {
            SHFrame d = decoded[k];
            assert(d.hours == 1 && d.minutes == 2);
            assert(d.drop_frame == df && !d.reverse);
            assert(d.time > previous); previous = d.time;
            // Decoder acquisition may shorten/lengthen its first frame.
            assert(fabs(d.measured_fps - fps) < (received == 0 ? 0.4 : 0.08));
            // Match decoded time to independently maintained expected label.
            int match = f;
            while (match > 0 && fabs(expected_ends[match - 1] - d.time) < fabs(expected_ends[match] - d.time)) match--;
            SMPTETimecode expected = expected_labels[match];
            assert(d.seconds == expected.secs && d.frames == expected.frame);
            if (f > 94) after_gap++;
            received++; sum += d.measured_fps;
        }
        ltc_encoder_inc_timecode(enc);
    }
    assert(received >= 175 && received <= 180);
    assert(after_gap > 95);
    assert(sh_overflows(capture) == 0);
    printf("PASS %.0f Hz / %.5f FPS / DF=%d / inverted=%d: %d frames, mean %.5f, recovered after silence\n", rate, fps, df, invert, received, sum/received);
    free(buf); free(stereo); sh_destroy(capture); sh_destroy(wrong); ltc_encoder_free(enc);
}
static void noise_test(void) {
    SHCapture *c = sh_create(48000, 0, 1);
    float buf[512]; unsigned rng = 1234;
    for (int b = 0; b < 1000; b++) {
        for (int n = 0; n < 512; n++) { rng = rng * 1664525u + 1013904223u; buf[n] = ((rng >> 8) / 16777216.0f - .5f) * .8f; }
        sh_feed(c, buf, 512, b * 512.0 / 48000);
    }
    SHFrame frames[10]; assert(sh_read(c, frames, 10) == 0);
    sh_destroy(c); puts("PASS deterministic noise is not accepted as LTC");
}
int main(void) {
    check_rate(48000, 24, 0, 0);
    check_rate(48000, 24000.0/1001, 0, 0);
    check_rate(48000, 25, 0, 0);
    check_rate(44100, 25, 0, 1);
    check_rate(48000, 30000.0/1001, 1, 0);
    check_rate(48000, 30, 0, 0);
    noise_test();
    assert(!sh_create(48000, 2, 2));
    puts("All decoder tests passed.");
}
