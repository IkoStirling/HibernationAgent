# Enumerate this fixture's direct Python children without the WMI service.
# WMI can stall for minutes on a busy Windows desktop during shutdown checks.
if(-not('FixtureOwnedProcesses'-as[type])){
 Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class FixtureOwnedProcesses {
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct Entry {
  public uint size,usage,pid; public UIntPtr heap; public uint module,threads,parent;
  public int priority; public uint flags;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=260)] public string name;
 }
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr CreateToolhelp32Snapshot(uint flags,uint pid);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,EntryPoint="Process32FirstW",SetLastError=true)] static extern bool First(IntPtr snapshot,ref Entry entry);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,EntryPoint="Process32NextW",SetLastError=true)] static extern bool Next(IntPtr snapshot,ref Entry entry);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 public static uint[] VoiceChildren(uint parent) {
  var result=new List<uint>();var snapshot=CreateToolhelp32Snapshot(2,0);
  if(snapshot==new IntPtr(-1))throw new System.ComponentModel.Win32Exception();
  try { var entry=new Entry {size=(uint)Marshal.SizeOf(typeof(Entry))};
   if(!First(snapshot,ref entry))throw new System.ComponentModel.Win32Exception();
   do {if(entry.parent==parent&&entry.name.StartsWith("python",StringComparison.OrdinalIgnoreCase))result.Add(entry.pid);}while(Next(snapshot,ref entry));
   if(Marshal.GetLastWin32Error()!=18)throw new System.ComponentModel.Win32Exception();
  } finally {CloseHandle(snapshot);}return result.ToArray();
 }
}
'@
}
function Owned-VoiceWorkers([uint32]$ParentId){[FixtureOwnedProcesses]::VoiceChildren($ParentId)}
