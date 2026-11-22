# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# PORT(dpnet) (docs/dpnet.md): the x64 build hosts through DPNet, the port's own DirectPlay 8,
# instead of Microsoft's dpnet.dll. Win32 keeps Microsoft's, as retail. On x64 these sources
# link first.
set(_dpnet_sources)
if(FL_X64)
  set(_dpnet_sources
    DPNetAdapter.cpp
    ../DPNet/dpnet_wire.cpp
    ../DPNet/dpnet_link.cpp
    ../DPNet/dpnet_session.cpp)
endif()

fl_add_module(DALib DLL RETAIL
  SOURCES
    ${_dpnet_sources}
    DALib.cpp
    md5hash.cpp
    CDPMsgList.cpp
    CDPNetCodec.cpp
    CDPClient.cpp
    CDPClientProxy.cpp
    CDPServer.cpp
    CGunWrapper.cpp
    DALib.rc
)

fl_includes(DALib AFTER_COMMON ../zlib/vendor)
fl_defines(DALib DALIB_EXPORTS ZLIB_WINAPI)
set(_dpnet_libs)
if(FL_X64)
  fl_includes(DALib AFTER_COMMON ../DPNet)
  fl_defines(DALib FREELANCER_DPNET)
  # Ahead of the others: the x64 link order since DPNet came in.
  set(_dpnet_libs ws2_32.lib)
endif()
fl_link(DALib
  LIBS ${_dpnet_libs} dacom.lib zlib.lib winmm.lib
  REFERENCES dacom zlib)
