cmake_minimum_required(VERSION 3.20)
project(buzzbridge C)
include(ps3-self)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(BUZZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(ps3buzz_vsh vsh.c logic.c ../game/format.c)
target_compile_features(ps3buzz_vsh PRIVATE c_std_11)
target_compile_options(ps3buzz_vsh PRIVATE -O2 -Wall -Wextra -Werror -Wno-misleading-indentation
 -fno-asynchronous-unwind-tables -fno-unwind-tables)
target_link_options(ps3buzz_vsh PRIVATE "-specs=${PS3DK}/ppu/lib/lv2-prx.specs" -nostartfiles -Wl,-q
 -Wl,-z,max-page-size=16 "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/../game/native-prx.ld"
 "-Wl,-Map,${CMAKE_BINARY_DIR}/ps3buzz_vsh.map" -Wl,--gc-sections)
target_link_libraries(ps3buzz_vsh PRIVATE io "${PS3DK}/ppu/lib/lv2-prx-crt.o")
set_property(TARGET ps3buzz_vsh APPEND PROPERTY LINK_DEPENDS
 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/sign_native_self.py"
 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/self_ecdsa.py")
add_custom_command(TARGET ps3buzz_vsh POST_BUILD
 COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/../tools/native_prx.py"
 "$<TARGET_FILE:ps3buzz_vsh>" "${BUZZ_ROOT}/build/ps3buzz_vsh.prx" --name ps3buzz_vsh
 COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/../tools/sign_native_self.py"
 "${BUZZ_ROOT}/build/ps3buzz_vsh.prx" "${BUZZ_ROOT}/build/ps3buzz_vsh.sprx" --vsh
 VERBATIM)
option(BB_BUNDLED_NETEMU "Include bundled finished netemu support" OFF)
add_executable(BuzzBridge app.c app_ui.c app_health.c ui_layout.c logic.c files.c menu.c bb_dir.c bb.c bb_io.c
 iso_patch.c netemu_installer.c netemu_platform.c)
if(BB_BUNDLED_NETEMU)
 target_compile_definitions(BuzzBridge PRIVATE BB_BUNDLED_NETEMU=1)
endif()
target_compile_features(BuzzBridge PRIVATE c_std_11)
target_compile_options(BuzzBridge PRIVATE -O2 -Wall -Wextra -Werror -Wno-misleading-indentation)
target_link_libraries(BuzzBridge PRIVATE rsx gcm_cmd gcm_sys io sysutil net netctl sysmodule_stub rt lv2)
ps3_add_self(BuzzBridge OUTPUT_DIRECTORY "${BUZZ_ROOT}/build/app")
