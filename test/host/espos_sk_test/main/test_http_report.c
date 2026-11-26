/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * Which 401/403 answers the HTTP helpers pass on to the token machine. A
 * plugin route that refuses our token says nothing about the token, and
 * reporting it took a panel's stream down every few minutes.
 */
#include "unity.h"
#include "espos_sk_priv.h"

TEST_CASE("only /signalk/ paths judge the token", "[sk_http]")
{
    TEST_ASSERT_TRUE(espos_sk_http_path_judges_token("/signalk/v1/api/self"));
    TEST_ASSERT_TRUE(espos_sk_http_path_judges_token("/signalk/v1/api/vessels/self/navigation/position"));
    TEST_ASSERT_TRUE(espos_sk_http_path_judges_token("/signalk/v2/api/resources/routes"));
    TEST_ASSERT_TRUE(espos_sk_http_path_judges_token("/signalk/"));

    TEST_ASSERT_FALSE(espos_sk_http_path_judges_token("/plugins/signalk-openwakeword/api/status"));
    TEST_ASSERT_FALSE(espos_sk_http_path_judges_token("/skServer/plugins"));
    TEST_ASSERT_FALSE(espos_sk_http_path_judges_token("/signalk"));      /* the discovery document's sibling */
    TEST_ASSERT_FALSE(espos_sk_http_path_judges_token("/signalkx/v1"));  /* a prefix, not a word */
    TEST_ASSERT_FALSE(espos_sk_http_path_judges_token("signalk/v1/api"));
    TEST_ASSERT_FALSE(espos_sk_http_path_judges_token(""));
    TEST_ASSERT_FALSE(espos_sk_http_path_judges_token(NULL));
}
