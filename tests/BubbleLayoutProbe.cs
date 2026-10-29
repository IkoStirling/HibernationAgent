using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class BubbleLayoutProbe {
 delegate bool Callback(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]struct Rect {public int left,top,right,bottom;}
 [DllImport("user32.dll")]static extern bool EnumChildWindows(IntPtr h,Callback f,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder name,int n);
 [DllImport("user32.dll")]static extern int GetDlgCtrlID(IntPtr window);
 [DllImport("user32.dll")]static extern bool GetClientRect(IntPtr h,out Rect rect);
 [DllImport("user32.dll")]static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int hgt,uint flags);
 [DllImport("kernel32.dll")]static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
 [DllImport("kernel32.dll")]static extern IntPtr VirtualAllocEx(IntPtr p,IntPtr addr,UIntPtr size,uint alloc,uint protect);
 [DllImport("kernel32.dll")]static extern bool ReadProcessMemory(IntPtr p,IntPtr addr,byte[] data,UIntPtr size,out UIntPtr count);
 [DllImport("kernel32.dll")]static extern bool VirtualFreeEx(IntPtr p,IntPtr addr,UIntPtr size,uint type);
 [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
 static long Send(IntPtr h,uint m,IntPtr w,IntPtr l){IntPtr result;if(SendMessageTimeout(h,m,w,l,2,2000,out result)==IntPtr.Zero)throw new Exception("owned_bubble_timeout");return result.ToInt64();}
 static void Own(IntPtr window,uint expected){uint pid;GetWindowThreadProcessId(window,out pid);if(pid!=expected)throw new ArgumentException("owned_fixture_window_required");}
 public sealed class Geometry {public IntPtr Window;public int Length,Height,Width,LastY,Lines;}
 public static Geometry[] Inspect(IntPtr viewport,uint pid){Own(viewport,pid);var rows=new List<Geometry>();EnumChildWindows(viewport,(h,p)=>{Own(h,pid);var name=new StringBuilder(80);GetClassName(h,name,name.Capacity);if(GetDlgCtrlID(h)<2000||!name.ToString().Contains("RICHEDIT",StringComparison.OrdinalIgnoreCase))return true;Rect r;GetClientRect(h,out r);int length=(int)Send(h,14,IntPtr.Zero,IntPtr.Zero);if(length==0)return true;
   var process=OpenProcess(0x38,false,pid);if(process==IntPtr.Zero)throw new Exception("owned_bubble_process_access");var point=VirtualAllocEx(process,IntPtr.Zero,new UIntPtr(8),0x3000,4);try{Send(h,0x426,point,new IntPtr(length-1));var bytes=new byte[8];UIntPtr read;if(!ReadProcessMemory(process,point,bytes,new UIntPtr(8),out read))throw new Exception("owned_bubble_geometry_read");rows.Add(new Geometry{Window=h,Length=length,Height=r.bottom,Width=r.right,LastY=BitConverter.ToInt32(bytes,4),Lines=(int)Send(h,0xBA,IntPtr.Zero,IntPtr.Zero)});}finally{if(point!=IntPtr.Zero)VirtualFreeEx(process,point,UIntPtr.Zero,0x8000);CloseHandle(process);}return true;},IntPtr.Zero);return rows.ToArray();}
 public static void Resize(IntPtr window,uint pid,int width,int height){Own(window,pid);if(!SetWindowPos(window,IntPtr.Zero,0,0,width,height,0x16))throw new Exception("owned_fixture_resize");}
}
