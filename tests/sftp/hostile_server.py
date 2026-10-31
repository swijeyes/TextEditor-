#!/usr/bin/env python3
"""A deliberately hostile SFTP server for TextEditor+'s integration tests.

Needs paramiko (pip install paramiko). Usage:

    hostile_server.py PORT HOST_KEY_FILE

Users:
    pw   password "secret"
    kbd  keyboard-interactive: answers "secret" and "42"

Files it serves (all made up; nothing on disk is touched):
    /small.txt   a normal small file
    /grow.txt    claims to be 10 bytes, but reads never end
    /slow.txt    every read takes 60 seconds to answer
    /bad/        a listing full of names that try to escape the folder
    /huge/       a listing of 20,000 entries
"""

import os
import socket
import stat
import sys
import threading
import time

import paramiko

SMALL = b"hello from the hostile server\n"


def attrs(name, size=0, folder=False):
    a = paramiko.SFTPAttributes()
    a.filename = name
    a.st_size = size
    a.st_mode = (stat.S_IFDIR | 0o755) if folder else (stat.S_IFREG | 0o644)
    a.st_mtime = a.st_atime = int(time.time())
    a.st_uid = a.st_gid = 1000
    return a


class Handle(paramiko.SFTPHandle):
    def __init__(self, path):
        super().__init__()
        self.path = path

    def read(self, offset, length):
        if self.path == "/grow.txt":
            return b"x" * length  # never reaches the end
        if self.path == "/slow.txt":
            time.sleep(60)
            return b"too late"
        data = SMALL
        return data[offset:offset + length] if offset < len(data) else b""

    def stat(self):
        return attrs(os.path.basename(self.path), 10 if self.path != "/small.txt" else len(SMALL))


class Sftp(paramiko.SFTPServerInterface):
    def canonicalize(self, path):
        # normpath keeps a leading "//", so strip slashes first.
        return "/" if path in (".", "") else os.path.normpath("/" + path.lstrip("/"))

    def list_folder(self, path):
        path = self.canonicalize(path)
        if path == "/":
            return [attrs("small.txt", len(SMALL)), attrs("grow.txt", 10),
                    attrs("slow.txt", 10), attrs("bad", folder=True), attrs("huge", folder=True)]
        if path == "/bad":
            return [attrs("../../etc/passwd"), attrs("a/b"), attrs("/etc/shadow"),
                    attrs("nul\x00name"), attrs(".."), attrs("."), attrs("ok.txt", 3)]
        if path == "/huge":
            return [attrs("entry%05d" % i) for i in range(20000)]
        return paramiko.SFTP_NO_SUCH_FILE

    def stat(self, path):
        path = self.canonicalize(path)
        if path in ("/", "/bad", "/huge"):
            return attrs(os.path.basename(path) or "/", folder=True)
        if path == "/small.txt":
            return attrs("small.txt", len(SMALL))
        if path in ("/grow.txt", "/slow.txt"):
            return attrs(os.path.basename(path), 10)
        return paramiko.SFTP_NO_SUCH_FILE

    lstat = stat

    def open(self, path, flags, attr):
        path = self.canonicalize(path)
        if flags & (os.O_WRONLY | os.O_RDWR):
            return paramiko.SFTP_PERMISSION_DENIED
        if self.stat(path) == paramiko.SFTP_NO_SUCH_FILE:
            return paramiko.SFTP_NO_SUCH_FILE
        return Handle(path)


class Server(paramiko.ServerInterface):
    def get_allowed_auths(self, username):
        return "keyboard-interactive" if username == "kbd" else "password"

    def check_auth_password(self, username, password):
        ok = username == "pw" and password == "secret"
        return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED

    def check_auth_interactive(self, username, submethods):
        if username != "kbd":
            return paramiko.AUTH_FAILED
        # The instruction contains terminal escapes, which must not reach
        # the user unfiltered.
        query = paramiko.InteractiveQuery("Hostile login", "Answer \x1b[31mboth\x1b[0m questions")
        query.add_prompt("Password: ", False)
        query.add_prompt("Code: ", True)
        return query

    def check_auth_interactive_response(self, responses):
        ok = list(responses) == ["secret", "42"]
        return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED


def serve(client, host_key):
    try:
        transport = paramiko.Transport(client)
        transport.add_server_key(host_key)
        transport.set_subsystem_handler("sftp", paramiko.SFTPServer, Sftp)
        transport.start_server(server=Server())
        while transport.is_active():
            time.sleep(0.5)
    except Exception:
        pass


def main():
    port = int(sys.argv[1])
    host_key = paramiko.Ed25519Key(filename=sys.argv[2])
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(16)
    print("hostile server listening on", port, flush=True)
    while True:
        client, _ = listener.accept()
        threading.Thread(target=serve, args=(client, host_key), daemon=True).start()


if __name__ == "__main__":
    main()
