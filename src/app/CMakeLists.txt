set(S ${CMAKE_SOURCE_DIR}/src)
set(MPV_DIR ${THIRD_PARTY}/mpv)
set(FF_DIR ${THIRD_PARTY}/ffmpeg)

add_executable(shear WIN32
  main.cpp MainWindow.cpp NavKeys.cpp SettingsDialog.cpp SingleInstance.cpp VerticalFader.cpp
  ${S}/player/MpvWidget.cpp ${S}/player/AudioScrubber.cpp
  ${S}/timeline/TimelineWidget.cpp ${S}/timeline/KeyframeScanner.cpp ${S}/timeline/DamageScanner.cpp
  ${S}/export/ExportJob.cpp
  ${S}/platform/Windows.cpp
  ${S}/util/Settings.cpp ${S}/util/Metrics.cpp ${S}/util/CrashLog.cpp
  ${CMAKE_SOURCE_DIR}/resources/shear.rc ${CMAKE_SOURCE_DIR}/resources/shear.qrc) # ícone do exe (logo)

target_include_directories(shear PRIVATE ${S} ${MPV_DIR}/include ${FF_DIR}/include)
target_link_directories(shear PRIVATE ${FF_DIR}/lib)
target_link_libraries(shear PRIVATE shear_core Qt6::Widgets Qt6::Network
  ${MPV_DIR}/mpv.lib avformat avcodec avutil)
target_link_options(shear PRIVATE /ENTRY:mainCRTStartup)

# Abertura rápida: libmpv (100 MB) e as DLLs do ffmpeg só são carregadas quando usadas.
file(GLOB DELAY_DLLS RELATIVE ${FF_DIR}/bin ${FF_DIR}/bin/avformat-*.dll ${FF_DIR}/bin/avcodec-*.dll
     ${FF_DIR}/bin/avutil-*.dll)
target_link_libraries(shear PRIVATE delayimp dbghelp)
target_link_options(shear PRIVATE /DELAYLOAD:libmpv-2.dll)
foreach(d ${DELAY_DLLS})
  target_link_options(shear PRIVATE /DELAYLOAD:${d})
endforeach()

# Deploy ao lado do exe: Qt, libmpv, DLLs e ffmpeg.exe
find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
add_custom_command(TARGET shear POST_BUILD
  COMMAND ${WINDEPLOYQT} --no-translations --no-system-d3d-compiler --no-opengl-sw $<TARGET_FILE:shear>
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MPV_DIR}/libmpv-2.dll $<TARGET_FILE_DIR:shear>
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${FF_DIR}/bin $<TARGET_FILE_DIR:shear>
  COMMENT "Copiando dependências para o diretório do executável")
