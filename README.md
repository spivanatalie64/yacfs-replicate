# YAcFS Replicate — Snapshot Replication Protocol

Replicate YAcFS snapshots between machines over TCP. Built-in disaster recovery.

## Protocol: YAcFS-REP v1

YAcFS-REP is a simple, fast, rsync-like protocol for sending and receiving YAcFS pool snapshots.

### Transfer Flow

```
Sender                               Receiver
  │                                     │
  │──── YACFS-REP v1\n ───────────────→│  (handshake)
  │──── file_count + metadata ────────→│
  │──── name_len + name + data ───────→│  (for each file)
  │──── 0x0000 (end marker) ──────────→│
  │←──────────────────── OK ───────────│
```

### Wire Format

```
HEADER:   "YACFS-REP v1\n" (13 bytes)
FILES:    [name_len:2][name:name_len][data_size:4][data:data_size] ...
END:      [name_len:2] = 0
ACK:      "OK" (2 bytes)
```

All integers are network byte order (big-endian).

## Tools

| Tool | Description |
|---|---|
| `yacfs-send` | Send a snapshot to a remote receiver |
| `yacfs-receive` | Receive snapshots from remote senders |

## Usage

### On the target machine (receiver)

```bash
# Listen on port 9999 for incoming snapshots
yacfs-receive /data/pool 9999
```

### On the source machine (sender)

```bash
# Send a snapshot to the receiver
yacfs-send /data/pool before-update 192.168.0.237 9999
```

### Restore a received snapshot

```bash
yacfs-ctl /data/pool snapshots
yacfs-ctl /data/pool rollback received-1717000000
```

## Use Cases

- **Backup**: `yacfs-ctl /data/pool snapshot daily && yacfs-send /data/pool daily backup-server 9999`
- **Migration**: Send snapshot to new machine, verify, rollback
- **DR**: Receive snapshots from multiple machines into a central backup pool

## Security

YAcFS-REP v1 has no built-in encryption. Use with:
- **SSH tunnel**: `ssh -L 9999:localhost:9999 remote yacfs-receive /data/pool 9999`
- **WireGuard**: Run over a VPN
- **Local network**: Bind to internal IP only

Encryption support is planned for YAcFS-REP v2 (TLS or Noise protocol).

## Building

```bash
make
sudo make install
```

## CI/CD

See [.github/workflows](.github/workflows/) for the build pipeline.
