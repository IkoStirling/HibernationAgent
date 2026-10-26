using System;
using System.Collections.Generic;

// Examines pixels returned by the test process's own window, never a desktop
// screenshot. Alpha is unused by GDI color-key buffers; RGB is authoritative.
public static class RenderedPixels {
    public sealed class Extent {public int Count,Left=int.MaxValue,Top=int.MaxValue,Right=-1,Bottom=-1;}
    public static Extent Avatar(byte[] bgra,int width,int height) {
        if(width<=0||height<=0||bgra.Length!=checked(width*height*4))throw new ArgumentException("invalid_owned_frame");
        var colors=new HashSet<int>{0x305543,0x7ba769,0xd2dea3,0x192b21,0xafca92,0x455a6c,0x9bb4c5,0xe1e9df,0x273a50,0xc8d4df,0x74523c,0xd6a66b,0xf3dfb0,0x463b34,0xeac48b};var result=new Extent();
        for(int y=0;y<height;y++)for(int x=0;x<width;x++){int offset=(y*width+x)*4;int rgb=(bgra[offset+2]<<16)|(bgra[offset+1]<<8)|bgra[offset];if(!colors.Contains(rgb))continue;result.Count++;result.Left=Math.Min(result.Left,x);result.Right=Math.Max(result.Right,x);result.Top=Math.Min(result.Top,y);result.Bottom=Math.Max(result.Bottom,y);}
        return result;
    }
}
