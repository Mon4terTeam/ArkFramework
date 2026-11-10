using System.Windows.Forms;

namespace OwnedDashboardDemo;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        var options = DemoOptions.Parse(args);
        if (options.ShowHelp)
        {
            MessageBox.Show(DemoOptions.HelpText, "Owned Dashboard Demo",
                MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        using var mutex = new Mutex(true, @"Local\OwnedDashboardDemo.SingleInstance",
            out var created);
        if (!created)
        {
            MessageBox.Show("Demo 已经在运行。", "Owned Dashboard Demo");
            return;
        }

        try
        {
            Application.Run(new DashboardForm(options));
        }
        finally
        {
            mutex.ReleaseMutex();
        }
    }
}

internal sealed class DemoOptions
{
    public string Layout { get; init; } = "frame";
    public int? GamePid { get; init; }
    public bool NoResize { get; init; }
    public bool ShowHelp { get; init; }

    public static DemoOptions Parse(string[] args)
    {
        var layout = "frame";
        int? gamePid = null;
        var noResize = false;
        var help = false;
        for (var index = 0; index < args.Length; index++)
        {
            switch (args[index])
            {
                case "--layout" when index + 1 < args.Length:
                    layout = args[++index].Trim().ToLowerInvariant();
                    break;
                case "--game-pid" when index + 1 < args.Length &&
                                       int.TryParse(args[++index], out var pid):
                    gamePid = pid;
                    break;
                case "--no-resize":
                    noResize = true;
                    break;
                case "--help":
                case "-h":
                    help = true;
                    break;
            }
        }
        if (layout is not ("frame" or "compact"))
        {
            layout = "frame";
        }
        return new DemoOptions
        {
            Layout = layout,
            GamePid = gamePid,
            NoResize = noResize,
            ShowHelp = help,
        };
    }

    public const string HelpText =
        "Owned Dashboard Demo\n\n" +
        "  --layout frame|compact   初始布局（默认 frame）\n" +
        "  --game-pid <pid>         指定 Arknights 进程\n" +
        "  --no-resize              不修改游戏窗口大小，强制紧凑布局\n" +
        "  --help                   显示本帮助\n\n" +
        "这是纯视觉 demo：面板使用静态数据，不连接插件、不发送战斗命令。";
}
