#!/usr/bin/env python3
"""Sends a CIA to FBI's Remote Install over Wi-Fi, so the SD card stays in the 3DS.
On the 3DS: FBI > Remote Install > Receive URLs over the network (it shows the console's IP).
Usage: send-cia.py <3DS IP> [file.cia]   (default: PKForge.cia next to this tools folder)
The PC serves the file over HTTP on port 8080 and tells FBI where to fetch it (FBI's protocol: a big-endian
u32 length, then the URLs, on TCP port 5000); FBI answers one byte when it is done."""
import http.server
import os
import socket
import socketserver
import struct
import sys
import threading
import urllib.parse

PORT = 8080


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    ds_ip = sys.argv[1]
    path = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else
                           os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'PKForge.cia'))
    folder, name = os.path.split(path)

    # The PC's address on the 3DS's network: the one a socket towards the 3DS goes out from.
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((ds_ip, 5000))
    pc_ip = probe.getsockname()[0]
    probe.close()

    handler = lambda *a, **k: http.server.SimpleHTTPRequestHandler(*a, directory=folder, **k)
    socketserver.TCPServer.allow_reuse_address = True
    server = socketserver.TCPServer(('', PORT), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    url = 'http://%s:%d/%s' % (pc_ip, PORT, urllib.parse.quote(name))
    print('Serving %s (%d bytes) at %s' % (name, os.path.getsize(path), url))
    payload = url.encode('ascii')
    with socket.create_connection((ds_ip, 5000), timeout=15) as s:
        s.sendall(struct.pack('>I', len(payload)) + payload)
        print('Sent to FBI at %s. Confirm on the 3DS; waiting for it to finish...' % ds_ip)
        s.settimeout(None)
        s.recv(1)  # one byte when FBI is done (or the connection closes)
    server.shutdown()
    print('Done: FBI has the file.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
