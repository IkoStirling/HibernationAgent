using System;
public static class AnimationPixels {
 public sealed class Sample {public int Visible,Partial,Invalid;public ulong Hash=14695981039346656037UL;}
 public static Sample Region(byte[] bgra,int width,int height,int left,int top,int size){var result=new Sample();for(int y=Math.Max(0,top);y<Math.Min(height,top+size);y++)for(int x=Math.Max(0,left);x<Math.Min(width,left+size);x++){int p=(y*width+x)*4,a=bgra[p+3];if(a>0)result.Visible++;if(a>0&&a<255)result.Partial++;if(bgra[p]>a||bgra[p+1]>a||bgra[p+2]>a)result.Invalid++;for(int n=0;n<4;n++){result.Hash^=bgra[p+n];result.Hash*=1099511628211UL;}}return result;}
}
