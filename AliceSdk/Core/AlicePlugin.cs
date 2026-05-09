using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Alice.SDK.Core;

public abstract class AlicePlugin
{
    protected AliceHost Host { get; private set; } = null!;

    public abstract void OnLoad(AliceHost host);
    public virtual void OnUnload() { }

    public void InternalLoad(nint hostBridgePtr)
    {
        Host = AliceHost.Initialize(hostBridgePtr);
        try { OnLoad(Host); }
        catch (Exception ex) { Host.Log.Error($"OnLoad failed: {ex.Message}"); }
    }

    public void InternalUnload()
    {
        try { OnUnload(); }
        catch (Exception ex) { Host.Log.Error($"OnUnload failed: {ex.Message}"); }
        Host.Dispose();
    }
}
