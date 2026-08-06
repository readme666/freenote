## freenote-thumbnailer executable ##

if (WIN32)
  # Windows does not support the thumbnailer like this
  return ()
endif ()

add_definitions (-DBUILD_THUMBNAILER)

if (DEV_ENABLE_GCOV)
  add_compile_options("--coverage")
  add_link_options("--coverage")
endif (DEV_ENABLE_GCOV)

add_executable (freenote-thumbnailer
        freenote-thumbnailer.cpp
        "${PROJECT_SOURCE_DIR}/src/util/GzUtil.cpp"
        "${PROJECT_SOURCE_DIR}/src/util/PlaceholderString.cpp"
        "${PROJECT_SOURCE_DIR}/src/util/StringUtils.cpp"
        "${PROJECT_SOURCE_DIR}/src/util/PathUtil.cpp"
        "${PROJECT_SOURCE_DIR}/src/util/Stacktrace.cpp"
        "${PROJECT_SOURCE_DIR}/src/util/XojPreviewExtractor.cpp"
        )

target_compile_features (freenote-thumbnailer PRIVATE cxx_std_20)
target_link_libraries (freenote-thumbnailer
  util
  ${ZLIB_LIBRARIES}
  ${librsvg_LDFLAGS}
  ${Glib_LDFLAGS}
  ${ZIP_LDFLAGS}
  )

target_include_directories (freenote-thumbnailer PRIVATE ${librsvg_INCLUDE_DIRS})

## Install ##

install (TARGETS freenote-thumbnailer
  RUNTIME DESTINATION bin
  COMPONENT freenote-thumbnailer
)

## Desktop install targets ##

configure_file (freenote-thumbnailer-xoj.schemas freenote-thumbnailer-xoj.schemas COPYONLY)

add_custom_target (desktop-install-thumbnailer
  COMMAND "${PROJECT_SOURCE_DIR}/src/xoj-preview-extractor/desktop-install.sh" install

  COMMENT "Install freenote-thumbnailer as xoj file thumbnail generator"
  SOURCES freenote-thumbnailer freenote-thumbnailer-xoj.schemas desktop-install.sh
)

add_custom_target (desktop-uninstall-thumbnailer
  COMMAND "${PROJECT_SOURCE_DIR}/src/xoj-preview-extractor/desktop-install.sh" uninstall

  COMMENT "Uninstall freenote-thumbnailer as xoj file thumbnail generator"
  SOURCES freenote-thumbnailer freenote-thumbnailer-xoj.schemas desktop-install.sh
)
