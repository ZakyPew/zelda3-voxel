cmake_minimum_required(VERSION 3.20)
project(zelda3_voxel C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB ZELDA_SOURCES CONFIGURE_DEPENDS
  "src/*.c"
  "snes/*.c"
)

add_executable(zelda3
  ${ZELDA_SOURCES}
  src/platform/win32/volume_control.c
  src/platform/win32/zelda3.rc
  third_party/gl_core/gl_core_3_1.c
  third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c
)

target_include_directories(zelda3 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-2.26.3/include"
)

target_compile_definitions(zelda3 PRIVATE
  _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
  target_compile_options(zelda3 PRIVATE /W3 /wd4018 /wd4244 /wd4996)
  target_link_libraries(zelda3 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-2.26.3/lib/x64/SDL2.lib"
    ole32 oleaut32 uuid winmm version imm32 setupapi
  )
  add_custom_command(TARGET zelda3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-2.26.3/lib/x64/SDL2.dll"
      "$<TARGET_FILE_DIR:zelda3>/SDL2.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/zelda3.ini"
      "$<TARGET_FILE_DIR:zelda3>/zelda3.ini"
  )
else()
  find_package(SDL2 REQUIRED)
  target_link_libraries(zelda3 PRIVATE SDL2::SDL2 m)
endif()
