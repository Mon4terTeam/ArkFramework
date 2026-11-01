#include "process_watch.h"

#include <tlhelp32.h>
#include <wchar.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "console.h"
#include "scoped_handle.h"
#include "win32_utils.h"

namespace ark::loader {

namespace {

constexpr DWORD kPollIntervalMs = 25;

enum class ScanResult {
    NotFound,
    Selected,
    Failed,
};

// Records every existing PID without opening any of those processes. A PID that is
// reused during the watch is therefore conservatively never selected either.
bool capture_baseline(std::vector<DWORD>& baseline, LoadReport& report) {
    ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        report.fail("watch_baseline_snapshot_failed", GetLastError());
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        report.fail("watch_baseline_enumeration_failed", GetLastError());
        return false;
    }
    do {
        baseline.push_back(entry.th32ProcessID);
    } while (Process32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        report.fail("watch_baseline_enumeration_failed", GetLastError());
        return false;
    }
    return true;
}

// Decides whether one snapshot entry is the newly launched target.
ScanResult examine_candidate(const PROCESSENTRY32W& entry, const std::vector<DWORD>& baseline,
                             Target& target, const WatchOptions& options,
                             const PROCESS_INFORMATION& test_host, LoadReport& report) {
    const DWORD pid = entry.th32ProcessID;
    if (_wcsicmp(entry.szExeFile, target.image_name) != 0 ||
        std::find(baseline.begin(), baseline.end(), pid) != baseline.end()) {
        return ScanResult::NotFound;
    }
    if (options.self_test && pid != test_host.dwProcessId) {
        return ScanResult::NotFound;
    }

    ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!process.valid()) {
        const DWORD error = GetLastError();
        report.pid = pid;
        report.fail("watch_candidate_open_failed", error);
        return ScanResult::Failed;
    }
    const ULONGLONG created = process_creation_time(process.get());
    if (created == 0) {
        const DWORD error = GetLastError();
        report.pid = pid;
        report.fail("watch_creation_query_failed", error);
        return ScanResult::Failed;
    }
    // Only a launch that happened after arming is eligible.
    if (created <= report.watch_armed_time) {
        return ScanResult::NotFound;
    }
    if (options.self_test && created != process_creation_time(test_host.hProcess)) {
        report.fail("watch_test_identity_mismatch", ERROR_INVALID_PARAMETER);
        return ScanResult::Failed;
    }

    target.pid = pid;
    target.creation_time = created;
    report.pid = target.pid;
    report.creation_time = created;
    if (WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0) {
        report.fail("watch_target_exited", ERROR_PROCESS_ABORTED);
        return ScanResult::Failed;
    }
    report.stage = "watch_new_process_selected";
    return ScanResult::Selected;
}

ScanResult scan_for_new_process(const std::vector<DWORD>& baseline, Target& target,
                                const WatchOptions& options, const PROCESS_INFORMATION& test_host,
                                LoadReport& report) {
    ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        report.fail("watch_snapshot_failed", GetLastError());
        return ScanResult::Failed;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        report.fail("watch_enumeration_failed", GetLastError());
        return ScanResult::Failed;
    }
    do {
        const ScanResult result =
            examine_candidate(entry, baseline, target, options, test_host, report);
        if (result != ScanResult::NotFound) {
            return result;
        }
    } while (Process32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        report.fail("watch_enumeration_failed", GetLastError());
        return ScanResult::Failed;
    }
    return ScanResult::NotFound;
}

void announce_armed(const Target& target, const WatchOptions& options) {
    if (options.unbounded) {
        print_status(L"正在等待明日方舟启动（不限时）。请正常启动游戏；关闭本窗口即可退出。");
        return;
    }
    std::printf("Startup watcher armed; only a new %ls process is eligible. Launch the game "
                "yourself.\n",
                target.image_name);
    std::fflush(stdout);
}

} // namespace

bool create_test_host(const std::filesystem::path& root, PROCESS_INFORMATION& child,
                      LoadReport& report) {
    const auto image = root / kTestHostImageName;
    std::wstring command = L"\"" + image.wstring() + L"\" --await-loader --auto-close";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       root.c_str(), &startup, &child)) {
        return true;
    }
    report.fail("create_test_host", GetLastError());
    return false;
}

bool watch_for_new_process(Target& target, const std::filesystem::path& root,
                           const WatchOptions& options, PROCESS_INFORMATION& test_host,
                           LoadReport& report) {
    std::vector<DWORD> baseline;
    if (!capture_baseline(baseline, report)) {
        return false;
    }
    FILETIME armed{};
    GetSystemTimePreciseAsFileTime(&armed);
    report.watch_armed_time = filetime_to_u64(armed);
    announce_armed(target, options);
    if (options.self_test && !create_test_host(root, test_host, report)) {
        return false;
    }

    while (options.unbounded || remaining_ms(options.deadline)) {
        switch (scan_for_new_process(baseline, target, options, test_host, report)) {
        case ScanResult::Selected:
            return true;
        case ScanResult::Failed:
            return false;
        case ScanResult::NotFound:
            break;
        }
        if (options.self_test && WaitForSingleObject(test_host.hProcess, 0) == WAIT_OBJECT_0) {
            report.stage = "watch_test_host_exited_before_selection";
            GetExitCodeProcess(test_host.hProcess, &report.error);
            return false;
        }
        Sleep(options.unbounded ? kPollIntervalMs
                                : std::min<DWORD>(kPollIntervalMs, remaining_ms(options.deadline)));
    }
    report.fail("watch_deadline_expired", WAIT_TIMEOUT);
    return false;
}

} // namespace ark::loader
