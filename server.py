import json, os, sys
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from urllib.request import Request, urlopen
ROOT = os.path.dirname(os.path.abspath(__file__))
class Handler(SimpleHTTPRequestHandler):
    def __init__(self,*args,**kwargs): super().__init__(*args,directory=ROOT,**kwargs)
    def do_POST(self):
        if self.path != '/api/assistant': self.send_error(404); return
        try:
            n=int(self.headers.get('Content-Length','0')); data=json.loads(self.rfile.read(n) or b'{}'); text=str(data.get('text','')).strip()[:1000]; key=os.environ.get('OPENAI_API_KEY','')
            if not key: self._json({'answer':'I can help with that. Start by defining one small next step, then tap the watch again when you are ready.','mode':'local-demo'}); return
            model=os.environ.get('OPENAI_MODEL','gpt-5-mini'); body=json.dumps({'model':model,'input':'You are VEYORU, a concise smartwatch assistant. User: '+text}).encode(); req=Request('https://api.openai.com/v1/responses',data=body,headers={'Authorization':'Bearer '+key,'Content-Type':'application/json'})
            with urlopen(req,timeout=25) as res: out=json.loads(res.read())
            answer=out.get('output_text','')
            if not answer:
                for item in out.get('output',[]):
                    for c in item.get('content',[]):
                        if c.get('text'): answer=c['text']; break
            self._json({'answer':answer or 'The model returned no text.','mode':'openai'})
        except Exception as exc: self._json({'answer':'The assistant gateway is unavailable right now.','mode':'error','error':str(exc)},502)
    def _json(self,payload,status=200):
        raw=json.dumps(payload).encode(); self.send_response(status); self.send_header('Content-Type','application/json'); self.send_header('Access-Control-Allow-Origin','*'); self.send_header('Content-Length',str(len(raw))); self.end_headers(); self.wfile.write(raw)
if __name__=='__main__':
    port=int(sys.argv[sys.argv.index('--port')+1]) if '--port' in sys.argv else 8000; print(f'VEYORU lab: http://localhost:{port}/'); ThreadingHTTPServer(('127.0.0.1',port),Handler).serve_forever()
