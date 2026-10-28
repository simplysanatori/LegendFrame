import struct,sys
def load(p):
    d=open(p,'rb').read()
    def rs(i):
        j=d.index(b'\0',i); return d[i:j].decode('utf8','replace'), j+1
    def parse(i):
        o={}
        while True:
            t=d[i]; i+=1
            if t==8: return o,i
            k,i=rs(i)
            if t==0: o[k],i=parse(i)
            elif t==1: o[k],i=rs(i)
            elif t==2: o[k]=struct.unpack('<I',d[i:i+4])[0]; i+=4
    return parse(0)[0]
def dump(o):
    b=b''
    for k,v in o.items():
        kb=k.encode()+b'\0'
        if isinstance(v,dict): b+=b'\0'+kb+dump(v)
        elif isinstance(v,int): b+=b'\2'+kb+struct.pack('<I',v)
        else: b+=b'\1'+kb+v.encode()+b'\0'
    return b+b'\x08'
if __name__=='__main__':
    for k,v in load(sys.argv[1])['shortcuts'].items(): print(k,v.get('appid'),repr(v.get('AppName')),v.get('Exe'),v.get('LaunchOptions'),'|',v.get('icon'))
