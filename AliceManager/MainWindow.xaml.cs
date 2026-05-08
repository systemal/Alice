using System.Collections.ObjectModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using AliceManager.Models;
using AliceManager.Services;
using MahApps.Metro.Controls;
using MahApps.Metro.Controls.Dialogs;

namespace AliceManager;

public partial class MainWindow : MetroWindow
{
    private readonly AliceClient _client = new();
    private readonly ModManager _modManager;
    private readonly ObservableCollection<LogEntry> _logs = [];
    private bool _realExit;

    public MainWindow()
    {
        InitializeComponent();

        var baseDir = AppDomain.CurrentDomain.BaseDirectory;
        _modManager = new ModManager(baseDir);
        _modManager.OnLog += (level, message) => Dispatcher.Invoke(() =>
        {
            _logs.Add(new LogEntry { Time = DateTime.Now.ToString("HH:mm:ss"), Level = level, Source = "ModManager", Message = message });
            while (_logs.Count > 2000) _logs.RemoveAt(0);
        });

        _modManager.RequestReload = async (id) => await _client.ReloadPluginAsync(id);
        _modManager.RequestUnload = async (id) => await _client.UnloadPluginAsync(id);

        ModsGrid.ItemsSource = _modManager.Mods;
        LogGrid.ItemsSource = _logs;
    }

    private async void Window_Loaded(object sender, RoutedEventArgs e)
    {
        MainTab.SelectedIndex = 1;
        UpdateSettingsContent();
        InitTrayIcon();

        // 扫描本地 mods
        _modManager.Scan();
        DashModCount.Text = _modManager.Mods.Count.ToString();

        _client.OnStateInit += (plugins, services) => Dispatcher.Invoke(() =>
        {
            // 用 Server 返回的状态更新已加载标记
            foreach (var p in plugins)
            {
                var mod = _modManager.Mods.FirstOrDefault(m => m.Id == p.Id);
                if (mod != null) mod.Status = ModStatus.Loaded;
            }
            DashModCount.Text = _modManager.Mods.Count.ToString();
            DashServiceCount.Text = services.Count.ToString();
            DashServerStatus.Text = "Online";
            DashServerStatus.Foreground = new SolidColorBrush(Color.FromRgb(0x4C, 0xAF, 0x50));
        });

        _client.OnConnectionChanged += connected => Dispatcher.Invoke(() =>
        {
            StatusDotColor.Color = connected ? Color.FromRgb(0x4C, 0xAF, 0x50) : Color.FromRgb(0xF4, 0x43, 0x36);
            StatusText.Text = connected ? "Connected" : "Disconnected";
            StatusButtonText.Text = connected ? "Online" : "Offline";
            DashServerStatus.Text = connected ? "Online" : "Offline";
            DashServerStatus.Foreground = new SolidColorBrush(
                connected ? Color.FromRgb(0x4C, 0xAF, 0x50) : Color.FromRgb(0xF4, 0x43, 0x36));
        });

        _client.OnPluginUnloaded += id => Dispatcher.Invoke(() =>
        {
            var mod = _modManager.Mods.FirstOrDefault(m => m.Id == id);
            if (mod != null) mod.Status = ModStatus.Unknown;
        });

        _client.OnLog += (level, source, plugin, message) => Dispatcher.BeginInvoke(() =>
        {
            _logs.Add(new LogEntry
            {
                Time = DateTime.Now.ToString("HH:mm:ss"),
                Level = level,
                Source = source,
                Plugin = plugin,
                Message = message
            });
            while (_logs.Count > 2000) _logs.RemoveAt(0);
        });

        await ConnectToServer();
    }

    private async Task ConnectToServer()
    {
        StatusText.Text = "Connecting...";
        StatusButtonText.Text = "Connecting...";

        if (!await _client.ConnectAsync(maxRetries: 3, retryDelayMs: 300))
        {
            if (!AliceClient.IsServerRunning())
            {
                StatusText.Text = "Starting Server...";
                if (_client.TryLaunchServer())
                {
                    await Task.Delay(2000);
                    await _client.ConnectAsync(maxRetries: 15, retryDelayMs: 500);
                }
            }
        }
    }

