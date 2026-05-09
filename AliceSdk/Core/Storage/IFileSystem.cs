namespace Alice.SDK.Core.Storage;

public interface IFileSystem
{
    string? ReadText(string path);
    bool WriteText(string path, string content);
    bool Exists(string path);
}

internal class AliceFileSystem : IFileSystem
{
    public string? ReadText(string path) => Alice.Fs.Read(path);
    public bool WriteText(string path, string content) => Alice.Fs.Write(path, content);
    public bool Exists(string path) => Alice.Fs.Exists(path);
}
