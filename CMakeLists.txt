cmake_minimum_required(VERSION 3.24)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(twitch-chatbot-cpp VERSION 1.0.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

option(TWITCHBOT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(twitchbot_core
    src/automation.cpp
    src/config.cpp
    src/irc.cpp
    src/rate_limiter.cpp
    src/session_protocol.cpp
)
add_library(twitchbot::core ALIAS twitchbot_core)

target_include_directories(twitchbot_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(twitchbot_core PUBLIC cxx_std_20)
target_link_libraries(twitchbot_core PUBLIC nlohmann_json::nlohmann_json)

find_package(Boost 1.74 REQUIRED COMPONENTS beast)
find_package(OpenSSL REQUIRED)

add_executable(twitch-chatbot
    src/main.cpp
    src/twitch_irc_client.cpp
)
target_compile_features(twitch-chatbot PRIVATE cxx_std_20)
target_link_libraries(twitch-chatbot
    PRIVATE
        twitchbot::core
        Boost::beast
        OpenSSL::SSL
        OpenSSL::Crypto
)
if(WIN32)
    target_compile_definitions(twitch-chatbot PRIVATE _WIN32_WINNT=0x0A00)
endif()

function(twitchbot_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(TWITCHBOT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(TWITCHBOT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

twitchbot_set_warnings(twitchbot_core)
twitchbot_set_warnings(twitch-chatbot)

if(BUILD_TESTING)
    add_executable(twitchbot-tests tests/test_main.cpp)
    target_compile_features(twitchbot-tests PRIVATE cxx_std_20)
    target_link_libraries(twitchbot-tests PRIVATE twitchbot::core)
    twitchbot_set_warnings(twitchbot-tests)
    add_test(NAME twitchbot-tests COMMAND twitchbot-tests)
endif()

install(TARGETS twitch-chatbot RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/bot.json DESTINATION ${CMAKE_INSTALL_DATADIR}/twitch-chatbot-cpp/config)
