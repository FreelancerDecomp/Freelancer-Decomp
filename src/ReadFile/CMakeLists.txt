# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(ReadFile DLL RETAIL
  SOURCES
    DacomStricmp.cpp
    ReadFile.cpp
    ReadFile.rc
)

fl_link(ReadFile
  LIBS dacom.lib
  REFERENCES dacom)
