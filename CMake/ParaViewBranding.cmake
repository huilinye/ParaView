###############################################################################
# This file defines the macros that ParaView-based clients can use of creating
# custom ParaView client builds with custom branding and configuration.
# 
# build_paraview_client(
#   # The name for this client. This is the name used for the executable created.
#   paraview
# 
#   # This is the title bar text. If none is provided the name will be used.
#   TITLE "Kitware ParaView"
#   
#   # This is the organization name.
#   ORGANIZATION "Kitware Inc."
# 
#   # PNG Image to be used for the Splash screen. If none is provided, default
#   # ParaView splash screen will be shown. 
#   SPLASH_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/Splash.png"
# 
#   # Provide version information for the client.
#   VERSION_MAJOR ${PARAVIEW_VERSION_MAJOR}
#   VERSION_MINOR ${PARAVIEW_VERSION_MINOR}
#   VERSION_PATCH ${PARAVIEW_VERSION_PATCH}
# 
#   # Icon to be used for the Mac bundle.
#   BUNDLE_ICON   "${CMAKE_CURRENT_SOURCE_DIR}/Icon.icns"
# 
#   # Icon to be used for the Windows application.
#   APPLICATION_ICON "${CMAKE_CURRENT_SOURCE_DIR}/Icon.ico"
#
#   # Name of the class to use for the main window. If none is specified,
#   # default QMainWindow will be used.
#   PVMAIN_WINDOW QMainWindow-subclass
#   PVMAIN_WINDOW_INCLUDE QMainWindow-subclass-header
# 
#   # Next specify the plugins that are needed to be built and loaded on startup
#   # for this client to work. These must be specified in the order that they
#   # should be loaded. The name is the name of the plugin specified in the
#   # add_paraview_plugin call.
#   # Currently, only client-based plugins are supported. i.e. no effort is made
#   # to load the plugins on the server side when a new server connection is made.
#   # That may be added in future, if deemed necessary.
#   REQUIRED_PLUGINS PointSpritePlugin
# 
#   # Next specify the plugin that are not required, but if available, should be
#   # loaded on startup. These must be specified in the order that they
#   # should be loaded. The name is the name of the plugin specified in the
#   # add_paraview_plugin call.
#   # Currently, only client-based plugins are supported. i.e. no effort is made
#   # to load the plugins on the server side when a new server connection is made.
#   # That may be added in future, if deemed necessary.
#   OPTIONAL_PLUGINS ClientGraphView ClientTreeView
#
#   # Extra targets that this executable depends on. Useful only if you are
#   # building extra libraries for your application.
#   EXTRA_DEPENDENCIES blah1 blah2
#
#   # GUI Configuration XMLs that are used to configure the client eg. readers,
#   # writers, sources menu, filters menu etc.
#   GUI_CONFIGURATION_XMLS <list of xml files>
#
#   # The Qt compressed help file (*.qch) which provides the documentation for the
#   # application. *.qch files are typically generated from *.qhp files using
#   # the qhelpgenerator executable.
#   COMPRESSED_HELP_FILE MyApp.qch
#
#   # Additional source files.
#   SOURCES <list of source files>
#
#   # If this option is present, the this macro won't create an executable,
#   # instead a shared library named {Name} with a class
#   # pq{Name}Initializer that can be used to initialize paraview in your own
#   # custom main, where {Name} is the first argument passed to this function.
#   DONT_MAKE_EXECUTABLE
#   )
# 
###############################################################################
MACRO(PV_PARSE_ARGUMENTS prefix arg_names option_names)
  SET(DEFAULT_ARGS)
  FOREACH(arg_name ${arg_names})    
    SET(${prefix}_${arg_name})
  ENDFOREACH(arg_name)
  FOREACH(option ${option_names})
    SET(${prefix}_${option} FALSE)
  ENDFOREACH(option)

  SET(current_arg_name DEFAULT_ARGS)
  SET(current_arg_list)
  FOREACH(arg ${ARGN})            
    SET(larg_names ${arg_names})    
    LIST(FIND larg_names "${arg}" is_arg_name)                   
    IF (is_arg_name GREATER -1)
      SET(${prefix}_${current_arg_name} ${current_arg_list})
      SET(current_arg_name ${arg})
      SET(current_arg_list)
    ELSE (is_arg_name GREATER -1)
      SET(loption_names ${option_names})    
      LIST(FIND loption_names "${arg}" is_option)            
      IF (is_option GREATER -1)
       SET(${prefix}_${arg} TRUE)
      ELSE (is_option GREATER -1)
       SET(current_arg_list ${current_arg_list} ${arg})
      ENDIF (is_option GREATER -1)
    ENDIF (is_arg_name GREATER -1)
  ENDFOREACH(arg)
  SET(${prefix}_${current_arg_name} ${current_arg_list})
ENDMACRO(PV_PARSE_ARGUMENTS)

