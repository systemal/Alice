using System.Text.Json;

namespace Alice.SDK.Core.Net;

public class HttpRequestOptions
{
    public string Method { get; set; } = "GET";
    public Dictionary<string, string> Headers { get; set; } = new();
    public string? Body { get; set; }
}

public class HttpResponse
{
    public int Status { get; set; }
    public string Body { get; set; } = "";

    public T? Json<T>()
    {
        try { return JsonSerializer.Deserialize<T>(Body); }
        catch { return default; }
    }

    public JsonElement JsonElement()
    {
        try { return JsonDocument.Parse(Body).RootElement; }
        catch { return default; }
    }

    public bool IsSuccess => Status >= 200 && Status < 300;
}
