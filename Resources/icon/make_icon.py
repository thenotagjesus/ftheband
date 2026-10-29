# FtheBand app icon - original vector-style art drawn with code (polygons), no fonts, no third-party art.
# Palette from the landing page styles.css.
from PIL import Image, ImageDraw, ImageFilter, ImageChops
import struct, io, sys

BG0, BG1 = (22,22,22), (7,7,7)          # desk gradient -> page bg
INK = (244,238,228); RED = (255,42,42); SHADOW = (26,0,0); PAPER=(201,184,150)
S = 2048                                  # supersampled design grid (2x of 1024 units)
U = S/1024

def rr_mask(size, radius):
    m = Image.new("L", (size,size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0,0,size-1,size-1], radius=radius, fill=255)
    return m

def poly(d, pts, fill, off=(0,0)):
    d.polygon([((x+off[0])*U,(y+off[1])*U) for x,y in pts], fill=fill)

def rect(x0,y0,x1,y1): return [(x0,y0),(x1,y0),(x1,y1),(x0,y1)]

def letter_F(x, y, cap, stem, arm, serif=True):
    """Heavy slab-serif F. (x,y)=top-left, cap=cap height."""
    P=[]
    P.append(rect(x+serif*-0, y, x+stem, y+cap))                         # stem
    P.append(rect(x, y, x+arm, y+stem*0.9))                              # top arm
    P.append(rect(x, y+cap*0.42, x+arm*0.78, y+cap*0.42+stem*0.78))      # mid arm
    if serif:
        sh=stem*0.55
        P.append(rect(x+arm-stem*0.45, y, x+arm, y+stem*0.9+sh))                       # top arm drop serif
        P.append(rect(x+arm*0.78-stem*0.38, y+cap*0.42-sh*0.55, x+arm*0.78, y+cap*0.42+stem*0.78+sh*0.55)) # mid arm serif
        P.append(rect(x-stem*0.35, y+cap-stem*0.42, x+stem*1.35, y+cap))               # foot slab
    return P

def letter_T(x, y, cap, stem, bar, serif=True):
    P=[]
    P.append(rect(x, y, x+bar, y+stem*0.9))                              # bar
    cx = x+bar/2
    P.append(rect(cx-stem/2, y, cx+stem/2, y+cap))                       # stem
    if serif:
        sh=stem*0.55
        P.append(rect(x, y, x+stem*0.45, y+stem*0.9+sh))                 # left drop serif
        P.append(rect(x+bar-stem*0.45, y, x+bar, y+stem*0.9+sh))         # right drop serif
        P.append(rect(cx-stem*0.85, y+cap-stem*0.42, cx+stem*0.85, y+cap))  # foot slab
    return P

def draw_letters(d, detail, shadow):
    if detail:
        cap, stem = 540, 128
        fx, ty = 128, 255
        F = letter_F(fx, ty, cap, stem, 330)
        T = letter_T(fx+330+34, ty, cap, stem, 400)
        TX = fx+330+34
    else:                       # small sizes: bolder, bigger, fewer details
        cap, stem = 640, 190
        fx, ty = 70, 192
        F = letter_F(fx, ty, cap, stem, 420, serif=False)
        T = letter_T(fx+420+34, ty, cap, stem, 450, serif=False)
    off = (10,10) if detail else (14,14)
    if shadow:
        for r in F+T: poly(d, r, SHADOW, off)
    return F, T

