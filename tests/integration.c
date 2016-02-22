#include <math.h>

#include "quiet.h"


int compare_chunk(const uint8_t *l, const uint8_t *r, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (l[i] != r[i]) {
            return -1;
        }
    }
    return 0;
}

int read_and_check(const uint8_t *payload, size_t payload_len,
                   size_t accum, quiet_decoder *d, uint8_t *payload_decoded,
                   size_t payload_blocklen) {
    while (accum > 0) {
        size_t want = (payload_blocklen < accum) ? payload_blocklen : accum;
        size_t read = quiet_decoder_readbuf(d, payload_decoded, want);
        if (want != read) {
            printf("failed, read less from decoder than asked for, want=%zu, read=%zu\n", want, read);
            return 1;
        }
        if (read > payload_len) {
            printf("failed, decoded more payload than encoded, read=%zu, remaining payload=%zu\n", read, payload_len);
            return 1;
        }
        if (compare_chunk(payload, payload_decoded, read)) {
            printf("failed, decoded chunk differs from encoded payload\n");
            return 1;
        }
        payload += read;
        payload_len -= read;
        accum -= read;
    }

    return 0;
}

int test_payload(const char *profiles_fname, const char *profile_name,
                 const uint8_t *payload, size_t payload_len,
                 unsigned int encode_rate, unsigned int decode_rate,
                 bool do_clamp) {
    quiet_encoder_options *encodeopt =
        quiet_encoder_profile_file(profiles_fname, profile_name);
    quiet_encoder_opt_set_sample_rate(encodeopt, encode_rate);
    quiet_encoder *e = quiet_encoder_create(encodeopt);

    quiet_decoder_options *decodeopt =
        quiet_decoder_profile_file(profiles_fname, profile_name);
    quiet_decoder_opt_set_sample_rate(decodeopt, decode_rate);
    quiet_decoder *d = quiet_decoder_create(decodeopt);

    size_t samplebuf_len = 16384;
    quiet_sample_t *samplebuf = malloc(samplebuf_len * sizeof(quiet_sample_t));
    if (do_clamp) {
        quiet_encoder_clamp_frame_len(e, samplebuf_len);
    }

    quiet_encoder_set_payload(e, payload, payload_len);

    size_t payload_blocklen = 4096;
    uint8_t *payload_decoded = malloc(payload_blocklen * sizeof(uint8_t));

    size_t written = samplebuf_len;
    while (written == samplebuf_len) {
        written = quiet_encoder_emit(e, samplebuf, samplebuf_len);
        size_t accum = quiet_decoder_recv(d, samplebuf, written);
        if (read_and_check(payload, payload_len, accum, d, payload_decoded, payload_blocklen)) {
            return 1;
        }
        payload += accum;
        payload_len -= accum;
    }

    size_t accum = quiet_decoder_flush(d);
    if (read_and_check(payload, payload_len, accum, d, payload_decoded, payload_blocklen)) {
        return 1;
    }
    payload += accum;
    payload_len -= accum;

    if (payload_len) {
        printf("failed, decoded less payload than encoded, remaining payload=%zu\n", payload_len);
        return 1;
    }

    free(payload_decoded);
    free(samplebuf);
    free(encodeopt);
    free(decodeopt);
    quiet_encoder_destroy(e);
    quiet_decoder_destroy(d);
    return 0;
}

int test_profile(unsigned int encode_rate, unsigned int decode_rate, const char *profile) {
    size_t payload_lens[] = { 1, 2, 4, 12, 320, 399, 400, 797, 798, 799, 800, 1023 };
    size_t payload_lens_len = sizeof(payload_lens)/sizeof(size_t);
    bool do_close_frame[] = { false, true };
    size_t do_close_frame_len = sizeof(do_close_frame)/sizeof(bool);
    for (size_t i = 0; i < payload_lens_len; i++) {
        size_t payload_len = payload_lens[i];
        uint8_t *payload = malloc(payload_len*sizeof(uint8_t));
        for (size_t j = 0; j < payload_len; j++) {
            payload[j] = rand() & 0xff;
        }
        for (size_t j = 0; j < do_close_frame_len; j++) {
            printf("    payload_len=%6zu, close_frame=%s... ",
                   payload_len, (do_close_frame[j]?" true":"false"));
            if (test_payload("test-profiles.json", profile, payload, payload_len,
                             encode_rate, decode_rate, do_close_frame[j])) {
                printf("FAILED\n");
                return -1;
            }
            printf("PASSED\n");
        }
        free(payload);
    }
    return 0;
}

int test_sample_rate_pair(unsigned int encode_rate, unsigned int decode_rate) {
    const char *profiles[] = { "ofdm", "modem", "robust" };
    size_t num_profiles = 3;
    for (size_t i = 0; i < num_profiles; i++) {
        const char *profile = profiles[i];
        printf("  profile=%s\n", profile);
        if (test_profile(encode_rate, decode_rate, profile)) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    unsigned int rates[][2]={ {44100, 44100}, {48000, 48000} };
    size_t rates_len = sizeof(rates)/(2 * sizeof(unsigned int));
    for (size_t i = 0; i < rates_len; i++) {
        unsigned int encode_rate = rates[i][0];
        unsigned int decode_rate = rates[i][1];
        printf("running tests on encode_rate=%u, decode_rate=%u\n", encode_rate, decode_rate);
        if (test_sample_rate_pair(encode_rate, decode_rate)) {
            return 1;
        }
    }
    return 0;
}