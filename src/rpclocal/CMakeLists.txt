# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(rpclocal DLL RETAIL
  SOURCES
    rpclocal.cpp
    rpc_transport.cpp
    rpc_packet_registry.cpp
    rpc_client_proxy.cpp
    rpc_dispatch.cpp
    rpc_payload_ch1.cpp
    rpc_payload_ch2.cpp
    rpc_payload_ch3.cpp
    rpc_containers.cpp
    rpc_wire_ch1.cpp
    rpc_wire_ch2.cpp
    rpc_wire_generic.cpp
    rpc_wire_fields.cpp
    rpc_wire_varlen.cpp
    rpc_wire_closeout.cpp
    rpc_wire_ch3.cpp
    rpc_packet_dump.cpp
)

# Common.lib: retail rpclocal.dll imports COMMON.DLL (FmtStr::flatten, FmtStr::unflatten, ~FmtStr
# and FmtStr::Val::get_flattened_size, IAT slots .rdata:06db30dc / 06db3008 / 06db3094 /
# 06db30e0), so linking it here is what retail does, not a new dependency. Common.dll does not
# import rpclocal.dll in either direction, so there is no cycle.
fl_defines(rpclocal _WINDOWS _USRDLL RPCLOCAL_EXPORTS)
fl_link(rpclocal
  LIBS kernel32.lib user32.lib Common.lib
  REFERENCES dacom Common)