    private void InitTrayIcon()
    {
        try
        {
            var uri = new Uri("pack://application:,,,/Assets/logo.jpg");
            var stream = Application.GetResourceStream(uri)?.Stream;
            if (stream != null)
            {
                using var bmp = new System.Drawing.Bitmap(stream);
                var hIcon = bmp.GetHicon();
                TrayIcon.Icon = System.Drawing.Icon.FromHandle(hIcon);
                Icon = new System.Windows.Media.Imaging.BitmapImage(uri);
            }
        }
        catch { }
    }

    // ── 窗口关闭 → 最小化到托盘 ──

    protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
    {
        if (!_realExit)
        {
            e.Cancel = true;
            Hide();
            return;
        }
        RealExit();
        base.OnClosing(e);
    }

    private void RealExit()
    {
        _client.Dispose();
        TrayIcon.Dispose();
    }

    // ── 托盘菜单 ──

    private void TrayIcon_DoubleClick(object sender, RoutedEventArgs e)
    {
        Show(); WindowState = WindowState.Normal; Activate();
    }

    private void TrayMenu_Show(object sender, RoutedEventArgs e)
    {
        Show(); WindowState = WindowState.Normal; Activate();
    }

    private void TrayMenu_Hide(object sender, RoutedEventArgs e) => Hide();

    private async void TrayMenu_RestartServer(object sender, RoutedEventArgs e)
    {
        _client.SendShutdownCommand();
        await Task.Run(() => _client.WaitForServerExit(5000));
        _client.ForceKillServer();
        _client.Reset();
        _modManager.Scan();
        await ConnectToServer();
    }

    private void TrayMenu_Exit(object sender, RoutedEventArgs e)
    {
        _realExit = true;
        Close();
    }

    // ── Drag ──

    [DllImport("user32.dll")]
    private static extern nint SendMessage(nint hWnd, int msg, nint wParam, nint lParam);
    private const int WM_NCLBUTTONDOWN = 0xA1;
    private const nint HTCAPTION = 2;

    protected override void OnPreviewMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        base.OnPreviewMouseLeftButtonDown(e);
        if (e.OriginalSource is DependencyObject source && IsInteractiveControl(source)) return;
        SendMessage(new WindowInteropHelper(this).Handle, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }

    private static bool IsInteractiveControl(DependencyObject source)
    {
        var current = source;
        while (current != null)
        {
            if (current is System.Windows.Controls.Primitives.ButtonBase
                or TextBox or DataGrid or DataGridRow or DataGridCell
                or System.Windows.Controls.Primitives.Thumb
                or CheckBox or TreeViewItem or ComboBox or ListBoxItem)
                return true;
            current = VisualTreeHelper.GetParent(current);
        }
        return false;
    }

    // ── Nav ──

