using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace OwnedDashboardDemo;

internal sealed class DashboardForm : Form
{
    private const string GamePath =
        @"E:\Games\ArkTest\Arknights\Arknights.exe";
    private const int GWLP_HWNDPARENT = -8;
    private const uint GW_OWNER = 4;
    private const uint MONITOR_DEFAULTTONEAREST = 2;
    private const uint SWP_NOSIZE = 0x0001;
    private const uint SWP_NOZORDER = 0x0004;
    private const uint SWP_NOACTIVATE = 0x0010;

    private readonly WebView2 _webView = new() { Dock = DockStyle.Fill };
    private readonly DemoOptions _options;
    private readonly System.Windows.Forms.Timer _gameWatch =
        new() { Interval = 1000 };
    private readonly System.Windows.Forms.Timer _rectWatch =
        new() { Interval = 1500 };
    private readonly string _html;
    private readonly SemaphoreSlim _layoutGate = new(1, 1);

    private IntPtr _game;
    private uint _gamePid;
    private bool _ownerSet;
    private bool _uiReady;
    private bool _closing;
    private bool _waitingPosted;
    private string _layout;
    private Rectangle _originalRect;
    private Rectangle _appliedRect;
    private int _originalClientWidth;
    private int _originalClientHeight;

    public DashboardForm(DemoOptions options)
    {
        _options = options;
        _layout = options.Layout;
        _html = File.ReadAllText(
            Path.Combine(AppContext.BaseDirectory, "demo.html"));

        Text = "BetterBattle · Owned Dashboard Demo";
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        Bounds = Screen.PrimaryScreen?.WorkingArea ??
                 new Rectangle(0, 0, 1280, 720);
        BackColor = Color.FromArgb(7, 9, 12);
        ShowInTaskbar = true;
        TopMost = false;
        AutoScaleMode = AutoScaleMode.Dpi;
        Controls.Add(_webView);

        Load += async (_, _) => await InitializeAsync();
        _gameWatch.Tick += (_, _) => EnsureGameAttached();
        _rectWatch.Tick += (_, _) => WatchGameRect();
    }

    private async Task InitializeAsync()
    {
        try
        {
            var dataDirectory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "BetterBattleDemo", "WebView2");
            Directory.CreateDirectory(dataDirectory);
            var environment = await CoreWebView2Environment.CreateAsync(
                null, dataDirectory, null);
            await _webView.EnsureCoreWebView2Async(environment);
            _webView.CoreWebView2.Settings.AreDevToolsEnabled = true;
            _webView.CoreWebView2.Settings.AreDefaultContextMenusEnabled = false;
            _webView.CoreWebView2.Settings.IsStatusBarEnabled = false;
            _webView.CoreWebView2.WebMessageReceived += OnWebMessageReceived;
            _webView.CoreWebView2.NavigationCompleted += (_, args) =>
            {
                _uiReady = args.IsSuccess;
                if (!_uiReady)
                {
                    return;
                }
                PostState(_game == IntPtr.Zero ? "waiting" : "attached",
                    _game == IntPtr.Zero
                        ? "等待 Arknights 窗口…"
                        : "已附着游戏窗口");
                PostLayout();
            };
            _webView.CoreWebView2.NavigateToString(_html);
            _gameWatch.Start();
            _rectWatch.Start();
            EnsureGameAttached();
        }
        catch (Exception error)
        {
            MessageBox.Show(this,
                "WebView2 初始化失败。\n\n" + error.Message,
                "Owned Dashboard Demo", MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }
    }

