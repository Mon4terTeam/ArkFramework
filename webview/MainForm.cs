using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace BetterBattleWeb;

internal sealed class MainForm : Form
{
    private const string PipeName = "ArkFramework.WebView";
    private const int MaxCommandLength = 16384;
    private readonly WebView2 _webView = new() { Dock = DockStyle.Fill };
    private readonly CancellationTokenSource _cts = new();
    private readonly ConcurrentQueue<string> _commands = new();
    private readonly ConcurrentQueue<AssetRequest> _assetRequests = new();
    private readonly HashSet<string> _assetQueued = new(StringComparer.OrdinalIgnoreCase);
    private readonly object _assetGate = new();
    private readonly ConcurrentDictionary<string, string> _lastTopicJson = new();
    private volatile bool _uiReady;
    private int _artLoadLogged;
    private readonly string? _fixturePath;
    private readonly int? _fixtureStartIndex;
    private readonly int? _fixtureStartFrame;
    private string? _fixtureDirectory;
    private JsonDocument? _fixture;
    private readonly List<FixtureStep> _fixtureSteps = new();
    private int _fixtureIndex = -1;
    private readonly Dictionary<string, string> _timelineFiles =
        new(StringComparer.OrdinalIgnoreCase);
    private readonly string _requestPath = Path.GetFullPath(
        Path.Combine(AppContext.BaseDirectory, "..", "BetterBattle.request"));
    private readonly string _artCacheDirectory;
    private readonly string _logPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "BetterBattleWeb", "host.log");

    private sealed class FixtureStep
    {
        public int Index { get; init; }
        public int Frame { get; init; }
        public double Seconds { get; init; }
        public string Label { get; init; } = "";
        public int Units { get; init; }
        public int Buffs { get; init; }
        public int ActiveSkills { get; init; }
        public int State { get; init; }
        public string File { get; init; } = "";
    }

    private sealed record AssetRequest(string Kind, string Id, string Key);

    public MainForm(string? fixturePath = null, int? fixtureIndex = null,
        int? fixtureFrame = null)
    {
        _artCacheDirectory = Path.Combine(
            Path.GetDirectoryName(_requestPath) ?? AppContext.BaseDirectory,
            "BetterBattleWeb", "art_cache");
        _fixturePath = fixturePath;
        _fixtureStartIndex = fixtureIndex;
        _fixtureStartFrame = fixtureFrame;
        Text = "BetterBattle · WebView";
        Width = 1420;
        Height = 900;
        MinimumSize = new System.Drawing.Size(980, 640);
        StartPosition = FormStartPosition.CenterScreen;
        WindowState = FormWindowState.Maximized;
        AutoScaleMode = AutoScaleMode.Dpi;
        BackColor = System.Drawing.Color.FromArgb(12, 15, 22);
        Controls.Add(_webView);
        Load += async (_, _) => await InitializeWebViewAsync();
        FormClosing += (_, _) => _cts.Cancel();
    }

    private async Task InitializeWebViewAsync()
    {
        try
        {
            var dataDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "BetterBattleWeb", "WebView2");
            Directory.CreateDirectory(dataDir);
            var environment = await CoreWebView2Environment.CreateAsync(null, dataDir, null);
            await _webView.EnsureCoreWebView2Async(environment);
            _webView.CoreWebView2.Settings.AreDevToolsEnabled = true;
            _webView.CoreWebView2.Settings.AreDefaultContextMenusEnabled = true;
            _webView.CoreWebView2.Settings.IsStatusBarEnabled = false;
            _webView.CoreWebView2.WebMessageReceived += OnWebMessageReceived;
            _webView.CoreWebView2.WebResourceResponseReceived += (_, args) =>
            {
                try
                {
                    var uri = args.Request.Uri;
                    if (!uri.Contains("/art_cache/", StringComparison.OrdinalIgnoreCase))
                    {
                        return;
                    }
                    var status = args.Response.StatusCode;
                    if (status >= 400)
                    {
                        Log($"asset web failure {status}: {uri}");
                    }
                    else if (_artLoadLogged < 3)
                    {
                        ++_artLoadLogged;
                        Log($"asset web loaded {status}: {Path.GetFileName(uri)}");
                    }
                }
                catch
                {
                }
            };
            _webView.CoreWebView2.NavigationCompleted += (_, args) =>
            {
                Log("UI navigation: " + (args.IsSuccess ? "ok" : args.WebErrorStatus.ToString()));
                if (!args.IsSuccess)
                {
                    return;
                }
                _uiReady = true;
                PostUiMessage(new { type = "requestPath", path = _requestPath });
                PostAssetCache();
                if (_fixture is not null)
                {
                    PostFixtureBootstrap();
                    return;
                }
                foreach (var json in _lastTopicJson.Values)
                {
                    PostJson(json);
                }
            };
            if (_fixturePath is not null && !LoadFixture(_fixturePath))
            {
                return;
            }
            _webView.CoreWebView2.SetVirtualHostNameToFolderMapping(
                "betterbattle.local", AppContext.BaseDirectory,
                CoreWebView2HostResourceAccessKind.Allow);
            _webView.CoreWebView2.Navigate(
                "https://betterbattle.local/ui/index.html");
            if (_fixture is null)
            {
                _ = Task.Run(() => PipeLoopAsync(_cts.Token));
                _ = Task.Run(() => CommandLoopAsync(_cts.Token));
            }
            PostUiMessage(new { type = "requestPath", path = _requestPath });
            Log(_fixture is null
                ? "WebView2 ready; requests=" + _requestPath
                : "WebView2 ready in fixture mode; fixture=" + _fixturePath);
        }
        catch (Exception error)
        {
            Log("WebView2 init failed: " + error);
            MessageBox.Show(this,
                "WebView2 初始化失败。请确认 Microsoft Edge WebView2 Runtime 已安装。\n\n" +
                error.Message,
                "BetterBattle WebView", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void OnWebMessageReceived(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
    {
        try
        {
            using var document = JsonDocument.Parse(e.WebMessageAsJson);
            var root = document.RootElement;
            var messageType = root.TryGetProperty("type", out var typeElement)
                ? typeElement.GetString() ?? ""
                : "";
            if (messageType == "log" &&
                root.TryGetProperty("message", out var logMessage))
            {
                Log("web: " + (logMessage.GetString() ?? ""));
                return;
            }
            if (messageType == "timeline-files")
            {
                var directory = root.TryGetProperty("directory", out var directoryElement)
                    ? directoryElement.GetString()
                    : null;
                PostTimelineCatalog(directory);
                return;
            }
            if (messageType == "pick-folder")
            {
                var initial = root.TryGetProperty("path", out var initialElement)
                    ? initialElement.GetString()
                    : null;
                var target = root.TryGetProperty("target", out var targetElement)
                    ? targetElement.GetString() ?? ""
                    : "";
                var start = string.IsNullOrWhiteSpace(initial)
                    ? DefaultRecordingDirectory()
                    : initial!;
                try
                {
                    if (Directory.Exists(start))
                    {
                        start = Path.GetFullPath(start);
                    }
                    using var dialog = new FolderBrowserDialog
                    {
                        Description = "选择录像目录",
                        SelectedPath = start,
                        ShowNewFolderButton = true,
                        UseDescriptionForTitle = true,
                    };
                    if (dialog.ShowDialog(this) == DialogResult.OK)
                    {
                        PostUiMessage(new
                        {
                            type = "path-picked",
                            target,
                            path = dialog.SelectedPath,
                        });
                    }
                }
                catch (Exception error)
                {
                    Log("pick folder failed: " + error);
                }
                return;
            }
            if (messageType == "timeline-load" &&
                root.TryGetProperty("id", out var timelineId))
            {
                PostTimelinePlan(timelineId.GetString() ?? "");
                return;
            }
            if (messageType == "asset-cache-scan")
            {
                PostAssetCache();
                return;
            }
            if (messageType == "asset-request")
            {
                var kind = root.TryGetProperty("kind", out var kindElement)
                    ? kindElement.GetString() ?? ""
                    : "";
                var id = root.TryGetProperty("id", out var idElement)
                    ? idElement.GetString() ?? ""
                    : "";
                EnqueueAsset(kind, id);
                return;
            }
            if (messageType == "asset-cache-all")
            {
                var total = 0;
                if (root.TryGetProperty("items", out var itemsElement) &&
                    itemsElement.ValueKind == JsonValueKind.Array)
                {
                    foreach (var item in itemsElement.EnumerateArray())
                    {
                        if (item.ValueKind != JsonValueKind.Object)
                        {
                            continue;
                        }
                        var kind = item.TryGetProperty("kind", out var kindElement)
                            ? kindElement.GetString() ?? ""
                            : "";
                        var id = item.TryGetProperty("id", out var idElement)
                            ? idElement.GetString() ?? ""
                            : "";
                        if (EnqueueAsset(kind, id))
                        {
                            total++;
                        }
                    }
                }
                PostUiMessage(new { type = "asset-batch", total });
                return;
            }
            if (messageType != "command" ||
                !root.TryGetProperty("line", out var lineElement))
            {
                return;
            }
            var line = lineElement.GetString()?.Trim() ?? "";
            if (line.Length == 0 || line.Length > MaxCommandLength ||
                line.Contains('\r') || line.Contains('\n'))
            {
                PostUiMessage(new { type = "command", state = "rejected", line });
                return;
            }
            if (_fixture is not null)
            {
                PostUiMessage(new
                {
                    type = "command",
                    state = "preview",
                    line,
                    message = "Fixture 模式：命令未写入游戏。"
                });
                return;
            }
            _commands.Enqueue(line);
            PostUiMessage(new { type = "command", state = "queued", line });
            Log("queued command: " + line);
        }
        catch (Exception error)
        {
            Log("bad web message: " + error.Message);
        }
    }

    private static bool IsAssetKind(string kind)
    {
        return kind is "avatar" or "portrait" or "skill" or "illust" or
            "enemy-icon";
    }

    private static string SanitizeCacheToken(string value)
    {
        var text = value ?? "";
        var builder = new StringBuilder(text.Length);
        foreach (var ch in text)
        {
            builder.Append(char.IsLetterOrDigit(ch) || ch is '_' or '-' or '.'
                ? ch
                : '_');
        }
        return builder.Length == 0 ? "unknown" : builder.ToString();
    }

    private string AssetFileName(string kind, string id)
    {
        return kind + "_" + SanitizeCacheToken(id) + ".png";
    }

    private string AssetPath(string kind, string id)
    {
        return Path.Combine(_artCacheDirectory, AssetFileName(kind, id));
    }

    private bool EnqueueAsset(string kind, string id)
    {
        kind = kind.Trim().ToLowerInvariant();
        id = id.Trim();
        if (!IsAssetKind(kind) || id.Length == 0 || id.Length > 256)
        {
            return false;
        }
        var key = kind + ":" + id;
        var path = AssetPath(kind, id);
        if (File.Exists(path))
        {
            Log("asset cached: " + key);
            PostAssetReady(kind, id, true, cached: true);
            return true;
        }
        lock (_assetGate)
        {
            if (!_assetQueued.Add(key))
            {
                return false;
            }
        }
        _assetRequests.Enqueue(new AssetRequest(kind, id, key));
        Log("asset queued: " + key);
        PostUiMessage(new { type = "asset-queued", kind, id, key });
        return true;
    }

    private void PostAssetReady(string kind, string id, bool ok,
        bool cached = false, string error = "")
    {
        var fileName = AssetFileName(kind, id);
        PostUiMessage(new
        {
            type = "asset-ready",
            kind,
            id,
            key = kind + ":" + id,
            ok,
            cached,
            url = ok ? "../art_cache/" + fileName : "",
            error,
        });
    }

    private void PostAssetCache()
    {
        try
        {
            Directory.CreateDirectory(_artCacheDirectory);
            var items = new List<object>();
            foreach (var metaPath in Directory.EnumerateFiles(
                         _artCacheDirectory, "*.json",
                         SearchOption.TopDirectoryOnly))
            {
                try
                {
                    using var document = JsonDocument.Parse(ReadSharedText(metaPath));
                    var root = document.RootElement;
                    var kind = root.TryGetProperty("kind", out var kindElement)
                        ? kindElement.GetString() ?? ""
                        : "";
                    var id = root.TryGetProperty("id", out var idElement)
                        ? idElement.GetString() ?? ""
                        : "";
                    var file = root.TryGetProperty("file", out var fileElement)
                        ? fileElement.GetString() ?? ""
                        : "";
                    if (!IsAssetKind(kind) || id.Length == 0 || file.Length == 0 ||
                        !File.Exists(Path.Combine(_artCacheDirectory, file)))
                    {
                        continue;
                    }
                    items.Add(new
                    {
                        kind,
                        id,
                        url = "../art_cache/" + file,
                    });
                }
                catch (Exception error)
                {
                    Log($"asset cache metadata skipped: {metaPath}: {error.Message}");
                }
            }
            PostUiMessage(new { type = "asset-cache", items });
            Log("asset cache scanned: " + items.Count);
        }
        catch (Exception error)
        {
            Log("asset cache scan failed: " + error);
            PostUiMessage(new
            {
                type = "asset-cache",
                items = Array.Empty<object>(),
                error = error.Message,
            });
        }
    }

    private string DefaultRecordingDirectory()
    {
        var requestDirectory =
            Path.GetDirectoryName(_requestPath) ?? AppContext.BaseDirectory;
        var directory = Path.Combine(requestDirectory, "recordings");
        Directory.CreateDirectory(directory);
        return directory;
    }

    private void PostTimelineCatalog(string? requestedDirectory = null)
    {
        try
        {
            _timelineFiles.Clear();
            var directory = string.IsNullOrWhiteSpace(requestedDirectory)
                ? DefaultRecordingDirectory()
                : Path.GetFullPath(requestedDirectory);
            if (File.Exists(directory))
            {
                directory = Path.GetDirectoryName(directory)
                    ?? DefaultRecordingDirectory();
            }
            Directory.CreateDirectory(directory);
            var candidates = Directory
                .EnumerateFiles(directory, "*.json", SearchOption.TopDirectoryOnly)
                .Select(path => (Path: path, Source: "录像目录"))
                .ToList();

            var files = new List<object>();
            foreach (var candidate in candidates
                         .GroupBy(item => Path.GetFullPath(item.Path),
                             StringComparer.OrdinalIgnoreCase)
                         .Select(group => group.First())
                         .OrderBy(item => item.Path, StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    var fullPath = Path.GetFullPath(candidate.Path);
                    var text = ReadSharedText(fullPath);
                    using var document = JsonDocument.Parse(text);
                    if (!TryReadRecordingMeta(document.RootElement, out var meta) ||
                        !TryCountOperations(document.RootElement, out var operations,
                            out var nodes))
                    {
                        continue;
                    }
                    var info = new FileInfo(fullPath);
                    _timelineFiles[fullPath] = fullPath;
                    files.Add(new
                    {
                        id = fullPath,
                        path = fullPath,
                        name = info.Name,
                        source = candidate.Source,
                        size = info.Length,
                        modified = new DateTimeOffset(info.LastWriteTimeUtc)
                            .ToUnixTimeMilliseconds(),
                        operations,
                        nodes,
                        stageId = meta.StageId,
                        stageCode = meta.StageCode,
                        stageName = meta.StageName,
                        levelId = meta.LevelId,
                        seed = meta.Seed,
                        rogue = meta.Rogue,
                        squadCount = meta.SquadCount,
                        modeType = meta.ModeType,
                        deterministic = meta.Deterministic,
                        isLive = info.Name.Equals(
                            "battle_operations.json", StringComparison.OrdinalIgnoreCase),
                    });
                }
                catch (Exception error)
                {
                    Log($"timeline candidate skipped: {candidate.Path}: {error.Message}");
                }
            }

            PostUiMessage(new
            {
                type = "timeline-files",
                requestPath = _requestPath,
                directory,
                files,
            });
        }
        catch (Exception error)
        {
            Log("timeline catalog failed: " + error);
            PostUiMessage(new
            {
                type = "timeline-files",
                directory = requestedDirectory ?? DefaultRecordingDirectory(),
                files = Array.Empty<object>(),
                error = error.Message,
            });
        }
    }

    private void PostTimelinePlan(string id)
    {
        try
        {
            if (!_timelineFiles.TryGetValue(id, out var path) || !File.Exists(path))
            {
                throw new FileNotFoundException("录像文件已不存在，请刷新列表。", id);
            }
            var text = ReadSharedText(path);
            using var document = JsonDocument.Parse(text);
            if (!TryReadRecordingMeta(document.RootElement, out var meta) ||
                !TryCountOperations(document.RootElement, out var operations,
                    out var nodes))
            {
                throw new InvalidDataException("文件不是操作录像格式。");
            }
            var info = new FileInfo(path);
            PostUiMessage(new
            {
                type = "timeline-plan",
                id = path,
                path,
                name = info.Name,
                source = "recordings",
                size = info.Length,
                modified = new DateTimeOffset(info.LastWriteTimeUtc)
                    .ToUnixTimeMilliseconds(),
                operations,
                nodes,
                stageId = meta.StageId,
                stageCode = meta.StageCode,
                stageName = meta.StageName,
                levelId = meta.LevelId,
                seed = meta.Seed,
                rogue = meta.Rogue,
                squadCount = meta.SquadCount,
                modeType = meta.ModeType,
                deterministic = meta.Deterministic,
                plan = document.RootElement.Clone(),
            });
        }
        catch (Exception error)
        {
            Log($"timeline plan failed: {id}: {error}");
            PostUiMessage(new
            {
                type = "timeline-plan",
                id,
                error = error.Message,
            });
        }
    }

    private static string ReadSharedText(string path)
    {
        using var stream = new FileStream(
            path, FileMode.Open, FileAccess.Read,
            FileShare.ReadWrite | FileShare.Delete);
        using var reader = new StreamReader(
            stream, Encoding.UTF8, detectEncodingFromByteOrderMarks: true);
        return reader.ReadToEnd();
    }

    private sealed record RecordingMetaInfo(
        string StageId,
        string StageCode,
        string StageName,
        string LevelId,
        int Seed,
        bool Rogue,
        int SquadCount,
        int ModeType,
        bool Deterministic);

    private static bool TryReadRecordingMeta(
        JsonElement root,
        out RecordingMetaInfo meta)
    {
        meta = new RecordingMetaInfo("", "", "", "", -1, false, 0, 0, false);
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty("schema", out var schema) ||
            schema.ValueKind != JsonValueKind.Number ||
            schema.GetInt32() != 1 ||
            !root.TryGetProperty("meta", out var metaElement) ||
            metaElement.ValueKind != JsonValueKind.Object ||
            !metaElement.TryGetProperty("stage_id", out var stageId) ||
            stageId.ValueKind != JsonValueKind.String ||
            !metaElement.TryGetProperty("seed", out var seed) ||
            seed.ValueKind != JsonValueKind.Number ||
            !metaElement.TryGetProperty("rogue", out var rogue) ||
            rogue.ValueKind is not (JsonValueKind.True or JsonValueKind.False) ||
            !metaElement.TryGetProperty("game_mode", out var gameMode) ||
            gameMode.ValueKind != JsonValueKind.Object ||
            !gameMode.TryGetProperty("mode_type", out var modeType) ||
            modeType.ValueKind != JsonValueKind.Number ||
            !gameMode.TryGetProperty("is_deterministic", out var deterministic) ||
            deterministic.ValueKind is not (JsonValueKind.True or JsonValueKind.False) ||
            !gameMode.TryGetProperty("allow_manual_tick", out var manualTick) ||
            manualTick.ValueKind is not (JsonValueKind.True or JsonValueKind.False) ||
            !gameMode.TryGetProperty("player_side", out var playerSide) ||
            playerSide.ValueKind != JsonValueKind.Number ||
            !metaElement.TryGetProperty("start", out var start) ||
            start.ValueKind != JsonValueKind.Object ||
            !start.TryGetProperty("squad", out var squad) ||
            squad.ValueKind != JsonValueKind.Array)
        {
            return false;
        }
        var levelId = metaElement.TryGetProperty("level_id", out var level) &&
                      level.ValueKind == JsonValueKind.String
            ? level.GetString() ?? ""
            : "";
        var stageCode = metaElement.TryGetProperty("stage_code", out var code) &&
                        code.ValueKind == JsonValueKind.String
            ? code.GetString() ?? ""
            : "";
        var stageName = metaElement.TryGetProperty("stage_name", out var name) &&
                        name.ValueKind == JsonValueKind.String
            ? name.GetString() ?? ""
            : "";
        meta = new RecordingMetaInfo(
            stageId.GetString() ?? "",
            stageCode,
            stageName,
            levelId,
            seed.GetInt32(),
            rogue.GetBoolean(),
            squad.GetArrayLength(),
            modeType.GetInt32(),
            deterministic.GetBoolean());
        return true;
    }

    private static bool TryCountOperations(
        JsonElement root, out int operations, out int nodeCount)
    {
        operations = 0;
        nodeCount = 0;
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty("tree", out var tree) ||
            tree.ValueKind != JsonValueKind.Object ||
            !tree.TryGetProperty("active_path", out var activePath) ||
            activePath.ValueKind != JsonValueKind.Array ||
            !tree.TryGetProperty("nodes", out var nodes) ||
            nodes.ValueKind != JsonValueKind.Array)
        {
            return false;
        }
        var active = new HashSet<string>(
            activePath.EnumerateArray()
                .Where(item => item.ValueKind == JsonValueKind.String)
                .Select(item => item.GetString() ?? ""),
            StringComparer.Ordinal);
        foreach (var node in nodes.EnumerateArray())
        {
            if (node.ValueKind != JsonValueKind.Object ||
                !node.TryGetProperty("id", out var idElement) ||
                idElement.ValueKind != JsonValueKind.String ||
                !node.TryGetProperty("operations", out var operationMap) ||
                operationMap.ValueKind != JsonValueKind.Object)
            {
                return false;
            }
            nodeCount++;
            if (!active.Contains(idElement.GetString() ?? ""))
            {
                continue;
            }
            foreach (var frameProperty in operationMap.EnumerateObject())
            {
                if (!int.TryParse(frameProperty.Name, out var frame) || frame < 0 ||
                    frameProperty.Value.ValueKind != JsonValueKind.Array)
                {
                    return false;
                }
                foreach (var operation in frameProperty.Value.EnumerateArray())
                {
                    if (operation.ValueKind != JsonValueKind.Array ||
                        operation.GetArrayLength() < 2)
                    {
                        return false;
                    }
                    var kind = operation[0].GetString();
                    if (kind is not ("deploy" or "skill" or "withdraw") ||
                        operation[1].ValueKind != JsonValueKind.String)
                    {
                        return false;
                    }
                    operations++;
                }
            }
        }
        return nodeCount > 0 && active.Count > 0;
    }

    private bool LoadFixture(string path)
    {
        try
        {
            var fullPath = Path.GetFullPath(path, AppContext.BaseDirectory);
            _fixture = JsonDocument.Parse(File.ReadAllText(fullPath));
            _fixtureDirectory = Path.GetDirectoryName(fullPath)
                ?? throw new InvalidOperationException("Fixture directory is missing.");
            var steps = _fixture.RootElement.GetProperty("snapshots");
            var index = 0;
            foreach (var step in steps.EnumerateArray())
            {
                var activeSkills = step.TryGetProperty("activeSkills", out var skills) &&
                                   skills.ValueKind == JsonValueKind.Array
                    ? skills.GetArrayLength()
                    : 0;
                _fixtureSteps.Add(new FixtureStep
                {
                    Index = index++,
                    Frame = step.GetProperty("frame").GetInt32(),
                    Seconds = step.GetProperty("seconds").GetDouble(),
                    Label = step.GetProperty("label").GetString() ?? "",
                    Units = step.GetProperty("units").GetInt32(),
                    Buffs = step.GetProperty("buffs").GetInt32(),
                    ActiveSkills = activeSkills,
                    State = step.TryGetProperty("state", out var state)
                        ? state.GetInt32()
                        : 2,
                    File = step.GetProperty("file").GetString() ?? ""
                });
            }
            if (_fixtureSteps.Count == 0)
            {
                throw new InvalidOperationException("Fixture has no snapshots.");
            }
            if (_fixtureStartIndex is int requestedIndex)
            {
                if (requestedIndex < 0 || requestedIndex >= _fixtureSteps.Count)
                {
                    throw new InvalidOperationException(
                        $"Fixture index {requestedIndex} is outside 0..{_fixtureSteps.Count - 1}.");
                }
                _fixtureIndex = requestedIndex;
            }
            else if (_fixtureStartFrame is int requestedFrame)
            {
                _fixtureIndex = _fixtureSteps
                    .OrderBy(step => Math.Abs(step.Frame - requestedFrame))
                    .ThenBy(step => step.Frame)
                    .Select(step => step.Index)
                    .First();
            }
            else
            {
                _fixtureIndex = _fixtureSteps
                    .Where(step => step.State == 2 && step.ActiveSkills > 0)
                    .OrderByDescending(step => step.ActiveSkills)
                    .ThenByDescending(step => step.Buffs)
                    .Select(step => step.Index)
                    .DefaultIfEmpty(_fixtureSteps[^1].Index)
                    .First();
            }
            Log($"fixture loaded: {_fixtureSteps.Count} snapshots; default={_fixtureIndex}");
            return true;
        }
        catch (Exception error)
        {
            Log("fixture load failed: " + error);
            MessageBox.Show(this,
                "Fixture 加载失败。\n\n" + error.Message,
                "BetterBattle WebView", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
    }

    private void PostFixtureBootstrap()
    {
        if (_fixture is null || _fixtureDirectory is null)
        {
            return;
        }
        var root = _fixture.RootElement;
        PostUiMessage(new { type = "host", connected = true });
        PostFixtureTopic("betterbattle.catalog",
            root.TryGetProperty("catalog", out var catalog) ? catalog.GetString() : null);
        PostFixtureTopic("betterbattle.profiles",
            root.TryGetProperty("profiles", out var profiles) ? profiles.GetString() : null);
        SelectFixture(_fixtureIndex);
    }

    private void PostFixtureTopic(string topic, string? relativePath)
    {
        if (_fixtureDirectory is null || string.IsNullOrWhiteSpace(relativePath))
        {
            return;
        }
        try
        {
            var path = Path.Combine(_fixtureDirectory, relativePath);
            var payload = File.ReadAllText(path);
            PostJson("{\"topic\":\"" + topic + "\",\"seq\":1,\"payload\":" + payload + "}");
        }
        catch (Exception error)
        {
            Log($"fixture topic {topic} failed: {error.Message}");
        }
    }

    private void SelectFixture(int index)
    {
        if (_fixtureDirectory is null || index < 0 || index >= _fixtureSteps.Count)
        {
            return;
        }
        _fixtureIndex = index;
        var step = _fixtureSteps[index];
        try
        {
            var path = Path.Combine(_fixtureDirectory, step.File);
            var payload = File.ReadAllText(path);
            PostJson("{\"topic\":\"betterbattle.snapshot\",\"seq\":" +
                     (index + 1) + ",\"fixture\":true,\"payload\":" + payload + "}");
            Log($"fixture snapshot selected: frame={step.Frame} index={index}");
        }
        catch (Exception error)
        {
            Log($"fixture snapshot {index} failed: {error.Message}");
        }
    }

    private async Task CommandLoopAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            if (_commands.TryDequeue(out var line))
            {
                await SendCommandLineAsync(line, token);
                continue;
            }
            if (_assetRequests.TryDequeue(out var assetRequest))
            {
                await ProcessAssetAsync(assetRequest, token);
                continue;
            }
            await DelayAsync(80, token);
        }
    }

    private async Task SendCommandLineAsync(string line, CancellationToken token)
    {
        try
        {
            await WriteRequestAsync(line, token);
            PostUiMessage(new { type = "command", state = "sent", line });
            Log("sent command: " + line);

            var waited = 0;
            while (!token.IsCancellationRequested && File.Exists(_requestPath) &&
                   waited < 10000)
            {
                await DelayAsync(10, token);
                waited += 10;
            }
            if (File.Exists(_requestPath))
            {
                PostUiMessage(new
                {
                    type = "command",
                    state = "waiting",
                    line,
                    message = "游戏插件尚未消费请求，后续命令保持排队。"
                });
                while (!token.IsCancellationRequested && File.Exists(_requestPath))
                {
                    await DelayAsync(250, token);
                }
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception error)
        {
            Log("command failed: " + error);
            PostUiMessage(new
            {
                type = "command",
                state = "failed",
                line,
                message = error.Message
            });
        }
    }

    private async Task ProcessAssetAsync(AssetRequest request, CancellationToken token)
    {
        var line = "asset " + request.Kind + " " + request.Id;
        try
        {
            await WriteRequestAsync(line, token);
            Log("sent asset command: " + line);

            var waited = 0;
            while (!token.IsCancellationRequested && File.Exists(_requestPath) &&
                   waited < 10000)
            {
                await DelayAsync(10, token);
                waited += 10;
            }
            if (File.Exists(_requestPath))
            {
                PostAssetReady(request.Kind, request.Id, false,
                    error: "游戏插件未消费请求");
                return;
            }

            var imagePath = AssetPath(request.Kind, request.Id);
            var metadataPath = Path.ChangeExtension(imagePath, ".json");
            var deadline = DateTime.UtcNow.AddSeconds(10);
            while (!token.IsCancellationRequested && DateTime.UtcNow < deadline)
            {
                if (File.Exists(imagePath) && File.Exists(metadataPath))
                {
                    Log("asset ready: " + request.Key);
                    PostAssetReady(request.Kind, request.Id, true);
                    return;
                }
                await DelayAsync(20, token);
            }
            PostAssetReady(request.Kind, request.Id, false, error: "导出超时");
            Log("asset timeout: " + request.Key);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception error)
        {
            Log("asset command failed: " + error);
            PostAssetReady(request.Kind, request.Id, false, error: error.Message);
        }
        finally
        {
            lock (_assetGate)
            {
                _assetQueued.Remove(request.Key);
            }
        }
    }

    private async Task WriteRequestAsync(string line, CancellationToken token)
    {
        while (!token.IsCancellationRequested && File.Exists(_requestPath))
        {
            await DelayAsync(10, token);
        }
        Directory.CreateDirectory(Path.GetDirectoryName(_requestPath)!);
        var temporary = _requestPath + "." + Guid.NewGuid().ToString("N") + ".tmp";
        await File.WriteAllTextAsync(temporary, line + "\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false), token);
        File.Move(temporary, _requestPath, overwrite: true);
    }

    private async Task PipeLoopAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            try
            {
                using var pipe = new NamedPipeClientStream(
                    ".", PipeName, PipeDirection.In, PipeOptions.Asynchronous);
                await pipe.ConnectAsync(1000, token);
                PostUiMessage(new { type = "host", connected = true });
                Log("pipe connected");
                using var reader = new BinaryReader(pipe, Encoding.UTF8, leaveOpen: true);
                while (!token.IsCancellationRequested && pipe.IsConnected)
                {
                    int length;
                    try
                    {
                        length = reader.ReadInt32();
                    }
                    catch (EndOfStreamException)
                    {
                        break;
                    }
                    if (length <= 0 || length > 16 * 1024 * 1024)
                    {
                        break;
                    }
                    var bytes = reader.ReadBytes(length);
                    if (bytes.Length != length)
                    {
                        break;
                    }
                    var json = Encoding.UTF8.GetString(bytes);
                    PostWebJson(json);
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (TimeoutException)
            {
            }
            catch (IOException)
            {
            }
            catch (Exception error)
            {
                Log("pipe error: " + error);
            }
            PostUiMessage(new { type = "host", connected = false });
            await DelayAsync(500, token);
        }
        PostUiMessage(new { type = "host", connected = false });
    }

    private void PostWebJson(string json)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            if (document.RootElement.TryGetProperty("topic", out var topic) &&
                topic.GetString() is { Length: > 0 } topicName)
            {
                _lastTopicJson[topicName] = json;
            }
        }
        catch (JsonException error)
        {
            Log("bad bridge frame: " + error.Message);
            return;
        }
        if (_uiReady)
        {
            PostJson(json);
        }
    }

    private void PostJson(string json)
    {
        if (IsDisposed || !IsHandleCreated)
        {
            return;
        }
        BeginInvoke(new Action(() =>
        {
            try
            {
                _webView.CoreWebView2?.PostWebMessageAsJson(json);
            }
            catch (Exception error)
            {
                Log("post failed: " + error.Message);
            }
        }));
    }

    private void PostUiMessage(object message)
    {
        if (IsDisposed || !IsHandleCreated)
        {
            return;
        }
        var json = JsonSerializer.Serialize(message);
        BeginInvoke(new Action(() =>
        {
            try
            {
                _webView.CoreWebView2?.PostWebMessageAsJson(json);
            }
            catch
            {
            }
        }));
    }

    private static async Task DelayAsync(int milliseconds, CancellationToken token)
    {
        try
        {
            await Task.Delay(milliseconds, token);
        }
        catch (OperationCanceledException)
        {
        }
    }

    private void Log(string text)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_logPath)!);
            File.AppendAllText(_logPath,
                DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + " " + text + Environment.NewLine);
        }
        catch
        {
        }
    }
}
