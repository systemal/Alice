using System.Reflection;
using System.Text.Json;

namespace Alice.SDK.Core.Services;

public interface IServiceRegistry
{
    void Register(string capability, Func<string, JsonElement, object?> handler);
    void Register<TService>(string capability, TService service) where TService : class;

    Result<TResult> Call<TResult>(string capability, string method, object? args = null);
    Result<JsonElement> CallRaw(string capability, string method, string argsJson = "{}");

    bool WaitFor(string capability, int timeoutMs = 5000);
}

internal class AliceServiceRegistry : IServiceRegistry
{
    public void Register(string capability, Func<string, JsonElement, object?> handler)
    {
        Alice.Service.Register(capability, (method, argsJson) =>
        {
            try
            {
                var args = JsonDocument.Parse(argsJson).RootElement;
                var result = handler(method, args);
                return JsonSerializer.Serialize(result);
            }
            catch (Exception ex)
            {
                return JsonSerializer.Serialize(new { error = ex.Message });
            }
        });
    }

    public void Register<TService>(string capability, TService service) where TService : class
    {
        var methods = typeof(TService).GetMethods(BindingFlags.Public | BindingFlags.Instance)
            .Where(m => m.GetCustomAttribute<ServiceMethodAttribute>() != null || m.DeclaringType == typeof(TService))
            .Where(m => m.DeclaringType != typeof(object))
            .ToDictionary(
                m => m.GetCustomAttribute<ServiceMethodAttribute>()?.Name ?? m.Name.ToLowerInvariant(),
                m => m);

        Alice.Service.Register(capability, (method, argsJson) =>
        {
            try
            {
                if (!methods.TryGetValue(method, out var mi))
                    return JsonSerializer.Serialize(new { error = $"Method '{method}' not found" });

                var parameters = mi.GetParameters();
                object?[] invokeArgs;

                if (parameters.Length == 0)
                {
                    invokeArgs = [];
                }
                else if (parameters.Length == 1)
                {
                    var argType = parameters[0].ParameterType;
                    var arg = JsonSerializer.Deserialize(argsJson, argType);
                    invokeArgs = [arg];
                }
                else
                {
                    var doc = JsonDocument.Parse(argsJson);
                    invokeArgs = new object?[parameters.Length];
                    for (int i = 0; i < parameters.Length; i++)
                    {
                        var p = parameters[i];
                        if (doc.RootElement.TryGetProperty(p.Name!, out var prop))
                            invokeArgs[i] = JsonSerializer.Deserialize(prop.GetRawText(), p.ParameterType);
                        else
                            invokeArgs[i] = p.HasDefaultValue ? p.DefaultValue : null;
                    }
                }

                var result = mi.Invoke(service, invokeArgs);
                return JsonSerializer.Serialize(result);
            }
            catch (Exception ex)
            {
                var inner = ex.InnerException ?? ex;
                return JsonSerializer.Serialize(new { error = inner.Message });
            }
        });
    }

    public Result<TResult> Call<TResult>(string capability, string method, object? args = null)
    {
        try
        {
            var argsJson = args is null ? "{}" : JsonSerializer.Serialize(args);
            var resultJson = Alice.Service.Call(capability, method, argsJson);
            if (resultJson is null or "null")
                return Result.Fail<TResult>($"Service '{capability}' not found");

            var result = JsonSerializer.Deserialize<TResult>(resultJson);
            if (result is null)
                return Result.Fail<TResult>("Deserialization returned null");
            return Result.Ok(result);
        }
        catch (Exception ex)
        {
            return Result.Fail<TResult>(ex.Message);
        }
    }

    public Result<JsonElement> CallRaw(string capability, string method, string argsJson = "{}")
    {
        try
        {
            var resultJson = Alice.Service.Call(capability, method, argsJson);
            if (resultJson is null or "null")
                return Result.Fail<JsonElement>($"Service '{capability}' not found");

            return Result.Ok(JsonDocument.Parse(resultJson).RootElement);
        }
        catch (Exception ex)
        {
            return Result.Fail<JsonElement>(ex.Message);
        }
    }

    public bool WaitFor(string capability, int timeoutMs = 5000)
        => Alice.Service.WaitFor(capability, timeoutMs);
}
