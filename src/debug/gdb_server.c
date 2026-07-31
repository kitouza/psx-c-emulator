#include "gdb_server.h"

#include "cpu.h"
#include "debugger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define GDB_REGISTER_COUNT 72

static const char TARGET_XML[] =
    "<?xml version=\"1.0\"?>"
    "<target version=\"1.0\">"
    "<architecture>mips:3000</architecture>"
    "<feature name=\"org.gnu.gdb.mips.cpu\">"
    "<reg name=\"r0\" bitsize=\"32\" regnum=\"0\"/>"
    "<reg name=\"r1\" bitsize=\"32\"/>"
    "<reg name=\"r2\" bitsize=\"32\"/>"
    "<reg name=\"r3\" bitsize=\"32\"/>"
    "<reg name=\"r4\" bitsize=\"32\"/>"
    "<reg name=\"r5\" bitsize=\"32\"/>"
    "<reg name=\"r6\" bitsize=\"32\"/>"
    "<reg name=\"r7\" bitsize=\"32\"/>"
    "<reg name=\"r8\" bitsize=\"32\"/>"
    "<reg name=\"r9\" bitsize=\"32\"/>"
    "<reg name=\"r10\" bitsize=\"32\"/>"
    "<reg name=\"r11\" bitsize=\"32\"/>"
    "<reg name=\"r12\" bitsize=\"32\"/>"
    "<reg name=\"r13\" bitsize=\"32\"/>"
    "<reg name=\"r14\" bitsize=\"32\"/>"
    "<reg name=\"r15\" bitsize=\"32\"/>"
    "<reg name=\"r16\" bitsize=\"32\"/>"
    "<reg name=\"r17\" bitsize=\"32\"/>"
    "<reg name=\"r18\" bitsize=\"32\"/>"
    "<reg name=\"r19\" bitsize=\"32\"/>"
    "<reg name=\"r20\" bitsize=\"32\"/>"
    "<reg name=\"r21\" bitsize=\"32\"/>"
    "<reg name=\"r22\" bitsize=\"32\"/>"
    "<reg name=\"r23\" bitsize=\"32\"/>"
    "<reg name=\"r24\" bitsize=\"32\"/>"
    "<reg name=\"r25\" bitsize=\"32\"/>"
    "<reg name=\"r26\" bitsize=\"32\"/>"
    "<reg name=\"r27\" bitsize=\"32\"/>"
    "<reg name=\"r28\" bitsize=\"32\"/>"
    "<reg name=\"r29\" bitsize=\"32\"/>"
    "<reg name=\"r30\" bitsize=\"32\"/>"
    "<reg name=\"r31\" bitsize=\"32\"/>"
    "<reg name=\"lo\" bitsize=\"32\"/>"
    "<reg name=\"hi\" bitsize=\"32\"/>"
    "<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
    "</feature>"
    "<feature name=\"org.gnu.gdb.mips.cp0\">"
    "<reg name=\"status\" bitsize=\"32\"/>"
    "<reg name=\"badvaddr\" bitsize=\"32\"/>"
    "<reg name=\"cause\" bitsize=\"32\"/>"
    "</feature>"
    "<feature name=\"org.gnu.gdb.mips.fpu\">"
    "<reg name=\"f0\" bitsize=\"32\"/>"
    "<reg name=\"f1\" bitsize=\"32\"/>"
    "<reg name=\"f2\" bitsize=\"32\"/>"
    "<reg name=\"f3\" bitsize=\"32\"/>"
    "<reg name=\"f4\" bitsize=\"32\"/>"
    "<reg name=\"f5\" bitsize=\"32\"/>"
    "<reg name=\"f6\" bitsize=\"32\"/>"
    "<reg name=\"f7\" bitsize=\"32\"/>"
    "<reg name=\"f8\" bitsize=\"32\"/>"
    "<reg name=\"f9\" bitsize=\"32\"/>"
    "<reg name=\"f10\" bitsize=\"32\"/>"
    "<reg name=\"f11\" bitsize=\"32\"/>"
    "<reg name=\"f12\" bitsize=\"32\"/>"
    "<reg name=\"f13\" bitsize=\"32\"/>"
    "<reg name=\"f14\" bitsize=\"32\"/>"
    "<reg name=\"f15\" bitsize=\"32\"/>"
    "<reg name=\"f16\" bitsize=\"32\"/>"
    "<reg name=\"f17\" bitsize=\"32\"/>"
    "<reg name=\"f18\" bitsize=\"32\"/>"
    "<reg name=\"f19\" bitsize=\"32\"/>"
    "<reg name=\"f20\" bitsize=\"32\"/>"
    "<reg name=\"f21\" bitsize=\"32\"/>"
    "<reg name=\"f22\" bitsize=\"32\"/>"
    "<reg name=\"f23\" bitsize=\"32\"/>"
    "<reg name=\"f24\" bitsize=\"32\"/>"
    "<reg name=\"f25\" bitsize=\"32\"/>"
    "<reg name=\"f26\" bitsize=\"32\"/>"
    "<reg name=\"f27\" bitsize=\"32\"/>"
    "<reg name=\"f28\" bitsize=\"32\"/>"
    "<reg name=\"f29\" bitsize=\"32\"/>"
    "<reg name=\"f30\" bitsize=\"32\"/>"
    "<reg name=\"f31\" bitsize=\"32\"/>"
    "<reg name=\"fcsr\" bitsize=\"32\"/>"
    "<reg name=\"fir\" bitsize=\"32\"/>"
    "</feature>"
    "</target>";

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char hex_digit(u8 value) {
    return "0123456789abcdef"[value & 0xf];
}

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static bool send_all(int fd, const char* data, size_t length) {
    while (length > 0) {
        ssize_t sent = send(fd, data, length, 0);
        if (sent > 0) {
            data += sent;
            length -= (size_t)sent;
        } else if (sent < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool send_packet(GdbServer* server, const char* payload) {
    char framed[GDB_PACKET_CAPACITY + 4];
    size_t length = strlen(payload);
    if (length > GDB_PACKET_CAPACITY - 1) return false;

    u8 checksum = 0;
    for (size_t i = 0; i < length; ++i) checksum += (u8)payload[i];

    framed[0] = '$';
    memcpy(&framed[1], payload, length);
    framed[length + 1] = '#';
    framed[length + 2] = hex_digit(checksum >> 4);
    framed[length + 3] = hex_digit(checksum);
    if (server->log_packets) {
        fprintf(stderr, "GDB -> %.*s%s\n",
                (int)(length > 160 ? 160 : length),
                payload,
                length > 160 ? "..." : "");
    }
    return send_all(server->client_fd, framed, length + 4);
}

static void encode_u32_le(char* output, u32 value) {
    for (u32 byte = 0; byte < 4; ++byte) {
        u8 part = (u8)(value >> (byte * 8));
        output[byte * 2] = hex_digit(part >> 4);
        output[byte * 2 + 1] = hex_digit(part);
    }
}

static bool decode_u32_le(const char* input, u32* value) {
    u32 result = 0;
    for (u32 byte = 0; byte < 4; ++byte) {
        int high = hex_value(input[byte * 2]);
        int low = hex_value(input[byte * 2 + 1]);
        if (high < 0 || low < 0) return false;
        result |= (u32)((high << 4) | low) << (byte * 8);
    }
    *value = result;
    return true;
}

static u32 gdb_register(const Cpu* cpu, u32 index) {
    if (index < 32) return cpu->regs[index];
    switch (index) {
        case 32: return cpu->lo;
        case 33: return cpu->hi;
        case 34: return cpu->pc;
        case 35: return cpu->sr;
        case 36: return cpu->badvaddr;
        case 37: return cpu->cause;
        default:
            // GDB currently requires a MIPS FPU target feature. The PS1 has no
            // general-purpose FPU, so these compatibility registers read zero.
            return 0;
    }
}

static bool set_gdb_register(Cpu* cpu, u32 index, u32 value) {
    if (index < 32) {
        if (index != 0) {
            cpu->regs[index] = value;
            cpu->out_regs[index] = value;
        }
        return true;
    }
    switch (index) {
        case 32: cpu->lo = value; return true;
        case 33: cpu->hi = value; return true;
        case 34:
            if ((value & 3) != 0) return false;
            cpu->pc = value;
            cpu->next_pc = value + 4;
            cpu->current_pc = value;
            cpu->branch = false;
            cpu->delay_slot = false;
            return true;
        case 35: cpu->sr = value; return true;
        case 36: cpu->badvaddr = value; return true;
        case 37: cpu->cause = value; return true;
        default:
            // Accept and ignore bulk restores of the compatibility FPU bank.
            return index < GDB_REGISTER_COUNT;
    }
}

static bool parse_hex_u32(const char** cursor, u32* value) {
    u32 result = 0;
    bool found = false;
    while (**cursor != '\0') {
        int digit = hex_value(**cursor);
        if (digit < 0) break;
        result = (result << 4) | (u32)digit;
        ++(*cursor);
        found = true;
    }
    *value = result;
    return found;
}

static bool send_stop_reason(GdbServer* server, const Debugger* debugger) {
    char response[40];
    switch (debugger->stop_reason) {
        case DEBUG_STOP_READ_WATCHPOINT:
            snprintf(response, sizeof(response), "T05rwatch:%08x;",
                     debugger->stop_address);
            break;
        case DEBUG_STOP_WRITE_WATCHPOINT:
            snprintf(response, sizeof(response), "T05watch:%08x;",
                     debugger->stop_address);
            break;
        default:
            strcpy(response, "S05");
            break;
    }
    return send_packet(server, response);
}

static bool handle_registers(GdbServer* server, const Cpu* cpu) {
    char response[GDB_REGISTER_COUNT * 8 + 1];
    for (u32 i = 0; i < GDB_REGISTER_COUNT; ++i) {
        encode_u32_le(&response[i * 8], gdb_register(cpu, i));
    }
    response[GDB_REGISTER_COUNT * 8] = '\0';
    return send_packet(server, response);
}

static bool handle_memory(GdbServer* server, const Cpu* cpu,
                          const char* command) {
    u32 addr;
    u32 length;
    const char* cursor = command + 1;
    if (!parse_hex_u32(&cursor, &addr) || *cursor++ != ','
        || !parse_hex_u32(&cursor, &length)
        || length > (GDB_PACKET_CAPACITY - 1) / 2) {
        return send_packet(server, "E01");
    }

    char response[GDB_PACKET_CAPACITY];
    for (u32 i = 0; i < length; ++i) {
        u32 examined;
        if (!cpu_examine(cpu, addr + i, ACCESS_BYTE, &examined)) {
            return send_packet(server, "E03");
        }
        u8 value = (u8)examined;
        response[i * 2] = hex_digit(value >> 4);
        response[i * 2 + 1] = hex_digit(value);
    }
    response[length * 2] = '\0';
    return send_packet(server, response);
}

static bool handle_memory_write(GdbServer* server, Cpu* cpu,
                                const char* command) {
    u32 addr;
    u32 length;
    const char* cursor = command + 1;
    if (!parse_hex_u32(&cursor, &addr) || *cursor++ != ','
        || !parse_hex_u32(&cursor, &length) || *cursor++ != ':'
        || length > (GDB_PACKET_CAPACITY - 1) / 2
        || strlen(cursor) != (size_t)length * 2) {
        return send_packet(server, "E01");
    }

    // Validate the entire operation before changing RAM.
    for (u32 i = 0; i < length; ++i) {
        if (mask_region(addr + i) >= RAM_SIZE
            || hex_value(cursor[i * 2]) < 0
            || hex_value(cursor[i * 2 + 1]) < 0) {
            return send_packet(server, "E03");
        }
    }
    for (u32 i = 0; i < length; ++i) {
        u8 value = (u8)((hex_value(cursor[i * 2]) << 4)
                        | hex_value(cursor[i * 2 + 1]));
        if (!cpu_deposit(cpu, addr + i, ACCESS_BYTE, value)) {
            return send_packet(server, "E03");
        }
    }
    return send_packet(server, "OK");
}

static bool handle_register_write(GdbServer* server, Cpu* cpu,
                                  const char* command) {
    const char* cursor = command + 1;
    u32 index;
    if (!parse_hex_u32(&cursor, &index) || *cursor++ != '='
        || strlen(cursor) != 8) {
        return send_packet(server, "E01");
    }
    u32 value;
    if (!decode_u32_le(cursor, &value)
        || !set_gdb_register(cpu, index, value)) {
        return send_packet(server, "E03");
    }
    return send_packet(server, "OK");
}

static bool handle_all_register_writes(GdbServer* server, Cpu* cpu,
                                       const char* command) {
    if (strlen(command + 1) != GDB_REGISTER_COUNT * 8) {
        return send_packet(server, "E01");
    }

    u32 values[GDB_REGISTER_COUNT];
    for (u32 i = 0; i < GDB_REGISTER_COUNT; ++i) {
        if (!decode_u32_le(command + 1 + i * 8, &values[i])) {
            return send_packet(server, "E01");
        }
    }
    for (u32 i = 0; i < GDB_REGISTER_COUNT; ++i) {
        if (!set_gdb_register(cpu, i, values[i])) {
            return send_packet(server, "E03");
        }
    }
    return send_packet(server, "OK");
}

static bool handle_target_description(GdbServer* server,
                                      const char* command) {
    static const char prefix[] = "qXfer:features:read:target.xml:";
    const char* cursor = command + sizeof(prefix) - 1;
    u32 offset;
    u32 requested;
    if (strncmp(command, prefix, sizeof(prefix) - 1) != 0
        || !parse_hex_u32(&cursor, &offset) || *cursor++ != ','
        || !parse_hex_u32(&cursor, &requested)) {
        return send_packet(server, "E01");
    }

    size_t total = strlen(TARGET_XML);
    if (offset >= total) return send_packet(server, "l");
    size_t remaining = total - offset;
    size_t count = requested < remaining ? requested : remaining;
    if (count > GDB_PACKET_CAPACITY - 2) count = GDB_PACKET_CAPACITY - 2;

    char response[GDB_PACKET_CAPACITY];
    response[0] = count == remaining ? 'l' : 'm';
    memcpy(response + 1, TARGET_XML + offset, count);
    response[count + 1] = '\0';
    return send_packet(server, response);
}

static bool handle_breakpoint(GdbServer* server, Debugger* debugger,
                              const char* command) {
    bool add = command[0] == 'Z';
    u32 type;
    u32 addr;
    u32 kind;
    const char* cursor = command + 1;
    if (!parse_hex_u32(&cursor, &type) || *cursor++ != ','
        || !parse_hex_u32(&cursor, &addr) || *cursor++ != ','
        || !parse_hex_u32(&cursor, &kind)) {
        return send_packet(server, "E01");
    }
    (void)kind;

    bool success = false;
    switch (type) {
        case 0:
        case 1:
            success = add ? debugger_add_breakpoint(debugger, addr)
                          : debugger_remove_breakpoint(debugger, addr);
            break;
        case 2:
            success = add ? debugger_add_write_watchpoint(debugger, addr)
                          : debugger_remove_write_watchpoint(debugger, addr);
            break;
        case 3:
            success = add ? debugger_add_read_watchpoint(debugger, addr)
                          : debugger_remove_read_watchpoint(debugger, addr);
            break;
        case 4: {
            bool read_ok = add ? debugger_add_read_watchpoint(debugger, addr)
                               : debugger_remove_read_watchpoint(debugger, addr);
            bool write_ok = add ? debugger_add_write_watchpoint(debugger, addr)
                                : debugger_remove_write_watchpoint(debugger, addr);
            success = read_ok && write_ok;
            break;
        }
        default:
            return send_packet(server, "");
    }
    return send_packet(server, success ? "OK" : "E02");
}

static bool handle_packet(GdbServer* server, Cpu* cpu, Debugger* debugger) {
    const char* command = server->packet;

    if (strcmp(command, "?") == 0) {
        server->stop_reported = true;
        return send_stop_reason(server, debugger);
    }
    if (strcmp(command, "g") == 0) return handle_registers(server, cpu);
    if (command[0] == 'G') {
        return handle_all_register_writes(server, cpu, command);
    }
    if (command[0] == 'p') {
        const char* cursor = command + 1;
        u32 index;
        if (!parse_hex_u32(&cursor, &index)) return send_packet(server, "E01");
        char response[9];
        encode_u32_le(response, gdb_register(cpu, index));
        response[8] = '\0';
        return send_packet(server, response);
    }
    if (command[0] == 'P') {
        return handle_register_write(server, cpu, command);
    }
    if (command[0] == 'm') return handle_memory(server, cpu, command);
    if (command[0] == 'M') return handle_memory_write(server, cpu, command);
    if (command[0] == 'Z' || command[0] == 'z') {
        return handle_breakpoint(server, debugger, command);
    }
    if (strcmp(command, "c") == 0 || strncmp(command, "vCont;c", 7) == 0) {
        debugger_continue(debugger);
        server->stop_reported = false;
        return true;
    }
    if (strcmp(command, "s") == 0 || strncmp(command, "vCont;s", 7) == 0) {
        debugger_step(debugger);
        server->stop_reported = false;
        return true;
    }
    if (strcmp(command, "vCont?") == 0) return send_packet(server, "vCont;c;s");
    if (strncmp(command, "qSupported", 10) == 0) {
        return send_packet(
            server,
            "PacketSize=1000;swbreak+;hwbreak+;qXfer:features:read+");
    }
    static const char target_query[] = "qXfer:features:read:target.xml:";
    if (strncmp(command, target_query, sizeof(target_query) - 1) == 0) {
        return handle_target_description(server, command);
    }
    if (strcmp(command, "qAttached") == 0) return send_packet(server, "1");
    if (strcmp(command, "qC") == 0) return send_packet(server, "QC1");
    if (strcmp(command, "qfThreadInfo") == 0) {
        // The emulator has one execution context: the emulated PSX CPU.
        return send_packet(server, "m1");
    }
    if (strcmp(command, "qsThreadInfo") == 0) return send_packet(server, "l");
    if (strcmp(command, "T1") == 0) return send_packet(server, "OK");
    if (strcmp(command, "QStartNoAckMode") == 0) {
        bool sent = send_packet(server, "OK");
        server->no_ack_mode = true;
        return sent;
    }
    if (command[0] == 'H') return send_packet(server, "OK");
    if (command[0] == 'D') {
        debugger_continue(debugger);
        return send_packet(server, "OK");
    }
    if (command[0] == 'k') return false;

    return send_packet(server, "");
}

static void reset_packet(GdbServer* server) {
    server->packet_length = 0;
    server->checksum = 0;
    server->received_checksum = 0;
    server->checksum_digits = 0;
    server->receiving_packet = false;
    server->reading_checksum = false;
}

static bool receive_byte(GdbServer* server, Cpu* cpu, Debugger* debugger,
                         u8 byte) {
    if (byte == 0x03) {
        debugger_pause(debugger);
        server->stop_reported = true;
        return send_stop_reason(server, debugger);
    }
    if (byte == '$') {
        reset_packet(server);
        server->receiving_packet = true;
        return true;
    }
    if (server->receiving_packet) {
        if (byte == '#') {
            server->receiving_packet = false;
            server->reading_checksum = true;
        } else if (server->packet_length < GDB_PACKET_CAPACITY - 1) {
            server->packet[server->packet_length++] = (char)byte;
            server->checksum += byte;
        } else {
            reset_packet(server);
        }
        return true;
    }
    if (server->reading_checksum) {
        int digit = hex_value((char)byte);
        if (digit < 0) {
            reset_packet(server);
            return true;
        }
        server->received_checksum = (u8)((server->received_checksum << 4)
                                         | (u8)digit);
        if (++server->checksum_digits == 2) {
            bool valid = server->received_checksum == server->checksum;
            if (!server->no_ack_mode
                && !send_all(server->client_fd, valid ? "+" : "-", 1)) {
                return false;
            }
            server->packet[server->packet_length] = '\0';
            server->reading_checksum = false;
            if (valid) {
                if (server->log_packets) {
                    fprintf(stderr, "GDB <- %.*s%s\n",
                            server->packet_length > 160
                                ? 160
                                : (int)server->packet_length,
                            server->packet,
                            server->packet_length > 160 ? "..." : "");
                }
                if (!handle_packet(server, cpu, debugger)) return false;
            }
        }
    }
    return true;
}

bool gdb_server_init(GdbServer* server, u16 port) {
    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;
    server->client_fd = -1;
    const char* log_setting = getenv("PSX_GDB_LOG");
    server->log_packets = log_setting != NULL
        && strcmp(log_setting, "0") != 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0
        || listen(fd, 1) < 0 || !set_nonblocking(fd)) {
        close(fd);
        return false;
    }

    server->listen_fd = fd;
    printf("GDB server listening on localhost:%u\n", (unsigned)port);
    return true;
}

bool gdb_server_poll(GdbServer* server, Cpu* cpu, Debugger* debugger) {
    if (server->client_fd < 0) {
        int client = accept(server->listen_fd, NULL, NULL);
        if (client >= 0) {
            if (!set_nonblocking(client)) {
                close(client);
                return false;
            }
            server->client_fd = client;
            reset_packet(server);
            debugger_pause(debugger);
            server->stop_reported = false;
            printf("GDB connected\n");
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return false;
        }
    }

    if (server->client_fd >= 0) {
        u8 input[1024];
        ssize_t count;
        while ((count = recv(server->client_fd, input, sizeof(input), 0)) > 0) {
            for (ssize_t i = 0; i < count; ++i) {
                if (!receive_byte(server, cpu, debugger, input[i])) {
                    close(server->client_fd);
                    server->client_fd = -1;
                    debugger_continue(debugger);
                    return true;
                }
            }
        }
        if (count == 0) {
            close(server->client_fd);
            server->client_fd = -1;
            debugger_continue(debugger);
            printf("GDB disconnected\n");
        } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK
                   && errno != EINTR) {
            return false;
        }

        if (server->client_fd >= 0 && debugger->stopped
            && !server->stop_reported && debugger->stop_reason != DEBUG_STOP_PAUSE) {
            if (!send_stop_reason(server, debugger)) return false;
            server->stop_reported = true;
        }
    }
    return true;
}

void gdb_server_destroy(GdbServer* server) {
    if (server->client_fd >= 0) close(server->client_fd);
    if (server->listen_fd >= 0) close(server->listen_fd);
    server->client_fd = -1;
    server->listen_fd = -1;
}