def render_design(detail=True):
    """returns RGBA image at S x S"""
    img = Image.new("RGBA", (S,S), (0,0,0,0))
    # background tile: vertical gradient like .desk, rounded
    g = Image.new("RGB", (1,S))
    for yy in range(S):
        t = yy/(S-1); g.putpixel((0,yy), tuple(int(BG0[i]*(1-t)+BG1[i]*t) for i in range(3)))
    g = g.resize((S,S))
    tile = g.convert("RGBA")
    # scanlines (detail only) - like .scan
    if detail:
        sl = Image.new("RGBA", (S,S), (0,0,0,0)); sd = ImageDraw.Draw(sl)
        for yy in range(0, S, 12): sd.rectangle([0,yy,S,yy+4], fill=(0,0,0,70))
        tile = Image.alpha_composite(tile, sl)
    # inner edge line like the 1px #2a2a2a desk border
    ed = ImageDraw.Draw(tile)
    ed.rounded_rectangle([6,6,S-7,S-7], radius=int(190*U)-6, outline=(42,42,42,255), width=int(6 if detail else 10))
    img.paste(tile, (0,0), rr_mask(S, int(190*U)))
    # red glow behind T
    d = ImageDraw.Draw(img)
    layer = Image.new("RGBA",(S,S),(0,0,0,0)); ld = ImageDraw.Draw(layer)
    F,T = draw_letters(ld, detail, shadow=True)   # shadow only first on layer
    img = Image.alpha_composite(img, layer)
    d = ImageDraw.Draw(img)
    if detail:
        glow = Image.new("RGBA",(S,S),(0,0,0,0)); gd = ImageDraw.Draw(glow)
        for r in T: poly(gd, r, RED+(255,))
        glow = glow.filter(ImageFilter.GaussianBlur(34*U/2))
        a = glow.split()[3].point(lambda v:int(v*0.55)); glow.putalpha(a)
        img = Image.alpha_composite(img, glow); d = ImageDraw.Draw(img)
    for r in F: poly(d, r, INK+(255,))
    for r in T: poly(d, r, RED+(255,))
    if detail:
        # red rule under the mark (the 3px red border on the page) + three little LEDs (.led .dim .on)
        d.rectangle([128*U, 838*U, 896*U, 858*U], fill=RED+(255,))
        for i,(c,gl) in enumerate([((42,16,16),False),((255,176,32),False),(RED,True)]):
            cx, cy, r = (150+i*62)*U, 160*U, 22*U
            if gl:
                gl_l = Image.new("RGBA",(S,S),(0,0,0,0)); ImageDraw.Draw(gl_l).ellipse([cx-r*1.4,cy-r*1.4,cx+r*1.4,cy+r*1.4], fill=RED+(255,))
                gl_l = gl_l.filter(ImageFilter.GaussianBlur(26)); gl_l.putalpha(gl_l.split()[3].point(lambda v:int(v*0.8)))
                img = Image.alpha_composite(img, gl_l); d = ImageDraw.Draw(img)
            fill = c + ((255,) if len(c)==3 else ())
            if c==(255,176,32): fill=(120,84,16,255)   # dim amber
            d.ellipse([cx-r,cy-r,cx+r,cy+r], fill=fill)
    return img

def at(img, n): return img.resize((n,n), Image.LANCZOS)

if __name__ == "__main__":
    full = render_design(True); small = render_design(False)
    full.resize((1024,1024), Image.LANCZOS).save("ftheband-icon-1024.png")
    sizes = [16,24,32,48,64,128,256]
    imgs = {n: (at(small,n) if n<=32 else at(full,n)) for n in sizes}
    at(full,256).save("ftheband-icon-256.png"); at(small,32).save("ftheband-icon-32.png")
    for n in sizes: imgs[n].save(f"png_{n}.png")
    # hand-built multi-size ICO (PNG-compressed entries, so each size keeps its own art)
    blobs=[]
    for n in sizes:
        b=io.BytesIO(); imgs[n].save(b,"PNG",optimize=True); blobs.append((n,b.getvalue()))
    hdr=struct.pack("<HHH",0,1,len(blobs)); off=6+16*len(blobs); ent=b""; data=b""
    for n,b in blobs:
        ent+=struct.pack("<BBBBHHII", 0 if n>=256 else n, 0 if n>=256 else n, 0,0,1,32,len(b),off+len(data)); data+=b
    open("ftheband.ico","wb").write(hdr+ent+data)
    # preview sheet
    W=1500; sheet=Image.new("RGB",(W,900),(30,28,26)); sd=ImageDraw.Draw(sheet)
    sheet.paste(Image.alpha_composite(Image.new("RGBA",(560,560),(30,28,26,255)), at(full,560)).convert("RGB"),(30,30))
    x=640; 
    for bgc,y in [((255,255,255),40),((10,10,10),330)]:
        sd.rectangle([620,y-10,W-20,y+250],fill=bgc); x=640
        for n in [256,128,64,48,32,24,16]:
            im=imgs[n]; sheet.paste(im,(x,y+ (256-n)//2 ),im); x+=n+22
    # 16px/32px zoomed (nearest) to check legibility
    z=imgs[16].resize((256,256),Image.NEAREST); sheet.paste(z,(640,620),z)
    z2=imgs[32].resize((256,256),Image.NEAREST); sheet.paste(z2,(930,620),z2)
    sd.text((640,600),"16 px and 32 px, 16x zoom / 8x zoom (nearest)",fill=(200,200,200))
    sd.text((30,610),"FtheBand icon - original art, drawn with code (Pillow), page palette",fill=(200,200,200))
    sheet.save("ftheband-icon-preview.png")
