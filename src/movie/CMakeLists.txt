# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# Win32 only.
if(NOT FL_WIN32)
  return()
endif()

fl_add_module(movie DLL RETAIL
  SOURCES
    MediaPlayer.cpp
    MovieSha.cpp
    MovieMod.cpp
    MovieAce.cpp
    MovieExp.cpp
    MovieField.cpp
    MovieFieldHigh.cpp
    MovieFieldMid.cpp
    MovieGraph.cpp
    MovieRuntime.cpp
    MovieCca.cpp
    MovieWindow.cpp
    MovieD4.cpp
    MovieWide.cpp
    MoviePlay.cpp
    MovieObject.cpp
)

fl_link(movie
  LIBS ole32.lib user32.lib)
