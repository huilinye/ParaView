#------------------------------------------------------------------------------
# Add extra library containing custom code for the client.
QT4_WRAP_CPP(MOC_BUILT_SOURCES
  ParaViewMainWindow.h)

QT4_WRAP_UI(UI_BUILT_SOURCES
  ParaViewMainWindow.ui
)

SOURCE_GROUP("Generated" FILES
  ${MOC_BUILT_SOURCES}
  ${UI_BUILT_SOURCES}
)

SET(APPLE_BUNDLE_RESOURCES
  "${CMAKE_SOURCE_DIR}/Common/DocumentIcons/OpenFOAM.icns"
  "${CMAKE_SOURCE_DIR}/Common/DocumentIcons/vtk.icns")

SET_SOURCE_FILES_PROPERTIES(
  ${APPLE_BUNDLE_RESOURCES}
  PROPERTIES
  MACOSX_PACKAGE_LOCATION Resources)

IF(QT_MAC_USE_COCOA)
  GET_FILENAME_COMPONENT(qt_menu_nib
    "@QT_QTGUI_LIBRARY_RELEASE@/Resources/qt_menu.nib"
    REALPATH)
  set(QT_MENU_NIB_SOURCES
    "${qt_menu_nib}/classes.nib"
    "${qt_menu_nib}/info.nib"
    "${qt_menu_nib}/keyedobjects.nib"
    )
  SET_SOURCE_FILES_PROPERTIES(
    ${QT_MENU_NIB_SOURCES}
    PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources/qt_menu.nib
    )
ELSE(QT_MAC_USE_COCOA)
  set(QT_MENU_NIB_SOURCES)
ENDIF(QT_MAC_USE_COCOA)

INCLUDE_DIRECTORIES(
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR})

SET(ParaView_SOURCE_FILES
  ParaViewMainWindow.cxx
  ParaViewMainWindow.h
  ${MOC_BUILT_SOURCES}
  ${UI_BUILT_SOURCES}
  ${APPLE_BUNDLE_RESOURCES}
  ${QT_MENU_NIB_SOURCES})

#------------------------------------------------------------------------------
# Build the client
build_paraview_client(paraview
    APPLICATION_NAME "ParaView"
    TITLE "ParaView ${PARAVIEW_VERSION_FULL}"
    ORGANIZATION  "ParaView"
    VERSION_MAJOR ${PARAVIEW_VERSION_MAJOR} 
    VERSION_MINOR ${PARAVIEW_VERSION_MINOR}
    VERSION_PATCH ${PARAVIEW_VERSION_PATCH}
    SPLASH_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/PVSplashScreen.png"
    PVMAIN_WINDOW ParaViewMainWindow
    PVMAIN_WINDOW_INCLUDE ParaViewMainWindow.h
    BUNDLE_ICON   "${CMAKE_CURRENT_SOURCE_DIR}/MacIcon.icns"
    APPLICATION_ICON  "${CMAKE_CURRENT_SOURCE_DIR}/WinIcon.ico"
    GUI_CONFIGURATION_XMLS
      ${CMAKE_CURRENT_SOURCE_DIR}/ParaViewSources.xml
      ${CMAKE_CURRENT_SOURCE_DIR}/ParaViewFilters.xml
      ${CMAKE_CURRENT_SOURCE_DIR}/ParaViewReaders.xml
      ${CMAKE_CURRENT_SOURCE_DIR}/ParaViewWriters.xml
    COMPRESSED_HELP_FILE "${ParaView_BINARY_DIR}/Documentation/paraview.qch"
    SOURCES ${ParaView_SOURCE_FILES}
)

# Configure the ".plugins" configuration xml for making paraview aware of the
# distributed plugins.
SET (plugins_ini "<?xml version=\"1.0\"?>\n<Plugins>\n")
FOREACH(pluginname ${PARAVIEW_PLUGINLIST})
  set (plugins_ini "${plugins_ini}  <Plugin name=\"${pluginname}\" auto_load=\"0\"/>\n")
ENDFOREACH(pluginname ${PARAVIEW_PLUGINLIST})
set (plugins_ini "${plugins_ini}</Plugins>\n")

FILE(WRITE "${EXECUTABLE_OUTPUT_PATH}/.plugins" "${plugins_ini}")

