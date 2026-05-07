#pragma once

#include "Runtime/Types.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <mutex>
#include <filesystem>

namespace alice {

    /// <summary>
    /// 键值存储 — per-namespace 隔离.
    /// 每个 plugin_id 一个独立 JSON 文件 (data/kv/{plugin_id}.json).
    /// 懒加载 + 内存缓存: 首次 Get 时加载, 之后从内存读.
    /// TODO: 缓存无大小限制, 长期运行可能占用过多内存. 考虑 LRU 淘汰.
    /// 每个 namespace (通常 = plugin_id) 有独立的 JSON 文件.
    /// </summary>
    class KvStore {
    public:
        /// <summary>
        /// 构造 KvStore
        /// </summary>
        /// <param name="base_dir">存储根目录 (data/kv/)</param>
        explicit KvStore( const std::filesystem::path& base_dir );

        Result<nlohmann::json> Get( const std::string& ns, const std::string& key );
        VoidResult Set( const std::string& ns, const std::string& key, const nlohmann::json& value );
        VoidResult Delete( const std::string& ns, const std::string& key );
        std::vector<std::string> Keys( const std::string& ns );

    private:
        std::filesystem::path base_dir_;
        std::unordered_map<std::string, nlohmann::json> cache_;
        mutable std::mutex mutex_;

        nlohmann::json& LoadNamespace( const std::string& ns );
        VoidResult SaveNamespace( const std::string& ns );
        std::filesystem::path NsPath( const std::string& ns ) const;
    };

} // namespace alice
