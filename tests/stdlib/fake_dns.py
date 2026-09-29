import socket, struct, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 15353))
while True:
    data, addr = s.recvfrom(512)
    i = 12; labels=[]
    while data[i]:
        labels.append(data[i+1:i+1+data[i]].decode()); i += data[i]+1
    name = ".".join(labels); qend = i+5
    if name == "nx.test":
        resp = data[:2]+b"\x81\x83"+data[4:6]+b"\0\0\0\0\0\0"+data[12:qend]
    else:
        # CNAME (compressed) then A
        cname = b"\xc0\x0c"+struct.pack(">HHIH",5,1,60,2)+b"\xc0\x0c"
        a = b"\xc0\x0c"+struct.pack(">HHIH",1,1,60,4)+bytes([10,1,2,3])
        resp = data[:2]+b"\x81\x80"+data[4:6]+b"\0\2\0\0\0\0"+data[12:qend]+cname+a
    s.sendto(resp, addr)
