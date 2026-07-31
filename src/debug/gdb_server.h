#ifndef GDB_SERVER_H
#define GDB_SERVER_H

#include "types.h"

#define GDB_PACKET_CAPACITY 4096

typedef struct Cpu Cpu;
typedef struct Debugger Debugger;

typedef struct {
    int listen_fd;
    int client_fd;
    char packet[GDB_PACKET_CAPACITY];
    u32 packet_length;
    u8 checksum;
    u8 received_checksum;
    u8 checksum_digits;
    bool receiving_packet;
    bool reading_checksum;
    bool no_ack_mode;
    bool stop_reported;
    bool log_packets;
} GdbServer;

bool gdb_server_init(GdbServer* server, u16 port);
bool gdb_server_poll(GdbServer* server, Cpu* cpu, Debugger* debugger);
void gdb_server_destroy(GdbServer* server);

#endif
