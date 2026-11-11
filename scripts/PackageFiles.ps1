$ErrorActionPreference='Stop'
function Assert-ChildPath([string]$Root,[string]$Path){
 $base=[IO.Path]::GetFullPath($Root).TrimEnd([char]92,[char]47)+[IO.Path]::DirectorySeparatorChar;$full=[IO.Path]::GetFullPath($Path)
 if(!$full.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw 'Package path escapes its root'}
 $full
}
function Assert-NoReparsePath([string]$Path){
 $cursor=[IO.Path]::GetFullPath($Path)
 while($cursor){try{$attributes=[IO.File]::GetAttributes($cursor);if($attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Reparse points are not accepted'}}catch [IO.FileNotFoundException]{}catch [IO.DirectoryNotFoundException]{};$parent=[IO.Path]::GetDirectoryName($cursor);if($parent-eq$cursor){break};$cursor=$parent}
}
function Test-PackageFiles([string]$PackageRoot,[switch]$AllowEngineManifest){
 $root=[IO.Path]::GetFullPath($PackageRoot).TrimEnd([char]92,[char]47);Assert-NoReparsePath (Join-Path $root package-manifest.json);$manifest=Get-Content -LiteralPath (Join-Path $root package-manifest.json) -Raw|ConvertFrom-Json
 if($manifest.schema_version-ne1-or!$manifest.files-or$manifest.files.Count-gt12000){throw 'Unsupported package manifest'}
 $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($entry in $manifest.files){
  $member=([string]$entry.path).Replace([char]92,[char]47)
  foreach($part in $member.Split('/')){if(!$part-or$part.EndsWith('.')-or$part.EndsWith(' ')-or$part-match'[<>:"|?*\x00-\x1f]|^(?i:con|prn|aux|nul|com[0-9]|lpt[0-9])(\.|$)'){throw 'Invalid Windows package path'}}
  if($member-eq'package-manifest.json'-or$entry.sha256-notmatch'^[0-9a-fA-F]{64}$'){throw 'Invalid manifest member'}
  if([IO.Path]::IsPathRooted($entry.path)-or$entry.path-match'(^|[\\/])\.{1,2}([\\/]|$)|:'-or!$seen.Add(([string]$entry.path).Replace([char]92,[char]47))){throw 'Invalid or duplicate package member'}
  $path=Assert-ChildPath $root (Join-Path $root $entry.path)
  Assert-NoReparsePath $path
  if(!(Test-Path -LiteralPath $path -PathType Leaf)-or(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash-ne$entry.sha256){throw ('Package member failed integrity check: '+$entry.path)}
 }
 $all=@(Get-ChildItem -LiteralPath $root -Recurse -Force);foreach($item in $all){if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Package contains a reparse point'}}
 $actual=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($item in $all|Where-Object {!$_.PSIsContainer-and$_.FullName-ne(Join-Path $root package-manifest.json)}){if($AllowEngineManifest-and$item.FullName-eq(Join-Path $root .manifest.json)){continue};$null=$actual.Add([IO.Path]::GetRelativePath($root,$item.FullName).Replace([char]92,[char]47))}
 if(!$actual.SetEquals($seen)){throw 'Package contains unlisted files'}
 $manifest
}
function Get-PeImports([string]$Path){
 $stream=[IO.File]::OpenRead($Path);$reader=[IO.BinaryReader]::new($stream)
 try{
  if($stream.Length-lt128-or$reader.ReadUInt16()-ne0x5a4d){return @()};$stream.Position=0x3c;$pe=$reader.ReadUInt32();if($pe+24-ge$stream.Length){throw 'Invalid PE offset'}
  $stream.Position=$pe;if($reader.ReadUInt32()-ne0x4550){throw 'Invalid PE signature'};$stream.Position=$pe+6;$sections=$reader.ReadUInt16();$stream.Position=$pe+20;$optionalSize=$reader.ReadUInt16();$optional=$pe+24;$stream.Position=$optional;$magic=$reader.ReadUInt16()
  $directories=if($magic-eq0x20b){$optional+112}elseif($magic-eq0x10b){$optional+96}else{throw 'Unknown PE optional header'}
  if($sections-gt96-or$optional+$optionalSize+$sections*40-gt$stream.Length){throw 'Invalid PE section table'}
  $stream.Position=$directories+8;$importRva=$reader.ReadUInt32();$null=$reader.ReadUInt32();if(!$importRva){return @()}
  $table=@();for($i=0;$i-lt$sections;$i++){$stream.Position=$optional+$optionalSize+$i*40+8;$virtual=$reader.ReadUInt32();$rva=$reader.ReadUInt32();$raw=$reader.ReadUInt32();$offset=$reader.ReadUInt32();$table+=@{rva=$rva;size=[Math]::Max($virtual,$raw);offset=$offset}}
  $resolve={param([uint32]$Rva)foreach($section in $table){if($Rva-ge$section.rva-and[uint64]$Rva-lt[uint64]$section.rva+$section.size){return [long]$section.offset+$Rva-$section.rva}};throw 'PE import RVA outside sections'}
  $descriptor=&$resolve $importRva;$names=@();for($i=0;$i-lt512;$i++){$at=$descriptor+$i*20;if($at+20-gt$stream.Length){throw 'Truncated PE imports'};$stream.Position=$at;$fields=@(1..5|ForEach-Object {$reader.ReadUInt32()});if(($fields|Where-Object {$_-ne0}).Count-eq0){break};$nameAt=&$resolve $fields[3];$stream.Position=$nameAt;$bytes=[Collections.Generic.List[byte]]::new();for($j=0;$j-lt260;$j++){if($stream.Position-ge$stream.Length){throw 'Truncated PE import name'};$b=$reader.ReadByte();if(!$b){break};$bytes.Add($b)};if($bytes.Count-ge260){throw 'Oversized PE import name'};$names+=[Text.Encoding]::ASCII.GetString($bytes.ToArray())};@($names|Sort-Object -Unique)
 }finally{$reader.Dispose();$stream.Dispose()}
}
