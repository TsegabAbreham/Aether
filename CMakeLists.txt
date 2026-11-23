
cmake_minimum_required(VERSION 3.22)

project(Aether VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)
add_subdirectory(external/glm)

juce_add_gui_app(Aether
    PRODUCT_NAME "Aether"
    COMPANY_NAME "Aether Audio"
    VERSION "0.1.0"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/Aether_Icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/Aether_Icon.png"
)

# Copy runtime image assets beside the executable
add_custom_command(TARGET Aether POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:Aether>/assets/icon"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/Aether_Icon.png"
        "$<TARGET_FILE_DIR:Aether>/assets/icon/Aether_Icon.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/Aether_Splashscreen.png"
        "$<TARGET_FILE_DIR:Aether>/assets/icon/Aether_Splashscreen.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/hrtf/d1.hrir"
        "$<TARGET_FILE_DIR:Aether>/d1.hrir"
)

juce_generate_juce_header(Aether)

target_sources(Aether PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/MainComponent.h

    src/components/knob.cpp
    src/components/knob.h
    src/components/MenuBar.h

    src/components/Helper_functions/obj_loader.cpp

    src/components/Workspace/Workspace.cpp
    src/components/Workspace/Panels/Viewport.cpp
    src/components/Workspace/Panels/Timeline.cpp
    src/components/Workspace/Panels/Inspector.cpp
    src/components/Workspace/Panels/FileExplorer.cpp
    src/components/Workspace/Panels/Hierarchy.cpp
    src/components/Workspace/Panels/Panel.cpp

    src/Audio/Core/AudioEngine.cpp
    src/Audio/Object/AudioObject.h
    src/Audio/Spatialization/SpatialGains.h
    src/Audio/Spatialization/SpatialMath.h
    src/Audio/Spatialization/DelayLine.h
    src/Audio/Spatialization/BinauralPanner.h
    src/Audio/Spatialization/HrirDatabase.h
    src/Audio/Spatialization/HrirPanner.h

    src/SplashScreen/SplashScreen.cpp
    src/SplashScreen/SplashScreenWindow.cpp
    src/SplashScreen/SplashScreenWindow.h

    src/Windows/Window.cpp
    src/Windows/Window.h
    src/Windows/WindowManager.cpp
    src/Windows/Settings/Setting.cpp
    src/Windows/Settings/SettingsManager.cpp

)

target_compile_definitions(Aether PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(Aether PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_opengl
    glm::glm
)

if (MSVC)
    target_compile_options(Aether PRIVATE
        $<$<CONFIG:Release>:/O2 /arch:AVX2 /fp:fast>)
endif()