# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# FLDedicated.exe: PORT(dedicated), a console dedicated server (no retail counterpart). It runs
# Server.dll with RemoteClient.dll as FLServer.exe does, without MFC or windows.

fl_add_module(FLDedicated EXE PORT CONSOLE
  SOURCES
    FLDedicated.cpp
    # The host shared by FLServer and FLDedicated (docs/dedicated_server.md).
    ../ServerHost/server_host.cpp
    FLDedicated.rc
)

fl_includes(FLDedicated .)
fl_defines(FLDedicated _CONSOLE)
fl_link(FLDedicated
  LIBS dacom.lib DALib.lib Common.lib Server.lib RemoteClient.lib ole32.lib winmm.lib
  REFERENCES dacom DALib Common Server RemoteClient)
