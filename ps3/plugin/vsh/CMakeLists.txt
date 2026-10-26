# Candidate source-only PS3DK build. Untested: no PS3 toolchain was installed.
cmake_minimum_required(VERSION 3.20)
project(ps3buzz_vsh C ASM)
include(ps3-self)
add_executable(ps3buzz_vsh main.c bridge.c ../core/buzz.c ../core/mailbox.c
               ../core/websocket.c ../net/server.c)
target_compile_features(ps3buzz_vsh PRIVATE c_std_11)
target_include_directories(ps3buzz_vsh PRIVATE ../core ../net)
target_compile_definitions(ps3buzz_vsh PRIVATE BUZZ_PS3=1)
target_link_libraries(ps3buzz_vsh PRIVATE io net)
ps3_add_prx(ps3buzz_vsh NAME ps3buzz_vsh VERSION 0.1
            OUTPUT_NAME ps3buzz_vsh.prx SIGN)
