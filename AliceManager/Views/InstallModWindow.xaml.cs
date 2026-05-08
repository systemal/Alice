using System.Windows;
using AliceManager.Services;
using MahApps.Metro.Controls;
using Microsoft.Win32;

namespace AliceManager.Views;

public partial class InstallModWindow : MetroWindow
{
    private readonly ModManager _modManager;

    public InstallModWindow(ModManager modManager)
    {
        InitializeComponent();
        _modManager = modManager;
    }

    private void BrowseFolder_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFolderDialog { Title = "Select Mod Source Folder" };
        if (dlg.ShowDialog() == true)
            TxtLocalPath.Text = dlg.FolderName;
    }

    private void BrowseFile_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Title = "Select Mod File",
            Filter = "Mod files (*.dll;*.lua;*.mjs;*.js)|*.dll;*.lua;*.mjs;*.js|All files (*.*)|*.*"
        };
        if (dlg.ShowDialog() == true)
        {
            TxtFilePath.Text = dlg.FileName;
            if (string.IsNullOrEmpty(TxtModId.Text))
                TxtModId.Text = System.IO.Path.GetFileNameWithoutExtension(dlg.FileName).ToLowerInvariant();
        }
    }

    private async void Install_Click(object sender, RoutedEventArgs e)
    {
        BtnInstall.IsEnabled = false;
        TxtStatus.Text = "Installing...";

        try
        {
            var tabIndex = InstallTab.SelectedIndex;
            bool success;
            string message;

            switch (tabIndex)
            {
                case 0: // GitHub
                    var url = TxtGitUrl.Text.Trim();
                    if (string.IsNullOrEmpty(url))
                    {
                        TxtStatus.Text = "Please enter a URL";
                        BtnInstall.IsEnabled = true;
                        return;
                    }
                    TxtStatus.Text = "Cloning repository...";
                    (success, message) = await _modManager.InstallFromGitAsync(url);
                    break;

                case 1: // Local Folder
                    var path = TxtLocalPath.Text.Trim();
                    if (string.IsNullOrEmpty(path))
                    {
                        TxtStatus.Text = "Please select a folder";
                        BtnInstall.IsEnabled = true;
                        return;
                    }
                    TxtStatus.Text = "Installing from local...";
                    (success, message) = await _modManager.InstallFromLocalAsync(path);
                    break;

                case 2: // Pre-built
                    var file = TxtFilePath.Text.Trim();
                    var modId = TxtModId.Text.Trim();
                    if (string.IsNullOrEmpty(file) || string.IsNullOrEmpty(modId))
                    {
                        TxtStatus.Text = "Please select a file and enter Mod ID";
                        BtnInstall.IsEnabled = true;
                        return;
                    }
                    (success, message) = _modManager.InstallPreBuilt(file, modId);
                    break;

                default:
                    return;
            }

            if (success)
            {
                TxtStatus.Text = "Installed successfully!";
                await System.Threading.Tasks.Task.Delay(800);
                DialogResult = true;
                Close();
            }
            else
            {
                TxtStatus.Text = $"Failed: {message}";
                BtnInstall.IsEnabled = true;
            }
        }
        catch (Exception ex)
        {
            TxtStatus.Text = $"Error: {ex.Message}";
            BtnInstall.IsEnabled = true;
        }
    }

    private void Cancel_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }
}
