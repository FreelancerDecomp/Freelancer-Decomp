# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(EngBase DLL RETAIL
  SOURCES
    EngBase.cpp
    EngBaseMixer.cpp
    EngBase.rc
)

fl_link(EngBase
  LIBS dacom.lib
  REFERENCES dacom)
