cmake_minimum_required(VERSION 3.20)
project(ps3buzz_game C ASM)
include(ps3-self)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(ps3buzz_game native.c format.c cellusbd_adapter.c import_hook.c resolve_game.c
 ../core/buzz.c ../core/usb_model.c ../core/websocket.c ../net/server.c)
target_compile_features(ps3buzz_game PRIVATE c_std_11)
target_include_directories(ps3buzz_game PRIVATE . ../core ../net)
target_compile_definitions(ps3buzz_game PRIVATE BUZZ_PS3=1)
target_compile_options(ps3buzz_game PRIVATE -O2 -Wall -Wextra -Werror
 -Wno-misleading-indentation -fno-asynchronous-unwind-tables -fno-unwind-tables)
target_link_options(ps3buzz_game PRIVATE "-Wl,-Map,${CMAKE_BINARY_DIR}/ps3buzz.map" "-Wl,--gc-sections")
target_link_libraries(ps3buzz_game PRIVATE io net_stub)
get_filename_component(BUZZ_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/../../build" ABSOLUTE)
target_link_options(ps3buzz_game PRIVATE "-specs=${PS3DK}/ppu/lib/lv2-prx.specs"
 "-nostartfiles" "-Wl,-q" "-Wl,-z,max-page-size=16"
 "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/native-prx.ld")
target_link_libraries(ps3buzz_game PRIVATE "${PS3DK}/ppu/lib/lv2-prx-crt.o")
set_property(TARGET ps3buzz_game APPEND PROPERTY LINK_DEPENDS
 "${CMAKE_CURRENT_SOURCE_DIR}/native-prx.ld" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/native_prx.py")
set_property(TARGET ps3buzz_game APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/sign_native_self.py")
add_custom_command(TARGET ps3buzz_game POST_BUILD
 COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/../tools/native_prx.py"
  "$<TARGET_FILE:ps3buzz_game>" "${BUZZ_OUTPUT}/ps3buzz.prx" --name ps3buzz
 COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/../tools/sign_native_self.py" "${BUZZ_OUTPUT}/ps3buzz.prx" "${BUZZ_OUTPUT}/ps3buzz.sprx"
 COMMAND "${PS3DK}/bin/make_sprx.exe" "${BUZZ_OUTPUT}/ps3buzz.prx" "${BUZZ_OUTPUT}/ps3buzz.system.sprx"
 COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/../tools/sign_native_self.py" "${BUZZ_OUTPUT}/ps3buzz.prx" "${BUZZ_OUTPUT}/ps3buzz.fake.sprx" --fake
 VERBATIM)
