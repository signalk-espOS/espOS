/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "espos_power_policy.h"

/* The defaults of the power config namespace. */
static const espos_power_policy_cfg_t CFG = {
    .window_ms = 300000,
    .awake_max_ms = 30000,
    .publish_ms = 1500,
};

/* A timer wake with everything done: the policy's happy path. Each test
 * breaks exactly the one thing it is about. */
static espos_power_policy_in_t ready_wake(void)
{
    espos_power_policy_in_t in = {
        .enabled = true,
        .timer_wake = true,
        .image_unconfirmed = false,
        .net_up = true,
        .net_up_for_ms = 5000,
        .have_stream = true,
        .stream_connected = true,
        .stream_up_for_ms = 5000,
        .uptime_ms = 8000,
        .holds = 0,
    };
    return in;
}

static espos_power_decision_t decide(const espos_power_policy_in_t *in, espos_power_why_t *why)
{
    return espos_power_policy_decide(&CFG, in, why);
}

TEST_CASE("a timer wake with everything done sleeps", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;
    TEST_ASSERT_EQUAL(ESPOS_POWER_SLEEP, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_DONE, why);
}

TEST_CASE("mode off never sleeps, and neither does a caller that says nothing", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    in.enabled = false;
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_OFF, why);

    in = ready_wake();
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, espos_power_policy_decide(NULL, &in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_OFF, why);
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, espos_power_policy_decide(&CFG, NULL, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_OFF, why);
    /* why is optional */
    TEST_ASSERT_EQUAL(ESPOS_POWER_SLEEP, espos_power_policy_decide(&CFG, &in, NULL));
}

TEST_CASE("an unconfirmed image never sleeps, not even past the deadline", "[power]")
{
    /* Every wake from deep sleep is a boot, and the bootloader aborts an image
     * still pending verification: sleeping here would roll the update back. */
    espos_power_policy_in_t in = ready_wake();
    in.image_unconfirmed = true;
    in.uptime_ms = 10 * CFG.awake_max_ms;
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_UNCONFIRMED, why);
}

TEST_CASE("a boot that was not a timer wake stays awake for the window", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    in.timer_wake = false;
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;

    in.uptime_ms = 0;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_WINDOW, why);

    in.uptime_ms = CFG.window_ms - 1;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_WINDOW, why);

    /* The window over and everything done: the cycle starts. */
    in.uptime_ms = CFG.window_ms;
    TEST_ASSERT_EQUAL(ESPOS_POWER_SLEEP, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_DONE, why);
}

TEST_CASE("the window does not apply to the cycle's own wake", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    in.uptime_ms = 2000;
    TEST_ASSERT_EQUAL(ESPOS_POWER_SLEEP, decide(&in, NULL));
}

TEST_CASE("a wake waits for the network, until its deadline", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    in.net_up = false;
    in.stream_connected = false;
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;

    in.uptime_ms = CFG.awake_max_ms - 1;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_NETWORK, why);

    /* A network that never comes up must not keep a battery device awake. */
    in.uptime_ms = CFG.awake_max_ms;
    TEST_ASSERT_EQUAL(ESPOS_POWER_SLEEP, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_DEADLINE, why);
}

TEST_CASE("after a non-timer boot the deadline starts when the window ends", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    in.timer_wake = false;
    in.net_up = false;
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;

    in.uptime_ms = CFG.window_ms + CFG.awake_max_ms - 1;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_NETWORK, why);

    in.uptime_ms = CFG.window_ms + CFG.awake_max_ms;
    TEST_ASSERT_EQUAL(ESPOS_POWER_SLEEP, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_DEADLINE, why);
}

TEST_CASE("with SignalK built, a wake waits for the stream", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    in.stream_connected = false;
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_STREAM, why);
}

TEST_CASE("a fresh stream gets publish_ms before the device sleeps", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;

    in.stream_up_for_ms = CFG.publish_ms - 1;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_PUBLISHING, why);

    in.stream_up_for_ms = CFG.publish_ms;
    TEST_ASSERT_EQUAL(ESPOS_POWER_SLEEP, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_DONE, why);
}

TEST_CASE("without a stream, publish_ms counts from the network coming up", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    in.have_stream = false;
    in.stream_connected = false;
    in.stream_up_for_ms = 0;
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;

    in.net_up_for_ms = CFG.publish_ms - 1;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_PUBLISHING, why);

    in.net_up_for_ms = CFG.publish_ms;
    TEST_ASSERT_EQUAL(ESPOS_POWER_SLEEP, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_DONE, why);
}

TEST_CASE("durations are ignored while their condition is false", "[power]")
{
    /* A stale "connected for" from the last connection must not count. */
    espos_power_policy_in_t in = ready_wake();
    in.stream_connected = false;
    in.stream_up_for_ms = 999999;
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_STREAM, why);

    in = ready_wake();
    in.net_up = false;
    in.net_up_for_ms = 999999;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_NETWORK, why);
}

TEST_CASE("an application hold keeps the device awake, but not past the deadline", "[power]")
{
    espos_power_policy_in_t in = ready_wake();
    in.holds = 2;
    espos_power_why_t why = ESPOS_POWER_WHY_MAX;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_HOLD, why);

    in.uptime_ms = CFG.awake_max_ms;
    TEST_ASSERT_EQUAL(ESPOS_POWER_SLEEP, decide(&in, &why));
    TEST_ASSERT_EQUAL(ESPOS_POWER_WHY_DEADLINE, why);
}

TEST_CASE("the deadline is awake_max for a timer wake and window + awake_max otherwise", "[power]")
{
    TEST_ASSERT_EQUAL_UINT32(CFG.awake_max_ms, espos_power_policy_deadline_ms(&CFG, true));
    TEST_ASSERT_EQUAL_UINT32(CFG.window_ms + CFG.awake_max_ms, espos_power_policy_deadline_ms(&CFG, false));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, espos_power_policy_deadline_ms(NULL, true));

    /* Saturates rather than wrapping into a deadline in the past. */
    espos_power_policy_cfg_t big = { .window_ms = UINT32_MAX - 10, .awake_max_ms = 100, .publish_ms = 0 };
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, espos_power_policy_deadline_ms(&big, false));
    espos_power_policy_in_t in = ready_wake();
    in.timer_wake = false;
    in.net_up = false;
    in.uptime_ms = UINT32_MAX - 1;
    TEST_ASSERT_EQUAL(ESPOS_POWER_STAY, espos_power_policy_decide(&big, &in, NULL));
}

TEST_CASE("every reason has a name", "[power]")
{
    for (int w = 0; w < ESPOS_POWER_WHY_MAX; w++) {
        const char *name = espos_power_why_str((espos_power_why_t)w);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE(strlen(name) > 1);
    }
    TEST_ASSERT_EQUAL_STRING("done", espos_power_why_str(ESPOS_POWER_WHY_DONE));
    TEST_ASSERT_EQUAL_STRING("?", espos_power_why_str(ESPOS_POWER_WHY_MAX));
    TEST_ASSERT_EQUAL_STRING("?", espos_power_why_str((espos_power_why_t)-1));
}
