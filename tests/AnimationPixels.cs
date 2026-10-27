using System;
public static class AnimationPixels {
 public sealed class Sample {public int Visible,Partial,Invalid;public ulong Hash=14695981039346656037UL;}
 public sealed class PixelMatch {public int Compared,Mismatched;}
 public static PixelMatch Nearest(byte[] bgra,int width,int height,int left,int top,int size,byte[] original,int sourceWidth,int sourceHeight,bool mirror=false){
  var result=new PixelMatch();for(int y=Math.Max(0,top);y<Math.Min(height,top+size);y++)for(int x=Math.Max(0,left);x<Math.Min(width,left+size);x++){
   int sx=(x-left)*sourceWidth/size;if(mirror)sx=sourceWidth-1-sx;int source=(((y-top)*sourceHeight/size)*sourceWidth+sx)*4;int p=(y*width+x)*4;
   int alpha=original[source+3];result.Compared++;bool wrong=bgra[p+3]!=alpha;
   for(int n=0;n<3;n++)if(Math.Abs(bgra[p+n]-(original[source+n]*alpha+127)/255)>1)wrong=true;
   if(wrong)result.Mismatched++;
  }return result;
 }
 public static int Outside(byte[] bgra,int width,int height,int left,int top,int size){int count=0;for(int y=0;y<height;y++)for(int x=0;x<width;x++)if((x<left||x>=left+size||y<top||y>=top+size)&&bgra[(y*width+x)*4+3]>0)count++;return count;}
 public static Sample Region(byte[] bgra,int width,int height,int left,int top,int size){var result=new Sample();for(int y=Math.Max(0,top);y<Math.Min(height,top+size);y++)for(int x=Math.Max(0,left);x<Math.Min(width,left+size);x++){int p=(y*width+x)*4,a=bgra[p+3];if(a>0)result.Visible++;if(a>0&&a<255)result.Partial++;if(bgra[p]>a||bgra[p+1]>a||bgra[p+2]>a)result.Invalid++;for(int n=0;n<4;n++){result.Hash^=bgra[p+n];result.Hash*=1099511628211UL;}}return result;}
}
