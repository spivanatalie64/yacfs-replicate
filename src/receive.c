/*
 * YAcFS Replication Protocol v1 — Receiver
 *
 * Receives a snapshot from a remote sender and stores it in a local pool.
 *
 * Usage: yacfs-receive <pool> <port>
 *
 * Protocol:
 *   1. Wait for connection
 *   2. Verify "YACFS-REP v1\n" header
 *   3. Receive snapshot metadata files (size-prefixed)
 *   4. Store files in .snapshots/<name>/
 *   5. Acknowledge completion
 */

#define _POSIX_C_SOURCE 199309L
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define CHUNK_SIZE 262144

static int recv_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, (char*)buf + total, len - total);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

static int recv_file(int fd, const char *path) {
    uint32_t size;
    if (recv_all(fd, &size, 4) < 0) return -1;

    if (size == 0) return 0;

    int f = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f < 0) { perror("open"); return -1; }

    char buf[CHUNK_SIZE];
    uint32_t remaining = size;
    while (remaining > 0) {
        uint32_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        if (recv_all(fd, buf, chunk) < 0) { close(f); return -1; }
        size_t written = 0;
        while (written < chunk) {
            ssize_t n = write(f, buf + written, chunk - written);
            if (n <= 0) { close(f); return -1; }
            written += n;
        }
        remaining -= chunk;
    }
    close(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <pool> <port>\n", argv[0]);
        return 1;
    }

    const char *pool = argv[1];
    int port = atoi(argv[2]);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        fprintf(stderr, "Try: yacfs-receive %s %d\n", pool, port);
        close(listen_fd);
        return 1;
    }

    listen(listen_fd, 1);
    printf("YAcFS Receive listening on port %d\n", port);
    printf("Waiting for sender...\n");

    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);
    int client_fd = accept(listen_fd, (struct sockaddr*)&client, &client_len);
    if (client_fd < 0) { perror("accept"); close(listen_fd); return 1; }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client.sin_addr, client_ip, sizeof(client_ip));
    printf("Connection from %s:%d\n", client_ip, ntohs(client.sin_port));

    char header[32] = {0};
    if (recv_all(client_fd, header, 13) < 0) {
        fprintf(stderr, "Protocol error: no header\n");
        close(client_fd);
        close(listen_fd);
        return 1;
    }
    header[13] = 0;

    if (strcmp(header, "YACFS-REP v1\n") != 0) {
        fprintf(stderr, "Protocol mismatch: got '%s'\n", header);
        close(client_fd);
        close(listen_fd);
        return 1;
    }

    printf("Protocol: YACFS-REP v1\n");
    printf("Receiving snapshot...\n");

    char snapname[256] = {0};
    int nfiles = 0;

    while (1) {
        uint16_t nlen;
        if (recv_all(client_fd, &nlen, 2) < 0) break;

        if (nlen == 0) break;

        char fname[256];
        if (recv_all(client_fd, fname, nlen) < 0) break;
        fname[nlen] = 0;

        if (nfiles == 0) {
            char rune[64];
            snprintf(rune, sizeof(rune), "%lu", (unsigned long)time(NULL));
            snprintf(snapname, sizeof(snapname), "received-%s", rune);
        }

        char snapdir[4096];
        snprintf(snapdir, sizeof(snapdir), "%s/.snapshots/%s", pool, snapname);
        mkdir(snapdir, 0755);

        char fpath[4096];
        snprintf(fpath, sizeof(fpath), "%s/%s", snapdir, fname);

        if (recv_file(client_fd, fpath) < 0) {
            fprintf(stderr, "Failed to receive %s\n", fname);
            break;
        }
        nfiles++;
        printf("  Received: %s\n", fname);
    }

    const char *ack = "OK";
    write(client_fd, ack, 2);

    printf("Snapshot '%s' received (%d files)\n", snapname, nfiles);
    printf("Restore with: yacfs-ctl %s rollback %s\n", pool, snapname);

    close(client_fd);
    close(listen_fd);
    return 0;
}