# Need an extended Info.plist to register all the file types on Mac OS X
# Further also fill it with version and copyright info.
SET_TARGET_PROPERTIES(paraview PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist.in
  MACOSX_BUNDLE_GUI_IDENTIFIER com.kitware.paraview
  MACOSX_BUNDLE_LONG_VERSION_STRING ${PARAVIEW_VERSION_FULL}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PARAVIEW_VERSION}
  MACOSX_BUNDLE_BUNDLE_VERSION ${PARAVIEW_VERSION_FULL}
  MACOSX_BUNDLE_INFO_STRING "ParaView visualization tool by Kitware Inc."
  MACOSX_BUNDLE_COPYRIGHT "© 2000-2009, Kitware Inc., Sandia National Laboratories, Los Alamos National Laboratory, Army Research Laboratory, CSimSoft"
  )

# This ensure that the paraview.qch file is generated.
ADD_DEPENDENCIES(paraview
  ParaViewOnlineHelp)

##------------------------------------------------------------------------------
## Create a drag and drop bundle on Mac OS X
#if(Q_WS_MAC)
#  CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/CreateDragNDropBundle.sh.in
#    ${CMAKE_CURRENT_BINARY_DIR}/CreateDragNDropBundle.sh @ONLY)
#  # read the license file
#  FILE(READ ${CMAKE_CURRENT_SOURCE_DIR}/DmgLicense_v1.2.txt LICENSE_TEXT)
#  # escape dangerous characters (that's just ridiculous...)
#  STRING(REGEX REPLACE "([\\\"])" "\\\\\\1" LICENSE_TEXT "${LICENSE_TEXT}")
#  # wrap lines in quotes and end them with \n
#  STRING(REPLACE "\n" "\\n\"\n\"" LICENSE_TEXT "${LICENSE_TEXT}")
#  SET(LICENSE_TEXT "\"${LICENSE_TEXT}\\n\"\n")
#  # configure the resource file
#  CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/DmgResource.r.in
#    ${CMAKE_CURRENT_BINARY_DIR}/DmgResource.r @ONLY)
#  # now define a target to run the thing
#  ADD_CUSTOM_TARGET(dmg
#    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/CreateDragNDropBundle.sh all
#    DEPENDS paraview
#    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#    COMMENT "Creating drag-n-drop installer disk image"
#    VERBATIM
#    )
#endif(Q_WS_MAC)

IF (BUILD_TESTING)
  SUBDIRS(Testing)
ENDIF (BUILD_TESTING)


#########################################################################
# Add install rules for installing certain dependencies.

#------------------------------------------------------------------------
# Install Qt 
SET (qtliblist QTCORE QTGUI QTNETWORK QTXML QTSQL QTHELP QTWEBKIT QTCLUCENE PHONON QTXMLPATTERNS)

# QTDBUS is Unix only
IF(NOT WIN32)
  LIST(APPEND qtliblist QTDBUS)
ENDIF(NOT WIN32)  

IF (VTK_USE_QVTK_QTOPENGL)
  LIST(APPEND qtliblist QTOPENGL)
ENDIF (VTK_USE_QVTK_QTOPENGL)

install_qt_libs("${qtliblist}" BrandedRuntime)

#------------------------------------------------------------------------
# Install ffmpeg, if used.
IF (VTK_USE_FFMPEG_ENCODER)
  FOREACH(fmpglib avcodec avformat avutil)
    IF (FFMPEG_${fmpglib}_LIBRARY)
      pv_install_library(${FFMPEG_${fmpglib}_LIBRARY}
        ${PV_INSTALL_LIB_DIR}
        BrandedRuntime)
    ENDIF(FFMPEG_${fmpglib}_LIBRARY)
  ENDFOREACH(fmpglib)
ENDIF (VTK_USE_FFMPEG_ENCODER)

#------------------------------------------------------------------------
# Import site specific installation rules file, if any.
SET (PARAVIEW_EXTRA_INSTALL_RULES_FILE "" CACHE FILEPATH 
  "Path to site-specific install rules for ParaView.")

IF (EXISTS "${PARAVIEW_EXTRA_INSTALL_RULES_FILE}")
  INCLUDE("${PARAVIEW_EXTRA_INSTALL_RULES_FILE}")
