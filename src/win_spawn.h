/*
 * win_spawn.h - Windows: start a helper with its stdin (and stdout) on our
 * pipes, for dof_pipe.c and dmd_pipe.c. Only the handles named here are
 * inherited: no other child, and no other helper, holds the game's pipe ends,
 * so a helper sees end of file when the game closes its end or dies.
 */
#pragma once
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* What Windows says about an error, on one line */
static const char *win_error(DWORD e)
{
    static char s[256];
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, s, sizeof s, NULL);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '.')) s[--n] = 0;
    if (!n) snprintf(s, sizeof s, "error %lu", (unsigned long)e);
    return s;
}

/* One argument onto a command line, as the C runtime will split it again */
static int win_arg(char *line, size_t cap, size_t *at, const char *a)
{
    size_t n = *at;
#define PUT(c) do { if (n + 1 >= cap) return 0; line[n++] = (c); } while (0)
    if (n) PUT(' ');
    PUT('"');
    for (const char *p = a;; p++) {
        size_t slashes = 0;
        while (*p == '\\') { p++; slashes++; }
        if (!*p) {                      /* before the closing quote: doubled */
            while (slashes--) { PUT('\\'); PUT('\\'); }
            break;
        }
        if (*p == '"') {                /* before a quote: doubled, and the quote escaped */
            while (slashes--) { PUT('\\'); PUT('\\'); }
            PUT('\\');
        } else {
            while (slashes--) PUT('\\');
        }
        PUT(*p);
    }
    PUT('"');
#undef PUT
    line[n] = 0;
    *at = n;
    return 1;
}

/* An inheritable copy of one of our standard handles, or NUL if there is none */
static HANDLE win_inheritable_std(DWORD which)
{
    HANDLE h = GetStdHandle(which), dup = NULL;
    if (h && h != INVALID_HANDLE_VALUE
        && DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return dup;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    dup = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    return dup == INVALID_HANDLE_VALUE ? NULL : dup;
}

/*
 * Run program with argv (argv[0] is its name), stdin from `in`, stdout to
 * `out` (NULL: the game's own), stderr the game's. `in` and `out` are made
 * inheritable here; the caller closes them after. 0 and *err on failure.
 */
static int win_spawn(const char *program, char *const argv[], HANDLE in, HANDLE out, HANDLE *proc, DWORD *err)
{
    static char line[4096];
    char path[MAX_PATH];
    size_t at = 0;
    *err = 0;
    snprintf(path, sizeof path, "%s", program);
    for (char *p = path; *p; p++) if (*p == '/') *p = '\\';
    for (int i = 0; argv[i]; i++)
        if (!win_arg(line, sizeof line, &at, i ? argv[i] : path)) { *err = ERROR_FILENAME_EXCED_RANGE; return 0; }

    HANDLE own_out = out ? NULL : win_inheritable_std(STD_OUTPUT_HANDLE);
    HANDLE errh = win_inheritable_std(STD_ERROR_HANDLE);
    if (!out) out = own_out;
    HANDLE list[3];
    int n = 0;
    SetHandleInformation(in, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    list[n++] = in;
    if (out && out != own_out) SetHandleInformation(out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (out) list[n++] = out;
    if (errh) list[n++] = errh;

    STARTUPINFOEXA si;
    PROCESS_INFORMATION pi;
    SIZE_T size = 0;
    int ok = 0;
    memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in;
    si.StartupInfo.hStdOutput = out;
    si.StartupInfo.hStdError = errh;
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, size);
    if (!si.lpAttributeList || !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size)) {
        *err = GetLastError();
    } else {
        if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list, n * sizeof *list, NULL, NULL))
            *err = GetLastError();
        else if (!CreateProcessA(path, line, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &si.StartupInfo, &pi))
            *err = GetLastError();
        else {
            CloseHandle(pi.hThread);
            *proc = pi.hProcess;
            ok = 1;
        }
        DeleteProcThreadAttributeList(si.lpAttributeList);
    }
    if (si.lpAttributeList) HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    if (own_out) CloseHandle(own_out);
    if (errh) CloseHandle(errh);
    return ok;
}

/* A pipe: our end not inherited, theirs made inheritable by win_spawn */
static int win_pipe(HANDLE *rd, HANDLE *wr, DWORD size)
{
    return CreatePipe(rd, wr, NULL, size) != 0;
}

/* Writes to our end of a pipe never wait: a full pipe takes what fits, or nothing */
static void win_nowait(HANDLE h)
{
    DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    SetNamedPipeHandleState(h, &mode, NULL, NULL);
}

static int win_file(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
#endif
