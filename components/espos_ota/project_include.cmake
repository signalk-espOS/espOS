# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Signing-key safety for a firmware that does NOT include the prologue -- one
# that installs espOS from the component registry, where
# cmake/espos_project.cmake cannot reach (it must run before project(), and
# this file runs after).
#
# espos_project_prologue() does this itself, so a submodule build gets it
# either way; the guard below keeps the two from both acting.
#
# What is at stake: espOS builds with CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT,
# so a device accepts an OTA only when it is signed with the key whose public
# half it was flashed with. Every way of getting that wrong produces the same
# symptom -- an image that flashes fine over USB and then refuses every
# update, on the device, long after the build went green.

if(NOT COMMAND espos_signing_key_watch)

#
# espos_signing_key_watch([KEY <path>])
#
# Re-link when the signing KEY changes, not only when the code does.
#
# ESP-IDF's signing step depends on the unsigned binary alone
# (esptool_py/project_include.cmake: DEPENDS "${build_dir}/.bin_timestamp"), so
# swapping the key and rebuilding silently keeps the signature made with the
# PREVIOUS key: no source changed, nothing re-links, and the build log looks
# entirely normal.
#
# Call it after project() from the consumer's root CMakeLists.txt. KEY
# defaults to what IDF itself signs with -- CONFIG_SECURE_BOOT_SIGNING_KEY,
# resolved against the project directory, which is the value that actually
# decides the signature.
#
function(espos_signing_key_watch)
    cmake_parse_arguments(_SK "" "KEY" "" ${ARGN})

    if(NOT CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES)
        return()  # unsigned build; nothing to watch
    endif()

    set(key "${_SK_KEY}")
    if(NOT key)
        set(key "${CONFIG_SECURE_BOOT_SIGNING_KEY}")
    endif()
    if(NOT key)
        message(WARNING
            "espos_ota: signed binaries are enabled but CONFIG_SECURE_BOOT_SIGNING_KEY is empty, "
            "so there is no key to watch. Set it, or pass KEY.")
        return()
    endif()
    get_filename_component(key "${key}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")

    if(NOT EXISTS "${key}")
        # Deliberately NOT generated here. The prologue generates a
        # development key because it runs in espOS's own tree, where a
        # throwaway key is the right default; a registry consumer is someone
        # else's project, and inventing a key they did not ask for is how a
        # device ends up trusting one nobody kept. IDF's own build step says
        # how to make one, so let it.
        message(STATUS
            "espos_ota: ${key} does not exist yet. Create it with "
            "`idf.py secure-generate-signing-key ${key}` and keep it safe: a device accepts "
            "an OTA only from the key it was flashed with, so losing it means no device can "
            "ever be updated again (docs/ota.md).")
        return()
    endif()

    # Re-run configure when the key file itself changes. Without this the
    # check below is dead weight on the path that matters: `idf.py build`
    # after swapping a key changes no CMake input, so ninja skips configure,
    # this file never runs, and the image keeps the old signature -- exactly
    # the failure the fingerprint exists to catch.
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}"
                 APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${key}")

    # Fingerprint the file's hash -- never its contents, which would put a
    # private key in the build directory.
    file(SHA256 "${key}" hash)
    set(stamp "${CMAKE_BINARY_DIR}/espos_signing_key.stamp")
    set(old "")
    if(EXISTS "${stamp}")
        file(READ "${stamp}" old)
    endif()
    if(NOT old STREQUAL hash)
        if(NOT old STREQUAL "")
            message(STATUS
                "espos_ota: signing key changed — forcing a re-link so the image is signed "
                "with the current key.")
            # Removing the unsigned binary's stamp is what re-triggers IDF's
            # sign-data step; deleting only the .bin leaves it satisfied.
            file(REMOVE "${CMAKE_BINARY_DIR}/.bin_timestamp"
                        "${CMAKE_BINARY_DIR}/.signed_bin_timestamp")
        endif()
        file(WRITE "${stamp}" "${hash}")
    endif()
endfunction()

# Run it here rather than asking the consumer to call it. Everything it needs
# -- the key from the generated sdkconfig, the build directory -- exists by
# now, so there is nothing for a project to pass and no reason for anyone to
# have to know this exists. espos_project_ui_partition() is different: it has
# to be called after project() because littlefs_create_partition_image() is.
#
# ESPOS_DIR is set by cmake/espos_project.cmake and by nothing else, so its
# absence means the prologue is not in this build and the work is ours. The
# prologue has already done it (and additionally cross-checks a stale
# sdkconfig, which it can because it sets the value from an argument).
if(NOT DEFINED ESPOS_DIR)
    espos_signing_key_watch()
endif()

endif()

# Unlike the prologue's version, this needs no sdkconfig cross-check: it reads
# CONFIG_SECURE_BOOT_SIGNING_KEY from the already-generated config, so it is
# watching the key the build will actually sign with by construction. The
# prologue checks because it sets the value from an argument and a stale
# sdkconfig can disagree with it.