    private async void EnsureGameAttached()
    {
        if (_closing || _game != IntPtr.Zero)
        {
            return;
        }
        try
        {
            var process = FindGameProcess();
            if (process is null)
            {
                if (_uiReady && !_waitingPosted)
                {
                    _waitingPosted = true;
                    PostState("waiting", "等待 Arknights 窗口…");
                }
                return;
            }

            var hwnd = process.MainWindowHandle;
            if (hwnd == IntPtr.Zero || !IsWindow(hwnd))
            {
                return;
            }

            _game = hwnd;
            _gamePid = (uint)process.Id;
            _waitingPosted = false;
            GetWindowRect(hwnd, out var frame);
            GetClientRect(hwnd, out var client);
            _originalRect = frame.ToRectangle();
            _originalClientWidth = client.Width;
            _originalClientHeight = client.Height;

            MoveDashboardToGameMonitor(hwnd);
            var previous = SetWindowLongPtrW(
                hwnd, GWLP_HWNDPARENT, Handle);
            _ownerSet = GetWindow(hwnd, GW_OWNER) == Handle;
            Log($"attach pid={process.Id} hwnd=0x{hwnd.ToInt64():X} " +
                $"prevOwner=0x{previous.ToInt64():X} ownerOk={_ownerSet} " +
                $"original={_originalRect.Width}x{_originalRect.Height}");

            await ApplyLayoutAsync(_layout, force: true);
            if (IsWindow(hwnd))
            {
                SetForegroundWindow(hwnd);
            }
            PostState("attached", "已附着游戏窗口");
        }
        catch (Exception error)
        {
            Log("attach failed: " + error);
        }
    }

