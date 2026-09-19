# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Work out what version this build is, and put it where IDF will use it.
#
# version.txt alone answers "which release is this meant to be", which is not
# the question anyone asks of a device in the field. That question is "which
# build is on it", and between two releases every build answers `0.6.0` — the
# firmware on the bench and the firmware from three weeks ago are
# indistinguishable in `GET /api/v1/system/info`, in the OTA status and in the
# log banner.
#
# So: `git describe` when the checkout has tags, version.txt when it has not
# (a tarball, or a repo whose release has not been tagged yet). That is also
# the shape docs/rest-api.md has always documented — "0.1.0-3-gabc1234".
#
# Include before project(); IDF honours a PROJECT_VER set beforehand.

include_guard(GLOBAL)

# ESP-IDF version policy, enforced by _espos_check_idf_version() in
# espos_project.cmake. .idf-version is the exact release CI builds and the
# docs say to install; it is the only one that is known to work. Any other
# release in [ESPOS_IDF_MIN, ESPOS_IDF_MAX_EXCL) is the same major.minor the
# components are written against, so it builds -- with a single warning that
# names the tested release, because a problem reported from such a build has
# to say so. Outside the range is a hard stop: another minor moves component
# APIs (esp_hosted, the TWAI driver, the linux target) under the code, and
# -DESPOS_ALLOW_IDF_MISMATCH=1 is the deliberate way past it, not a habit.
# Bump ESPOS_IDF_MAX_EXCL together with the pin, never ahead of it.
set(ESPOS_IDF_MIN "6.0.0")
set(ESPOS_IDF_MAX_EXCL "6.1.0")

macro(espos_project_version)
    # The project being built, which for a firmware is its own repository and
    # not the espOS submodule inside it -- a consumer's version is its own.
    # CMAKE_SOURCE_DIR rather than CMAKE_CURRENT_LIST_DIR because a macro body
    # expands in the caller's context, so the latter would name whichever file
    # happened to invoke it.
    set(_espos_ver_file "${CMAKE_SOURCE_DIR}/version.txt")
    set(_espos_ver_base "")
    if(EXISTS "${_espos_ver_file}")
        file(READ "${_espos_ver_file}" _espos_ver_base)
        string(STRIP "${_espos_ver_base}" _espos_ver_base)
    endif()

    set(PROJECT_VER "${_espos_ver_base}")

    find_package(Git QUIET)
    if(GIT_FOUND)
        # --always so a checkout with no tags still yields the commit; --dirty
        # so a build with uncommitted changes says so on the device rather
        # than claiming to be the release it was branched from.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty --always
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _espos_ver_git
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _espos_ver_rc)
        if(_espos_ver_rc EQUAL 0 AND _espos_ver_git)
            string(REGEX REPLACE "^v" "" _espos_ver_git "${_espos_ver_git}")
            # --always makes describe fall back to a bare commit hash when the
            # repository has no tags at all, and a hash is not a version: the
            # OTA manifest compares these numerically to decide whether an
            # update is newer. Take describe's answer only when it actually
            # starts with a release number.
            if(_espos_ver_git MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)")
                set(PROJECT_VER "${_espos_ver_git}")

                # A tag NEWER than version.txt means one of them was forgotten,
                # and the device would report whichever this file happened to
                # prefer. Say so at build time instead.
                #
                # Only that direction. version.txt AHEAD of the nearest tag is
                # the ordinary state of a release commit -- release-please
                # bumps the file in the PR and the tag appears when it merges
                # -- so warning in both directions fired on the release PR
                # itself and blamed release-please for not doing what it had
                # just done. Worse, describe's older number won: a build of
                # the PR that bumps to 0.8.1 reported 0.8.0. When the file is
                # ahead it is the deliberate answer, so take it and say which
                # commit it was built from.
                if(_espos_ver_base AND CMAKE_MATCH_1 VERSION_GREATER _espos_ver_base)
                    message(WARNING
                        "espOS: version.txt says ${_espos_ver_base} but the nearest tag is "
                        "v${CMAKE_MATCH_1}, which is newer. Bump version.txt (release-please's "
                        "release PR does it; docs/releasing.md).")
                elseif(_espos_ver_base VERSION_GREATER CMAKE_MATCH_1)
                    # Save the tag first: the REGEX REPLACE below resets
                    # CMAKE_MATCH_1, so using it in the message afterwards
                    # printed a bare "(v)".
                    set(_espos_ver_tag "${CMAKE_MATCH_1}")
                    # Keep the git suffix so builds within the window stay
                    # distinguishable, but on the version being released.
                    string(REGEX REPLACE "^[0-9]+\\.[0-9]+\\.[0-9]+" "${_espos_ver_base}"
                           PROJECT_VER "${_espos_ver_git}")
                    message(STATUS
                        "espOS: version.txt (${_espos_ver_base}) is ahead of the nearest tag "
                        "(v${_espos_ver_tag}) — a release in progress; reporting ${PROJECT_VER}.")
                endif()
            else()
                message(STATUS
                    "espOS: no release tag reachable from HEAD (${_espos_ver_git}) — reporting "
                    "${PROJECT_VER} from version.txt. Tag releases (docs/releasing.md) and "
                    "builds between them become distinguishable.")
            endif()
        endif()
    endif()

    if(NOT PROJECT_VER)
        set(PROJECT_VER "0.0.0-unknown")
        message(WARNING "espOS: no version.txt and no git — building as ${PROJECT_VER}")
    endif()
    message(STATUS "espOS: version ${PROJECT_VER}")
