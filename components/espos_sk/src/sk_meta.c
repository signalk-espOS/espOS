/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * What one meta reconciliation leg means for the pass. Pure, so the host
 * tests can drive it.
 *
 * A leg that gets no HTTP answer at all has already spent its whole timeout,
 * and every further path would spend another: on a server that accepts the
 * stream but is too busy to answer REST -- the usual state of a signalk-server
 * still starting its plugins -- a round over every declared path holds the
 * client task, and the token machine with it, for minutes. So no answer or a
 * server error (5xx, about the server and not the path) ends the round, a
 * refused token ends it too (every following leg carries the same token), and
 * any other answer leaves that path for the retry while the round carries on.
 * A reply whose meta cannot be read is the server's meta all the same:
 * PUTting ours over it would replace what the user configured.
 */
#include "espos_sk_priv.h"

espos_sk_meta_step_t espos_sk_meta_after_get(int status, espos_sk_meta_seen_t seen)
{
    if (status <= 0 || status >= 500) {
        return ESPOS_SK_META_BACK_OFF;
    }
    if (status == 401 || status == 403) {
        return ESPOS_SK_META_UNAUTHORIZED;
    }
    if (status == 404 || (status == 200 && seen == ESPOS_SK_META_NONE)) {
        return ESPOS_SK_META_PUT;
    }
    if (status == 200) {
        return ESPOS_SK_META_DONE; /* present, or unreadable: the server's either way */
    }
    return ESPOS_SK_META_LATER;
}

espos_sk_meta_step_t espos_sk_meta_after_put(int status)
{
    if (status >= 200 && status < 300) {
        return ESPOS_SK_META_DONE;
    }
    if (status <= 0 || status >= 500) {
        return ESPOS_SK_META_BACK_OFF;
    }
    if (status == 401 || status == 403) {
        return ESPOS_SK_META_UNAUTHORIZED;
    }
    return ESPOS_SK_META_LATER;
}
