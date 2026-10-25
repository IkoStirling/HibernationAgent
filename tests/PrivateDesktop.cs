using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

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
    [DllImport("user32.dll")]public static extern uint GetGuiResources(IntPtr process,uint kind);
    [DllImport("kernel32.dll")]public static extern uint SetThreadExecutionState(uint flags);
    IntPtr desktop;
    public Process Child {get;private set;}
    public string Name {get;private set;}
    public PrivateDesktop(string executable,string config,string ready,string directory){
        foreach(string value in new[]{executable,config,ready,directory})if(value.IndexOfAny(new[]{'"','\0','\r','\n'})>=0)throw new ArgumentException("invalid_fixture_path");
        executable=Path.GetFullPath(executable);config=Path.GetFullPath(config);ready=Path.GetFullPath(ready);directory=Path.GetFullPath(directory);
        Name="HibernationTest_"+Guid.NewGuid().ToString("N");desktop=CreateDesktopW(Name,IntPtr.Zero,IntPtr.Zero,0,0xC7,IntPtr.Zero);
        if(desktop==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"private_test_desktop_failed");
        var startup=new Startup{size=Marshal.SizeOf<Startup>(),desktop="WinSta0\\"+Name,flags=1,show=0};Created made;
        var args=new StringBuilder("\""+executable+"\" --desktop --config \""+config+"\" --ready-file \""+ready+"\"");
        if(!CreateProcessW(executable,args,IntPtr.Zero,IntPtr.Zero,false,0x08000000,IntPtr.Zero,directory,ref startup,out made)){
            int error=Marshal.GetLastWin32Error();CloseDesktop(desktop);desktop=IntPtr.Zero;throw new Win32Exception(error,"private_test_process_failed");
        }
        try{Child=Process.GetProcessById((int)made.processId);}finally{CloseHandle(made.thread);CloseHandle(made.process);}
    }
    public void Dispose(){if(desktop!=IntPtr.Zero){CloseDesktop(desktop);desktop=IntPtr.Zero;}}
}
