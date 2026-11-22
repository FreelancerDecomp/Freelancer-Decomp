# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(x86math DLL RETAIL
  SOURCES
    x86math.cpp
    dacom_stricmp.cpp
    x86math.rc
)

fl_link(x86math
  LIBS dacom.lib
  REFERENCES dacom)
