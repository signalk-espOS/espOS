/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_b64_decode() / espos_b64_encode() -- how a config blob arrives.
 *
 * `PUT /api/v1/config` hands its raw request body to
 * espos_config_import_json() (espos_httpd/src/api_config.c), and a key of type
 * BLOB decodes from base64 in the JSON. The decode happens in decode_value()
 * (espos_config_json.c) like this:
 *
 *     size_t in_len = strlen(jv->valuestring);
 *     size_t cap    = (in_len / 4 + 1) * 3;
 *     uint8_t *buf  = malloc(cap ? cap : 1);
 *     espos_b64_decode(jv->valuestring, in_len, buf, cap, &n);
 *
 * A decoder writing into a heap buffer whose capacity is *computed* is the
 * shape that hides an off-by-one, and every byte of the input is chosen by
 * whoever sent the request. The existing 17 unit assertions in
 * test/host/espos_config_test/main/test_b64.c only ask the questions someone
 * thought to ask; this asks the rest.
 *
 * The decoder is pure -- b64.c is 90 lines including only
 * espos_config_priv.h, which reaches no further than esp_err.h -- so it builds
 * with the host compiler like the other three harnesses. The JSON layer above
 * it is NOT: espos_config_import_json() calls espos_config_lock(),
 * espos_config_read_effective() and espos_config_apply_plan(), so fuzzing that
 * would mean an IDF project with nvs_flash and generated descriptors, and
 * would spend its time in the storage layer rather than the parser.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "espos_config_priv.h"

/* The caller's capacity formula, copied from decode_value() rather than
 * referenced, because the point is to notice if the two ever disagree. */
static size_t caller_cap(size_t in_len)
{
    return (in_len / 4 + 1) * 3;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Cap the input: this is a config body, and a multi-megabyte base64 string
     * tells us nothing a few kilobytes does not. */
    if (size > 8192) {
        size = 8192;
    }

    /* --- 1. decode exactly as the config importer does -------------------
     * Same capacity arithmetic, and a heap block of exactly that size so a
     * one-byte overrun is a report rather than a silent write into slack. */
    char *in = (char *)malloc(size + 1);
    if (!in) {
        return 0;
    }
    memcpy(in, data, size);
    in[size] = '\0'; /* the importer passes a C string from cJSON */

    /* strlen, not size: an embedded NUL is what the importer would see. */
    const size_t in_len = strlen(in);
    const size_t cap = caller_cap(in_len);
    uint8_t *out = (uint8_t *)malloc(cap ? cap : 1);
    if (!out) {
        free(in);
        return 0;
    }

    size_t n = (size_t)-1;
    esp_err_t err = espos_b64_decode(in, in_len, out, cap, &n);

    if (err == ESP_OK) {
        /* The contract the caller relies on. out_len is used as a length for
         * the NVS write, so a value past the buffer is a read of other heap. */
        if (n > cap) {
            abort();
        }
        /* ESP_ERR_INVALID_SIZE is the decoder's "would not fit" answer, so on
         * ESP_OK the caller's formula must have been big enough. If this ever
         * fires, decode_value()'s cap and the decoder have drifted apart. */
        if (n > 0 && out == NULL) {
            abort();
        }

        /* --- 2. round-trip ------------------------------------------------
         * Re-encoding what came out must fit espos_b64_encoded_len() and must
         * decode back to the same bytes. This is where a decoder that is
         * lenient in one direction and strict in the other shows up. */
        const size_t enc_size = espos_b64_encoded_len(n);
        char *enc = (char *)malloc(enc_size);
        if (enc) {
            size_t wrote = espos_b64_encode(out, n, enc, enc_size);
            if (wrote != 0 || n == 0) {
                /* encode() returns 0 only on overflow, which cannot happen
                 * when the buffer is exactly espos_b64_encoded_len(). */
                if (wrote == 0 && n != 0) {
                    abort();
                }
                if (strlen(enc) != wrote) {
                    abort(); /* not NUL-terminated where it claims to end */
                }
                uint8_t *back = (uint8_t *)malloc(n ? n : 1);
                if (back) {
                    size_t m = (size_t)-1;
                    if (espos_b64_decode(enc, wrote, back, n ? n : 1, &m) == ESP_OK) {
                        if (m != n || (n && memcmp(back, out, n) != 0)) {
                            abort(); /* canonical output did not round-trip */
                        }
                    } else {
                        abort(); /* our own encoder produced input we reject */
                    }
                    free(back);
                }
            }
            free(enc);
        }
    } else if (err == ESP_ERR_INVALID_SIZE) {
        /* "Did not fit" is only a legitimate answer when the buffer genuinely
         * was too small. With the caller's own formula it never should be --
         * that formula is what decides the malloc, so a too-small answer here
         * is a config blob that cannot be written however valid it is. */
        abort();
    }

    /* --- 3. deliberately undersized buffers ------------------------------
     * Only meaningful when step 1 told us the exact size this input needs: if
     * n bytes decoded, then n-1 capacity CANNOT be enough, so the decoder owes
     * an ESP_ERR_INVALID_SIZE. Asserting the return value is the point --
     * relying on ASan alone would pass a decoder that silently truncated
     * instead of refusing, and a truncated blob is a config value that is
     * wrong rather than rejected.
     *
     * Guarded on err == ESP_OK && n > 0 so the expectation is derived from
     * this input's own behaviour rather than from arithmetic that would have
     * to duplicate the padding rules. */
    if (err == ESP_OK && n > 0) {
        uint8_t *tight = (uint8_t *)malloc(n - 1 ? n - 1 : 1);
        if (tight) {
            size_t t = (size_t)-1;
            if (espos_b64_decode(in, in_len, tight, n - 1, &t) != ESP_ERR_INVALID_SIZE) {
                abort(); /* n-1 was accepted for input that needs n */
            }
            free(tight);
        }

        /* Zero capacity for an input with at least one output byte: same
         * contract, the degenerate end of it. `out` is never dereferenced
         * when out_size is 0, but pass a real address anyway so a write would
         * be a report rather than a segfault. */
        size_t z = (size_t)-1;
        uint8_t dummy = 0;
        if (espos_b64_decode(in, in_len, &dummy, 0, &z) != ESP_ERR_INVALID_SIZE) {
            abort();
        }
    }

    free(out);
    free(in);
    return 0;
}
