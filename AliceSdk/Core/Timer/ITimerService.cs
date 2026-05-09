using System.Text.Json;

namespace Alice.SDK.Core.Timer;

public class TimerInfo
{
    public string Id { get; set; } = "";
    public string Label { get; set; } = "";
    public string Duration { get; set; } = "";
}

public interface ITimerService
{
    string Set(string duration, string label, object? data = null);
    void Remove(string id);
    List<TimerInfo> List();
}

internal class AliceTimerService : ITimerService
{
    public string Set(string duration, string label, object? data = null)
    {
        var dataJson = data is null ? "{}" : JsonSerializer.Serialize(data);
        return Alice.Timer.Set(duration, label, dataJson);
    }

    public void Remove(string id) => Alice.Timer.Remove(id);

    public List<TimerInfo> List()
    {
        try
        {
            var json = Alice.Timer.List();
            return JsonSerializer.Deserialize<List<TimerInfo>>(json) ?? [];
        }
        catch { return []; }
    }
}
