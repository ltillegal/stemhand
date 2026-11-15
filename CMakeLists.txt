cmake_minimum_required(VERSION 3.24)

project(Stemhand VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    # Photon's own floor is macOS 14 on Apple silicon; match it.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Concurrent Multimedia)
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

# The public half of the model-signing key is compiled in, so a swapped
# model directory cannot be made to look valid by shipping a matching key.
# The signing key belongs to the checkout, not to the repository: a public key
# committed by someone else would pin a trust anchor nobody can sign with.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/keys/dev.pub"
   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.dev-keys/dev-signing.key")
    message(STATUS "Generating a development model-signing key")
    execute_process(
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dev-keys.sh"
        RESULT_VARIABLE key_result
        OUTPUT_QUIET)
    if(NOT key_result EQUAL 0)
        message(FATAL_ERROR "scripts/dev-keys.sh failed; models cannot be trusted without a key")
    endif()
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/keys/dev.pub" STEMHAND_PINNED_KEY)
string(STRIP "${STEMHAND_PINNED_KEY}" STEMHAND_PINNED_KEY)
configure_file(cmake/pinned_key.h.in "${CMAKE_CURRENT_BINARY_DIR}/pinned_key.h" @ONLY)

# ONNX Runtime is optional at configure time: the shell, the model store and
# the CLI all build without it, and the denoise engine simply reports that it
# is unavailable. Point ONNXRUNTIME_ROOT at an unpacked release (the repo's
# third_party/ copy is the Linux dev default; on macOS use `brew --prefix
# onnxruntime`).
set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime-linux-x64-1.32.0"
    CACHE PATH "ONNX Runtime root")

# Turn an accidentally engine-less build into a configuration error. The app
# still builds without ONNX Runtime, but shipping it that way is never intended.
option(STEMHAND_REQUIRE_ENGINE "Fail configuration when ONNX Runtime is missing" OFF)

# The release tarball puts headers in <root>/include, Homebrew in
# <root>/include/onnxruntime. Both are searched; guessing one of them cost a
# macOS debugging round.
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS
        "${ONNXRUNTIME_ROOT}/include"
        "${ONNXRUNTIME_ROOT}/include/onnxruntime"
        "${ONNXRUNTIME_ROOT}/Headers")
find_library(ONNXRUNTIME_LIBRARY onnxruntime HINTS "${ONNXRUNTIME_ROOT}/lib")

set(STEMHAND_ENGINE_SOURCES
    src/cli.cpp
    src/engine/audioio.cpp
    src/engine/fft.cpp
    src/engine/gtcrn.cpp
    src/engine/htdemucs.cpp
    src/engine/wavwriter.cpp
    src/engine/wavreader.cpp
    src/engine/stemmixer.cpp
    src/engine/stemplayer.cpp
)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    message(STATUS "ONNX Runtime: ${ONNXRUNTIME_LIBRARY}")
elseif(STEMHAND_REQUIRE_ENGINE)
    message(FATAL_ERROR
        "ONNX Runtime not found under ${ONNXRUNTIME_ROOT}.\n"
        "  looked for onnxruntime_cxx_api.h in: include, include/onnxruntime, Headers\n"
        "  looked for libonnxruntime in: lib\n"
        "  macOS: brew install onnxruntime, then -DONNXRUNTIME_ROOT=$(brew --prefix onnxruntime)\n"
        "  Linux: ./scripts/fetch-onnxruntime.sh")
else()
    message(WARNING "ONNX Runtime not found — the denoise engine will be stubbed")
endif()

qt_add_executable(stemhand
    src/main.cpp
    src/keys.cpp
    src/manifest.cpp
    src/modelstore.cpp
    src/audiofile.cpp
    src/waveformitem.cpp
    src/denoisecontroller.cpp
    src/updatecontroller.cpp
    src/separatecontroller.cpp
    src/registry.cpp
    ${STEMHAND_ENGINE_SOURCES}
)

target_include_directories(stemhand PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" src)

target_compile_definitions(stemhand PRIVATE
    STEMHAND_VERSION="${PROJECT_VERSION}"
    STEMHAND_MODEL_ENGINE_ABI=1
)

qt_add_resources(stemhand "qml"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    FILES
        qml/Main.qml
        qml/ModelRow.qml
        qml/Wave.qml
)

target_link_libraries(stemhand PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Concurrent
    Qt6::Multimedia
    PkgConfig::SODIUM
)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    target_compile_definitions(stemhand PRIVATE STEMHAND_HAVE_ONNXRUNTIME=1)
    target_include_directories(stemhand PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
    target_link_libraries(stemhand PRIVATE "${ONNXRUNTIME_LIBRARY}")
endif()

if(APPLE)
    set_target_properties(stemhand PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Stemhand"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.stemhand.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# Developer tool: generate keys, sign and verify model manifests.
add_executable(stemhand-sign src/signtool.cpp src/keys.cpp src/manifest.cpp)
target_include_directories(stemhand-sign PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(stemhand-sign PRIVATE STEMHAND_MODEL_ENGINE_ABI=1)
target_link_libraries(stemhand-sign PRIVATE Qt6::Core PkgConfig::SODIUM)

install(TARGETS stemhand BUNDLE DESTINATION . RUNTIME DESTINATION bin)

# Tests need the same objects as the app, minus main.cpp and the QML layer.
enable_testing()
find_package(Qt6 6.5 QUIET COMPONENTS Test)
if(Qt6Test_FOUND)
    qt_add_executable(stemhand-tests
        tests/controllers_test.cpp
        src/manifest.cpp
        src/keys.cpp
        src/modelstore.cpp
        src/denoisecontroller.cpp
        src/separatecontroller.cpp
        src/updatecontroller.cpp
        src/registry.cpp
        src/engine/audioio.cpp
        src/engine/fft.cpp
        src/engine/gtcrn.cpp
        src/engine/htdemucs.cpp
        src/engine/wavwriter.cpp
        src/engine/wavreader.cpp
        src/engine/stemmixer.cpp
        src/engine/stemplayer.cpp
    )
    target_include_directories(stemhand-tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(stemhand-tests PRIVATE STEMHAND_MODEL_ENGINE_ABI=1)
    target_link_libraries(stemhand-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Network Qt6::Test Qt6::Concurrent Qt6::Multimedia
        PkgConfig::SODIUM
    )
    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        target_compile_definitions(stemhand-tests PRIVATE STEMHAND_HAVE_ONNXRUNTIME=1)
        target_include_directories(stemhand-tests PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
        target_link_libraries(stemhand-tests PRIVATE "${ONNXRUNTIME_LIBRARY}")
    endif()
    add_test(NAME controllers COMMAND stemhand-tests)
    set_tests_properties(controllers PROPERTIES
        ENVIRONMENT "STEMHAND_TEST_MODELS=${CMAKE_CURRENT_SOURCE_DIR}/build/models;STEMHAND_TEST_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/models-dev/gtcrn/mix.wav;STEMHAND_TEST_REGISTRY=${CMAKE_CURRENT_SOURCE_DIR}/build/registry/index.json;STEMHAND_TEST_STEMS=${CMAKE_CURRENT_SOURCE_DIR}/build/testmix/mix3_stems"
    )
endif()
