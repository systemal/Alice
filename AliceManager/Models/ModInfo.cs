using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;

namespace AliceManager.Models;

public enum ModSource { Git, Local, PreBuilt }

public enum ModStatus { Loaded, NotCompiled, CompileError, Disabled, Unknown }

public enum ModValidation { Valid, NoAliceJson, InvalidAliceJson, NoEntryFile, UnknownRuntime }

public class ModInfo : INotifyPropertyChanged
{
    // alice.json
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Type { get; set; } = "";
    public string Version { get; set; } = "";
    public string Runtime { get; set; } = "";
    public string Description { get; set; } = "";
    public string EntryFile { get; set; } = "";
    public string EntryType { get; set; } = "";

    // 来源
    public ModSource Source { get; set; } = ModSource.PreBuilt;
    public string GitUrl { get; set; } = "";
    public string SrcPath { get; set; } = "";
    public string RuntimePath { get; set; } = "";

    // 状态
    private ModStatus _status = ModStatus.Unknown;
    public ModStatus Status
    {
        get => _status;
        set { _status = value; OnPropertyChanged(); OnPropertyChanged(nameof(StatusText)); }
    }

    private bool _enabled = true;
    public bool Enabled
    {
        get => _enabled;
        set { _enabled = value; OnPropertyChanged(); }
    }

    // 派生属性
    public bool HasSource => !string.IsNullOrEmpty(SrcPath) && Directory.Exists(SrcPath);
    public bool HasRuntime => !string.IsNullOrEmpty(RuntimePath) && Directory.Exists(RuntimePath);
    public bool IsCSharp => Runtime == "dotnet";
    public bool IsScript => Runtime is "lua" or "js";
    public bool CanCompile => HasSource && IsCSharp;
    public bool CanUpdate => Source == ModSource.Git && HasSource;

    public string StatusText => Status switch
    {
        ModStatus.Loaded => "Loaded",
        ModStatus.NotCompiled => "Not Compiled",
        ModStatus.CompileError => "Compile Error",
        ModStatus.Disabled => "Disabled",
        _ => "Unknown"
    };

    public string Location => HasSource ? SrcPath : RuntimePath;

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