    private Process? FindGameProcess()
    {
        if (_options.GamePid is int pid)
        {
            try
            {
                var process = Process.GetProcessById(pid);
                process.Refresh();
                return process.MainWindowHandle != 0 ? process : null;
            }
            catch
            {
                return null;
            }
        }

        Process? best = null;
        long bestArea = 0;
        foreach (var process in Process.GetProcessesByName("Arknights"))
        {
            try
            {
                process.Refresh();
                var hwnd = process.MainWindowHandle;
                if (hwnd == 0 || !IsWindow(hwnd))
                {
                    continue;
                }
                var path = process.MainModule?.FileName;
                if (!string.IsNullOrEmpty(path) &&
                    !path.Equals(GamePath, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                if (!GetWindowRect(hwnd, out var rect))
                {
                    continue;
                }
                var area = (long)rect.Width * rect.Height;
                if (area > bestArea)
                {
                    bestArea = area;
                    best = process;
                }
            }
            catch
            {
            }
        }
        return best;
    }

    private void MoveDashboardToGameMonitor(IntPtr game)
    {
        var monitor = MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST);
        var info = new MONITORINFO
        {
            cbSize = Marshal.SizeOf<MONITORINFO>(),
        };
        if (GetMonitorInfoW(monitor, ref info))
        {
            Bounds = Rectangle.FromLTRB(
                info.rcWork.Left, info.rcWork.Top,
                info.rcWork.Right, info.rcWork.Bottom);
        }
    }

    private async Task ApplyLayoutAsync(string layout, bool force = false)
    {
        if (_game == IntPtr.Zero)
        {
            PostLayout();
            return;
        }
        if (!await _layoutGate.WaitAsync(0))
        {
            return;
        }
        try
        {
            layout = layout == "compact" ? "compact" : "frame";
            if (_options.NoResize && layout == "frame")
            {
                layout = "compact";
            }
            _layout = layout;

            var work = Screen.FromHandle(_game).WorkingArea;
            var scale = GetScale();
            GetWindowRect(_game, out var currentFrame);
            GetClientRect(_game, out var currentClient);
            var borderWidth = currentFrame.Width - currentClient.Width;
            var borderHeight = currentFrame.Height - currentClient.Height;

            var targetClientWidth = _originalClientWidth;
            var targetClientHeight = _originalClientHeight;
            if (layout == "frame")
            {
                targetClientWidth = 1600;
                targetClientHeight = 900;
            }

            var targetWidth = targetClientWidth + borderWidth;
            var targetHeight = targetClientHeight + borderHeight;
            int x;
            int y;

            if (layout == "frame")
            {
                var leftPanel = (int)Math.Round(220 * scale);
                var rightPanel = (int)Math.Round(390 * scale);
                var gap = (int)Math.Round(6 * scale);
                var margin = (int)Math.Round(12 * scale);
                x = work.Left + leftPanel + margin;
                y = work.Top + margin;

                var availableWidth =
                    work.Right - x - gap - rightPanel;
                if (targetWidth > availableWidth)
                {
                    targetWidth = Math.Max(900, availableWidth);
                    targetClientWidth = targetWidth - borderWidth;
                    targetHeight = targetClientHeight + borderHeight;
                }
                var bottomPanel = (int)Math.Round(250 * scale);
                var availableHeight =
                    work.Bottom - y - gap - bottomPanel;
                if (targetHeight > availableHeight)
                {
                    targetHeight = Math.Max(500, availableHeight);
                    targetClientHeight = targetHeight - borderHeight;
                }
            }
            else
            {
                x = work.Left;
                y = work.Top;
                targetWidth = _originalRect.Width;
                targetHeight = _originalRect.Height;
                targetClientWidth = _originalClientWidth;
                targetClientHeight = _originalClientHeight;
            }

            SetWindowPos(_game, IntPtr.Zero, x, y, targetWidth, targetHeight,
                SWP_NOZORDER | SWP_NOACTIVATE);
            await Task.Delay(300);

            GetClientRect(_game, out var afterClient);
            if (layout == "frame" &&
                (Math.Abs(afterClient.Width - targetClientWidth) > 8 ||
                 Math.Abs(afterClient.Height - targetClientHeight) > 8))
            {
                Log($"resize rejected: wanted {targetClientWidth}x" +
                    $"{targetClientHeight}, got {afterClient.Width}x" +
                    $"{afterClient.Height}; falling back to compact");
                _layout = "compact";
                SetWindowPos(_game, IntPtr.Zero, work.Left, work.Top,
                    _originalRect.Width, _originalRect.Height,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                await Task.Delay(250);
            }

            GetWindowRect(_game, out var finalFrame);
            _appliedRect = finalFrame.ToRectangle();
            Log($"layout={_layout} work={work.Width}x{work.Height} " +
                $"scale={scale:0.##} game={targetWidth}x{targetHeight} " +
                $"final={_appliedRect.Width}x{_appliedRect.Height} " +
                $"at {_appliedRect.Left},{_appliedRect.Top}");
            PostLayout();
            PostState("attached", "布局已更新 · " +
                (_layout == "frame" ? "环绕 1600×900" : "紧凑 1920×1080"));
        }
        catch (Exception error)
        {
            Log("layout failed: " + error);
        }
        finally
        {
            _layoutGate.Release();
        }
    }

    private void PostLayout()
    {
        if (!_uiReady || _webView.CoreWebView2 is null ||
            !IsHandleCreated)
        {
            return;
        }
        try
        {
            var scale = GetScale();
            var origin = PointToScreen(Point.Empty);
            int gameX;
            int gameY;
            int gameWidth;
            int gameHeight;
            if (_game != IntPtr.Zero && IsWindow(_game))
            {
                GetWindowRect(_game, out var rect);
                gameX = (int)Math.Round((rect.Left - origin.X) / scale);
                gameY = (int)Math.Round((rect.Top - origin.Y) / scale);
                gameWidth = (int)Math.Round(rect.Width / scale);
                gameHeight = (int)Math.Round(rect.Height / scale);
            }
            else
            {
                gameX = 220;
                gameY = 24;
                gameWidth = 1081;
                gameHeight = 637;
            }

            var payload = JsonSerializer.Serialize(new
            {
                type = "layout",
                layout = _game == IntPtr.Zero ? "frame" : _layout,
                attached = _game != IntPtr.Zero,
                game = new
                {
                    x = gameX,
                    y = gameY,
                    w = gameWidth,
                    h = gameHeight,
                },
                viewport = new
                {
                    w = Math.Max(1, (int)Math.Round(ClientSize.Width / scale)),
                    h = Math.Max(1, (int)Math.Round(ClientSize.Height / scale)),
                },
                scale,
            });
            _webView.CoreWebView2.PostWebMessageAsJson(payload);
        }
        catch (Exception error)
        {
            Log("post layout failed: " + error.Message);
        }
    }

    private void PostState(string state, string message)
    {
        if (!_uiReady || _webView.CoreWebView2 is null)
        {
            return;
        }
        try
        {
            _webView.CoreWebView2.PostWebMessageAsJson(
                JsonSerializer.Serialize(new { type = "state", state, message }));
        }
        catch
        {
        }
    }

    private async void OnWebMessageReceived(
        object? sender, CoreWebView2WebMessageReceivedEventArgs args)
    {
        try
        {
            using var document = JsonDocument.Parse(args.WebMessageAsJson);
            var root = document.RootElement;
            if (!root.TryGetProperty("type", out var type) ||
                type.GetString() != "action")
            {
                return;
            }
            var action = root.TryGetProperty("action", out var actionElement)
                ? actionElement.GetString()
                : null;
            switch (action)
            {
                case "layout-frame":
                    await ApplyLayoutAsync("frame");
                    break;
                case "layout-compact":
                    await ApplyLayoutAsync("compact");
                    break;
                case "resnap":
                    await ApplyLayoutAsync(_layout);
                    break;
                case "exit":
                    Close();
                    break;
            }
        }
        catch (Exception error)
        {
            Log("web action failed: " + error.Message);
        }
    }

    private async void WatchGameRect()
    {
        if (_closing || _game == IntPtr.Zero || _layout != "frame" ||
            !IsWindow(_game) || _appliedRect.Width <= 0)
        {
            return;
        }
        GetWindowRect(_game, out var rect);
        var current = rect.ToRectangle();
        if (Math.Abs(current.Width - _appliedRect.Width) <= 6 &&
            Math.Abs(current.Height - _appliedRect.Height) <= 6 &&
            Math.Abs(current.Left - _appliedRect.Left) <= 6 &&
            Math.Abs(current.Top - _appliedRect.Top) <= 6)
        {
            return;
        }
        Log($"game rect changed to {current.Width}x{current.Height} at " +
            $"{current.Left},{current.Top}; resnap");
        await ApplyLayoutAsync(_layout);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _closing = true;
        _gameWatch.Stop();
        _rectWatch.Stop();
        if (_game != IntPtr.Zero && IsWindow(_game))
        {
            if (_ownerSet)
            {
                SetWindowLongPtrW(_game, GWLP_HWNDPARENT, IntPtr.Zero);
            }
            SetWindowPos(_game, IntPtr.Zero,
                _originalRect.Left, _originalRect.Top,
                _originalRect.Width, _originalRect.Height,
                SWP_NOZORDER | SWP_NOACTIVATE);
            Log("detached and restored game window");
        }
        try
        {
            _webView.Dispose();
        }
        catch
        {
        }
        base.OnFormClosing(e);
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        if (_uiReady)
        {
            PostLayout();
        }
    }

    protected override void OnDpiChanged(DpiChangedEventArgs e)
    {
        base.OnDpiChanged(e);
        if (_uiReady)
        {
            PostLayout();
        }
    }

    private double GetScale()
    {
        var dpi = GetDpiForWindow(Handle);
        if (dpi == 0)
        {
            dpi = (uint)DeviceDpi;
        }
        return Math.Max(1.0, dpi / 96.0);
    }

    private static void Log(string text)
    {
        try
        {
            var path = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "BetterBattleDemo", "demo.log");
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.AppendAllText(path,
                DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") +
                " " + text + Environment.NewLine);
        }
        catch
        {
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;

        public int Width => Right - Left;
        public int Height => Bottom - Top;

        public Rectangle ToRectangle() =>
            Rectangle.FromLTRB(Left, Top, Right, Bottom);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MONITORINFO
    {
        public int cbSize;
        public RECT rcMonitor;
        public RECT rcWork;
        public uint dwFlags;
    }

    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW",
        SetLastError = true)]
    private static extern IntPtr SetWindowLongPtrW(
        IntPtr hWnd, int nIndex, IntPtr dwNewLong);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(IntPtr hWnd, uint uCmd);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(
        IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y,
        int cx, int cy, uint flags);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    private static extern bool GetClientRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern IntPtr MonitorFromWindow(
        IntPtr hWnd, uint flags);

    [DllImport("user32.dll", EntryPoint = "GetMonitorInfoW",
        CharSet = CharSet.Unicode)]
    private static extern bool GetMonitorInfoW(
        IntPtr monitor, ref MONITORINFO info);

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hWnd);
}
