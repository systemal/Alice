namespace Alice.SDK.Core.Services;

[AttributeUsage(AttributeTargets.Class)]
public class AliceServiceAttribute(string capability) : Attribute
{
    public string Capability { get; } = capability;
}

[AttributeUsage(AttributeTargets.Method)]
public class ServiceMethodAttribute(string? name = null) : Attribute
{
    public string? Name { get; } = name;
}
