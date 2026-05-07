#include "pch.h"
#include "Builtin/BuiltinHello.h"
#include "Host/IHostAPI.h"

namespace alice::builtin {

    PluginManifest BuiltinHello::Manifest( ) const {
        return {
            .id = "builtin-hello",
            .name = "Hello Plugin",
            .version = "1.0.0",
            .author = "Alice Core",
            .description = "内置测试插件, 验证插件架构",
            .type = "plugin",
        };
    }

    void BuiltinHello::OnLoad( IHostAPI* host ) {
        host_ = host;

        host_->Log( )->Info( "Hello 插件已加载!" );

        // 注册一个服务: hello.greet
        (void)host_->Service( )->Register( "hello.greet",
            []( const std::string& method, const nlohmann::json& args ) -> Result<nlohmann::json> {
                if ( method == "say" ) {
                    auto name = args.value( "name", "World" );
                    return nlohmann::json{ { "greeting", "Hello, " + name + "!" } };
                }
                return std::unexpected( MakeError( ErrorCode::InvalidArgument,
                    "未知方法: " + method ) );
            } );

        // 监听事件: hello.ping
        event_handle_ = host_->Event( )->On( "hello.ping", [this]( const nlohmann::json& data ) {
            auto from = data.value( "from", "unknown" );
            host_->Log( )->Info( "收到 ping! 来自: " + from );

            // 回复 pong 事件
            host_->Event( )->Emit( "hello.pong", { { "from", "builtin-hello" }, { "message", "pong!" } } );
        } );

        host_->Log( )->Info( "Hello 插件初始化完成: 已注册服务 hello.greet, 监听事件 hello.ping" );
    }

    void BuiltinHello::OnUnload( ) {
        if ( host_ ) {
            host_->Event( )->Off( event_handle_ );
            host_->Log( )->Info( "Hello 插件已卸载" );
        }
        host_ = nullptr;
    }

} // namespace alice::builtin
