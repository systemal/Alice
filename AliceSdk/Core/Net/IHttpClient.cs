using System.Text.Json;

namespace Alice.SDK.Core.Net;

public interface IHttpClient
{
    Result<HttpResponse> Get(string url, Dictionary<string, string>? headers = null);
    Result<HttpResponse> Post(string url, object? body = null, Dictionary<string, string>? headers = null);
    Result<HttpResponse> Fetch(string url, HttpRequestOptions opts);
}

internal class AliceHttpClient : IHttpClient
{
    public Result<HttpResponse> Get(string url, Dictionary<string, string>? headers = null)
    {
        return Fetch(url, new HttpRequestOptions { Method = "GET", Headers = headers ?? [] });
    }

    public Result<HttpResponse> Post(string url, object? body = null, Dictionary<string, string>? headers = null)
    {
        var bodyJson = body is string s ? s : (body is null ? null : JsonSerializer.Serialize(body));
        return Fetch(url, new HttpRequestOptions
        {
            Method = "POST",
            Body = bodyJson,
            Headers = headers ?? new() { ["Content-Type"] = "application/json" }
        });
    }

    public Result<HttpResponse> Fetch(string url, HttpRequestOptions opts)
    {
        try
        {
            var optsJson = JsonSerializer.Serialize(new
            {
                method = opts.Method,
                headers = opts.Headers,
                body = opts.Body
            });

            var resultJson = Alice.Net.Fetch(url, optsJson);
            if (resultJson is null)
                return Result.Fail<HttpResponse>("Fetch returned null");

            var doc = JsonDocument.Parse(resultJson);
            var root = doc.RootElement;

            return Result.Ok(new HttpResponse
            {
                Status = root.TryGetProperty("status", out var st) ? st.GetInt32() : 0,
                Body = root.TryGetProperty("body", out var bd) ? bd.GetString() ?? "" : ""
            });
        }
        catch (Exception ex)
        {
            return Result.Fail<HttpResponse>(ex.Message);
        }
    }
}
