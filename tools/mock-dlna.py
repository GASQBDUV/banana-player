#!/usr/bin/env python3
# A small DLNA/UPnP media server for trying the DLNA backend without a real one (see DEVELOPMENT.md): a ContentDirectory
# with folders, titles with no extension, two files of one title, a folder of 450 files (paging), non-ASCII and
# XML-special characters in titles, and HTTP Range reads of a 5 MiB random file (saved as /tmp/mock.bin, its md5 printed).
# The folder Quirks has what servers get wrong: "Alien", "Alien", "Alien (2)"; titles with extensions the player knows
# (.m2ts, .m4b); items with no res@size whose server drops HEAD, ignores Range (200, the whole file) or answers HEAD after
# 20 s (for cancel-open); an item redirected to file://; a folder whose id is gone. Browsing an id that isn't there is a SOAP fault, UPnP error 701.
#   python3 tools/mock-dlna.py [port]            then:   build/bp-net ls dlna://http://127.0.0.1:8765/desc.xml - - /
import http.server, socketserver, os, re, sys, hashlib
from xml.sax.saxutils import escape
PORT=int(sys.argv[1]) if len(sys.argv)>1 else 8765
DATA=os.urandom(5*1024*1024+123)
open('/tmp/mock.bin','wb').write(DATA)
BASE=f'http://127.0.0.1:{PORT}'
def res(i,kind=''): return f'{BASE}/{kind}/{i}' if kind else f'{BASE}/media/{i}'
TREE={ '0':[('c','1','Filmer'),('c','2','Musik & Ljud'),('c','3','Många'),('c','5','Quirks')],
 '1':[('i','10','Alien åäö',"video/x-matroska"),('i','11','Alien åäö',"video/x-matroska"),('i','12','clip.mp4',"video/mp4"),('i','13','Ruta <1> "x"',"video/mp4")],
 '2':[('i','20','Song 1',"audio/mpeg"),('c','4','Album')],
 '4':[('i','40','Låt',"audio/flac")],
 '3':[('i',str(1000+n),f'Fil {n:03d}',"video/mp4") for n in range(450)],
 '5':[('i','50','Alien',"video/mp4"),('i','51','Alien',"video/mp4"),('i','52','Alien (2)',"video/mp4"),('i','53','Concert.m2ts',"video/mp2t"),
      ('i','54','Audiobook.m4b',"audio/mp4"),('i','55','No HEAD',"video/mp4",'nohead'),('i','56','No Range',"video/mp4",'norange'),
      ('i','57','Slow HEAD',"video/mp4",'slowhead'),('i','58','To a file',"video/mp4",'redir'),('c','99','Gone after a refresh')]}
NOSIZE={'nohead','norange','slowhead'}
DESC=f'''<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>Mock</friendlyName><serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><controlURL>/cd/control</controlURL></service></serviceList></device></root>'''
def didl(oid,start,count):
    kids=TREE.get(oid,[]); total=len(kids); part=kids[start:start+count]; out=''
    for k in part:
        if k[0]=='c': out+=f'<container id="{k[1]}" parentID="{oid}" restricted="1"><dc:title>{escape(k[2])}</dc:title><upnp:class>object.container</upnp:class></container>'
        else:
            kind=k[4] if len(k)>4 else ''; size='' if kind in NOSIZE else f' size="{len(DATA)}"'
            out+=f'<item id="{k[1]}" parentID="{oid}" restricted="1"><dc:title>{escape(k[2])}</dc:title><upnp:class>object.item.videoItem</upnp:class><res protocolInfo="http-get:*:{k[3]}:*"{size}>{res(k[1],kind)}?a=1&amp;b=2</res></item>'
    x=f'<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">{out}</DIDL-Lite>'
    return x,len(part),total
class H(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*a): pass
    def do_GET(self):
        if self.path.startswith('/desc.xml'):
            b=DESC.encode(); self.send_response(200); self.send_header('Content-Length',str(len(b))); self.end_headers(); self.wfile.write(b); return
        if self.path.startswith('/redir/'):
            self.send_response(302); self.send_header('Location','file:///etc/passwd'); self.send_header('Content-Length','0'); self.end_headers(); return
        if self.path.startswith('/norange/'):
            self.send_response(200); self.send_header('Content-Length',str(len(DATA))); self.end_headers()
            try: self.wfile.write(DATA)
            except OSError: pass
            return
        if self.path.startswith(('/media/','/nohead/','/slowhead/')):
            rng=self.headers.get('Range'); n=len(DATA)
            if rng:
                m=re.match(r'bytes=(\d+)-(\d*)',rng); a=int(m.group(1)); b=int(m.group(2)) if m.group(2) else n-1; b=min(b,n-1)
                if a>=n: self.send_response(416); self.send_header('Content-Length','0'); self.end_headers(); return
                body=DATA[a:b+1]; self.send_response(206); self.send_header('Content-Range',f'bytes {a}-{b}/{n}')
            else: body=DATA; self.send_response(200)
            self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body); return
        self.send_response(404); self.send_header('Content-Length','0'); self.end_headers()
    def do_HEAD(self):
        if self.path.startswith('/nohead/'): self.close_connection=True; return	# dropped: no answer at all
        if self.path.startswith('/norange/'): self.send_response(405); self.send_header('Content-Length','0'); self.end_headers(); return
        if self.path.startswith('/slowhead/'): import time; time.sleep(20)
        self.send_response(200); self.send_header('Content-Length',str(len(DATA))); self.end_headers()
    def do_POST(self):
        l=int(self.headers['Content-Length']); body=self.rfile.read(l).decode()
        oid=re.search(r'<ObjectID>(.*?)</ObjectID>',body).group(1); st=int(re.search(r'<StartingIndex>(\d+)',body).group(1)); rc=int(re.search(r'<RequestedCount>(\d+)',body).group(1))
        if oid not in TREE:
            f='<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail><UPnPError xmlns="urn:schemas-upnp-org:control-1-0"><errorCode>701</errorCode><errorDescription>No such object</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>'.encode()
            self.send_response(500); self.send_header('Content-Type','text/xml'); self.send_header('Content-Length',str(len(f))); self.end_headers(); self.wfile.write(f); return
        d,nr,tot=didl(oid,st,min(rc,100) or 100)
        resp=f'<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:BrowseResponse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1"><Result>{escape(d)}</Result><NumberReturned>{nr}</NumberReturned><TotalMatches>{tot}</TotalMatches><UpdateID>1</UpdateID></u:BrowseResponse></s:Body></s:Envelope>'.encode()
        self.send_response(200); self.send_header('Content-Type','text/xml'); self.send_header('Content-Length',str(len(resp))); self.end_headers(); self.wfile.write(resp)
class S(socketserver.ThreadingMixIn,http.server.HTTPServer): daemon_threads=True
print('md5',hashlib.md5(DATA).hexdigest(),flush=True)
S(('127.0.0.1',PORT),H).serve_forever()
