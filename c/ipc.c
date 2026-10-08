/* Pipe transport engine for the Alya ipc package.
 * Part of the Alya Language package ecosystem (https://github.com/alya-lang).
 *
 * Windows uses named pipes (\\.\pipe\alya-ipc-<name>); POSIX systems use
 * Unix domain stream sockets (<tmp>/alya-ipc-<name>.sock). Both expose the
 * same slot-based API so the Alya framing layer stays transport-agnostic.
 *
 * Return contract:
 * - listen/connect/accept return a slot index >= 0, or -1 on failure.
 * - send returns bytes sent, or -1 on failure.
 * - recv returns bytes read, 0 on orderly EOF, -2 on timeout, -1 on error.
 * - poll returns 1 when readable, 0 on timeout, -1 on error.
 * Failure details are available via alya_ipc_pipe_error().
 */

#include "ipc.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define ALYA_IPC_MAX_SLOTS 128
#define ALYA_IPC_NAME_MAX 32

typedef struct {
    int used;
    int is_server;
    char name[ALYA_IPC_NAME_MAX + 1];
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
    char path[160];
#endif
} alya_ipc_slot;

static alya_ipc_slot g_slots[ALYA_IPC_MAX_SLOTS];
static char g_last_error[256];

static void set_error(const char *msg) {
    size_t i = 0;
    if (msg == 0) {
        msg = "ipc: unknown pipe error";
    }
    while (msg[i] != '\0' && i + 1 < sizeof(g_last_error)) {
        g_last_error[i] = msg[i];
        i++;
    }
    g_last_error[i] = '\0';
}

const char *alya_ipc_pipe_error(void) {
    return g_last_error;
}

static int alloc_slot(void) {
    int i = 0;
    while (i < ALYA_IPC_MAX_SLOTS) {
        if (!g_slots[i].used) {
            g_slots[i].used = 1;
            g_slots[i].is_server = 0;
            g_slots[i].name[0] = '\0';
#ifdef _WIN32
            g_slots[i].handle = 0;
#else
            g_slots[i].fd = -1;
            g_slots[i].path[0] = '\0';
#endif
            return i;
        }
        i++;
    }
    set_error("ipc: no free pipe slots");
    return -1;
}

static void free_slot(int slot) {
    g_slots[slot].used = 0;
    g_slots[slot].is_server = 0;
}

/* Keeps [A-Za-z0-9_-], truncates to ALYA_IPC_NAME_MAX. Empty output means
 * the name carried no usable characters and the caller must fail. */
static void sanitize_name(const char *name, char *out) {
    size_t j = 0;
    size_t i = 0;
    out[0] = '\0';
    if (name == 0) {
        return;
    }
    while (name[i] != '\0' && j < ALYA_IPC_NAME_MAX) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out[j] = c;
            j++;
        }
        i++;
    }
    out[j] = '\0';
}

static int valid_slot(int slot) {
    return slot >= 0 && slot < ALYA_IPC_MAX_SLOTS && g_slots[slot].used;
}

#ifdef _WIN32

static void full_pipe_name(const char *clean, char *out, size_t out_sz) {
    snprintf(out, out_sz, "\\\\.\\pipe\\alya-ipc-%s", clean);
}

static HANDLE create_listener(const char *clean) {
    char full[128];
    full_pipe_name(clean, full, sizeof(full));
    return CreateNamedPipeA(full, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                            PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, NULL);
}

int alya_ipc_pipe_listen(const char *name) {
    char clean[ALYA_IPC_NAME_MAX + 1];
    HANDLE h;
    int slot = 0;
    sanitize_name(name, clean);
    if (clean[0] == '\0') {
        set_error("ipc: invalid pipe name");
        return -1;
    }
    h = create_listener(clean);
    if (h == INVALID_HANDLE_VALUE) {
        set_error("ipc: CreateNamedPipe failed");
        return -1;
    }
    slot = alloc_slot();
    if (slot < 0) {
        CloseHandle(h);
        return -1;
    }
    g_slots[slot].is_server = 1;
    g_slots[slot].handle = h;
    strncpy(g_slots[slot].name, clean, sizeof(g_slots[slot].name) - 1);
    g_slots[slot].name[sizeof(g_slots[slot].name) - 1] = '\0';
    return slot;
}

