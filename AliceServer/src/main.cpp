#include "pch.h"
#include "Runtime/Runtime.h"

#include <csignal>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
    alice::Runtime* g_runtime = nullptr;

    void SignalHandler( int sig ) {
        if ( sig == SIGINT || sig == SIGTERM ) {
            ALICE_INFO( "收到关闭信号 ({})", sig );
            if ( g_runtime ) g_runtime->Shutdown( );
        }
    }
}

int main( ) {
#ifdef _WIN32
    SetConsoleOutputCP( CP_UTF8 );
    SetConsoleCP( CP_UTF8 );
#endif

    alice::Logger::Init( );
    ALICE_INFO( "Alice Server v0.2.0 (runtime)" );

    alice::Runtime runtime;
    g_runtime = &runtime;

    std::signal( SIGINT, SignalHandler );
    std::signal( SIGTERM, SignalHandler );

    auto result = runtime.Initialize( );
    if ( !result ) {
        ALICE_ERROR( "初始化失败: {}", result.error( ).message );
        return 1;
    }

    runtime.Run( );
    return 0;
}
