namespace Alice.SDK.Core;

public readonly struct Result<T>
{
    public bool Success { get; }
    public T? Value { get; }
    public string? Error { get; }

    private Result(bool success, T? value, string? error)
    {
        Success = success;
        Value = value;
        Error = error;
    }

    public static Result<T> Ok(T value) => new(true, value, null);
    public static Result<T> Fail(string error) => new(false, default, error);

    public T ValueOr(T fallback) => Success && Value is not null ? Value : fallback;

    public Result<TOut> Map<TOut>(Func<T, TOut> transform)
        => Success && Value is not null
            ? Result<TOut>.Ok(transform(Value))
            : Result<TOut>.Fail(Error ?? "No value");

    public static implicit operator bool(Result<T> r) => r.Success;
    public override string ToString() => Success ? $"Ok({Value})" : $"Fail({Error})";
}

public static class Result
{
    public static Result<T> Ok<T>(T value) => Result<T>.Ok(value);
    public static Result<T> Fail<T>(string error) => Result<T>.Fail(error);
}