endmacro()

#
# _espos_report_base_version(<label> <dir>)
#
# Say which espOS this firmware is building against, and refuse to guess.
#
# A submodule records one commit and nothing else. Which RELEASE that commit is
# can only come from `git describe`, and inside a submodule describe is
# routinely wrong -- not unavailable, wrong, which is worse. The everyday
# `git submodule update` fetches the pinned commit but no tags, so describe can
# only ever name a tag that existed when the submodule was first initialised.
# Measured on both consumers: each holds v0.6.0 and v0.7.0 only, so the
# cockpit's pin reports v0.7.0-66-g814b72b while the same commit in a full
# clone is v0.7.1-13-g814b72b. A fresh `--init` gets it right; every
# incremental update afterwards drifts further, silently, for as long as the
# pin stands.
#
# So the version is taken from version.txt -- a tracked file, which a
# submodule checkout always has -- and describe is used only to CONTRADICT it.
# That inverts the trust: the file is the answer, the tag is the cross-check,
# and a missing tag can no longer produce a confident wrong number.
#
function(_espos_report_base_version label dir)
    # First line only, and it has to look like a version. STRIP alone trims
    # the ends and would carry a stray second line straight into the
    # comparison below, where it reads as a different version and prints as
    # two lines of build output.
    set(base "")
    if(EXISTS "${dir}/version.txt")
        file(STRINGS "${dir}/version.txt" _lines LIMIT_COUNT 1)
        if(_lines)
            list(GET _lines 0 base)
            string(STRIP "${base}" base)
        endif()
    endif()
    if(NOT base MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+")
        message(FATAL_ERROR
            "${label}: ${dir}/version.txt does not begin with a version number (read '${base}'), "
            "so there is no way to say which espOS this firmware builds against. If espOS is a "
            "submodule, run `git submodule update --init`.")
    endif()

    # Same checkout: the prologue already reported it as the project's own
    # version, and there is no pin to be honest about.
    if("${dir}" STREQUAL "${CMAKE_SOURCE_DIR}")
        return()
    endif()

    set(described "")
    set(tag "")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty --always
            WORKING_DIRECTORY "${dir}"
            OUTPUT_VARIABLE described
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE rc)
        if(NOT rc EQUAL 0)
            set(described "")
        endif()
    endif()
    if(described MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(tag "${CMAKE_MATCH_1}")
    endif()

    # Which espOS, not just which version of it. A tag says nothing about the
    # repository it came from: `v0.8.0` in a fork and `v0.8.0` upstream print
    # identically while the code behind them can differ completely, and pointing
    # a submodule at a fork is a normal thing to do mid-change. So name the
    # remote whenever it is not the canonical one, and stay quiet when it is --
    # a line that appears on every build is a line nobody reads.
    #
    # The expected URL is read from espos_core's manifest rather than written
    # here: that file is the same tracked copy that gets published, so a fork
    # that legitimately becomes upstream carries its own answer instead of
    # tripping a check on someone else's constant.
    set(_origin_note "")
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" remote get-url origin
            WORKING_DIRECTORY "${dir}"
            OUTPUT_VARIABLE _origin
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _origin_rc)
        if(NOT _origin_rc EQUAL 0)
            set(_origin "")
        endif()
        set(_manifest "${dir}/components/espos_core/idf_component.yml")
        set(_expect "")
        if(EXISTS "${_manifest}")
            file(STRINGS "${_manifest}" _repo_lines REGEX "^repository:")
            if(_repo_lines)
                list(GET _repo_lines 0 _expect)
                string(REGEX REPLACE "^repository:[ \t]*\"?([^\"]*)\"?[ \t]*$" "\\1" _expect "${_expect}")
            endif()
        endif()
        if(_origin AND _expect)
            # Compare on host + path, so https, scp-style ssh, ssh:// and a
            # trailing .git for the SAME repository all read as one place --
            # while a different host does not. Keeping the host matters: a
            # path alone makes gitlab.com/signalk-espOS/espOS indistinguishable
            # from github.com/signalk-espOS/espOS, which is a different
            # repository that merely borrowed the name. (Caught by testing;
            # an earlier version of this stripped the host and said upstream.)
            foreach(_v _origin _expect)
                set(_n "${${_v}}")
                string(REGEX REPLACE "\\.git$" "" _n "${_n}")
                string(REGEX REPLACE "/$" "" _n "${_n}")
                # scp-style `git@host:owner/repo` -> `host/owner/repo`
                string(REGEX REPLACE "^[^@/]+@([^:]+):" "\\1/" _n "${_n}")
                # `scheme://[user@]host/path` -> `host/path`
                string(REGEX REPLACE "^[a-z+]+://([^@/]+@)?" "" _n "${_n}")
                string(TOLOWER "${_n}" ${_v}_norm)
            endforeach()
            if(NOT _origin_norm STREQUAL _expect_norm)
                set(_origin_note " from ${_origin}")
            endif()
        elseif(NOT _origin)
            # No origin at all: a local clone with the remote removed, or a
            # plain directory. Worth saying, because nothing can be fetched
            # into it and its tags will never move.
            set(_origin_note " (no git remote)")
        endif()
    endif()

    if(NOT tag)
        # No usable tag is the ordinary state of a submodule, not a fault.
        message(STATUS "espos: base ${base}${_origin_note} (pin ${described}, no release tag fetched)")
    elseif(tag VERSION_EQUAL base)
        message(STATUS "espos: base ${base}${_origin_note} (pin ${described})")
    elseif(tag VERSION_LESS base)
        # The common shape: the pin is past a tag the checkout HAS, on the way
        # to a release it has not fetched. version.txt is ahead because
        # release-please bumps it in the release commit. Nothing is wrong, but
        # the describe string is misleading, so do not print it without saying
        # so.
        message(STATUS
            "espos: base ${base}${_origin_note} from version.txt; `git describe` here says ${described}, which "
            "names an older tag because this checkout has not fetched the newer ones "
            "(`git -C ${dir} fetch --tags`). Trust version.txt.")
    else()
        # The tag is NEWER than version.txt. That cannot happen from missing
        # tags -- a tag reachable from HEAD is a tag this checkout has -- so
        # one of the two was genuinely forgotten.
        message(WARNING
            "${label}: the espOS pin${_origin_note} is at tag v${tag} but its version.txt says ${base}. One of "
            "them was not bumped; a device would report whichever this build preferred. "
            "Check the pinned commit in ${dir} (docs/releasing.md).")
    endif()
endfunction()
