using System.Runtime.InteropServices;

namespace Alice.SDK;

/// <summary>
/// C++ HostBridge 结构体的 P/Invoke 映射.
/// 字段顺序必须与 C++ HostBridge.h 完全一致.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeBridge
{
    public nint ctx;

    // event
    public delegate* unmanaged[Cdecl]<nint, byte*, nint, delegate* unmanaged[Cdecl]<nint, byte*, void>, ulong> event_on;
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*, int, void> event_emit;
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*, int, void> event_emit_async;
    public delegate* unmanaged[Cdecl]<nint, ulong, void> event_off;

    // service
    public delegate* unmanaged[Cdecl]<nint, byte*, nint, delegate* unmanaged[Cdecl]<nint, byte*, byte*, byte*>, int> service_register;
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*, byte*, byte*> service_call;
    public delegate* unmanaged[Cdecl]<nint, byte*, int, int> service_wait_for;

    // storage
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*> storage_read;
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*, int> storage_write;
    public delegate* unmanaged[Cdecl]<nint, byte*, int> storage_exists;

    // net
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*, byte*> net_fetch;
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*, nint, delegate* unmanaged[Cdecl]<nint, byte*, byte*, int>, int> net_fetch_stream;

    // kv
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*> kv_get;
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*, void> kv_set;

    // timer
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*, byte*, byte*> timer_set;
    public delegate* unmanaged[Cdecl]<nint, byte*, void> timer_remove;
    public delegate* unmanaged[Cdecl]<nint, byte*> timer_list;

    // log
    public delegate* unmanaged[Cdecl]<nint, byte*, void> log_info;
    public delegate* unmanaged[Cdecl]<nint, byte*, void> log_warn;
    public delegate* unmanaged[Cdecl]<nint, byte*, void> log_error;
    public delegate* unmanaged[Cdecl]<nint, byte*, void> log_debug;

    // platform
    public delegate* unmanaged[Cdecl]<nint, byte*> platform_name;
    public delegate* unmanaged[Cdecl]<nint, byte*> platform_data_dir;
    public delegate* unmanaged[Cdecl]<nint, byte*> platform_exe_dir;

    // script
    public delegate* unmanaged[Cdecl]<nint, byte*, byte*, byte*> script_eval;

    // memory
    public delegate* unmanaged[Cdecl]<byte*, void> free_string;
}
