#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <string>

namespace ark::crash_log {

namespace detail {

inline const wchar_t* log_path() {
    static const std::wstring path = [] {
        wchar_t base[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        std::wstring directory;
        if (length > 0 && length < MAX_PATH) {
            directory.assign(base, length);
        } else {
            wchar_t temp[MAX_PATH]{};
            const DWORD temp_length = GetTempPathW(MAX_PATH, temp);
            if (temp_length > 0 && temp_length < MAX_PATH) {
                directory.assign(temp, temp_length);
            } else {
                directory = L".";
            }
        }
        directory += L"\\ArkFramework";
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory + L"\\native-crash.log";
    }();
    return path.c_str();
}

inline const char*& stage() {
    thread_local const char* value = "startup";
    return value;
}

inline void append(const char* text) {
    char line[4096]{};
    SYSTEMTIME time{};
    GetLocalTime(&time);
    const int length = std::snprintf(
        line, sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u [pid=%lu tid=%lu] [%s] %s\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()),
        stage() ? stage() : "unknown", text ? text : "");
    if (length <= 0) {
        return;
    }
    HANDLE file = CreateFileW(
        log_path(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
    CloseHandle(file);
}

inline void write_stack(const char* reason) {
    void* frames[48]{};
    const USHORT count = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    append(reason);
    for (USHORT index = 0; index < count; ++index) {
        HMODULE module = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(frames[index]), &module);
        wchar_t module_path[MAX_PATH]{};
        if (module) {
            GetModuleFileNameW(module, module_path, MAX_PATH);
        } else {
            std::wcscpy(module_path, L"<unknown>");
        }
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto address = reinterpret_cast<std::uintptr_t>(frames[index]);
        char line[1024]{};
        std::snprintf(
            line, sizeof(line), "  #%02u %p %ls+0x%llx", index,
            frames[index], module_path,
            static_cast<unsigned long long>(address - base));
        append(line);
    }
}

inline void on_terminate() {
    append("std::terminate called");
    try {
        std::exception_ptr current = std::current_exception();
        if (current) {
            std::rethrow_exception(current);
        }
    } catch (const std::exception& error) {
        append((std::string("uncaught exception: ") + error.what()).c_str());
    } catch (...) {
        append("uncaught exception: unknown type");
    }
    write_stack("terminate stack:");
    std::abort();
}

inline void on_invalid_parameter(
    const wchar_t*, const wchar_t*, const wchar_t*, unsigned int,
    std::uintptr_t) {
    append("invalid parameter handler called");
    write_stack("invalid parameter stack:");
}

inline LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info ? info->ExceptionRecord : nullptr;
    if (record) {
        char line[512]{};
        std::snprintf(
            line, sizeof(line),
            "unhandled SEH code=0x%08lx address=%p flags=0x%lx",
            static_cast<unsigned long>(record->ExceptionCode),
            record->ExceptionAddress,
            static_cast<unsigned long>(record->ExceptionFlags));
        append(line);
    } else {
        append("unhandled SEH without exception record");
    }
    write_stack("SEH stack:");
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace detail

inline void set_stage(const char* value) {
    detail::stage() = value ? value : "unknown";
}

inline void write(const char* text) {
    detail::append(text);
}

inline void writef(const char* format, ...) {
    char text[2048]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    detail::append(text);
}

inline void install(const char* module) {
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;
    writef("%s crash handlers installed, log=%ls", module,
           detail::log_path());
    std::set_terminate(&detail::on_terminate);
    SetUnhandledExceptionFilter(&detail::on_unhandled_exception);
    _set_invalid_parameter_handler(&detail::on_invalid_parameter);
}

}  // namespace ark::crash_log
