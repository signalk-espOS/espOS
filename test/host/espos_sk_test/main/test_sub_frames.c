/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The subscribe and unsubscribe frames the stream task sends. signalk-server
 * 2.38 closes the connection on any unsubscribe but unsubscribe-all, which is
 * what took the stream down once at every boot when SNTP set the clock and
 * the clock fallback dropped navigation.datetime. A server that lists
 * "subscriptionIds" in its hello (signalk-server#3288) ends one subscription
 * by its id instead.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "espos_sk.h"
#include "espos_sk_priv.h"

#define MAX_FRAMES 32

static char *s_frames[MAX_FRAMES];
static int s_n;

static void clear(void)
{
    for (int i = 0; i < s_n; i++) {
        free(s_frames[i]);
    }
    s_n = 0;
}

/* Everything the stream task would send now, in order. */
static void drain(void)
{
    clear();
    char *f;
    while ((f = espos_sk_inbound_take_frame()) != NULL) {
        TEST_ASSERT_LESS_THAN(MAX_FRAMES, s_n);
        s_frames[s_n++] = f;
    }
}

static int frames_with(const char *needle)
{
    int n = 0;
    for (int i = 0; i < s_n; i++) {
        n += strstr(s_frames[i], needle) != NULL;
    }
    return n;
}

static void hello(bool ids)
{
    const char *j = ids ? "{\"name\":\"signalk-server\",\"version\":\"2.39.0\",\"self\":\"vessels.urn:x\","
                          "\"roles\":[\"master\",\"main\"],\"capabilities\":[\"subscriptionIds\"]}"
                        : "{\"name\":\"signalk-server\",\"version\":\"2.38.0\",\"self\":\"vessels.urn:x\","
                          "\"roles\":[\"master\",\"main\"]}";
    TEST_ASSERT_TRUE(espos_sk_inbound_handle_frame(j, strlen(j), NULL, 0));
}

/* A fresh connection: what a reconnect does, then the server's hello. */
static void stream_up(bool ids)
{
    espos_sk_inbound_set_connected(false);
    espos_sk_inbound_set_connected(true);
    hello(ids);
}

static void stream_down(void)
{
    espos_sk_inbound_set_connected(false);
    clear();
}

static void noop_cb(const espos_sk_update_t *u, void *arg)
{
    (void)u;
    (void)arg;
}

static const char *const UNSUB_ALL = "{\"context\":\"*\",\"unsubscribe\":[{\"path\":\"*\"}]}";

TEST_CASE("each pattern goes out in a subscribe of its own, named by the pattern", "[inbound][subframes]")
{
    stream_up(false);
    int a = espos_sk_subscribe("t1.a", 1000, noop_cb, NULL);
    int b = espos_sk_subscribe("t1.b", 500, noop_cb, NULL);
    drain();
    TEST_ASSERT_EQUAL(2, s_n);
    TEST_ASSERT_EQUAL_STRING("{\"context\":\"vessels.self\",\"subscriptionId\":\"t1.a\",\"subscribe\":[{\"path\":\"t1.a\","
                             "\"period\":1000,\"format\":\"delta\",\"policy\":\"instant\",\"minPeriod\":1000}]}",
                             s_frames[0]);
    TEST_ASSERT_NOT_NULL(strstr(s_frames[1], "\"subscriptionId\":\"t1.b\""));
    TEST_ASSERT_NOT_NULL(strstr(s_frames[1], "\"period\":500"));
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(a));
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(b));
    stream_down();
}

TEST_CASE("a server without ids gets unsubscribe-all and the rest subscribed again", "[inbound][subframes]")
{
    stream_up(false);
    int clock_sub = espos_sk_subscribe("t2.datetime", 60000, noop_cb, NULL);
    int keep = espos_sk_subscribe("t2.keep", 1000, noop_cb, NULL);
    drain();
    TEST_ASSERT_EQUAL(2, s_n);

    /* The clock fallback letting go once SNTP has the time. */
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(clock_sub));
    drain();
    TEST_ASSERT_EQUAL(2, s_n);
    TEST_ASSERT_EQUAL_STRING(UNSUB_ALL, s_frames[0]);
    TEST_ASSERT_NOT_NULL(strstr(s_frames[1], "\"subscriptionId\":\"t2.keep\""));
    TEST_ASSERT_EQUAL(0, frames_with("t2.datetime"));

    /* The last one: nothing left to subscribe again. */
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(keep));
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    TEST_ASSERT_EQUAL_STRING(UNSUB_ALL, s_frames[0]);
    stream_down();
}

