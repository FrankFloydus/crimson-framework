# Copyright Francesco Di Blasi 2026 - All Rights Reserved

"""Serve deterministic HTTP and optional self-signed HTTPS responses on loopback."""

import argparse
import json
import ssl
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class SteamHttpFixture(BaseHTTPRequestHandler):
    """Respond to the Steam HTTP fixture's known requests."""

    def log_message(self, *_args):
        """Keep the harness output limited to the bound port."""

    def do_GET(self):
        """Serve responses for success, HTTP status, deadline, and body limits."""
        if self.path == "/ok":
            self.respond(200, b"crimson-http-ok")
        elif self.path == "/status":
            self.respond(418, b"crimson-http-status")
        elif self.path == "/slow":
            time.sleep(3)
            self.respond(200, b"crimson-http-slow")
        elif self.path == "/large":
            self.respond(200, b"x" * 16384)
        elif self.path == "/abort":
            self.close_connection = True
        elif self.path == "/json":
            self.respond_json()
        elif self.path == "/json-null":
            self.respond(200, b"null", "application/json")
        elif self.path == "/json-nul":
            self.respond(200, b'{"mode":"rotation"}\x00extra', "application/json")
        elif self.path == "/unavailable":
            self.respond(503, b"crimson-http-unavailable")
        elif self.path == "/large-header":
            self.respond(200, b"crimson-http-ok", headers=(("X-Fixture-Large", "x" * 2049),))
        else:
            self.respond(404, b"unknown test route")

    def do_POST(self):
        """Echo one bounded request body and its test header."""
        if self.path == "/json":
            self.echo_json()
            return
        length = self.headers.get("Content-Length", "")
        if (self.path != "/echo" or self.headers.get("X-Crimson-Test") != "scoped"
                or not length.isdecimal() or int(length) > 1024 * 1024):
            self.respond(400, b"invalid test request")
            return
        self.respond(200, self.rfile.read(int(length)))

    def do_PUT(self):
        """Echo a JSON object submitted with PUT."""
        self.echo_json()

    def do_PATCH(self):
        """Echo a JSON object submitted with PATCH."""
        self.echo_json()

    def do_HEAD(self):
        """Send metadata for the JSON route with no body."""
        if self.path == "/json":
            self.respond_json(head=True)
        else:
            self.respond(404, b"unknown test route", head=True)

    def do_DELETE(self):
        """Acknowledge a fixture request without mutating stored data."""
        if self.path == "/json":
            self.respond(200, b"crimson-http-deleted", headers=(("X-Fixture-Method", "DELETE"),))
        else:
            self.respond(404, b"unknown test route")

    def do_OPTIONS(self):
        """Advertise the fixture route's supported methods."""
        if self.path == "/json":
            self.respond(204, b"", headers=(("Allow", "GET, HEAD, POST, PUT, PATCH, DELETE, OPTIONS"),))
        else:
            self.respond(404, b"unknown test route")

    def respond_json(self, head=False):
        """Return deterministic JSON with a checked request-header marker."""
        accept = "json" if self.headers.get("Accept") == "application/json" else "other"
        self.respond(200, b'{"mode":"rotation"}', "application/json",
                     (("X-Fixture-Accept", accept),), head=head)

    def echo_json(self):
        """Confirm bounded JSON-object delivery and echo it with the exact method."""
        length = self.headers.get("Content-Length", "")
        if (self.path != "/json" or self.headers.get("Content-Type") != "application/json"
                or not length.isdecimal() or int(length) > 1024 * 1024):
            self.respond(400, b"invalid test request")
            return
        body = self.rfile.read(int(length))
        try:
            payload = json.loads(body)
        except (UnicodeDecodeError, ValueError, RecursionError):
            self.respond(400, b"invalid JSON")
            return
        if not isinstance(payload, dict):
            self.respond(400, b"expected object")
            return
        self.respond(200, body, "application/json", (("X-Fixture-Method", self.command),))

    def respond(self, status, body, content_type="application/octet-stream", headers=(), head=False):
        """Send a response and tolerate disconnects from cancelled requests."""
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Type", content_type)
        for name, value in headers:
            self.send_header(name, value)
        try:
            self.end_headers()
            if not head:
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    """Publish the test port or both ports when given a self-signed certificate."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--tls-cert")
    parser.add_argument("--tls-key")
    args = parser.parse_args()
    if bool(args.tls_cert) != bool(args.tls_key):
        parser.error("Supply both the TLS certificate and private key.")
    with ThreadingHTTPServer(("127.0.0.1", 0), SteamHttpFixture) as server:
        print(f"CRIMSON_STEAM_HTTP_PORT={server.server_port}", flush=True)
        if not args.tls_cert:
            server.serve_forever()
            return
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(args.tls_cert, args.tls_key)
        with ThreadingHTTPServer(("127.0.0.1", 0), SteamHttpFixture) as secure:
            secure.socket = context.wrap_socket(secure.socket, server_side=True)
            threading.Thread(target=secure.serve_forever, daemon=True).start()
            print(f"CRIMSON_STEAM_HTTPS_PORT={secure.server_port}", flush=True)
            server.serve_forever()


if __name__ == "__main__":
    main()
