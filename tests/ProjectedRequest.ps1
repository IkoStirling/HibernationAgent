# Read local provider fixtures as an append-only request, rather than assuming
# message 1 is always the latest event or receipt 0 belongs to the current turn.
function Model-Event($Message){
 if($Message.role-ne'user'){return $null}
 $text=if($Message.content-is[string]){$Message.content}else{($Message.content|Where-Object {$_.text}|Select-Object -First 1).text}
 if(!$text){return $null};try{$value=$text|ConvertFrom-Json -Depth 90;if($value.event_kind-and$value.PSObject.Properties['event_id']){return $value}}catch{}
 return $null
}
function Model-CurrentMessages($Capture){
 $messages=if($Capture.PSObject.Properties['messages']){@($Capture.messages)}else{@($Capture.input)}
 $start=0;for($i=0;$i-lt$messages.Count;$i++){if(Model-Event $messages[$i]){$start=$i}}
 $messages[$start..($messages.Count-1)]
}
function Model-AuthorizedRows($Capture){
 $messages=if($Capture.PSObject.Properties['messages']){@($Capture.messages)}else{@($Capture.input)}
 $rows=[ordered]@{};foreach($message in $messages){$frame=Model-Event $message;if(!$frame){continue};foreach($row in $frame.authorized_context){$rows[$row.id]=$row}}
 $rows.Values
}
function Model-CurrentEvent($Capture){
 $messages=if($Capture.PSObject.Properties['messages']){@($Capture.messages)}else{@($Capture.input)}
 $current=$null
 foreach($message in $messages){
  $event=Model-Event $message;if(!$event){continue}
  $node=[System.Text.Json.Nodes.JsonNode]::Parse(($event|ConvertTo-Json -Depth 90 -Compress))
  if($event.projection.mode-eq'delta'){
   if(!$current-or$current['event_id'].ToString()-cne$event.projection.base_event_id){throw 'Fixture projection base mismatch'}
   foreach($patch in $node['projection_patch']){
    $path=$patch['path'].ToString();$operation=$patch['op'].ToString();$value=$null;if($null-ne$patch['value']){$value=$patch['value'].DeepClone()}
    if(!$path){$current=$value;continue}
    $parts=@($path.Substring(1).Split('/')|ForEach-Object {$_.Replace('~1','/').Replace('~0','~')});$parent=$current
    for($i=0;$i-lt$parts.Count-1;$i++){if($parent-is[System.Text.Json.Nodes.JsonArray]){$parent=$parent[[int]$parts[$i]]}else{$parent=$parent[$parts[$i]]}}
    $key=$parts[-1]
    if($parent-is[System.Text.Json.Nodes.JsonArray]){
     $index=if($key-eq'-'){$parent.Count}else{[int]$key}
     switch($operation){'remove'{$parent.RemoveAt($index)}'add'{$parent.Insert($index,$value)}'replace'{$parent[$index]=$value}default{throw 'Unsupported fixture patch'}}
    }else{if($operation-eq'remove'){$null=$parent.Remove($key)}elseif($operation-in@('add','replace')){$parent[$key]=$value}else{throw 'Unsupported fixture patch'}}
   }
   foreach($field in $node){if($field.Key-notin@('projection','projection_patch','authorized_context')){$copy=$null;if($null-ne$field.Value){$copy=$field.Value.DeepClone()};$current[$field.Key]=$copy}}
  }else{$current=$node}
 }
 if(!$current){throw 'No model event in fixture request'}
 $null=$current.Remove('projection');$null=$current.Remove('projection_patch')
 $current['authorized_context']=[System.Text.Json.Nodes.JsonNode]::Parse((ConvertTo-Json -InputObject @(Model-AuthorizedRows $Capture) -Depth 90 -Compress))
 $current.ToJsonString()|ConvertFrom-Json -Depth 90
}
