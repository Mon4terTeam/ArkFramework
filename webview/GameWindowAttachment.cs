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
    private const uint SWP_SHOWWINDOW = 0x0040;
    private const uint SWP_FRAMECHANGED = 0x0020;
    private const int SW_HIDE = 0;
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

    private readonly Form _dashboard;
    private readonly WebView2 _webView;
    private readonly Action<string> _log;
    private readonly Action<object> _post;
    private readonly System.Windows.Forms.Timer _watch =
        new() { Interval = 1000 };
    private readonly System.Windows.Forms.Timer _settle =
        new() { Interval = 120 };

    private bool _active;
    private bool _disposed;
    private bool _ownerSet;
    private bool _captureGame;
    private IntPtr _game;
    private int _gamePid;
    private IntPtr _previousOwner;
    private long _originalStyle;
    private Rectangle _originalRect;
    private int _originalClientWidth;
    private int _originalClientHeight;
    private bool _originalVisible;
    private int _settleTicks;
    private Rectangle _appliedFrame;
    private double _cssX;
    private double _cssY;
    private double _cssWidth;
    private double _cssHeight;
    private double _dpr = 1.5;

    private sealed class RestoreState
    {
        public long Hwnd { get; set; }
        public int Pid { get; set; }
        public long Style { get; set; }
        public int Left { get; set; }
        public int Top { get; set; }
        public int Right { get; set; }
        public int Bottom { get; set; }
        public bool Visible { get; set; }
    }

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
    }

    public void Enter(double x, double y, double width, double height, double dpr)
    {
        if (_disposed) return;
        _active = true;
        _cssX = x;
        _cssY = y;
        _cssWidth = width;
        _cssHeight = height;
        _dpr = Math.Max(1.0, dpr);
        _watch.Start();
        AttachOrUpdate();
    }

    public void UpdateLayout(
        double x, double y, double width, double height, double dpr)
    {
        if (_disposed || !_active) return;
        _cssX = x;
        _cssY = y;
        _cssWidth = width;
        _cssHeight = height;
        _dpr = Math.Max(1.0, dpr);
        if (_game != IntPtr.Zero)
        {
            ApplyLayout();
        }
    }

    public void Leave()
    {
        _active = false;
        _watch.Stop();
        Detach(hideAfterRestore: true, keepHandle: true);
        _post(new
        {
            type = "gameview-state",
            state = "waiting",
            message = "已离开游戏视图",
        });
    }

    public void DetachAndShow()
    {
        _active = false;
        _watch.Stop();
        Detach(hideAfterRestore: false, keepHandle: true, forceShow: true);
        _post(new
        {
            type = "gameview-state",
            state = "detached",
            message = "已解除吸附",
        });
    }

    private void Watch()
    {
        if (!_active || _disposed) return;
        if (_game == IntPtr.Zero || !IsWindow(_game))
        {
            ResetGameHandle();
            AttachOrUpdate();
            return;
        }

        EnsureCaptionlessStyle();
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

    private void AttachOrUpdate()
    {
        if (!_active || _disposed) return;
        if (_game != IntPtr.Zero && IsWindow(_game))
        {
            if (!_ownerSet)
            {
                _previousOwner = GetWindow(_game, GW_OWNER);
                SetWindowLongPtrW(_game, GWLP_HWNDPARENT, _dashboard.Handle);
                _ownerSet = GetWindow(_game, GW_OWNER) == _dashboard.Handle;
            }
            if (!_ownerSet)
            {
                PostState("error", "无法把游戏窗口设为仪表盘 owner");
                return;
            }
            if (_captureGame)
            {
                SaveRestoreState(
                    _game, _gamePid, _originalStyle, _originalRect,
                    _originalVisible);
            }
            EnsureCaptionlessStyle();
            ApplyLayout();
            BeginSettle();
            PostState("attached", "游戏窗口已吸附");
            return;
        }

        var process = FindGameProcess();
        if (process is null)
        {
            PostState("waiting", "等待 Arknights 窗口…");
            return;
        }

        try
        {
            process.Refresh();
            var hwnd = FindGameWindow((uint)process.Id);
            if (hwnd == IntPtr.Zero || !IsWindow(hwnd))
            {
                PostState("waiting", "游戏进程已找到，等待主窗口…");
                return;
            }

            _game = hwnd;
            _gamePid = process.Id;
            _captureGame = true;
            _previousOwner = GetWindow(hwnd, GW_OWNER);
            _originalStyle = GetWindowLongPtrW(hwnd, GWL_STYLE).ToInt64();
            _originalVisible = IsWindowVisible(hwnd);
            GetWindowRect(hwnd, out var original);
            GetClientRect(hwnd, out var client);
            _originalRect = original.ToRectangle();
            _originalClientWidth = client.Width;
            _originalClientHeight = client.Height;
            _appliedFrame = Rectangle.Empty;

            var saved = LoadRestoreState();
            if (saved is not null && saved.Hwnd == hwnd.ToInt64() &&
                saved.Pid == process.Id)
            {
                _originalStyle = saved.Style;
                _originalRect = Rectangle.FromLTRB(
                    saved.Left, saved.Top, saved.Right, saved.Bottom);
                _originalVisible = saved.Visible;
                _log("gameview original state restored from file");
            }
            else if ((_originalStyle & WS_CAPTION) == 0)
            {
                var repaired = _originalStyle |
                    WS_CAPTION | WS_SYSMENU |
                    WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
                _originalRect = FrameForStyle(hwnd, repaired);
                _originalStyle = repaired;
                _log("gameview captionless leftover repaired");
            }
            SaveRestoreState(
                hwnd, process.Id, _originalStyle, _originalRect,
                _originalVisible);

            SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, _dashboard.Handle);
            _ownerSet = GetWindow(hwnd, GW_OWNER) == _dashboard.Handle;
            _log($"gameview attach pid={process.Id} hwnd=0x{hwnd.ToInt64():X} " +
                 $"ownerOk={_ownerSet} original={_originalRect.Width}x{_originalRect.Height}");
            if (!_ownerSet)
            {
                ResetGameHandle();
                PostState("error", "无法把游戏窗口设为仪表盘 owner");
                return;
            }

            EnsureCaptionlessStyle();
            ApplyLayout();
            BeginSettle();
            PostState("attached", "游戏窗口已吸附");
        }
        catch (Exception error)
        {
            _log("gameview attach failed: " + error);
            PostState("error", error.Message);
        }
        finally
        {
            process.Dispose();
        }
    }

    private void ApplyLayout()
    {
        if (_game == IntPtr.Zero || !IsWindow(_game) ||
            _cssWidth <= 1 || _cssHeight <= 1)
        {
            return;
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
        }
        catch (Exception error)
        {
            _log("gameview layout failed: " + error);
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

    private void BeginSettle()
    {
        if (_disposed || _game == IntPtr.Zero || !IsWindow(_game)) return;
        _settleTicks = 12;
        _settle.Stop();
        _settle.Start();
        _log($"gameview present begin hwnd=0x{_game.ToInt64():X} " +
             $"visible={IsWindowVisible(_game)} iconic={IsIconic(_game)}");
        SettleTick();
    }

    private void SettleTick()
    {
        if (_game == IntPtr.Zero || !IsWindow(_game))
        {
            _settle.Stop();
            return;
        }

        try
        {
            if (_settleTicks == 12)
            {
                ShowWindow(_game,
                    IsIconic(_game) ? SW_RESTORE : SW_SHOWNOACTIVATE);
            }
            if (_settleTicks == 11 || _settleTicks == 9)
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

    private void ResetGameHandle()
    {
        _game = IntPtr.Zero;
        _gamePid = 0;
        _ownerSet = false;
        _captureGame = false;
        _appliedFrame = Rectangle.Empty;
    }

    private void Detach(
        bool hideAfterRestore, bool keepHandle = false, bool forceShow = false)
    {
        if (_game == IntPtr.Zero || !IsWindow(_game))
        {
            ResetGameHandle();
            return;
        }

        try
        {
            if (_ownerSet)
            {
                SetWindowLongPtrW(_game, GWLP_HWNDPARENT, _previousOwner);
            }
            if (_captureGame)
            {
                SetWindowLongPtrW(_game, GWL_STYLE, new IntPtr(_originalStyle));
                SetWindowPos(
                    _game, IntPtr.Zero,
                    _originalRect.Left, _originalRect.Top,
                    _originalRect.Width, _originalRect.Height,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                DeleteRestoreState();
            }
            ShowWindow(_game,
                hideAfterRestore
                    ? SW_HIDE
                    : (forceShow || _originalVisible
                        ? SW_SHOWNOACTIVATE
                        : SW_HIDE));
            if (forceShow)
            {
                if (IsIconic(_game))
                {
                    ShowWindow(_game, SW_RESTORE);
                }
                ShowWindow(_game, SW_SHOW);
                GetWindowRect(_game, out var current);
                SetWindowPos(
                    _game, IntPtr.Zero,
                    current.Left, current.Top,
                    current.Width, current.Height,
                    SWP_SHOWWINDOW);
                SetForegroundWindow(_game);
                BeginSettle();
            }
            GetWindowRect(_game, out var restored);
            _log($"gameview detached and restored " +
                 $"frame={restored.Width}x{restored.Height} " +
                 $"at {restored.Left},{restored.Top} " +
                 $"visible={IsWindowVisible(_game)}");
        }
        catch (Exception error)
        {
            _log("gameview detach failed: " + error);
        }
        finally
        {
            _ownerSet = false;
            if (!keepHandle)
            {
                ResetGameHandle();
            }
        }
    }

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

    private static void SaveRestoreState(
        IntPtr hwnd, int pid, long style, Rectangle rect, bool visible)
    {
        try
        {
            Directory.CreateDirectory(
                Path.GetDirectoryName(RestoreStatePath)!);
            File.WriteAllText(
                RestoreStatePath,
                JsonSerializer.Serialize(new RestoreState
                {
                    Hwnd = hwnd.ToInt64(),
                    Pid = pid,
                    Style = style,
                    Left = rect.Left,
                    Top = rect.Top,
                    Right = rect.Right,
                    Bottom = rect.Bottom,
                    Visible = visible,
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

    private void PostState(string state, string message)
    {
        _post(new
        {
            type = "gameview-state",
            state,
            message,
        });
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _watch.Stop();
        _watch.Dispose();
        _settle.Stop();
        _settle.Dispose();
        Detach(hideAfterRestore: false, forceShow: true);
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
