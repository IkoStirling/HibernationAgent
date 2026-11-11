using System;using System.Text;using System.Runtime.InteropServices;using System.Threading;
public static class ChatWindowTest {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback f,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtrW(IntPtr h,int index);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]static extern IntPtr ReadText(IntPtr h,uint m,IntPtr w,StringBuilder text);
 [DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out Rect rect);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 [DllImport("user32.dll")]static extern IntPtr GetWindowDpiAwarenessContext(IntPtr window);
 public static void UseWindowCoordinates(IntPtr window){SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(window));}
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect rect);
 [DllImport("user32.dll")]static extern bool ClientToScreen(IntPtr h,ref Point point);
 [DllImport("user32.dll")]static extern bool GetCursorPos(out Point point);
 [DllImport("user32.dll")]static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]static extern bool LogicalToPhysicalPointForPerMonitorDPI(IntPtr window,ref Point point);
 [DllImport("user32.dll")]static extern IntPtr WindowFromPoint(Point point);
 [DllImport("user32.dll")]static extern uint SendInput(uint n,Input[] data,int size);
 [DllImport("user32.dll")]static extern bool GetGUIThreadInfo(uint thread,ref Gui info);
 [StructLayout(LayoutKind.Sequential)]struct Point{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]struct Gui{public int size,flags;public IntPtr active,focus,capture,menu,move,caret;public Rect rect;}
 [StructLayout(LayoutKind.Sequential)]struct Mouse{public int x,y;public uint data,flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Sequential)]struct Key{public ushort vk,scan;public uint flags,time;public UIntPtr extra;}
 [StructLayout(LayoutKind.Explicit)]struct Payload{[FieldOffset(0)]public Mouse mouse;[FieldOffset(0)]public Key key;}
 [StructLayout(LayoutKind.Sequential)]struct Input{public uint type;public Payload payload;}
 public static IntPtr Focus(IntPtr window){uint pid;var thread=GetWindowThreadProcessId(window,out pid);var info=new Gui{size=Marshal.SizeOf<Gui>()};return GetGUIThreadInfo(thread,ref info)?info.focus:IntPtr.Zero;}
 public static void MiddleClick(IntPtr widget,int x,int y){Point previous;GetCursorPos(out previous);var point=new Point{x=x,y=y};ClientToScreen(widget,ref point);LogicalToPhysicalPointForPerMonitorDPI(widget,ref point);var previousDpi=SetThreadDpiAwarenessContext(new IntPtr(-4));GetCursorPos(out previous);try{for(int attempt=0;attempt<20&&WindowFromPoint(point)!=widget;attempt++)Thread.Sleep(25);SetCursorPos(point.x,point.y);if(WindowFromPoint(point)!=widget){var actual=WindowFromPoint(point);uint pid;GetWindowThreadProcessId(actual,out pid);var cls=new StringBuilder(128);GetClassName(actual,cls,128);Rect r;GetClientRect(widget,out r);throw new Exception("fixture_widget_not_at_cursor point="+point.x+","+point.y+" local="+x+","+y+" client="+r.right+","+r.bottom+" actual="+cls+" pid="+pid);}var inputs=new[]{new Input{type=0,payload=new Payload{mouse=new Mouse{flags=0x20}}},new Input{type=0,payload=new Payload{mouse=new Mouse{flags=0x40}}}};if(SendInput(2,inputs,Marshal.SizeOf<Input>())!=2)throw new Exception("middle_input_failed");}finally{Point current;GetCursorPos(out current);if(current.x==point.x&&current.y==point.y)SetCursorPos(previous.x,previous.y);SetThreadDpiAwarenessContext(previousDpi);}}
 public static void TypeUnicode(IntPtr window,IntPtr input,string text){foreach(char c in text){if(GetForegroundWindow()!=window||Focus(window)!=input)throw new Exception("fixture_input_lost_focus");var keys=new[]{new Input{type=1,payload=new Payload{key=new Key{scan=c,flags=4}}},new Input{type=1,payload=new Payload{key=new Key{scan=c,flags=6}}}};if(SendInput(2,keys,Marshal.SizeOf<Input>())!=2)throw new Exception("text_input_failed");}}
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int left,top,right,bottom;}
 [DllImport("ole32.dll")]static extern int OleInitialize(IntPtr x);
 [DllImport("ole32.dll")]static extern void OleUninitialize();
 [DllImport("ole32.dll")]static extern int OleGetClipboard(out System.Runtime.InteropServices.ComTypes.IDataObject data);
 [DllImport("ole32.dll")]static extern int OleSetClipboard(System.Runtime.InteropServices.ComTypes.IDataObject data);
 [DllImport("ole32.dll")]static extern int OleFlushClipboard();
 [DllImport("user32.dll",SetLastError=true)]static extern bool OpenClipboard(IntPtr x);
 [DllImport("user32.dll")]static extern bool CloseClipboard();
 [DllImport("user32.dll")]static extern IntPtr GetClipboardData(uint format);
 [DllImport("user32.dll")]static extern uint GetClipboardSequenceNumber();
 [DllImport("kernel32.dll")]static extern IntPtr GlobalLock(IntPtr x);
 [DllImport("kernel32.dll")]static extern bool GlobalUnlock(IntPtr x);
 [DllImport("kernel32.dll")]static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
 [DllImport("kernel32.dll")]static extern IntPtr VirtualAllocEx(IntPtr process,IntPtr addr,UIntPtr size,uint alloc,uint protect);
 [DllImport("kernel32.dll")]static extern bool ReadProcessMemory(IntPtr process,IntPtr addr,byte[] data,UIntPtr size,out UIntPtr read);
 [DllImport("kernel32.dll")]static extern bool VirtualFreeEx(IntPtr process,IntPtr addr,UIntPtr size,uint type);
 [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr handle);
 public static string Selection(IntPtr h){uint pid;GetWindowThreadProcessId(h,out pid);var process=OpenProcess(0x38,false,pid);var buffer=VirtualAllocEx(process,IntPtr.Zero,new UIntPtr(8),0x3000,4);try{Send(h,0x434,0,buffer.ToInt64());var data=new byte[8];UIntPtr read;if(!ReadProcessMemory(process,buffer,data,new UIntPtr(8),out read))throw new Exception("selection_read");return BitConverter.ToInt32(data,0)+":"+BitConverter.ToInt32(data,4);}finally{VirtualFreeEx(process,buffer,UIntPtr.Zero,0x8000);CloseHandle(process);}}
 public static IntPtr Find(uint pid,string name){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid){var s=new StringBuilder(128);GetClassName(h,s,128);if(s.ToString()==name){result=h;return false;}}return true;},IntPtr.Zero);return result;}
 public static long Send(IntPtr h,uint m,long w=0,long l=0){IntPtr result;if(SendMessageTimeout(h,m,new IntPtr(w),new IntPtr(l),2,1500,out result)==IntPtr.Zero)throw new Exception("own_window_message_timeout");return result.ToInt64();}
 public static void SetText(IntPtr h,string text){SendText(h,12,IntPtr.Zero,text);}
 public static string Text(IntPtr h){var text=new StringBuilder(300000);ReadText(h,13,new IntPtr(text.Capacity),text);return text.ToString();}
 public static string CopySelection(IntPtr h){
  string copied=null;Exception failure=null;
  var thread=new Thread(()=>{
   OleInitialize(IntPtr.Zero);System.Runtime.InteropServices.ComTypes.IDataObject old=null;uint after=0;
   try{
    int snapshot=0;for(int attempt=0;attempt<12;++attempt){snapshot=OleGetClipboard(out old);if(snapshot>=0)break;if(old!=null){Marshal.ReleaseComObject(old);old=null;}Thread.Sleep(25);}
    if(snapshot<0)throw new Exception("clipboard_snapshot_failed_hresult_"+snapshot.ToString("X8"));
    Send(h,0x301);after=GetClipboardSequenceNumber();
    bool opened=false;int error=0;
    for(int attempt=0;attempt<12&&!opened;++attempt){
     if(GetClipboardSequenceNumber()!=after)throw new Exception("clipboard_changed_during_fixture");
     opened=OpenClipboard(IntPtr.Zero);if(!opened){error=Marshal.GetLastWin32Error();Thread.Sleep(25);}
    }
    if(!opened)throw new Exception("clipboard_busy_win32_"+error);
    try{
     if(GetClipboardSequenceNumber()!=after)throw new Exception("clipboard_changed_during_fixture");
     var handle=GetClipboardData(13);if(handle==IntPtr.Zero)throw new Exception("clipboard_text_unavailable");
     var p=GlobalLock(handle);if(p==IntPtr.Zero)throw new Exception("clipboard_text_locked");
     try{copied=Marshal.PtrToStringUni(p);}finally{GlobalUnlock(handle);}
    }finally{CloseClipboard();}
   }catch(Exception e){failure=e;}
   finally{if(after!=0&&GetClipboardSequenceNumber()==after){OleSetClipboard(old);OleFlushClipboard();}if(old!=null)Marshal.ReleaseComObject(old);OleUninitialize();}
  });
  thread.SetApartmentState(ApartmentState.STA);thread.Start();thread.Join();if(failure!=null)throw failure;return copied;
 }
}