ENDIF (EXISTS "${PARAVIEW_EXTRA_INSTALL_RULES_FILE}")

#------------------------------------------------------------------------
# Install the .plugins configuration file.
INSTALL(FILES "${EXECUTABLE_OUTPUT_PATH}/.plugins"
        DESTINATION ${PV_INSTALL_LIB_DIR}
        COMPONENT BrandedRuntime)

# Call this last so the install script runs after the above targets are installed:
#
IF(APPLE)
  SET(distributed_plugins ${PARAVIEW_PLUGINLIST})
  SET(PV_EXE_NAME "paraview")
  SET(OSX_MAKE_STANDALONE_BUNDLE_CMAKE_SCRIPT
    "${CMAKE_CURRENT_BINARY_DIR}/ParaView_OSX_MakeStandAloneBundle.cmake")
  SET(OSX_MAKE_STANDALONE_BUNDLE_BASH_SCRIPT
    "${CMAKE_CURRENT_BINARY_DIR}/ParaView_OSX_MakeStandAloneBundle.sh")

  CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/CompleteBundle.cmake.in"
    "${OSX_MAKE_STANDALONE_BUNDLE_CMAKE_SCRIPT}" @ONLY IMMEDIATE)
  CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/CreateBundle.sh.in"
    "${OSX_MAKE_STANDALONE_BUNDLE_BASH_SCRIPT}" @ONLY IMMEDIATE)

  INSTALL(SCRIPT "${OSX_MAKE_STANDALONE_BUNDLE_CMAKE_SCRIPT}" COMPONENT Bundle)
ENDIF(APPLE)

#------------------------------------------------------------------------
# Install ParaView client
build_paraview_client_cpack_config_init(
    PACKAGE_NAME "ParaView"
    ORGANIZATION "Kitware Inc."
    VERSION_MAJOR "${PARAVIEW_VERSION_MAJOR}"
    VERSION_MINOR "${PARAVIEW_VERSION_MINOR}"
    VERSION_PATCH "${PARAVIEW_VERSION_PATCH}"
    DESCRIPTION "ParaView is a scientific visualization tool"
    LICENSE_FILE "${ParaView_SOURCE_DIR}/License_v1.2.txt"
    DESCRIPTION_FILE "${ParaView_SOURCE_DIR}/License_v1.2.txt"
    PACKAGE_EXECUTABLES "paraview;ParaView"
)

# Set NSIS specific CPACK vars for install icons, strings etc..
IF(WIN32 AND NOT UNIX)
    # There is a bug in NSI that does not handle full unix paths properly. Make
    # sure there is at least one set of four (4) backlasshes.
    SET(CPACK_PACKAGE_ICON "${ParaView_SOURCE_DIR}/Applications/ParaView\\\\pvIcon.png")
    SET(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\paraview.exe")
    SET(CPACK_NSIS_DISPLAY_NAME "${CPACK_PACKAGE_INSTALL_DIRECTORY} a cross-platform, open-source visualization system")
    SET(CPACK_NSIS_PACKAGE_NAME "${CPACK_PACKAGE_INSTALL_DIRECTORY}")
    SET(CPACK_NSIS_HELP_LINK "http://www.paraview.org")
    SET(CPACK_NSIS_URL_INFO_ABOUT "http://www.kitware.com")
    SET(CPACK_NSIS_CONTACT "webmaster@paraview.org")
    SET(CPACK_NSIS_MODIFY_PATH OFF)
    SET(CPACK_NSIS_MUI_ICON "${ParaView_SOURCE_DIR}/Applications/ParaView\\\\WinIcon.ico")
    SET(CPACK_NSIS_MUI_UNIICON "${ParaView_SOURCE_DIR}/Applications/ParaView\\\\WinIcon.ico")
  
  ENDIF(WIN32 AND NOT UNIX)

# This ensure that the only component that is used by cpack when building the
# drag-n-drop on Mac is the "Bundle" component.
SET (CPACK_INSTALL_CMAKE_PROJECTS_DRAGNDROP
  "${CMAKE_CURRENT_BINARY_DIR}" "ParaView Mac Bundle" "Bundle" "/")

build_paraview_client_cpack_config()
#########################################################################
