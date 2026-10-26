using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

// Own-process test fixture only. It creates an inactive desktop and never
// calls SwitchDesktop or changes the user's active desktop / window focus.
public sealed class PrivateDesktop : IDisposable {
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct Startup {
        public int size;public string reserved,desktop,title;
        public int x,y,width,height,charsX,charsY,fill,flags;
        public short show,reservedSize;public IntPtr reservedBytes,input,output,error;
    }
    [StructLayout(LayoutKind.Sequential)] struct Created {public IntPtr process,thread;public uint processId,threadId;}
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern IntPtr CreateDesktopW(string name,IntPtr device,IntPtr mode,uint flags,uint access,IntPtr security);
    [DllImport("user32.dll")]static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool CreateProcessW(string application,StringBuilder arguments,IntPtr processSecurity,IntPtr threadSecurity,bool inherit,uint flags,IntPtr environment,string directory,ref Startup startup,out Created result);
    [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")]static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
    [DllImport("kernel32.dll")]static extern bool ReadProcessMemory(IntPtr process,IntPtr address,byte[] data,UIntPtr size,out UIntPtr count);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr GetPropW(IntPtr window,string name);
    [DllImport("user32.dll")]public static extern uint GetGuiResources(IntPtr process,uint kind);
    delegate bool WindowCallback(IntPtr window,IntPtr context);
    [DllImport("user32.dll")]static extern bool EnumDesktopWindows(IntPtr desktop,WindowCallback callback,IntPtr context);
    [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr window,StringBuilder name,int capacity);
    [DllImport("user32.dll")]static extern bool SetThreadDesktop(IntPtr desktop);
    [DllImport("user32.dll")]static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")]static extern IntPtr GetWindowDpiAwarenessContext(IntPtr window);
    [StructLayout(LayoutKind.Sequential)]struct ClientRect{public int left,top,right,bottom;}
    [StructLayout(LayoutKind.Sequential)]struct BitmapInfo{public uint size;public int width,height;public ushort planes,bits;public uint compression,imageSize;public int xResolution,yResolution;public uint used,important;}
    [DllImport("user32.dll")]static extern bool GetClientRect(IntPtr window,out ClientRect rect);
    [DllImport("user32.dll")]static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
    [DllImport("gdi32.dll")]static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")]static extern IntPtr CreateDIBSection(IntPtr dc,ref BitmapInfo info,uint usage,out IntPtr pixels,IntPtr section,uint offset);
    [DllImport("gdi32.dll")]static extern IntPtr SelectObject(IntPtr dc,IntPtr value);
    [DllImport("gdi32.dll")]static extern bool DeleteObject(IntPtr value);
    [DllImport("gdi32.dll")]static extern bool DeleteDC(IntPtr dc);
    [DllImport("kernel32.dll")]public static extern uint SetThreadExecutionState(uint flags);
    IntPtr desktop;
    public Process Child {get;private set;}
    public string Name {get;private set;}
    public sealed class CapturedClient {public int Width,Height;public byte[] Bgra;}
    // A DC must belong to the inactive fixture desktop, not the user's visible
    // desktop. Use a fresh windowless thread and only the fixture's own HWND.
    public CapturedClient CaptureClient(IntPtr window){
        uint pid;GetWindowThreadProcessId(window,out pid);if(pid!=Child.Id)throw new ArgumentException("capture_requires_own_fixture_window");
        CapturedClient result=null;Exception failure=null;
        var thread=new Thread(()=>{IntPtr dc=IntPtr.Zero,bitmap=IntPtr.Zero,old=IntPtr.Zero;try{
            if(!SetThreadDesktop(desktop))throw new Win32Exception(Marshal.GetLastWin32Error(),"capture_desktop_failed");
            SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(window));
            ClientRect rect;if(!GetClientRect(window,out rect)||rect.right<=0||rect.bottom<=0||rect.right>4096||rect.bottom>2160)throw new Exception("capture_client_bounds");
            // Per-pixel layered windows have no PrintWindow redirection bitmap
            // on an inactive desktop. Only this opt-in fixture publishes its
            // OWN CPU surface; never inspect another process or desktop pixels.
            var ownedPixels=GetPropW(window,"HibernationFixturePixels");
            if(ownedPixels!=IntPtr.Zero){var process=OpenProcess(0x10,false,pid);try{var bytes=new byte[checked(rect.right*rect.bottom*4)];UIntPtr read;if(process==IntPtr.Zero||!ReadProcessMemory(process,ownedPixels,bytes,new UIntPtr((uint)bytes.Length),out read)||read.ToUInt64()!=(ulong)bytes.Length)throw new Exception("capture_owned_fixture_buffer_failed");result=new CapturedClient{Width=rect.right,Height=rect.bottom,Bgra=bytes};return;}finally{if(process!=IntPtr.Zero)CloseHandle(process);}}
            var info=new BitmapInfo{size=(uint)Marshal.SizeOf<BitmapInfo>(),width=rect.right,height=-rect.bottom,planes=1,bits=32};
            dc=CreateCompatibleDC(IntPtr.Zero);IntPtr pixels;bitmap=CreateDIBSection(dc,ref info,0,out pixels,IntPtr.Zero,0);if(dc==IntPtr.Zero||bitmap==IntPtr.Zero||pixels==IntPtr.Zero)throw new Exception("capture_bitmap_failed");old=SelectObject(dc,bitmap);
            if(!PrintWindow(window,dc,1))throw new Exception("capture_print_failed");
            var data=new byte[rect.right*rect.bottom*4];Marshal.Copy(pixels,data,0,data.Length);result=new CapturedClient{Width=rect.right,Height=rect.bottom,Bgra=data};
        }catch(Exception e){failure=e;}finally{if(old!=IntPtr.Zero)SelectObject(dc,old);if(bitmap!=IntPtr.Zero)DeleteObject(bitmap);if(dc!=IntPtr.Zero)DeleteDC(dc);}});
        thread.Start();thread.Join();if(failure!=null)throw failure;return result;
    }
    public IntPtr Find(string className){IntPtr found=IntPtr.Zero;EnumDesktopWindows(desktop,(window,context)=>{uint pid;GetWindowThreadProcessId(window,out pid);if(pid==Child.Id){var name=new StringBuilder(128);GetClassName(window,name,128);if(name.ToString()==className){found=window;return false;}}return true;},IntPtr.Zero);return found;}
    public PrivateDesktop(string executable,string config,string ready,string directory){
        foreach(string value in new[]{executable,config,ready,directory})if(value.IndexOfAny(new[]{'"','\0','\r','\n'})>=0)throw new ArgumentException("invalid_fixture_path");
        executable=Path.GetFullPath(executable);config=Path.GetFullPath(config);ready=Path.GetFullPath(ready);directory=Path.GetFullPath(directory);
        Name="HibernationTest_"+Guid.NewGuid().ToString("N");desktop=CreateDesktopW(Name,IntPtr.Zero,IntPtr.Zero,0,0xC7,IntPtr.Zero);
        if(desktop==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"private_test_desktop_failed");
        var startup=new Startup{size=Marshal.SizeOf<Startup>(),desktop="WinSta0\\"+Name,flags=1,show=0};Created made;
        var args=new StringBuilder("\""+executable+"\" --desktop --config \""+config+"\" --ready-file \""+ready+"\"");
        var captureFlag=Environment.GetEnvironmentVariable("HIBERNATION_TEST_CAPTURE");bool started;
        try{Environment.SetEnvironmentVariable("HIBERNATION_TEST_CAPTURE","owned-native-buffer");started=CreateProcessW(executable,args,IntPtr.Zero,IntPtr.Zero,false,0x08000000,IntPtr.Zero,directory,ref startup,out made);}
        finally{Environment.SetEnvironmentVariable("HIBERNATION_TEST_CAPTURE",captureFlag);}
        if(!started){
            int error=Marshal.GetLastWin32Error();CloseDesktop(desktop);desktop=IntPtr.Zero;throw new Win32Exception(error,"private_test_process_failed");
        }
        // Retain a query/synchronize handle before the child exits. An attached
        // Process otherwise may be unable to retrieve ExitCode after exit.
        try{Child=Process.GetProcessById((int)made.processId);var retained=Child.Handle;}
        catch{if(Child!=null)Child.Dispose();CloseDesktop(desktop);desktop=IntPtr.Zero;throw;}
        finally{CloseHandle(made.thread);CloseHandle(made.process);}
    }
    public void Dispose(){if(desktop!=IntPtr.Zero){CloseDesktop(desktop);desktop=IntPtr.Zero;}}
}
