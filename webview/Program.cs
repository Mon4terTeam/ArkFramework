using System;
using System.Windows.Forms;

namespace BetterBattleWeb;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        string? fixturePath = null;
        int? fixtureIndex = null;
        int? fixtureFrame = null;
        var fixtureMode = false;
        for (var index = 0; index < args.Length; ++index)
        {
            if (args[index] == "--fixture" && index + 1 < args.Length)
            {
                fixtureMode = true;
                fixturePath = args[++index];
            }
            else if (args[index] == "--fixture-index" && index + 1 < args.Length &&
                     int.TryParse(args[++index], out var parsedIndex))
            {
                fixtureIndex = parsedIndex;
            }
            else if (args[index] == "--fixture-frame" && index + 1 < args.Length &&
                     int.TryParse(args[++index], out var parsedFrame))
            {
                fixtureFrame = parsedFrame;
            }
        }

        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        Mutex? singleInstance = null;
        if (!fixtureMode)
        {
            singleInstance = new Mutex(true, @"Local\BetterBattleWeb.SingleInstance",
                out var createdNew);
            if (!createdNew)
            {
                return;
            }
        }

        try
        {
            Application.Run(new MainForm(fixturePath, fixtureIndex, fixtureFrame));
        }
        finally
        {
            if (singleInstance is not null)
            {
                singleInstance.ReleaseMutex();
                singleInstance.Dispose();
            }
        }
    }
}
