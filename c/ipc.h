#ifndef ALYA_IPC_H
#define ALYA_IPC_H

/* Slot-based pipe transport for the Alya ipc package.
 * Windows: named pipes (\\.\pipe\alya-ipc-<name>).
 * POSIX: Unix domain stream sockets (<tmp>/alya-ipc-<name>.sock).
 * All handles cross the FFI boundary as small integer slots. */

int alya_ipc_pipe_listen(const char *name);
int alya_ipc_pipe_accept(int server_slot, int timeout_ms);
int alya_ipc_pipe_connect(const char *name, int timeout_ms);
int alya_ipc_pipe_send(int slot, const char *data);
int alya_ipc_pipe_recv(int slot, char *buf, int maxlen, int timeout_ms);
int alya_ipc_pipe_poll(int slot, int timeout_ms);
int alya_ipc_pipe_close(int slot);
const char *alya_ipc_pipe_error(void);

#endif
