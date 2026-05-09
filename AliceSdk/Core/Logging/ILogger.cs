namespace Alice.SDK.Core.Logging;

public interface ILogger
{
    void Info(string message);
    void Warn(string message);
    void Error(string message);
    void Debug(string message);
}

internal class AliceLogger : ILogger
{
    public void Info(string message) => Alice.Log.Info(message);
    public void Warn(string message) => Alice.Log.Warn(message);
    public void Error(string message) => Alice.Log.Error(message);
    public void Debug(string message) => Alice.Log.Debug(message);
}
