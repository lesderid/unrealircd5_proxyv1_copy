# UnrealIRCd PROXYv1 copy module

This UnrealIRCd module **copies** incoming packets to a server implementing the [PROXY protocol (version 1)](http://www.haproxy.org/download/2.3/doc/proxy-protocol.txt).

Additionally, on plugin load, most existing user and channel data is replicated to the target server through the regular IRC commands (`NICK`, `USER`, `JOIN`, `TOPIC`, `MODE`, etc.).

I wrote this module to test [salty-ircd](https://github.com/lesderid/salty-ircd), by allowing it to accept real traffic from a production (single-server) IRC network running UnrealIRCd.

Important notes:
* UnrealIRCd terminates TLS before copying to the target server
* Packets sent by clients are still handled as usual by UnrealIRCd
* Packets sent by UnrealIRCd to clients are not relayed to the target server in any way

## Build and installation

Place [proxyv1\_copy.c](/proxyv1_copy.c) in `src/modules/third`, and run `make` and `make install`.

## Configuration

```
loadmodule "third/proxyv1_copy";

proxyv1_copy {
    ip target-server-ip;
    port target-server-port;
};
```
