using System.Text.Json;

namespace Alice.SDK.Core.Events;

public interface IEventBus
{
    ulong On(string eventName, Action<JsonElement> handler);
    ulong On<T>(string eventName, Action<T> handler);
    void Emit(string eventName, object? data = null, int ttl = -1);
    void EmitAsync(string eventName, object? data = null, int ttl = -1);
    void Off(ulong handle);
}

internal class AliceEventBus : IEventBus
{
    public ulong On(string eventName, Action<JsonElement> handler)
    {
        return Alice.Event.On(eventName, json =>
        {
            try
            {
                var doc = JsonDocument.Parse(json);
                handler(doc.RootElement);
            }
            catch { }
        });
    }

    public ulong On<T>(string eventName, Action<T> handler)
    {
        return Alice.Event.On(eventName, json =>
        {
            try
            {
                var obj = JsonSerializer.Deserialize<T>(json);
                if (obj is not null) handler(obj);
            }
            catch { }
        });
    }

    public void Emit(string eventName, object? data = null, int ttl = -1)
    {
        var json = data is null ? "{}" : JsonSerializer.Serialize(data);
        Alice.Event.Emit(eventName, json, ttl);
    }

    public void EmitAsync(string eventName, object? data = null, int ttl = -1)
    {
        var json = data is null ? "{}" : JsonSerializer.Serialize(data);
        Alice.Event.EmitAsync(eventName, json, ttl);
    }

    public void Off(ulong handle) => Alice.Event.Off(handle);
}
