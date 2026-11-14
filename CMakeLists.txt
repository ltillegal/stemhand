cmake_minimum_required(VERSION 3.24)

project(Photone VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    # Photon's own floor is macOS 14 on Apple silicon; match it.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network)
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

# The public half of the model-signing key is compiled in, so a swapped
# model directory cannot be made to look valid by shipping a matching key.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/keys/dev.pub")
    message(FATAL_ERROR "keys/dev.pub is missing. Run scripts/dev-keys.sh to create it.")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/keys/dev.pub" PHOTONE_PINNED_KEY)
string(STRIP "${PHOTONE_PINNED_KEY}" PHOTONE_PINNED_KEY)
configure_file(cmake/pinned_key.h.in "${CMAKE_CURRENT_BINARY_DIR}/pinned_key.h" @ONLY)

qt_add_executable(photone
    src/main.cpp
    src/keys.cpp
    src/manifest.cpp
    src/modelstore.cpp
    src/audiofile.cpp
    src/waveformitem.cpp
)

target_include_directories(photone PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" src)

target_compile_definitions(photone PRIVATE
    PHOTONE_VERSION="${PROJECT_VERSION}"
    PHOTONE_MODEL_ENGINE_ABI=1
)

qt_add_resources(photone "qml"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    FILES
        qml/Main.qml
        qml/ModelRow.qml
        qml/Wave.qml
)

target_link_libraries(photone PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network
    PkgConfig::SODIUM
)

if(APPLE)
    set_target_properties(photone PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Photone"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.photone.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# Developer tool: generate keys, sign and verify model manifests.
add_executable(photone-sign src/signtool.cpp src/keys.cpp src/manifest.cpp)
target_include_directories(photone-sign PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(photone-sign PRIVATE PHOTONE_MODEL_ENGINE_ABI=1)
target_link_libraries(photone-sign PRIVATE Qt6::Core PkgConfig::SODIUM)

install(TARGETS photone BUNDLE DESTINATION . RUNTIME DESTINATION bin)
