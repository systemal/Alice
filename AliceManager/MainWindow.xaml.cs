using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
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
    private readonly AliceClient _client;
    private readonly ModManager _modManager;
    private readonly AppSettings _settings;
    private readonly ObservableCollection<LogEntry> _logs = [];
    private readonly CollectionViewSource _modsViewSource = new();
    private readonly List<LogEntry> _logBuffer = [];
    private readonly object _logLock = new();
    private System.Windows.Threading.DispatcherTimer? _logTimer;
    private bool _realExit;

    public MainWindow()
    {
        InitializeComponent();

        _settings = AppSettings.Load();
        _client = new AliceClient(_settings.Host, _settings.Port);

        var baseDir = AppDomain.CurrentDomain.BaseDirectory;
        _modManager = new ModManager(baseDir);
        _modManager.OnLog += (level, message) =>
        {
            lock (_logLock)
            {
                _logBuffer.Add(new LogEntry { Time = DateTime.Now.ToString("HH:mm:ss"), Level = level, Source = "ModManager", Message = message });
            }
        };
        _modManager.RequestReload = async (id) => await _client.ReloadPluginAsync(id);
        _modManager.RequestUnload = async (id) => await _client.UnloadPluginAsync(id);

        _modsViewSource.Source = _modManager.Mods;
        ModsGrid.ItemsSource = _modsViewSource.View;
        LogGrid.ItemsSource = _logs;

        DashServerAddr.Text = $"{_settings.Host}:{_settings.Port}";
    }

    private async void Window_Loaded(object sender, RoutedEventArgs e)
    {
        MainTab.SelectedIndex = 1;
        UpdateSettingsContent();
        InitTrayIcon();

        _modManager.Scan();
        foreach (var mod in _modManager.Mods) WatchModEnabled(mod);
        _modManager.Mods.CollectionChanged += (_, args) =>
        {
            if (args.NewItems != null)
                foreach (ModInfo mod in args.NewItems) WatchModEnabled(mod);
        };
        RefreshDashboard();

        // 事件绑定
        _client.OnStateInit += (plugins, services) => Dispatcher.BeginInvoke(() =>
        {
            _handlingToggle = true;
            var loadedIds = new HashSet<string>(plugins.Select(p => p.Id));
            foreach (var mod in _modManager.Mods)
            {
                if (loadedIds.Contains(mod.Id))
                {
                    mod.Status = ModStatus.Loaded;
                    mod.Enabled = true;
                }
                else if (mod.Status == ModStatus.Loaded)
                {
                    // Server 没报告这个插件 = 没加载 = 保持当前状态但不覆盖 Disabled
                }
            }
            DashServiceCount.Text = services.Count.ToString();
            RefreshDashboard();
            _handlingToggle = false;
        });

        _client.OnConnectionChanged += connected => Dispatcher.BeginInvoke(() =>
        {
            SetConnectionStatus(connected);
        });

        _client.OnPluginUnloaded += id => Dispatcher.BeginInvoke(() =>
        {
            var mod = _modManager.Mods.FirstOrDefault(m => m.Id == id);
            if (mod != null)
            {
                mod.Status = mod.Enabled ? ModStatus.Unknown : ModStatus.Disabled;
            }
        });

        _client.OnLog += (level, source, plugin, message) =>
        {
            lock (_logLock)
            {
                _logBuffer.Add(new LogEntry
                {
                    Time = DateTime.Now.ToString("HH:mm:ss"),
                    Level = level, Source = source,
                    Plugin = plugin, Message = message
                });
            }
        };

        // 日志定时刷新 (100ms 批量, 不堵塞 UI) — 必须存字段防 GC
        _logTimer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
        _logTimer.Tick += (_, _) => FlushLogBuffer();
        _logTimer.Start();

        // 启动连接
        if (_settings.AutoConnect)
            await ConnectWithLoadingAsync();
        else
            LoadingBar.Visibility = Visibility.Collapsed;
    }

    // ── 连接 (带加载动画) ──

    private async Task ConnectWithLoadingAsync()
    {
        LoadingBar.Visibility = Visibility.Visible;
        LoadingText.Text = "Connecting to Server...";

        if (!await _client.ConnectAsync(maxRetries: 3, retryDelayMs: 300))
        {
            if (!AliceClient.IsServerRunning())
            {
                LoadingText.Text = "Starting Alice Server...";
                if (_client.TryLaunchServer())
                {
                    LoadingText.Text = "Waiting for Server...";
                    await _client.ConnectAsync(maxRetries: 20, retryDelayMs: 500);
                }
            }
        }

        LoadingBar.Visibility = Visibility.Collapsed;
    }

    // ── 状态更新 ──

    private void SetConnectionStatus(bool connected)
    {
        StatusDotColor.Color = connected ? Color.FromRgb(0x4C, 0xAF, 0x50) : Color.FromRgb(0xF4, 0x43, 0x36);
        StatusText.Text = connected ? "Connected" : "Disconnected";
        DashServerStatus.Text = connected ? "Online" : "Offline";
        DashServerStatus.Foreground = new SolidColorBrush(
            connected ? Color.FromRgb(0x4C, 0xAF, 0x50) : Color.FromRgb(0xF4, 0x43, 0x36));
    }

    private void RefreshDashboard()
    {
        DashModCount.Text = _modManager.Mods.Count.ToString();
    }

    private void TrimLogs()
    {
        while (_logs.Count > 2000) _logs.RemoveAt(0);
    }

    private void FlushLogBuffer()
    {
        List<LogEntry> batch;
        lock (_logLock)
        {
            if (_logBuffer.Count == 0) return;
            batch = [.. _logBuffer];
            _logBuffer.Clear();
        }

        foreach (var entry in batch)
            _logs.Add(entry);

        TrimLogs();

        if (ChkAutoScroll?.IsChecked == true && LogGrid.Items.Count > 0)
            LogGrid.ScrollIntoView(LogGrid.Items[^1]);
    }

    // ── 托盘 ──

    private void InitTrayIcon()
    {
        try
        {
            var uri = new Uri("pack://application:,,,/Assets/logo.jpg");
            var stream = Application.GetResourceStream(uri)?.Stream;
            if (stream != null)
            {
                using var bmp = new System.Drawing.Bitmap(stream);
                TrayIcon.Icon = System.Drawing.Icon.FromHandle(bmp.GetHicon());
                Icon = new System.Windows.Media.Imaging.BitmapImage(uri);
            }
        }
        catch { }
    }

    protected override async void OnClosing(System.ComponentModel.CancelEventArgs e)
    {
        if (!_realExit && _settings.MinimizeToTray)
        {
            e.Cancel = true;
            Hide();
            return;
        }

        if (_closing) return;
        _closing = true;
        e.Cancel = true;

        Show(); WindowState = WindowState.Normal;
        BusyOverlay.Visibility = Visibility.Visible;
        BusyText.Text = "Shutting down...";

        await Task.Run(() =>
        {
            _client.ShutdownServer();
        });

        TrayIcon.Dispose();
        _client.Dispose();
        Application.Current.Shutdown();
    }
    private bool _closing;

    private void TrayIcon_DoubleClick(object sender, RoutedEventArgs e)
    { Show(); WindowState = WindowState.Normal; Activate(); }

    private void TrayMenu_Show(object sender, RoutedEventArgs e)
    { Show(); WindowState = WindowState.Normal; Activate(); }

    private void TrayMenu_Hide(object sender, RoutedEventArgs e) => Hide();

    private async void TrayMenu_RestartServer(object sender, RoutedEventArgs e)
    {
        Show(); WindowState = WindowState.Normal; Activate();
        BusyOverlay.Visibility = Visibility.Visible;
        BusyText.Text = "Restarting Server...";

        await Task.Run(() =>
        {
            _client.SendShutdownCommand();
            _client.WaitForServerExit(5000);
            _client.ForceKillServer();
        });

        _client.Reset();
        _modManager.Scan();
        RefreshDashboard();
        BusyOverlay.Visibility = Visibility.Collapsed;

        await ConnectWithLoadingAsync();
    }

    private async void TrayMenu_Exit(object sender, RoutedEventArgs e)
    {
        Show(); WindowState = WindowState.Normal;
        _realExit = true;

        if (_closing) return;
        _closing = true;

        BusyOverlay.Visibility = Visibility.Visible;
        BusyText.Text = "Shutting down...";

        await Task.Run(() =>
        {
            _client.ShutdownServer();
        });

        TrayIcon.Dispose();
        _client.Dispose();
        Application.Current.Shutdown();
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

    private void ModsGrid_CurrentCellChanged(object sender, EventArgs e)
    {
        if (sender is DataGrid dg) dg.BeginEdit();
    }

    private void SearchTextBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        var filter = SearchTextBox.Text.Trim().ToLowerInvariant();
        _modsViewSource.View.Filter = string.IsNullOrEmpty(filter) ? null : obj =>
        {
            if (obj is ModInfo mod)
                return mod.Name.Contains(filter, StringComparison.OrdinalIgnoreCase)
                    || mod.Id.Contains(filter, StringComparison.OrdinalIgnoreCase)
                    || mod.Type.Contains(filter, StringComparison.OrdinalIgnoreCase);
            return false;
        };
    }

    private bool _handlingToggle;

    private void WatchModEnabled(ModInfo mod)
    {
        mod.PropertyChanged += async (s, args) =>
        {
            if (args.PropertyName != "Enabled" || _handlingToggle) return;
            if (s is not ModInfo m) return;

            _handlingToggle = true;

            if (m.Enabled)
            {
                await Task.Run(async () => await _client.ReloadPluginAsync(m.Id));
                m.Status = ModStatus.Loaded;
            }
            else
            {
                await Task.Run(async () => await _client.UnloadPluginAsync(m.Id));
                m.Status = ModStatus.Disabled;
            }

            _handlingToggle = false;
        };
    }

    private async void UpdateAll_Click(object sender, RoutedEventArgs e)
    {
        ProgressIndicator.IsActive = true;
        var (updated, failed, upToDate) = await _modManager.UpdateAllAsync();
        ProgressIndicator.IsActive = false;
        _logs.Add(new LogEntry
        {
            Time = DateTime.Now.ToString("HH:mm:ss"), Level = "info", Source = "ModManager",
            Message = $"Update All: {updated} updated, {failed} failed, {upToDate} up-to-date"
        });
    }

    private async void CompileAll_Click(object sender, RoutedEventArgs e)
    {
        ProgressIndicator.IsActive = true;
        var (ok, fail) = await _modManager.CompileAllAsync();
        ProgressIndicator.IsActive = false;
        _logs.Add(new LogEntry
        {
            Time = DateTime.Now.ToString("HH:mm:ss"), Level = "info", Source = "ModManager",
            Message = $"Compile All: {ok} success, {fail} failed"
        });
    }

    private void InstallButton_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new Views.InstallModWindow(_modManager) { Owner = this };
        dlg.ShowDialog();
        _modManager.Scan();
        RefreshDashboard();
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
        if (ModsGrid.SelectedItem is ModInfo mod) _modManager.OpenSourceFolder(mod.Id);
    }

    private void MenuOpenRuntime_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod) _modManager.OpenRuntimeFolder(mod.Id);
    }

    private void MenuGithub_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod) _modManager.OpenGitHub(mod.Id);
    }

    private async void MenuRemove_Click(object sender, RoutedEventArgs e)
    {
        if (ModsGrid.SelectedItem is ModInfo mod)
        {
            var result = await this.ShowMessageAsync("Confirm Remove",
                $"Remove mod '{mod.Name}'?\nThis will delete source and runtime files.",
                MessageDialogStyle.AffirmativeAndNegative);
            if (result == MessageDialogResult.Affirmative)
            {
                await _modManager.RemoveAsync(mod.Id);
                RefreshDashboard();
            }
        }
    }

    private void ClearLog_Click(object sender, RoutedEventArgs e) => _logs.Clear();

    private void OpenModsFolder_Click(object sender, RoutedEventArgs e)
    {
        var modsDir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "mods");
        if (Directory.Exists(modsDir))
            Process.Start("explorer", modsDir);
    }

    // ── Settings (持久化) ──

    private void SettingsTree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        => UpdateSettingsContent();

    private void UpdateSettingsContent()
    {
        if (SettingsFrame is null || SettingsTree is null || _settings is null) return;
        var header = (SettingsTree.SelectedItem as TreeViewItem)?.Header?.ToString() ?? "General";
        SettingsFrame.Content = header switch
        {
            "General" => BuildGeneralSettings(),
            "Server" => BuildServerSettings(),
            "Paths" => BuildPathSettings(),
            _ => null
        };
    }

    private UIElement BuildGeneralSettings()
    {
        var panel = new StackPanel();
        panel.Children.Add(MakeToggle("Start with Windows", _settings.StartWithWindows, v => { _settings.StartWithWindows = v; _settings.Save(); AppSettings.SetStartWithWindows(v); }));
        panel.Children.Add(MakeToggle("Minimize to tray on close", _settings.MinimizeToTray, v => { _settings.MinimizeToTray = v; _settings.Save(); }));
        panel.Children.Add(MakeToggle("Auto-connect on startup", _settings.AutoConnect, v => { _settings.AutoConnect = v; _settings.Save(); }));
        panel.Children.Add(MakeToggle("Watch mods folder", _settings.WatchModsFolder, v => { _settings.WatchModsFolder = v; _settings.Save(); }));
        return panel;
    }

    private UIElement BuildServerSettings()
    {
        var panel = new StackPanel();
        panel.Children.Add(MakeField("Host", _settings.Host, v => { _settings.Host = v; _settings.Save(); }));
        panel.Children.Add(MakeField("Port", _settings.Port.ToString(), v => { if (int.TryParse(v, out var p)) { _settings.Port = p; _settings.Save(); } }));

        var btn = new Button { Content = "Test Connection", Width = 120, Height = 25, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 8, 0, 0) };
        btn.Click += async (_, _) =>
        {
            btn.Content = "Testing...";
            btn.IsEnabled = false;
            try
            {
                using var http = new System.Net.Http.HttpClient { Timeout = TimeSpan.FromSeconds(3) };
                var resp = await http.GetAsync($"http://{_settings.Host}:{_settings.Port}/api/ping");
                btn.Content = resp.IsSuccessStatusCode ? "Connected!" : "Failed";
            }
            catch { btn.Content = "Failed"; }
            await Task.Delay(1500);
            btn.Content = "Test Connection";
            btn.IsEnabled = true;
        };
        panel.Children.Add(btn);
        return panel;
    }

    private UIElement BuildPathSettings()
    {
        var baseDir = AppDomain.CurrentDomain.BaseDirectory;
        var panel = new StackPanel();
        panel.Children.Add(MakeField("Mods Directory", Path.Combine(baseDir, "mods"), readOnly: true));
        panel.Children.Add(MakeField("Mods Source", Path.Combine(baseDir, "mods-src"), readOnly: true));
        panel.Children.Add(MakeField("SDK Directory", Path.Combine(baseDir, "sdk"), readOnly: true));
        return panel;
    }

    private static UIElement MakeToggle(string label, bool isChecked, Action<bool>? onChanged = null)
    {
        var cb = new CheckBox { Content = label, IsChecked = isChecked, Margin = new Thickness(0, 0, 0, 8) };
        if (onChanged != null)
            cb.Checked += (_, _) => onChanged(true);
            cb.Unchecked += (_, _) => onChanged!(false);
        return cb;
    }

    private static UIElement MakeField(string label, string value, Action<string>? onChanged = null, bool readOnly = false)
    {
        var sp = new StackPanel { Margin = new Thickness(0, 0, 0, 10) };
        sp.Children.Add(new TextBlock { Text = label, FontSize = 12, Margin = new Thickness(0, 0, 0, 4) });
        var tb = new TextBox { Text = value, IsReadOnly = readOnly, Height = 23 };
        if (onChanged != null)
            tb.LostFocus += (_, _) => onChanged(tb.Text);
        sp.Children.Add(tb);
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