TEST_CASE("a server with ids gets an unsubscribe by id and nothing else", "[inbound][subframes]")
{
    stream_up(true);
    int clock_sub = espos_sk_subscribe("t3.datetime", 60000, noop_cb, NULL);
    int keep = espos_sk_subscribe("t3.keep", 1000, noop_cb, NULL);
    drain();
    TEST_ASSERT_EQUAL(2, s_n);

    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(clock_sub));
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    TEST_ASSERT_EQUAL_STRING("{\"context\":\"*\",\"unsubscribe\":[{\"subscriptionId\":\"t3.datetime\"}]}", s_frames[0]);

    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(keep));
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    TEST_ASSERT_EQUAL_STRING("{\"context\":\"*\",\"unsubscribe\":[{\"subscriptionId\":\"t3.keep\"}]}", s_frames[0]);
    stream_down();
}

TEST_CASE("no unsubscribe ever names a path", "[inbound][subframes]")
{
    /* The frame that made signalk-server close the stream. */
    for (int ids = 0; ids < 2; ids++) {
        stream_up(ids);
        int h = espos_sk_subscribe("t4.x", 1000, noop_cb, NULL);
        drain();
        TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(h));
        drain();
        TEST_ASSERT_EQUAL(1, s_n);
        TEST_ASSERT_NULL(strstr(s_frames[0], "\"unsubscribe\":[{\"path\":\"t4.x\""));
        TEST_ASSERT_NULL(strstr(s_frames[0], "\"context\":\"vessels.self\",\"unsubscribe\""));
        stream_down();
    }
}

TEST_CASE("until the hello is read the server counts as one without ids", "[inbound][subframes]")
{
    espos_sk_inbound_set_connected(false);
    espos_sk_inbound_set_connected(true);
    int h = espos_sk_subscribe("t5.x", 1000, noop_cb, NULL);
    drain();
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(h));
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    TEST_ASSERT_EQUAL_STRING(UNSUB_ALL, s_frames[0]);
    stream_down();
}

TEST_CASE("what the server can do is learned again on every connection", "[inbound][subframes]")
{
    stream_up(true);
    int h = espos_sk_subscribe("t6.x", 1000, noop_cb, NULL);
    drain();
    /* Reconnected to a server without ids, e.g. a failover to an older one. */
    stream_up(false);
    drain();
    TEST_ASSERT_EQUAL(1, s_n); /* subscribed again on the new connection */
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(h));
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    TEST_ASSERT_EQUAL_STRING(UNSUB_ALL, s_frames[0]);
    stream_down();
}

TEST_CASE("more unsubscribes than one frame holds end everything and resubscribe the rest", "[inbound][subframes]")
{
    stream_up(true);
    int h[6];
    char path[16];
    for (int i = 0; i < 6; i++) {
        snprintf(path, sizeof(path), "t7.p%d", i);
        h[i] = espos_sk_subscribe(path, 1000, noop_cb, NULL);
    }
    drain();
    TEST_ASSERT_EQUAL(6, s_n);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(h[i]));
    }
    drain();
    TEST_ASSERT_EQUAL(2, s_n);
    TEST_ASSERT_EQUAL_STRING(UNSUB_ALL, s_frames[0]);
    TEST_ASSERT_NOT_NULL(strstr(s_frames[1], "\"subscriptionId\":\"t7.p5\""));
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(h[5]));
    stream_down();
}

TEST_CASE("a pattern taken twice is subscribed once at the shorter period", "[inbound][subframes]")
{
    stream_up(true);
    int slow = espos_sk_subscribe("t8.x", 1000, noop_cb, NULL);
    int fast = espos_sk_subscribe("t8.x", 200, noop_cb, NULL);
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    TEST_ASSERT_NOT_NULL(strstr(s_frames[0], "\"period\":200"));

    /* The server is told only when the last one goes. */
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(slow));
    drain();
    TEST_ASSERT_EQUAL(0, s_n);
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(fast));
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    TEST_ASSERT_EQUAL_STRING("{\"context\":\"*\",\"unsubscribe\":[{\"subscriptionId\":\"t8.x\"}]}", s_frames[0]);
    stream_down();
}

