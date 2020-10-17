# UnrealIRCd PROXYv1 copy module

This UnrealIRCd module **copies** incoming packets to a server implementing the [PROXY protocol (version 1)](http://www.haproxy.org/download/2.3/doc/proxy-protocol.txt).

I wrote this module to test [salty-ircd](https://github.com/lesderid/salty-ircd), by allowing it to accept real traffic from a production (single-server) IRC network running UnrealIRCd.

Important notes:
* Packets sent by clients are still handled as usual by UnrealIRCd
* Packets sent by UnrealIRCd to clients are not relayed to the target server in any way
* Packets sent by the target server are ignored

## Build and installation

Place [proxyv1\_copy.c](/proxyv1_copy.c) in `src/modules/third`, and run `make` and `make install`.

## Configuration

```
proxyv1_copy {
    ip "target-server-ip";
    port target-server-port;
};
```

(Config not implemented yet.)
