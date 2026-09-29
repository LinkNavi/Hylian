import http.server, socketserver, sys, os
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self,*a): pass
    def do_GET(self):
        if self.path == "/hello":
            b=b"hello world\n"; self.send_response(200); self.send_header("Content-Length",str(len(b))); self.send_header("X-Test","Yes"); self.end_headers(); self.wfile.write(b)
        elif self.path == "/redir":
            self.send_response(302); self.send_header("Location","/hello"); self.send_header("Content-Length","0"); self.end_headers()
        elif self.path == "/chunked":
            self.send_response(200); self.send_header("Transfer-Encoding","chunked"); self.end_headers()
            for c in (b"abc", b"defgh", b"x"*300):
                self.wfile.write(b"%x\r\n"%len(c)+c+b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/bin":
            b=bytes(range(256))*20; self.send_response(200); self.send_header("Content-Length",str(len(b))); self.end_headers(); self.wfile.write(b)
        elif self.path.endswith(".tar.gz") or self.path.endswith(".tar"):
            b=open("."+self.path,"rb").read(); self.send_response(200); self.send_header("Content-Length",str(len(b))); self.end_headers(); self.wfile.write(b)
        else:
            self.send_response(404); self.send_header("Content-Length","0"); self.end_headers()
    def do_POST(self):
        n=int(self.headers["Content-Length"]); d=self.rfile.read(n)
        b=b"got:"+d; self.send_response(200); self.send_header("Content-Length",str(len(b))); self.end_headers(); self.wfile.write(b)
socketserver.TCPServer.allow_reuse_address=True
if len(sys.argv) > 1 and sys.argv[1] == "tls":
    import ssl
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", 18443), H)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain("cert.pem", "key.pem")
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
else:
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", 18099), H)
srv.serve_forever()