int alya_ipc_pipe_accept(int server_slot, int timeout_ms) {
    HANDLE h;
    HANDLE next;
    OVERLAPPED ov;
    HANDLE ev;
    BOOL ok;
    DWORD err;
    DWORD wait_ms;
    DWORD w;
    DWORD transferred = 0;
    int slot = 0;
    if (!valid_slot(server_slot) || !g_slots[server_slot].is_server) {
        set_error("ipc: invalid server slot");
        return -1;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    h = g_slots[server_slot].handle;
    memset(&ov, 0, sizeof(ov));
    ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (ev == NULL) {
        set_error("ipc: CreateEvent failed");
        return -1;
    }
    ov.hEvent = ev;
    ok = ConnectNamedPipe(h, &ov);
    err = GetLastError();
    if (!ok && err == ERROR_IO_PENDING) {
        wait_ms = (DWORD)timeout_ms;
        w = WaitForSingleObject(ev, wait_ms);
        if (w != WAIT_OBJECT_0) {
            CancelIo(h);
            GetOverlappedResult(h, &ov, &transferred, TRUE);
            CloseHandle(ev);
            set_error("ipc: accept timeout");
            return -1;
        }
        GetOverlappedResult(h, &ov, &transferred, FALSE);
    } else if (!ok && err != ERROR_PIPE_CONNECTED) {
        CloseHandle(ev);
        set_error("ipc: ConnectNamedPipe failed");
        return -1;
    }
    CloseHandle(ev);
    /* Install a fresh listener so the server slot keeps accepting. */
    next = create_listener(g_slots[server_slot].name);
    slot = alloc_slot();
    if (slot < 0) {
        if (next != INVALID_HANDLE_VALUE) {
            CloseHandle(next);
        }
        return -1;
    }
    g_slots[slot].is_server = 0;
    g_slots[slot].handle = h;
    if (next != INVALID_HANDLE_VALUE) {
        g_slots[server_slot].handle = next;
    } else {
        /* Listener exhausted; retire the server slot but keep the connection. */
        free_slot(server_slot);
        set_error("ipc: listener re-arm failed");
    }
    return slot;
}

int alya_ipc_pipe_connect(const char *name, int timeout_ms) {
    char clean[ALYA_IPC_NAME_MAX + 1];
    char full[128];
    ULONGLONG deadline;
    ULONGLONG now;
    DWORD remaining;
    int slot = 0;
    sanitize_name(name, clean);
    if (clean[0] == '\0') {
        set_error("ipc: invalid pipe name");
        return -1;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    full_pipe_name(clean, full, sizeof(full));
    deadline = GetTickCount64() + (ULONGLONG)timeout_ms;
    while (1) {
        HANDLE h = CreateFileA(full, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            slot = alloc_slot();
            if (slot < 0) {
                CloseHandle(h);
                return -1;
            }
            g_slots[slot].is_server = 0;
            g_slots[slot].handle = h;
            return slot;
        }
        if (GetLastError() == ERROR_PIPE_BUSY) {
            now = GetTickCount64();
            remaining = now >= deadline ? 0 : (DWORD)(deadline - now);
            if (!WaitNamedPipeA(full, remaining)) {
                set_error("ipc: pipe busy (connect timeout)");
                return -1;
            }
            continue;
        }
        now = GetTickCount64();
        if (now >= deadline) {
            set_error("ipc: connection failed");
            return -1;
        }
        Sleep(25);
    }
}

/* Blocking byte write with an explicit deadline. Returns bytes written,
 * -2 on timeout, -1 on hard failure. */
static int win_write_all(HANDLE h, const char *data, int len, int timeout_ms) {
    int done = 0;
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)(timeout_ms < 0 ? 0 : timeout_ms);
    while (done < len) {
        OVERLAPPED ov;
        HANDLE ev;
        DWORD chunk = 0;
        DWORD remaining;
        ULONGLONG now;
        DWORD w;
        memset(&ov, 0, sizeof(ov));
        ev = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (ev == NULL) {
            return -1;
        }
        ov.hEvent = ev;
        if (!WriteFile(h, data + done, (DWORD)(len - done), NULL, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) {
                CloseHandle(ev);
                return -1;
            }
        }
        now = GetTickCount64();
        remaining = now >= deadline ? 0 : (DWORD)(deadline - now);
        w = WaitForSingleObject(ev, remaining);
        if (w != WAIT_OBJECT_0) {
            CancelIo(h);
            GetOverlappedResult(h, &ov, &chunk, TRUE);
            CloseHandle(ev);
            return -2;
        }
        if (!GetOverlappedResult(h, &ov, &chunk, FALSE) || chunk == 0) {
            CloseHandle(ev);
            return -1;
        }
        CloseHandle(ev);
        done += (int)chunk;
    }
    return done;
}

/* Single blocking read. Returns bytes read, 0 on broken pipe (EOF),
 * -2 on timeout, -1 on hard failure. */
static int win_read_once(HANDLE h, char *buf, int maxlen, int timeout_ms) {
    OVERLAPPED ov;
    HANDLE ev;
    DWORD got = 0;
    DWORD w;
    memset(&ov, 0, sizeof(ov));
    ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (ev == NULL) {
        return -1;
    }
    ov.hEvent = ev;
    if (!ReadFile(h, buf, (DWORD)maxlen, NULL, &ov)) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            CloseHandle(ev);
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                return 0;
            }
            return -1;
        }
    }
    w = WaitForSingleObject(ev, (DWORD)(timeout_ms < 0 ? 0 : timeout_ms));
    if (w != WAIT_OBJECT_0) {
        CancelIo(h);
        GetOverlappedResult(h, &ov, &got, TRUE);
        CloseHandle(ev);
        return -2;
    }
    if (!GetOverlappedResult(h, &ov, &got, FALSE)) {
        DWORD err = GetLastError();
        CloseHandle(ev);
        if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
            return 0;
        }
        return -1;
    }
    CloseHandle(ev);
    return (int)got;
}

