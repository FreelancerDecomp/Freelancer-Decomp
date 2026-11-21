# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# x64 has its own export list (its C++ names are decorated differently); CI checks that it
# exports what dacom.def does (.github/scripts/check_def_twins.py).
if(FL_X64)
  set(_def dacom_x64.def)
else()
  set(_def dacom.def)
endif()

fl_add_module(dacom DLL RETAIL DEF ${_def}
  SOURCES
    dacom.cpp
    dacom.rc
)

fl_link(dacom)
