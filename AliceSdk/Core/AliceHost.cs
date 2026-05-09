using Alice.SDK.Core.Events;
using Alice.SDK.Core.Logging;
using Alice.SDK.Core.Net;
using Alice.SDK.Core.Platform;
using Alice.SDK.Core.Services;
using Alice.SDK.Core.Storage;
using Alice.SDK.Core.Timer;

namespace Alice.SDK.Core;

public class AliceHost : IDisposable
{
    public ILogger Log { get; }
    public IEventBus Events { get; }
    public IServiceRegistry Services { get; }
    public IFileSystem Files { get; }
    public IKvStore Kv { get; }
    public IHttpClient Http { get; }
    public ITimerService Timers { get; }
    public IPlatform Platform { get; }

    private AliceHost()
    {
        Log = new AliceLogger();
        Events = new AliceEventBus();
        Services = new AliceServiceRegistry();
        Files = new AliceFileSystem();
        Kv = new AliceKvStore();
        Http = new AliceHttpClient();
        Timers = new AliceTimerService();
        Platform = new AlicePlatform();
    }

    public static AliceHost Initialize(nint hostBridgePtr)
    {
        Alice.Initialize(hostBridgePtr);
        return new AliceHost();
    }

    public void Dispose()
    {
        Alice.Cleanup();
    }
}
