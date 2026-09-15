add_executable(MurphyWhiteFlashTest MurphyWhiteFlashTest.cpp)
target_include_directories(MurphyWhiteFlashTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src
)
target_link_libraries(MurphyWhiteFlashTest PRIVATE crosspoint_test_common)
add_test(NAME MurphyWhiteFlashTest COMMAND MurphyWhiteFlashTest)

add_executable(MurphyGrayExitTest MurphyGrayExitTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp
)
target_include_directories(MurphyGrayExitTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src
  ${REPO_ROOT}/freeink-sdk/libs/hardware/BoardConfig/include
)
target_compile_options(MurphyGrayExitTest PRIVATE -UNDEBUG)
target_link_libraries(MurphyGrayExitTest PRIVATE crosspoint_test_common)
add_test(NAME MurphyGrayExitTest COMMAND MurphyGrayExitTest)
