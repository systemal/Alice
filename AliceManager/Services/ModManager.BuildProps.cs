namespace AliceManager.Services;

public partial class ModManager
{
    /// <summary>
    /// 在 mods-src/ 下创建 Directory.Build.props + Directory.Build.targets
    /// 作用: 把插件 csproj 里的 Alice.SDK ProjectReference 替换为 DLL 直接引用
    /// 这样复制到 mods-src/ 后不需要修改 csproj
    /// </summary>
    public void EnsureBuildProps()
    {
        var propsPath = System.IO.Path.Combine(_modsSrcDir, "Directory.Build.props");
        var targetsPath = System.IO.Path.Combine(_modsSrcDir, "Directory.Build.targets");

        // Directory.Build.props: 设置 SDK DLL 路径变量 + 输出配置
        var props = $"""
            <Project>
              <PropertyGroup>
                <AliceSdkDll>{_sdkDll}</AliceSdkDll>
                <EnableDynamicLoading>true</EnableDynamicLoading>
                <SuppressNETCoreSdkPreviewMessage>true</SuppressNETCoreSdkPreviewMessage>
              </PropertyGroup>
            </Project>
            """;
        System.IO.File.WriteAllText(propsPath, props);

        // Directory.Build.targets: 移除所有 ProjectReference 里含 Alice.SDK 的, 替换为 DLL Reference
        var targets = """
            <Project>
              <ItemGroup>
                <ProjectReference Remove="**\Alice.SDK.csproj" />
              </ItemGroup>
              <ItemGroup Condition="'$(AliceSdkDll)' != '' And Exists('$(AliceSdkDll)')">
                <Reference Include="Alice.SDK">
                  <HintPath>$(AliceSdkDll)</HintPath>
                  <Private>false</Private>
                </Reference>
              </ItemGroup>
            </Project>
            """;
        System.IO.File.WriteAllText(targetsPath, targets);
    }
}
