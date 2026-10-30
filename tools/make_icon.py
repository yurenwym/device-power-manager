"""Optional asset generator. The application and its build do not need Python."""
import math
import struct
import zlib
from pathlib import Path

def raster(size):
    pixels=bytearray()
    samples=4 if size<=64 else 2
    for y in range(size-1,-1,-1):
        for x in range(size):
            sums=[0,0,0,0]
            for sy in range(samples):
                for sx in range(samples):
                    a=(x+(sx+.5)/samples)/size
                    b=(y+(sy+.5)/samples)/size
                    if math.hypot(a-.5,b-.5)>.47:continue
                    dx,dy=a-.5,b-.53
                    ring=abs(math.hypot(dx,dy)-.245)<.043 and abs(math.atan2(dx,-dy))>math.radians(38)
                    stem=abs(dx)<.043 and .175<b<.49
                    color=(255,255,255) if ring or stem else (24,119,88)
                    for i,v in enumerate(color):sums[i]+=v
                    sums[3]+=255
            n=samples*samples
            # ICO stores straight BGRA; only alpha is averaged at transparent edges.
            alpha=sums[3]/n
            if sums[3]:rgb=[round(v*255/sums[3]) for v in sums[:3]]
            else:rgb=[0,0,0]
            pixels.extend((rgb[2],rgb[1],rgb[0],round(alpha)))
    return bytes(pixels)

sizes=[16,20,24,32,48,64,128,256]
images=[]
def png(size,data):
    rows=[]
    for y in range(size-1,-1,-1):
        row=bytearray([0])
        for x in range(size):
            b,g,r,a=data[(y*size+x)*4:(y*size+x+1)*4]
            row.extend((r,g,b,a))
        rows.append(bytes(row))
    def chunk(tag,payload):
        return struct.pack('>I',len(payload))+tag+payload+struct.pack('>I',zlib.crc32(tag+payload)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows),9))+chunk(b'IEND',b'')
for size in sizes:
    data=raster(size)
    if size>=64:
        images.append(png(size,data));continue
    header=struct.pack('<IiiHHIIiiII',40,size,size*2,1,32,0,len(data),0,0,0,0)
    mask=bytes(((size+31)//32)*4*size)
    images.append(header+data+mask)
offset=6+16*len(images)
entries=[]
for size,data in zip(sizes,images):
    entries.append(struct.pack('<BBBBHHII',size%256,size%256,0,0,1,32,len(data),offset))
    offset+=len(data)
root=Path(__file__).resolve().parents[1]
(root/'assets').mkdir(exist_ok=True)
(root/'assets/power.ico').write_bytes(struct.pack('<HHH',0,1,len(images))+b''.join(entries)+b''.join(images))
(root/'assets/power.png').write_bytes(png(128,raster(128)))