int alya_ipc_pipe_send(int slot, const char *data) {
    size_t len = 0;
    if (!valid_slot(slot)) {
        set_error("ipc: invalid pipe slot");
        return -1;
    }
    if (data == 0) {
        return 0;
    }
    while (data[len] != '\0') {
        len++;
    }
    if (len == 0) {
        return 0;
    }
    if (win_write_all(g_slots[slot].handle, data, (int)len, 5000) < 0) {
        set_error("ipc: pipe send failed");
        return -1;
    }
    return (int)len;
}

int alya_ipc_pipe_recv(int slot, char *buf, int maxlen, int timeout_ms) {
    int n = 0;
    if (!valid_slot(slot)) {
        set_error("ipc: invalid pipe slot");
        return -1;
    }
    if (buf == 0 || maxlen <= 0) {
        set_error("ipc: invalid receive buffer");
        return -1;
    }
    n = win_read_once(g_slots[slot].handle, buf, maxlen, timeout_ms);
    if (n == -1) {
        set_error("ipc: pipe receive failed");
    }
    return n;
}

int alya_ipc_pipe_poll(int slot, int timeout_ms) {
    DWORD avail = 0;
    ULONGLONG deadline;
    (void)timeout_ms;
    if (!valid_slot(slot)) {
        return -1;
    }
    /* PeekNamedPipe is non-blocking; poll by sampling until the deadline. */
    deadline = GetTickCount64() + (ULONGLONG)(timeout_ms < 0 ? 0 : timeout_ms);
    while (1) {
        if (PeekNamedPipe(g_slots[slot].handle, NULL, 0, NULL, &avail, NULL)) {
            if (avail > 0) {
                return 1;
            }
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                return 1;
            }
            return -1;
        }
        if (GetTickCount64() >= deadline) {
            return 0;
        }
        Sleep(5);
    }
}

int alya_ipc_pipe_close(int slot) {
    if (!valid_slot(slot)) {
        return 0;
    }
    if (g_slots[slot].handle != 0 && g_slots[slot].handle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_slots[slot].handle);
    }
    free_slot(slot);
    return 0;
}

#else /* POSIX: Unix domain stream sockets */

static void socket_path(const char *clean, char *out, size_t out_sz) {
    const char *tmp = getenv("TMPDIR");
    if (tmp == 0 || tmp[0] == '\0') {
        tmp = "/tmp";
    }
    snprintf(out, out_sz, "%s/alya-ipc-%s.sock", tmp, clean);
}

static int make_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
#ifdef SO_NOSIGPIPE
    int one = 1;
    if (fd >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    }
#endif
    return fd;
}

int alya_ipc_pipe_listen(const char *name) {
    char clean[ALYA_IPC_NAME_MAX + 1];
    char path[160];
    struct sockaddr_un addr;
    int fd = -1;
    int slot = 0;
    sanitize_name(name, clean);
    if (clean[0] == '\0') {
        set_error("ipc: invalid pipe name");
        return -1;
    }
    socket_path(clean, path, sizeof(path));
    unlink(path);
    fd = make_socket();
    if (fd < 0) {
        set_error(strerror(errno));
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        set_error(strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 8) != 0) {
        set_error(strerror(errno));
        close(fd);
        unlink(path);
        return -1;
    }
    slot = alloc_slot();
    if (slot < 0) {
        close(fd);
        unlink(path);
        return -1;
    }
    g_slots[slot].is_server = 1;
    g_slots[slot].fd = fd;
    strncpy(g_slots[slot].name, clean, sizeof(g_slots[slot].name) - 1);
    strncpy(g_slots[slot].path, path, sizeof(g_slots[slot].path) - 1);
    return slot;
}