FUNCTION(build_paraview_client BPC_NAME)
  PV_PARSE_ARGUMENTS(BPC 
    "TITLE;ORGANIZATION;SPLASH_IMAGE;VERSION_MAJOR;VERSION_MINOR;VERSION_PATCH;BUNDLE_ICON;APPLICATION_ICON;REQUIRED_PLUGINS;OPTIONAL_PLUGINS;PVMAIN_WINDOW;PVMAIN_WINDOW_INCLUDE;EXTRA_DEPENDENCIES;GUI_CONFIGURATION_XMLS;COMPRESSED_HELP_FILE;SOURCES"
    "DONT_MAKE_EXECUTABLE"
    ${ARGN}
    )

  # Version numbers are required. Throw an error is not set correctly.
  IF (NOT DEFINED BPC_VERSION_MAJOR OR NOT DEFINED BPC_VERSION_MINOR OR NOT DEFINED BPC_VERSION_PATCH)
    MESSAGE(ERROR 
      "VERSION_MAJOR, VERSION_MINOR and VERSION_PATCH must be specified")
  ENDIF (NOT DEFINED BPC_VERSION_MAJOR OR NOT DEFINED BPC_VERSION_MINOR OR NOT DEFINED BPC_VERSION_PATCH)

  # If no title is provided, make one up using the name.
  IF (NOT BPC_TITLE)
    SET (BPC_TITLE ${BPC_NAME})
  ENDIF (NOT BPC_TITLE)
  SET (BPC_NAME ${BPC_NAME})

  IF (NOT BPC_ORGANIZATION)
    SET (BPC_ORGANIZATION "Humanity")
  ENDIF (NOT BPC_ORGANIZATION)


  SET (branding_source_dir "${ParaView_SOURCE_DIR}/CMake")

  # If APPLICATION_ICON is specified, use that for the windows executable.
  IF (WIN32 AND BPC_APPLICATION_ICON)
    FILE (WRITE "${CMAKE_CURRENT_BINARY_DIR}/Icon.rc"
      "// Icon with lowest ID value placed first to ensure application icon\n"
      "// remains consistent on all systems.\n"
      "IDI_ICON1 ICON \"@BPC_APPLICATION_ICON@\"")
    SET(exe_icon "${CMAKE_CURRENT_BINARY_DIR}/Icon.rc")
  ENDIF (WIN32 AND BPC_APPLICATION_ICON)

  # If BPC_BUNDLE_ICON is set, setup the macosx bundle.
  IF (APPLE)
    IF (BPC_BUNDLE_ICON)
      SET(apple_bundle_sources ${BPC_BUNDLE_ICON})
      SET_SOURCE_FILES_PROPERTIES(
        ${BPC_BUNDLE_ICON}
        PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
        )
      SET(MACOSX_BUNDLE_ICON_FILE ${BPC_BUNDLE_ICON})
    ENDIF (BPC_BUNDLE_ICON)
    SET(MAKE_BUNDLE MACOSX_BUNDLE)
  ENDIF (APPLE)

  IF(WIN32)
    LINK_DIRECTORIES(${QT_LIBRARY_DIR})
  ENDIF(WIN32)

  # If splash image is not specified, use the standard ParaView splash image.
  IF (NOT BPC_SPLASH_IMAGE)
    SET (BPC_SPLASH_IMAGE "${branding_source_dir}/branded_splash.png")
  ENDIF (NOT BPC_SPLASH_IMAGE)
  CONFIGURE_FILE("${BPC_SPLASH_IMAGE}"
                  ${CMAKE_CURRENT_BINARY_DIR}/SplashImage.img COPYONLY)
  SET (BPC_SPLASH_IMAGE ${CMAKE_CURRENT_BINARY_DIR}/SplashImage.img)
  GET_FILENAME_COMPONENT(BPC_SPLASH_RESOURCE ${BPC_SPLASH_IMAGE} NAME)
  SET (BPC_SPLASH_RESOURCE ":/${BPC_NAME}/${BPC_SPLASH_RESOURCE}")

  IF (NOT BPC_PVMAIN_WINDOW)
    SET (BPC_PVMAIN_WINDOW "QMainWindow")
  ENDIF (NOT BPC_PVMAIN_WINDOW)

  IF (NOT BPC_PVMAIN_WINDOW_INCLUDE)
    SET (BPC_PVMAIN_WINDOW_INCLUDE "QMainWindow")
  ENDIF (NOT BPC_PVMAIN_WINDOW_INCLUDE)

  SET (BPC_HAS_GUI_CONFIGURATION_XMLS 0)
  IF (BPC_GUI_CONFIGURATION_XMLS)
    SET (BPC_HAS_GUI_CONFIGURATION_XMLS 1)
  ENDIF (BPC_GUI_CONFIGURATION_XMLS)

  # Generate a resource file out of the splash image.
  GENERATE_QT_RESOURCE_FROM_FILES(
    "${CMAKE_CURRENT_BINARY_DIR}/${BPC_NAME}_generated.qrc" 
    "/${BPC_NAME}" ${BPC_SPLASH_IMAGE}) 

  GENERATE_QT_RESOURCE_FROM_FILES(
    "${CMAKE_CURRENT_BINARY_DIR}/${BPC_NAME}_configuration.qrc"
    "/${BPC_NAME}/Configuration"
    "${BPC_GUI_CONFIGURATION_XMLS}")

  SET (ui_resources
    "${CMAKE_CURRENT_BINARY_DIR}/${BPC_NAME}_generated.qrc"
    "${CMAKE_CURRENT_BINARY_DIR}/${BPC_NAME}_configuration.qrc"
    )

  IF (BPC_COMPRESSED_HELP_FILE)
    # If a help collection file is specified, create a resource from it so that
    # when the ParaView help system can locate it at runtime and show the
    # appropriate help when the user asks for it. The 
    set (outfile "${CMAKE_CURRENT_BINARY_DIR}/${BPC_NAME}_help.qrc")
    GENERATE_QT_RESOURCE_FROM_FILES("${outfile}"
      "/${BPC_NAME}/Documentation"
      "${BPC_COMPRESSED_HELP_FILE};")
    SET (ui_resources ${ui_resources} "${outfile}")
  ENDIF (BPC_COMPRESSED_HELP_FILE)
  
  QT4_ADD_RESOURCES(rcs_sources
    ${ui_resources}
    )

  SOURCE_GROUP("Resources" FILES
    ${ui_resources}
    ${exe_icon}
    )

  SOURCE_GROUP("Generated" FILES
    ${rcs_sources}
    )

  IF (NOT BPC_DONT_MAKE_EXECUTABLE)
    CONFIGURE_FILE(${branding_source_dir}/branded_paraview_main.cxx.in
                   ${CMAKE_CURRENT_BINARY_DIR}/${BPC_NAME}_main.cxx @ONLY)
  ENDIF (NOT BPC_DONT_MAKE_EXECUTABLE)

  CONFIGURE_FILE(${branding_source_dir}/branded_paraview_initializer.cxx.in
                 ${CMAKE_CURRENT_BINARY_DIR}/pq${BPC_NAME}Initializer.cxx @ONLY)
  CONFIGURE_FILE(${branding_source_dir}/branded_paraview_initializer.h.in
                 ${CMAKE_CURRENT_BINARY_DIR}/pq${BPC_NAME}Initializer.h @ONLY)

  IF (NOT Q_WS_MAC)
    SET(pv_exe_name ${BPC_NAME}${PV_EXE_SUFFIX})
  ELSE (NOT Q_WS_MAC)
    SET(pv_exe_name ${BPC_NAME})
  ENDIF (NOT Q_WS_MAC)

  INCLUDE_DIRECTORIES(
    ${PARAVIEW_GUI_INCLUDE_DIRS}
    )

  IF (NOT BPC_DONT_MAKE_EXECUTABLE)
    # needed to set up shared forwarding correctly.
    SET (PV_EXE_LIST ${BPC_NAME})
    ADD_EXECUTABLE(${pv_exe_name} WIN32 ${MAKE_BUNDLE}
                   ${BPC_NAME}_main.cxx
                   pq${BPC_NAME}Initializer.cxx
                   ${rcs_sources}
                   ${exe_icon}
                   ${apple_bundle_sources}
                   ${BPC_SOURCES}
                   )
    TARGET_LINK_LIBRARIES(${pv_exe_name}
      pqApplicationComponents
      ${QT_QTMAIN_LIBRARY}
      ${BPC_EXTRA_DEPENDENCIES}
      )

    # Add shared link forwarding executables if necessary.
    IF(PV_NEED_SHARED_FORWARD)
      FOREACH(pvexe ${PV_EXE_LIST})
        SET(PV_FORWARD_EXE ${pvexe}${PV_EXE_SUFFIX})
        CONFIGURE_FILE(
          ${ParaView_SOURCE_DIR}/Servers/Executables/pv-forward.c.in
          ${CMAKE_CURRENT_BINARY_DIR}/${pvexe}-forward.c
          @ONLY IMMEDIATE)
        ADD_EXECUTABLE(${pvexe} ${CMAKE_CURRENT_BINARY_DIR}/${pvexe}-forward.c)
        ADD_DEPENDENCIES(${pvexe} ${pvexe}${PV_EXE_SUFFIX})
        # INSTALL(TARGETS ${pvexe} DESTINATION ${PV_INSTALL_BIN_DIR} COMPONENT Runtime)
      ENDFOREACH(pvexe)
    ENDIF(PV_NEED_SHARED_FORWARD)

  ELSE (NOT BPC_DONT_MAKE_EXECUTABLE)

    ADD_LIBRARY(${BPC_NAME} SHARED 
                pq${BPC_NAME}Initializer.cxx
                ${rcs_sources}
                ${BPC_SOURCES}
                )
    TARGET_LINK_LIBRARIES(${BPC_NAME}
      pqApplicationComponents
      ${QT_QTMAIN_LIBRARY}
      ${BPC_EXTRA_DEPENDENCIES}
      )
  ENDIF (NOT BPC_DONT_MAKE_EXECUTABLE)

  # TODO: Fix install rules.
  # TODO: Fix assistant location logic.
ENDFUNCTION(build_paraview_client)
