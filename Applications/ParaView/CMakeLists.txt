INCLUDE(${PARAVIEW_BRANDING_CMAKE_FILE})
INCLUDE(${PARAVIEW_PLUGINS_CMAKE_FILE})

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

INCLUDE_DIRECTORIES(
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR})

SET(ParaView_SOURCE_FILES
  ParaViewMainWindow.cxx
  ParaViewMainWindow.h
  ${MOC_BUILT_SOURCES}
  ${UI_BUILT_SOURCES}
  ${APPLE_BUNDLE_RESOURCES})

#------------------------------------------------------------------------------
# Build the client
build_paraview_client(paraview
    TITLE "ParaView ${PARAVIEW_VERSION_FULL}"
    ORGANIZATION  "Kitware"
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

IF (BUILD_TESTING)
  SUBDIRS(Testing)
ENDIF (BUILD_TESTING)


