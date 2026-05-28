/*
 * YAcFS Replication Protocol v1 — Sender
 *
 * Sends a snapshot from a local pool to a remote receiver.
 *
 * Usage: yacfs-send <pool> <snapshot> <host> <port>
 *
 * Protocol:
 *   1. Connect to receiver
 *   2. Send "YACFS-REP v1\n" header
 *   3. Send snapshot metadata files (size-prefixed)
 *   4. Send block files (hash:size:data)
 *   5. Send end marker
 */

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CHUNK_SIZE 262144

static int send_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, (const char*)buf + total, len - total);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

static int send_file(int fd, const char *path) {
    int f = open(path, O_RDONLY);
    if (f < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    struct stat sb;
    fstat(f, &sb);
    uint32_t size = sb.st_size;

    if (send_all(fd, &size, 4) < 0) { close(f); return -1; }

    char buf[CHUNK_SIZE];
    ssize_t n;
    while ((n = read(f, buf, sizeof(buf))) > 0) {
        if (send_all(fd, buf, n) < 0) { close(f); return -1; }
    }
    close(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <pool> <snapshot> <host> <port>\n", argv[0]);
        fprintf(stderr, "Pool formats:\n");
        fprintf(stderr, "  local:/data/pool    Local pool directory\n");
        return 1;
    }

    const char *pool = argv[1];
    const char *snap = argv[2];
    const char *host = argv[3];
    int port = atoi(argv[4]);

    char snapdir[4096];
    snprintf(snapdir, sizeof(snapdir), "%s/.snapshots/%s", pool, snap);

    struct stat sb;
    if (stat(snapdir, &sb) < 0) {
        fprintf(stderr, "Snapshot '%s' not found in pool %s\n", snap, pool);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    struct hostent *he = gethostbyname(host);
    if (!he) { fprintf(stderr, "Cannot resolve host: %s\n", host); close(sock); return 1; }
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(sock); return 1;
    }

    printf("Connected to %s:%d\n", host, port);

    send_all(sock, "YACFS-REP v1\n", 13);

    printf("Sending snapshot '%s'...\n", snap);

    DIR *d = opendir(snapdir);
    if (!d) { perror("opendir"); close(sock); return 1; }

    struct dirent *de;
    int nfiles = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char fpath[4096];
        snprintf(fpath, sizeof(fpath), "%s/%s", snapdir, de->d_name);

        uint16_t nlen = strlen(de->d_name);
        send_all(sock, &nlen, 2);
        send_all(sock, de->d_name, nlen);

        if (send_file(sock, fpath) < 0) {
            closedir(d);
            close(sock);
            fprintf(stderr, "Failed to send %s\n", de->d_name);
            return 1;
        }
        nfiles++;
        printf("  Sent: %s\n", de->d_name);
    }
    closedir(d);

    uint16_t end_marker = 0;
    send_all(sock, &end_marker, 2);

    printf("Snapshot '%s' sent (%d files)\n", snap, nfiles);

    shutdown(sock, SHUT_WR);

    char resp[256];
    ssize_t n = read(sock, resp, sizeof(resp) - 1);
    if (n > 0) {
        resp[n] = 0;
        printf("Receiver: %s", resp);
    }

    close(sock);
    return 0;
}