int alya_ipc_pipe_accept(int server_slot, int timeout_ms) {
    struct pollfd pfd;
    int pr = 0;
    int cfd = -1;
    int slot = 0;
    if (!valid_slot(server_slot) || !g_slots[server_slot].is_server) {
        set_error("ipc: invalid server slot");
        return -1;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    pfd.fd = g_slots[server_slot].fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    pr = poll(&pfd, 1, timeout_ms);
    if (pr < 0) {
        set_error(strerror(errno));
        return -1;
    }
    if (pr == 0) {
        set_error("ipc: accept timeout");
        return -1;
    }
    cfd = accept(g_slots[server_slot].fd, NULL, NULL);
    if (cfd < 0) {
        set_error(strerror(errno));
        return -1;
    }
    slot = alloc_slot();
    if (slot < 0) {
        close(cfd);
        return -1;
    }
    g_slots[slot].is_server = 0;
    g_slots[slot].fd = cfd;
    return slot;
}

int alya_ipc_pipe_connect(const char *name, int timeout_ms) {
    char clean[ALYA_IPC_NAME_MAX + 1];
    char path[160];
    struct sockaddr_un addr;
    int fd = -1;
    int flags = 0;
    int rc = 0;
    struct pollfd pfd;
    int pr = 0;
    int soerr = 0;
    socklen_t solen = sizeof(soerr);
    int slot = 0;
    sanitize_name(name, clean);
    if (clean[0] == '\0') {
        set_error("ipc: invalid pipe name");
        return -1;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    socket_path(clean, path, sizeof(path));
    fd = make_socket();
    if (fd < 0) {
        set_error(strerror(errno));
        return -1;
    }
    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        set_error(strerror(errno));
        close(fd);
        return -1;
    }
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0) {
        if (pr == 0) {
            set_error("ipc: connection timeout");
        } else {
            set_error(strerror(errno));
        }
        close(fd);
        return -1;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &solen) != 0 || soerr != 0) {
        if (soerr != 0) {
            set_error(strerror(soerr));
        } else {
            set_error(strerror(errno));
        }
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, flags);
    slot = alloc_slot();
    if (slot < 0) {
        close(fd);
        return -1;
    }
    g_slots[slot].is_server = 0;
    g_slots[slot].fd = fd;
    return slot;
}

int alya_ipc_pipe_send(int slot, const char *data) {
    size_t len = 0;
    size_t done = 0;
    if (!valid_slot(slot)) {
        set_error("ipc: invalid pipe slot");
        return -1;
    }
    if (data == 0) {
        return 0;
    }
    while (data[len] != '\0') {
        len++;
    }
    while (done < len) {
        ssize_t n = send(g_slots[slot].fd, data + done, len - done, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(strerror(errno));
            return -1;
        }
        done += (size_t)n;
    }
    return (int)len;
}

int alya_ipc_pipe_recv(int slot, char *buf, int maxlen, int timeout_ms) {
    struct pollfd pfd;
    int pr = 0;
    ssize_t n = 0;
    if (!valid_slot(slot)) {
        set_error("ipc: invalid pipe slot");
        return -1;
    }
    if (buf == 0 || maxlen <= 0) {
        set_error("ipc: invalid receive buffer");
        return -1;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    pfd.fd = g_slots[slot].fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    pr = poll(&pfd, 1, timeout_ms);
    if (pr < 0) {
        set_error(strerror(errno));
        return -1;
    }
    if (pr == 0) {
        return -2;
    }
    n = recv(g_slots[slot].fd, buf, (size_t)maxlen, 0);
    if (n < 0) {
        if (errno == EINTR) {
            return -2;
        }
        set_error(strerror(errno));
        return -1;
    }
    return (int)n;
}

int alya_ipc_pipe_poll(int slot, int timeout_ms) {
    struct pollfd pfd;
    int pr = 0;
    if (!valid_slot(slot)) {
        return -1;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    pfd.fd = g_slots[slot].fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    pr = poll(&pfd, 1, timeout_ms);
    if (pr < 0) {
        return -1;
    }
    if (pr == 0) {
        return 0;
    }
    return 1;
}

int alya_ipc_pipe_close(int slot) {
    if (!valid_slot(slot)) {
        return 0;
    }
    if (g_slots[slot].fd >= 0) {
        close(g_slots[slot].fd);
    }
    if (g_slots[slot].is_server && g_slots[slot].path[0] != '\0') {
        unlink(g_slots[slot].path);
    }
    free_slot(slot);
    return 0;
}

#endif
