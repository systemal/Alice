#pragma once

#include "Core/Types.h"
#include "Core/Error.h"
#include <filesystem>

namespace alice {

    /// <summary>
    /// 用户目录结构管理.
    /// 每个用户在 data/users/{id}/ 下有独立的目录树.
    /// </summary>
    class UserDirectory {
    public:
        /// <summary>
        /// 构造用户目录管理器
        /// </summary>
        /// <param name="base_data_dir">数据根目录 (可执行文件同级 data/)</param>
        /// <param name="user_id">用户 ID</param>
        UserDirectory( const std::filesystem::path& base_data_dir, const UserID& user_id );

        /// <summary>
        /// 确保用户完整目录结构存在
        /// </summary>
        VoidResult EnsureStructure( );

        /// <summary>
        /// 获取用户根目录
        /// </summary>
        const std::filesystem::path& Root( ) const { return root_; }

        /// <summary>
        /// 获取用户角色目录 (chars/)
        /// </summary>
        std::filesystem::path Chars( ) const { return root_ / "chars"; }

        /// <summary>
        /// 获取用户设置目录 (settings/)
        /// </summary>
        std::filesystem::path Settings( ) const { return root_ / "settings"; }

        /// <summary>
        /// 获取用户 Shell 数据目录 (shells/)
        /// </summary>
        std::filesystem::path Shells( ) const { return root_ / "shells"; }

        /// <summary>
        /// 获取用户世界观目录 (worlds/)
        /// </summary>
        std::filesystem::path Worlds( ) const { return root_ / "worlds"; }

        /// <summary>
        /// 获取用户人格目录 (personas/)
        /// </summary>
        std::filesystem::path Personas( ) const { return root_ / "personas"; }

        /// <summary>
        /// 获取用户插件目录 (plugins/)
        /// </summary>
        std::filesystem::path Plugins( ) const { return root_ / "plugins"; }

        /// <summary>
        /// 获取聊天记录目录 (chats/)
        /// </summary>
        std::filesystem::path Chats( ) const { return root_ / "chats"; }

    private:
        std::filesystem::path root_;
    };

} // namespace alice