TEST_CASE("a pattern the server already sends is not subscribed a second time", "[inbound][subframes]")
{
    stream_up(true);
    int first = espos_sk_subscribe("t9.x", 1000, noop_cb, NULL);
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    int second = espos_sk_subscribe("t9.x", 1000, noop_cb, NULL);
    drain();
    TEST_ASSERT_EQUAL(0, s_n);

    /* The second one keeps the server's subscription when the first goes. */
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(first));
    drain();
    TEST_ASSERT_EQUAL(0, s_n);
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(second));
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    TEST_ASSERT_EQUAL_STRING("{\"context\":\"*\",\"unsubscribe\":[{\"subscriptionId\":\"t9.x\"}]}", s_frames[0]);
    stream_down();
}

TEST_CASE("a subscription not yet sent takes over the one on the server", "[inbound][subframes]")
{
    stream_up(true);
    int first = espos_sk_subscribe("t10.x", 1000, noop_cb, NULL);
    drain();
    int second = espos_sk_subscribe("t10.x", 1000, noop_cb, NULL);
    /* Gone before the stream task got to the second one. */
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(first));
    drain();
    TEST_ASSERT_EQUAL(0, s_n);
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(second));
    drain();
    TEST_ASSERT_EQUAL(1, s_n);
    TEST_ASSERT_NOT_NULL(strstr(s_frames[0], "\"subscriptionId\":\"t10.x\""));
    stream_down();
}

TEST_CASE("a pattern ended and taken again is unsubscribed before it is subscribed", "[inbound][subframes]")
{
    stream_up(true);
    int old = espos_sk_subscribe("t11.x", 1000, noop_cb, NULL);
    drain();
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(old));
    int again = espos_sk_subscribe("t11.x", 500, noop_cb, NULL);
    drain();
    /* In this order the id is free when the new subscribe arrives. */
    TEST_ASSERT_EQUAL(2, s_n);
    TEST_ASSERT_EQUAL_STRING("{\"context\":\"*\",\"unsubscribe\":[{\"subscriptionId\":\"t11.x\"}]}", s_frames[0]);
    TEST_ASSERT_NOT_NULL(strstr(s_frames[1], "\"subscriptionId\":\"t11.x\""));
    TEST_ASSERT_NOT_NULL(strstr(s_frames[1], "\"period\":500"));
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(again));
    stream_down();
}

TEST_CASE("unsubscribing while disconnected sends nothing on the next connection", "[inbound][subframes]")
{
    stream_up(true);
    int h = espos_sk_subscribe("t12.x", 1000, noop_cb, NULL);
    drain();
    espos_sk_inbound_set_connected(false);
    TEST_ASSERT_EQUAL(ESP_OK, espos_sk_unsubscribe(h));
    stream_up(true);
    drain();
    TEST_ASSERT_EQUAL(0, s_n);
    stream_down();
}

TEST_CASE("the hello's capabilities are read only from a hello", "[parse][subframes]")
{
    static const struct {
        const char *json;
        bool ids;
    } cases[] = {
        { "{\"name\":\"signalk-server\",\"version\":\"2.39.0\",\"self\":\"v\",\"capabilities\":[\"subscriptionIds\"]}", true },
        { "{\"name\":\"signalk-server\",\"version\":\"2.39.0\",\"self\":\"v\",\"capabilities\":[\"other\",\"subscriptionIds\"]}", true },
        { "{\"name\":\"signalk-server\",\"version\":\"2.38.0\",\"self\":\"v\"}", false },
        { "{\"name\":\"signalk-server\",\"version\":\"2.39.0\",\"self\":\"v\",\"capabilities\":[]}", false },
        { "{\"name\":\"signalk-server\",\"version\":\"2.39.0\",\"self\":\"v\",\"capabilities\":\"subscriptionIds\"}", false },
        { "{\"name\":\"signalk-server\",\"version\":\"2.39.0\",\"self\":\"v\",\"capabilities\":[1,null,{}]}", false },
        { "{\"name\":\"signalk-server\",\"version\":\"2.39.0\",\"self\":\"v\",\"capabilities\":{\"x\":\"subscriptionIds\"}}", false },
        { "{\"context\":\"vessels.self\",\"updates\":[],\"capabilities\":[\"subscriptionIds\"]}", false },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        espos_sk_frame_t info;
        espos_sk_frame_parse(cases[i].json, strlen(cases[i].json), &info, NULL, NULL);
        TEST_ASSERT_EQUAL_MESSAGE(cases[i].ids, espos_sk_frame_has_capability(&info, "subscriptionIds"), cases[i].json);
        espos_sk_frame_free(&info);
    }
    TEST_ASSERT_FALSE(espos_sk_frame_has_capability(NULL, "subscriptionIds"));
}
