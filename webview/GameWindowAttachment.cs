using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows.Forms;
using Microsoft.Web.WebView2.WinForms;

namespace BetterBattleWeb;

internal sealed class GameWindowAttachment : IDisposable
{
    private const string GamePath =
        @"E:\Games\ArkTest\Arknights\Arknights.exe";
    private static readonly string RestoreStatePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "BetterBattleWeb", "gameview-restore.json");
    private const int GWL_STYLE = -16;
    private const int GWL_EXSTYLE = -20;
    private const int GWLP_HWNDPARENT = -8;
    private const uint GW_OWNER = 4;
    private const uint SWP_NOSIZE = 0x0001;
    private const uint SWP_NOMOVE = 0x0002;
    private const uint SWP_NOZORDER = 0x0004;
    private const uint SWP_NOACTIVATE = 0x0010;
    private const uint SWP_FRAMECHANGED = 0x0020;
    private const uint SWP_SHOWWINDOW = 0x0040;
    private const uint SWP_HIDEWINDOW = 0x0080;
    private const int SW_HIDE = 0;
    private const int SW_SHOWNORMAL = 1;
    private const int SW_SHOWMINIMIZED = 2;
    private const int SW_SHOWMAXIMIZED = 3;
    private const int SW_SHOW = 5;
    private const int SW_SHOWNOACTIVATE = 4;
    private const int SW_RESTORE = 9;
    private const uint RDW_INVALIDATE = 0x0001;
    private const uint RDW_ALLCHILDREN = 0x0080;
    private const uint RDW_UPDATENOW = 0x0100;
    private const uint RDW_FRAME = 0x0400;
    private const long WS_CAPTION = 0x00C00000L;
    private const long WS_SYSMENU = 0x00080000L;
    private const long WS_THICKFRAME = 0x00040000L;
    private const long WS_MINIMIZEBOX = 0x00020000L;
    private const long WS_MAXIMIZEBOX = 0x00010000L;

    private enum GameWindowState
    {
        Unavailable,
        Detached,
        Attaching,
        Attached,
        Hidden,
        Error,
    }

    private sealed class Capture
    {
        public IntPtr Hwnd { get; init; }
        public int Pid { get; init; }
        public IntPtr PreviousOwner { get; init; }
        public long Style { get; init; }
        public Rectangle Rect { get; init; }
        public int ShowCmd { get; init; }
        public bool Visible { get; init; }
    }

    private sealed class RestoreState
    {
        public long Hwnd { get; set; }
        public int Pid { get; set; }
        public long PreviousOwner { get; set; }
        public long Style { get; set; }
        public int Left { get; set; }
        public int Top { get; set; }
        public int Right { get; set; }
        public int Bottom { get; set; }
        public int ShowCmd { get; set; }
        public bool Visible { get; set; }
    }

    private readonly Form _dashboard;
    private readonly WebView2 _webView;
    private readonly Action<string> _log;
    private readonly Action<object> _post;
    private readonly System.Windows.Forms.Timer _watch =
        new() { Interval = 1000 };
    private readonly System.Windows.Forms.Timer _settle =
        new() { Interval = 120 };

    private GameWindowState _state = GameWindowState.Unavailable;
    private Capture? _capture;
    private IntPtr _game;
    private bool _attachWanted;
    private long _requestId;
    private int _generation;
    private int _settleGeneration;
    private int _settleTicks;
    private bool _settleFocusGame;
    private string _message = "";
    private bool _disposed;
    private bool _dashboardMinimized;
    private Rectangle _appliedFrame;
    private double _cssX;
    private double _cssY;
    private double _cssWidth;
    private double _cssHeight;
    private double _dpr = 1.5;

    public GameWindowAttachment(
        Form dashboard,
        WebView2 webView,
        Action<string> log,
        Action<object> post)
    {
        _dashboard = dashboard;
        _webView = webView;
        _log = log;
        _post = post;
        _watch.Tick += (_, _) => Watch();
        _settle.Tick += (_, _) => SettleTick();
        _dashboard.Resize += OnDashboardStateChanged;
        _dashboard.VisibleChanged += OnDashboardStateChanged;
    }

    private void OnDashboardStateChanged(object? sender, EventArgs e)
    {
        SyncDashboardState();
    }

    private void SyncDashboardState()
    {
        if (_disposed) return;
        if (_dashboard.WindowState == FormWindowState.Minimized)
        {
            _dashboardMinimized = true;
            _settle.Stop();
            if (_state is GameWindowState.Attached or GameWindowState.Attaching &&
                _game != IntPtr.Zero && IsWindow(_game))
            {
                ShowWindow(_game, SW_HIDE);
            }
            return;
        }
        if (!_dashboardMinimized) return;
        _dashboardMinimized = false;
        if (_state != GameWindowState.Attached ||
            _game == IntPtr.Zero || !IsWindow(_game))
        {
            return;
        }
        ApplyLayout();
        ShowWindow(_game,
            IsIconic(_game) ? SW_RESTORE : SW_SHOWNOACTIVATE);
        BeginSettle(focusGame: false);
        NudgeWebView();
    }

    private void NudgeWebView()
    {
        if (_webView.IsDisposed || !_webView.IsHandleCreated) return;
        var bounds = _webView.Bounds;
        if (bounds.Width <= 4 || bounds.Height <= 4) return;
        _webView.Bounds = new Rectangle(
            bounds.X, bounds.Y, bounds.Width - 1, bounds.Height);
        _webView.Bounds = bounds;
        _webView.Invalidate();
        _webView.Update();
    }

    public void Attach(
        double x, double y, double width, double height, double dpr,
        long requestId)
    {
        if (_disposed) return;
        _cssX = x;
        _cssY = y;
        _cssWidth = width;
        _cssHeight = height;
        _dpr = Math.Max(1.0, dpr);
        _requestId = Math.Max(_requestId, requestId);
        _attachWanted = true;

        if (_state == GameWindowState.Attached)
        {
            ApplyLayout();
            PostState();
            return;
        }
        if (_state == GameWindowState.Attaching)
        {
            if (ApplyLayout())
            {
                PromoteAttached(layoutApplied: true);
            }
            else
            {
                PostState();
            }
            return;
        }
        TryAttach();
    }

    public void UpdateLayout(
        double x, double y, double width, double height, double dpr,
        long requestId)
    {
        if (_disposed) return;
        if (requestId > 0 && requestId < _requestId) return;
        _cssX = x;
        _cssY = y;
        _cssWidth = width;
        _cssHeight = height;
        _dpr = Math.Max(1.0, dpr);
        _requestId = Math.Max(_requestId, requestId);

        if (_state == GameWindowState.Attached)
        {
            ApplyLayout();
        }
        else if (_state == GameWindowState.Attaching && ApplyLayout())
        {
            PromoteAttached(layoutApplied: true);
        }
    }

    public void Hide(long requestId)
    {
        if (_disposed) return;
        _requestId = Math.Max(_requestId, requestId);
        _attachWanted = false;
        _watch.Stop();
        _settle.Stop();
        if (_state is GameWindowState.Attached or GameWindowState.Attaching)
        {
            RestoreAndHide();
            return;
        }
        PostState();
    }

    public void DetachAndShow(long requestId)
    {
        if (_disposed) return;
        _requestId = Math.Max(_requestId, requestId);
        _attachWanted = false;
        _watch.Stop();
        _settle.Stop();
        if (_state is GameWindowState.Attached or GameWindowState.Attaching
            or GameWindowState.Hidden)
        {
            RestoreAndShow();
            return;
        }
        if (_capture is not null && _game != IntPtr.Zero && IsWindow(_game))
        {
            RestoreAndShow();
            return;
        }
        _state = GameWindowState.Unavailable;
        _message = "未吸附";
        PostState();
    }

    private void TryAttach()
    {
        if (_game == IntPtr.Zero || !IsWindow(_game))
        {
            _game = IntPtr.Zero;
            _capture = null;
        }

        if (_game == IntPtr.Zero)
        {
            var process = FindGameProcess();
            if (process is null)
            {
                SetState(GameWindowState.Unavailable, "等待 Arknights 窗口…");
                _watch.Start();
                return;
            }
            try
            {
                process.Refresh();
                var hwnd = FindGameWindow((uint)process.Id);
                if (hwnd == IntPtr.Zero || !IsWindow(hwnd))
                {
                    SetState(
                        GameWindowState.Unavailable,
                        "游戏进程已找到，等待主窗口…");
                    _watch.Start();
                    return;
                }
                _game = hwnd;
                _capture = LoadOrCapture(hwnd, process.Id);
            }
            catch (Exception error)
            {
                _log("gameview attach failed: " + error);
                SetState(GameWindowState.Error, error.Message);
                return;
            }
            finally
            {
                process.Dispose();
            }
        }

        if (_capture is null)
        {
            SetState(GameWindowState.Error, "无法保存窗口原始状态");
            return;
        }

        SaveRestoreState(_capture);

        if (GetWindow(_game, GW_OWNER) != _dashboard.Handle)
        {
            SetWindowLongPtrW(_game, GWLP_HWNDPARENT, _dashboard.Handle);
        }
        if (GetWindow(_game, GW_OWNER) != _dashboard.Handle)
        {
            SetState(GameWindowState.Error, "无法把游戏窗口设为仪表盘 owner");
            return;
        }

        SetState(GameWindowState.Attaching, "正在吸附游戏窗口");
        if (IsIconic(_game) || IsZoomed(_game))
        {
            ShowWindow(_game, SW_RESTORE);
        }
        EnsureCaptionlessStyle();
        if (_cssWidth > 1 && _cssHeight > 1)
        {
            if (ApplyLayout())
            {
                PromoteAttached(layoutApplied: true);
            }
            else
            {
                SetState(GameWindowState.Error, "游戏窗口布局失败");
            }
        }
        else
        {
            PostState();
        }
    }

    private void PromoteAttached(bool layoutApplied = false)
    {
        if (_game == IntPtr.Zero || _capture is null || !IsWindow(_game))
        {
            SetState(GameWindowState.Error, "游戏窗口已丢失");
            return;
        }
        if (!layoutApplied && !ApplyLayout())
        {
            SetState(GameWindowState.Error, "游戏窗口布局无效");
            return;
        }

        _state = GameWindowState.Attached;
        _message = "游戏窗口已吸附";
        _generation += 1;
        if (_dashboard.WindowState != FormWindowState.Minimized)
        {
            ShowWindow(
                _game, IsIconic(_game) ? SW_RESTORE : SW_SHOWNOACTIVATE);
        }
        _watch.Start();
        BeginSettle();
        PostState();
    }

    private void RestoreAndHide()
    {
        _attachWanted = false;
        _generation += 1;
        _settle.Stop();
        _watch.Stop();
        if (_game == IntPtr.Zero || !IsWindow(_game) || _capture is null)
        {
            _game = IntPtr.Zero;
            _capture = null;
            DeleteRestoreState();
            SetState(GameWindowState.Unavailable, "游戏窗口已关闭");
            return;
        }

        RestoreWindow(show: false, hide: true);
        _state = GameWindowState.Hidden;
        _message = "游戏窗口已隐藏";
        DeleteRestoreState();
        SetForegroundWindow(_dashboard.Handle);
        PostState();
    }

    private void RestoreAndShow()
    {
        _attachWanted = false;
        _generation += 1;
        _settle.Stop();
        _watch.Stop();
        if (_game == IntPtr.Zero || !IsWindow(_game) || _capture is null)
        {
            _game = IntPtr.Zero;
            _capture = null;
            DeleteRestoreState();
            SetState(GameWindowState.Unavailable, "游戏窗口已关闭");
            return;
        }

        RestoreWindow(show: true, hide: false);
        _state = GameWindowState.Detached;
        _message = "游戏窗口已解除吸附";
        _capture = null;
        _game = IntPtr.Zero;
        DeleteRestoreState();
        PostState();
    }

    private void RestoreWindow(bool show, bool hide)
    {
        if (_capture is null || _game == IntPtr.Zero || !IsWindow(_game))
        {
            return;
        }
        SetWindowLongPtrW(_game, GWLP_HWNDPARENT, _capture.PreviousOwner);
        SetWindowLongPtrW(_game, GWL_STYLE, new IntPtr(_capture.Style));
        if (show)
        {
            ShowWindow(_game, SW_RESTORE);
            if (_capture.ShowCmd == SW_SHOWMAXIMIZED)
            {
                ShowWindow(_game, SW_SHOWMAXIMIZED);
            }
            else
            {
                var frame = FrameForStyle(_game, _capture.Style);
                if (frame.Width < 200 || frame.Height < 200)
                {
                    frame = _capture.Rect;
                }
                SetWindowPos(
                    _game, IntPtr.Zero,
                    frame.Left, frame.Top,
                    frame.Width, frame.Height,
                    SWP_NOZORDER | SWP_NOACTIVATE |
                    SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                ShowWindow(_game, SW_SHOW);
            }
            SetForegroundWindow(_game);
        }

        if (hide)
        {
            ShowWindow(_game, SW_HIDE);
            SetWindowPos(
                _game, IntPtr.Zero, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_HIDEWINDOW);
        }
        GetWindowRect(_game, out var restored);
        _log($"gameview restore show={show} hide={hide} " +
             $"frame={restored.Width}x{restored.Height} " +
             $"at {restored.Left},{restored.Top} " +
             $"visible={IsWindowVisible(_game)}");
    }

    private Capture? LoadOrCapture(IntPtr hwnd, int pid)
    {
        if (_capture is not null &&
            _capture.Hwnd == hwnd && _capture.Pid == pid)
        {
            return _capture;
        }

        var saved = LoadRestoreState();
        if (saved is not null &&
            saved.Hwnd == hwnd.ToInt64() && saved.Pid == pid)
        {
            var previous = new IntPtr(saved.PreviousOwner);
            if (previous != IntPtr.Zero && !IsWindow(previous))
            {
                previous = IntPtr.Zero;
            }
            var savedRect = ClampRect(new RECT
            {
                Left = saved.Left,
                Top = saved.Top,
                Right = saved.Right,
                Bottom = saved.Bottom,
            });
            _log("gameview original state restored from file");
            return new Capture
            {
                Hwnd = hwnd,
                Pid = pid,
                PreviousOwner = previous,
                Style = saved.Style,
                Rect = savedRect.ToRectangle(),
                ShowCmd = saved.ShowCmd == SW_SHOWMAXIMIZED
                    ? SW_SHOWMAXIMIZED
                    : SW_SHOWNORMAL,
                Visible = saved.Visible,
            };
        }

        var style = GetWindowLongPtrW(hwnd, GWL_STYLE).ToInt64();
        GetWindowRect(hwnd, out var rect);
        var placement = new WINDOWPLACEMENT
        {
            length = Marshal.SizeOf<WINDOWPLACEMENT>(),
        };
        var hasPlacement = GetWindowPlacement(hwnd, ref placement);
        var showCmd = hasPlacement ? placement.showCmd : SW_SHOWNORMAL;
        if (showCmd != SW_SHOWMAXIMIZED &&
            (showCmd == SW_SHOWMINIMIZED || IsIconic(hwnd) ||
             rect.Left <= -30000 || rect.Top <= -30000 ||
             rect.Width < 200 || rect.Height < 200))
        {
            if (hasPlacement &&
                placement.rcNormalPosition.Width >= 200 &&
                placement.rcNormalPosition.Height >= 200)
            {
                rect = placement.rcNormalPosition;
            }
            showCmd = SW_SHOWNORMAL;
        }
        rect = ClampRect(rect);
        var visible = IsWindowVisible(hwnd);
        var previousOwner = GetWindow(hwnd, GW_OWNER);
        if (previousOwner != IntPtr.Zero && !IsWindow(previousOwner))
        {
            previousOwner = IntPtr.Zero;
        }

        if ((style & WS_CAPTION) == 0)
        {
            var repaired = style |
                WS_CAPTION | WS_SYSMENU |
                WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
            var frame = FrameForStyle(hwnd, repaired);
            rect = new RECT
            {
                Left = frame.Left,
                Top = frame.Top,
                Right = frame.Right,
                Bottom = frame.Bottom,
            };
            style = repaired;
            _log("gameview captionless leftover repaired");
        }

        var capture = new Capture
        {
            Hwnd = hwnd,
            Pid = pid,
            PreviousOwner = previousOwner,
            Style = style,
            Rect = rect.ToRectangle(),
            ShowCmd = showCmd,
            Visible = visible,
        };
        SaveRestoreState(capture);
        _log($"gameview capture hwnd=0x{hwnd.ToInt64():X} " +
             $"original={capture.Rect.Width}x{capture.Rect.Height} " +
             $"at {capture.Rect.Left},{capture.Rect.Top} " +
             $"showCmd={capture.ShowCmd}");
        return capture;
    }

    private void Watch()
    {
        if (_disposed) return;
        if (_state == GameWindowState.Unavailable && _attachWanted)
        {
            TryAttach();
            return;
        }
        if (_state != GameWindowState.Attached &&
            _state != GameWindowState.Attaching)
        {
            return;
        }
        if (_game == IntPtr.Zero || !IsWindow(_game))
        {
            HandleLost();
            return;
        }
        if (_dashboard.WindowState == FormWindowState.Minimized)
        {
            return;
        }

        var generation = _generation;
        if (GetWindow(_game, GW_OWNER) != _dashboard.Handle)
        {
            TryAttach();
            return;
        }
        EnsureCaptionlessStyle();
        if (generation != _generation) return;

        if (_state == GameWindowState.Attached)
        {
            if (!IsWindowVisible(_game))
            {
                ShowWindow(_game, SW_SHOWNOACTIVATE);
            }
            GetWindowRect(_game, out var frame);
            var current = frame.ToRectangle();
            if (_appliedFrame.Width <= 0 ||
                Math.Abs(current.Left - _appliedFrame.Left) > 8 ||
                Math.Abs(current.Top - _appliedFrame.Top) > 8 ||
                Math.Abs(current.Width - _appliedFrame.Width) > 8 ||
                Math.Abs(current.Height - _appliedFrame.Height) > 8)
            {
                ApplyLayout();
            }
        }
    }

    private void HandleLost()
    {
        _generation += 1;
        _settle.Stop();
        _watch.Stop();
        _game = IntPtr.Zero;
        _capture = null;
        DeleteRestoreState();
        SetState(GameWindowState.Unavailable, "游戏窗口已关闭");
        if (_attachWanted)
        {
            _watch.Start();
        }
    }

    private bool ApplyLayout()
    {
        if (_game == IntPtr.Zero || !IsWindow(_game) ||
            _cssWidth <= 1 || _cssHeight <= 1)
        {
            return false;
        }

        try
        {
            EnsureCaptionlessStyle();
            GetWindowRect(_game, out var frame);
            GetClientRect(_game, out var client);
            var borderWidth = frame.Width - client.Width;
            var borderHeight = frame.Height - client.Height;
            var clientPoint = new POINT { X = 0, Y = 0 };
            ClientToScreen(_game, ref clientPoint);
            var borderLeft = clientPoint.X - frame.Left;
            var borderTop = clientPoint.Y - frame.Top;

            var origin = _webView.PointToScreen(Point.Empty);
            var clientWidth = Math.Max(1600,
                (int)Math.Round(_cssWidth * _dpr));
            var clientHeight = Math.Max(900,
                (int)Math.Round(_cssHeight * _dpr));
            var targetX = origin.X + (int)Math.Round(_cssX * _dpr) - borderLeft;
            var targetY = origin.Y + (int)Math.Round(_cssY * _dpr) - borderTop;
            var targetWidth = clientWidth + borderWidth;
            var targetHeight = clientHeight + borderHeight;

            SetWindowPos(
                _game,
                IntPtr.Zero,
                targetX,
                targetY,
                targetWidth,
                targetHeight,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            GetWindowRect(_game, out var finalFrame);
            GetClientRect(_game, out var finalClient);
            _appliedFrame = finalFrame.ToRectangle();
            _log($"gameview layout client={finalClient.Width}x{finalClient.Height} " +
                 $"frame={_appliedFrame.Width}x{_appliedFrame.Height} " +
                 $"at {_appliedFrame.Left},{_appliedFrame.Top} dpr={_dpr:0.##}");
            return true;
        }
        catch (Exception error)
        {
            _log("gameview layout failed: " + error);
            return false;
        }
    }

    private void EnsureCaptionlessStyle()
    {
        if (_game == IntPtr.Zero || !IsWindow(_game)) return;
        var style = GetWindowLongPtrW(_game, GWL_STYLE).ToInt64();
        var captionless = style &
            ~(WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
              WS_MAXIMIZEBOX | WS_THICKFRAME);
        if ((captionless & WS_CAPTION) == 0 && captionless != style)
        {
            SetWindowLongPtrW(_game, GWL_STYLE, new IntPtr(captionless));
            SetWindowPos(
                _game, IntPtr.Zero, 0, 0, 0, 0,
                SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_FRAMECHANGED);
            _log("gameview caption style removed");
        }
    }

    private void BeginSettle(bool focusGame = true)
    {
        if (_disposed || _game == IntPtr.Zero || !IsWindow(_game)) return;
        if (_dashboard.WindowState == FormWindowState.Minimized) return;
        _settleGeneration = _generation;
        _settleTicks = 12;
        _settleFocusGame = focusGame;
        _settle.Stop();
        _settle.Start();
        _log($"gameview present begin hwnd=0x{_game.ToInt64():X} " +
             $"visible={IsWindowVisible(_game)} iconic={IsIconic(_game)}");
        SettleTick();
    }

    private void SettleTick()
    {
        if (_state != GameWindowState.Attached ||
            _generation != _settleGeneration ||
            _game == IntPtr.Zero || !IsWindow(_game))
        {
            _settle.Stop();
            return;
        }
        if (_dashboard.WindowState == FormWindowState.Minimized)
        {
            return;
        }

        try
        {
            if (_settleTicks == 12)
            {
                ShowWindow(_game,
                    IsIconic(_game) ? SW_RESTORE : SW_SHOWNOACTIVATE);
            }
            if (_settleFocusGame &&
                (_settleTicks == 11 || _settleTicks == 9))
            {
                SetForegroundWindow(_game);
            }
            if (_settleTicks == 10 || _settleTicks == 4)
            {
                NudgeSwapchain();
            }
            RedrawWindow(
                _game, IntPtr.Zero, IntPtr.Zero,
                RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
            DwmFlush();
        }
        catch (Exception error)
        {
            _log("gameview present failed: " + error.Message);
            _settle.Stop();
            return;
        }

        if (--_settleTicks <= 0)
        {
            _settle.Stop();
            _log($"gameview present done visible={IsWindowVisible(_game)}");
        }
    }

    private void NudgeSwapchain()
    {
        if (_game == IntPtr.Zero || !GetWindowRect(_game, out var frame))
        {
            return;
        }
        SetWindowPos(
            _game, IntPtr.Zero,
            frame.Left, frame.Top, frame.Width, frame.Height + 1,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowPos(
            _game, IntPtr.Zero,
            frame.Left, frame.Top, frame.Width, frame.Height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    private void SetState(GameWindowState state, string message)
    {
        _state = state;
        _message = message;
        PostState();
    }

    private void PostState()
    {
        var ownerOk = _game != IntPtr.Zero && IsWindow(_game) &&
            GetWindow(_game, GW_OWNER) == _dashboard.Handle;
        var visible = _game != IntPtr.Zero && IsWindow(_game) &&
            IsWindowVisible(_game);
        _post(new
        {
            type = "gameview-state",
            state = StateName(_state),
            requestId = _requestId,
            ownerOk,
            visible,
            message = _message,
        });
    }

    private static string StateName(GameWindowState state) =>
        state switch
        {
            GameWindowState.Unavailable => "unavailable",
            GameWindowState.Detached => "detached",
            GameWindowState.Attaching => "attaching",
            GameWindowState.Attached => "attached",
            GameWindowState.Hidden => "hidden",
            _ => "error",
        };

    private Process? FindGameProcess()
    {
        Process? best = null;
        long bestArea = 0;
        foreach (var process in Process.GetProcessesByName("Arknights"))
        {
            try
            {
                process.Refresh();
                var path = process.MainModule?.FileName;
                if (string.IsNullOrWhiteSpace(path) ||
                    !path.Equals(GamePath, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                var hwnd = FindGameWindow((uint)process.Id);
                if (hwnd == IntPtr.Zero || !IsWindow(hwnd)) continue;
                if (!GetWindowRect(hwnd, out var rect)) continue;
                var area = (long)rect.Width * rect.Height;
                if (area > bestArea)
                {
                    best?.Dispose();
                    best = process;
                    bestArea = area;
                    continue;
                }
            }
            catch (Exception error)
            {
                _log("gameview process skipped: " + error.Message);
            }
            process.Dispose();
        }
        return best;
    }

    private static IntPtr FindGameWindow(uint pid)
    {
        var best = IntPtr.Zero;
        long bestArea = 0;
        var fallback = IntPtr.Zero;
        long fallbackArea = 0;
        var hidden = IntPtr.Zero;
        long hiddenArea = 0;
        EnumWindows((hwnd, _) =>
        {
            GetWindowThreadProcessId(hwnd, out var windowPid);
            if (windowPid != pid) return true;
            var title = new System.Text.StringBuilder(128);
            var className = new System.Text.StringBuilder(128);
            GetWindowTextW(hwnd, title, title.Capacity);
            GetClassNameW(hwnd, className, className.Capacity);
            if (!string.Equals(className.ToString(), "UnityWndClass",
                    StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(title.ToString(), "明日方舟",
                    StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
            if (!GetWindowRect(hwnd, out var rect)) return true;
            var area = (long)rect.Width * rect.Height;
            if (!IsWindowVisible(hwnd))
            {
                if (rect.Width >= 640 && rect.Height >= 480 &&
                    area > hiddenArea)
                {
                    hidden = hwnd;
                    hiddenArea = area;
                }
                return true;
            }
            if (IsIconic(hwnd) ||
                (rect.Width >= 640 && rect.Height >= 480))
            {
                if (area > bestArea)
                {
                    best = hwnd;
                    bestArea = area;
                }
            }
            else if (rect.Width >= 100 && rect.Height >= 100)
            {
                if (area > fallbackArea)
                {
                    fallback = hwnd;
                    fallbackArea = area;
                }
            }
            return true;
        }, IntPtr.Zero);
        return best != IntPtr.Zero ? best
            : fallback != IntPtr.Zero ? fallback
            : hidden;
    }

    private static Rectangle FrameForStyle(IntPtr hwnd, long style)
    {
        GetClientRect(hwnd, out var client);
        var frame = new RECT
        {
            Left = 0,
            Top = 0,
            Right = client.Width,
            Bottom = client.Height,
        };
        AdjustWindowRectEx(
            ref frame, (int)style, false,
            (int)GetWindowLongPtrW(hwnd, GWL_EXSTYLE).ToInt64());
        var origin = new POINT { X = 0, Y = 0 };
        ClientToScreen(hwnd, ref origin);
        return Rectangle.FromLTRB(
            origin.X + frame.Left,
            origin.Y + frame.Top,
            origin.X + frame.Right,
            origin.Y + frame.Bottom);
    }

    private static RECT ClampRect(RECT rect)
    {
        var screen = SystemInformation.VirtualScreen;
        var width = rect.Width;
        var height = rect.Height;
        if (width < 200 || height < 200)
        {
            width = 1622;
            height = 956;
        }
        var left = rect.Left;
        var top = rect.Top;
        var offScreen =
            left < screen.Left - 50 ||
            top < screen.Top - 50 ||
            left > screen.Right - 100 ||
            top > screen.Bottom - 100 ||
            rect.Width < 200 || rect.Height < 200;
        if (offScreen)
        {
            var work = SystemInformation.WorkingArea;
            left = work.Left + Math.Max(0, (work.Width - width) / 2);
            top = work.Top + Math.Max(0, (work.Height - height) / 2);
        }
        return new RECT
        {
            Left = left,
            Top = top,
            Right = left + width,
            Bottom = top + height,
        };
    }

    private static RestoreState? LoadRestoreState()
    {
        try
        {
            if (!File.Exists(RestoreStatePath)) return null;
            return JsonSerializer.Deserialize<RestoreState>(
                File.ReadAllText(RestoreStatePath));
        }
        catch
        {
            return null;
        }
    }

    private static void SaveRestoreState(Capture capture)
    {
        try
        {
            Directory.CreateDirectory(
                Path.GetDirectoryName(RestoreStatePath)!);
            File.WriteAllText(
                RestoreStatePath,
                JsonSerializer.Serialize(new RestoreState
                {
                    Hwnd = capture.Hwnd.ToInt64(),
                    Pid = capture.Pid,
                    PreviousOwner = capture.PreviousOwner.ToInt64(),
                    Style = capture.Style,
                    Left = capture.Rect.Left,
                    Top = capture.Rect.Top,
                    Right = capture.Rect.Right,
                    Bottom = capture.Rect.Bottom,
                    ShowCmd = capture.ShowCmd,
                    Visible = capture.Visible,
                }));
        }
        catch
        {
        }
    }

    private static void DeleteRestoreState()
    {
        try
        {
            if (File.Exists(RestoreStatePath))
            {
                File.Delete(RestoreStatePath);
            }
        }
        catch
        {
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _attachWanted = false;
        _dashboard.Resize -= OnDashboardStateChanged;
        _dashboard.VisibleChanged -= OnDashboardStateChanged;
        _watch.Stop();
        _watch.Dispose();
        _settle.Stop();
        _settle.Dispose();
        if (_capture is not null &&
            _game != IntPtr.Zero && IsWindow(_game))
        {
            RestoreWindow(show: true, hide: false);
            DeleteRestoreState();
        }
        _game = IntPtr.Zero;
        _capture = null;
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
    private struct POINT
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct WINDOWPLACEMENT
    {
        public int Length;
        public int Flags;
        public int ShowCmd;
        public POINT MinPosition;
        public POINT MaxPosition;
        public RECT NormalPosition;

        public int length
        {
            get => Length;
            set => Length = value;
        }

        public int showCmd
        {
            get => ShowCmd;
            set => ShowCmd = value;
        }

        public RECT rcNormalPosition
        {
            get => NormalPosition;
            set => NormalPosition = value;
        }
    }

    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW",
        SetLastError = true)]
    private static extern IntPtr SetWindowLongPtrW(
        IntPtr hWnd, int nIndex, IntPtr dwNewLong);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW",
        SetLastError = true)]
    private static extern IntPtr GetWindowLongPtrW(
        IntPtr hWnd, int nIndex);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(IntPtr hWnd, uint uCmd);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(
        IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y,
        int cx, int cy, uint flags);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(
        IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    private static extern bool GetClientRect(
        IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    private static extern bool AdjustWindowRectEx(
        ref RECT rect, int style, bool menu, int exStyle);

    [DllImport("user32.dll")]
    private static extern bool ClientToScreen(
        IntPtr hWnd, ref POINT point);

    [DllImport("user32.dll")]
    private static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    private static extern bool IsIconic(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool IsZoomed(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool GetWindowPlacement(
        IntPtr hWnd, ref WINDOWPLACEMENT placement);

    [DllImport("user32.dll")]
    private static extern bool RedrawWindow(
        IntPtr hWnd, IntPtr updateRect, IntPtr updateRgn, uint flags);

    [DllImport("dwmapi.dll")]
    private static extern int DwmFlush();

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(
        EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(
        IntPtr hWnd, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(
        IntPtr hWnd, System.Text.StringBuilder text, int maxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassNameW(
        IntPtr hWnd, System.Text.StringBuilder text, int maxCount);
}
