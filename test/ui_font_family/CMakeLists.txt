add_executable(UiFontFamilyTest UiFontFamilyTest.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontManager.cpp)
target_include_directories(UiFontFamilyTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_include_directories(UiFontFamilyTest PRIVATE ${REPO_ROOT}/lib/EpdFont)
target_compile_options(UiFontFamilyTest PRIVATE -UNDEBUG)
target_link_libraries(UiFontFamilyTest PRIVATE crosspoint_test_common)
add_test(NAME UiFontFamilyTest COMMAND UiFontFamilyTest)
