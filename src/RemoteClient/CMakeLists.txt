# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(RemoteClient DLL RETAIL
  SOURCES
    RemoteClient_main.cpp
    ClientImpl.cpp
    rpc_server_channel_senders.cpp
    RemoteClient_transport.cpp
    rpc_client_packets.cpp
    ../RemoteCommon/remote_packet_codecs.cpp
    ../RemoteCommon/remote_server_channel_codecs.cpp
)

fl_defines(RemoteClient FL_SERVER REMOTECLIENT_EXPORTS _WINDOWS _USRDLL)
fl_link(RemoteClient
  LIBS kernel32.lib user32.lib dacom.lib Common.lib Server.lib rpclocal.lib DALib.lib
  REFERENCES Server dacom Common DALib rpclocal)
