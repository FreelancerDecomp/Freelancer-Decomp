# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(RemoteServer DLL RETAIL OUTPUT_NAME remoteserver
  SOURCES
    RemoteServer_main.cpp
    rpc_server_channel_receive.cpp
    IServerImpl.cpp
    rpc_server_packets.cpp
    ../RemoteCommon/remote_packet_codecs.cpp
    ../RemoteCommon/remote_server_channel_codecs.cpp
)

fl_defines(RemoteServer REMOTESERVER_EXPORTS FL_CLIENT _WINDOWS _USRDLL)
fl_link(RemoteServer
  LIBS kernel32.lib user32.lib dacom.lib Common.lib Server.lib freelancer.lib rpclocal.lib DALib.lib
  REFERENCES dacom freelancer Common DALib rpclocal Server)