    private void NavButton_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button btn && btn.Tag is string tag && int.TryParse(tag, out var index))
            MainTab.SelectedIndex = index;
    }

    // ── Mods 操作 ──

    private async void ModsGrid_CellEditEnding(object sender, DataGridCellEditEndingEventArgs e)
    {
        if (e.Column is DataGridCheckBoxColumn && e.Row.Item is ModInfo mod)
        {
            // CheckBox 的新值在 commit 之后才生效, 延迟一帧读取
            await Task.Yield();
            if (mod.Enabled)
            {
                await _client.ReloadPluginAsync(mod.Id);
                mod.Status = ModStatus.Loaded;
            }
            else
            {
                await _client.UnloadPluginAsync(mod.Id);
                mod.Status = ModStatus.Disabled;
            }
        }
    }

    private void SearchTextBox_TextChanged(object sender, TextChangedEventArgs e) { }

    private async void UpdateAll_Click(object sender, RoutedEventArgs e)
    {
        var (updated, failed, upToDate) = await _modManager.UpdateAllAsync();
        _logs.Add(new LogEntry
        {
            Time = DateTime.Now.ToString("HH:mm:ss"), Level = "info", Source = "ModManager",
            Message = $"Update All: {updated} updated, {failed} failed, {upToDate} up-to-date"
        });
    }

    private async void CompileAll_Click(object sender, RoutedEventArgs e)
    {
        var (ok, fail) = await _modManager.CompileAllAsync();
        _logs.Add(new LogEntry
        {
            Time = DateTime.Now.ToString("HH:mm:ss"), Level = "info", Source = "ModManager",
            Message = $"Compile All: {ok} success, {fail} failed"
        });
    }

    private async void InstallButton_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new Views.InstallModWindow(_modManager) { Owner = this };
        dlg.ShowDialog();
        _modManager.Scan();
        DashModCount.Text = _modManager.Mods.Count.ToString();
    }

    private async void MenuReload_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod)
            await _client.ReloadPluginAsync(mod.Id);
    }

    private async void MenuCompile_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod)
            await _modManager.CompileAsync(mod.Id);
    }

    private async void MenuUpdate_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod)
            await _modManager.UpdateAsync(mod.Id);
    }

    private void MenuOpenSource_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod)
            _modManager.OpenSourceFolder(mod.Id);
    }

    private void MenuOpenRuntime_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod)
            _modManager.OpenRuntimeFolder(mod.Id);
    }

    private void MenuGithub_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod)
            _modManager.OpenGitHub(mod.Id);
    }

    private async void MenuRemove_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod)
        {
            var result = await this.ShowMessageAsync("Confirm Remove",
                $"Remove mod '{mod.Name}'?\nThis will delete source and runtime files.",
                MahApps.Metro.Controls.Dialogs.MessageDialogStyle.AffirmativeAndNegative);
            if (result == MahApps.Metro.Controls.Dialogs.MessageDialogResult.Affirmative)
            {
                await _modManager.RemoveAsync(mod.Id);
                DashModCount.Text = _modManager.Mods.Count.ToString();
            }
        }
    }

    private void ClearLog_Click(object sender, RoutedEventArgs e) => _logs.Clear();

    // ── Settings ──

    private void SettingsTree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        => UpdateSettingsContent();

    private void UpdateSettingsContent()
    {
        if (SettingsFrame is null || SettingsTree is null) return;
        var selected = SettingsTree.SelectedItem as TreeViewItem;
        var header = selected?.Header?.ToString() ?? "General";
        SettingsFrame.Content = header switch
        {
            "General" => BuildGeneralSettings(),
            "Server" => BuildServerSettings(),
            "Paths" => BuildPathSettings(),
            _ => null
        };
    }

    private static UIElement BuildGeneralSettings()
    {
        var panel = new StackPanel();
        panel.Children.Add(MakeCheckBox("Start with Windows", false));
        panel.Children.Add(MakeCheckBox("Minimize to tray on close", true));
        panel.Children.Add(MakeCheckBox("Auto-connect to Server on startup", true));
        panel.Children.Add(MakeCheckBox("Watch mods folder for changes", true));
        return panel;
    }

    private static UIElement BuildServerSettings()
    {
        var panel = new StackPanel();
        panel.Children.Add(MakeField("Host", "localhost"));
        panel.Children.Add(MakeField("Port", "645"));
        var btn = new Button { Content = "Test Connection", Width = 120, Height = 25, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 8, 0, 0) };
        panel.Children.Add(btn);
        return panel;
    }

    private static UIElement BuildPathSettings()
    {
        var panel = new StackPanel();
        panel.Children.Add(MakeField("Mods Directory", "", true));
        panel.Children.Add(MakeField("Server Executable", "", true));
        return panel;
    }

    private static UIElement MakeCheckBox(string label, bool isChecked)
        => new CheckBox { Content = label, IsChecked = isChecked, Margin = new Thickness(0, 0, 0, 8) };

    private static UIElement MakeField(string label, string value, bool readOnly = false)
    {
        var sp = new StackPanel { Margin = new Thickness(0, 0, 0, 10) };
        sp.Children.Add(new TextBlock { Text = label, FontSize = 12, Margin = new Thickness(0, 0, 0, 4) });
        sp.Children.Add(new TextBox { Text = value, IsReadOnly = readOnly, Height = 23 });
        return sp;
    }
}

public class LogEntry
{
    public string Time { get; set; } = "";
    public string Level { get; set; } = "";
    public string Source { get; set; } = "";
    public string Plugin { get; set; } = "";
    public string Message { get; set; } = "";
}
