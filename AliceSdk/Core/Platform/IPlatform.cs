namespace Alice.SDK.Core.Platform;

public interface IPlatform
{
    string Name { get; }
    string DataDir { get; }
    string ExeDir { get; }
}

internal class AlicePlatform : IPlatform
{
    public string Name => Alice.Platform.Name();
    public string DataDir => Alice.Platform.DataDir();
    public string ExeDir => Alice.Platform.ExeDir();
}
