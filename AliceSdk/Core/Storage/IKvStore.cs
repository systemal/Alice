using System.Text.Json;

namespace Alice.SDK.Core.Storage;

public interface IKvStore
{
    T? Get<T>(string key);
    void Set<T>(string key, T value);
    string? GetRaw(string key);
    void SetRaw(string key, string json);
}

internal class AliceKvStore : IKvStore
{
    public T? Get<T>(string key)
    {
        var json = Alice.Kv.Get(key);
        if (json is null) return default;
        try { return JsonSerializer.Deserialize<T>(json); }
        catch { return default; }
    }

    public void Set<T>(string key, T value)
    {
        var json = JsonSerializer.Serialize(value);
        Alice.Kv.Set(key, json);
    }

    public string? GetRaw(string key) => Alice.Kv.Get(key);

    public void SetRaw(string key, string json) => Alice.Kv.Set(key, json);
}
