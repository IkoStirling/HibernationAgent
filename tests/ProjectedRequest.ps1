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
  $event=Model-Event $message
  if(!$event){
   if($current-and$message.role-eq'user'){
    $raw=if($message.content-is[string]){$message.content}else{($message.content|Where-Object {$_.text}|Select-Object -First 1).text}
    try{$update=($raw|ConvertFrom-Json -Depth 90).runtime_event}catch{$update=$null}
    if($update-and$update.source_event_id-ceq$current['event_id'].ToString()){
     $node=[System.Text.Json.Nodes.JsonNode]::Parse(($update.changes|ConvertTo-Json -Depth 90 -Compress))
     foreach($field in $node){$copy=$null;if($null-ne$field.Value){$copy=$field.Value.DeepClone()};$current[$field.Key]=$copy}
    }
   }
   continue
  }
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
function Model-PatchNode($Current,$Patches){
 foreach($patch in $Patches){
  $path=$patch['path'].ToString();$operation=$patch['op'].ToString();$value=$null;if($null-ne$patch['value']){$value=$patch['value'].DeepClone()}
  if(!$path){$Current=$value;continue}
  $parts=@($path.Substring(1).Split('/')|ForEach-Object {$_.Replace('~1','/').Replace('~0','~')});$parent=$Current
  for($i=0;$i-lt$parts.Count-1;$i++){if($parent-is[System.Text.Json.Nodes.JsonArray]){$parent=$parent[[int]$parts[$i]]}else{$parent=$parent[$parts[$i]]}}
  $key=$parts[-1]
  if($parent-is[System.Text.Json.Nodes.JsonArray]){$index=if($key-eq'-'){$parent.Count}else{[int]$key};switch($operation){'remove'{$parent.RemoveAt($index)}'add'{$parent.Insert($index,$value)}'replace'{$parent[$index]=$value}default{throw 'Unsupported review fixture patch'}}}
  else{if($operation-eq'remove'){$null=$parent.Remove($key)}elseif($operation-in@('add','replace')){$parent[$key]=$value}else{throw 'Unsupported review fixture patch'}}
 }
 return ,$Current
}
function Model-ReviewEvent($Capture){
 $messages=if($Capture.PSObject.Properties['messages']){@($Capture.messages)}else{@($Capture.input)}
 $capabilities=@{};$evidence=@{};$current=$null;$authority=$null
 foreach($message in $messages){
  if($message.role-ne'user'){continue}
  $raw=if($message.content-is[string]){$message.content}else{($message.content|Where-Object {$_.text}|Select-Object -First 1).text}
  if(!$raw){continue};try{$node=[System.Text.Json.Nodes.JsonNode]::Parse($raw)}catch{continue}
  if($node-isnot[System.Text.Json.Nodes.JsonObject]){continue}
  if($node['review_baseline']){$authority=$node['review_baseline']['authority'].DeepClone();continue}
  if($node['review_event']){if(!$authority){throw 'Review delta lacks a baseline'};$authority=Model-PatchNode $authority $node['review_event']['patch'];continue}
  if($node['review_candidate']){if(!$authority){throw 'Review candidate lacks authority'};$current=$authority.DeepClone();foreach($field in $node['review_candidate']){if($field.Key-eq'sequence'){continue};$copy=$null;if($null-ne$field.Value){$copy=$field.Value.DeepClone()};$current[$field.Key]=$copy};continue}
  if($node['review_capability']){$item=$node['review_capability'];$capabilities[$item['id'].ToString()]=$item['data'].DeepClone()}
  elseif($node['review_evidence']){$item=$node['review_evidence'];$evidence[$item['id'].ToString()]=$item['data'].DeepClone()}
  elseif($node['review_snapshot']){$current=$node['review_snapshot']['data'].DeepClone()}
  elseif($node.ContainsKey('candidate')-and($node.ContainsKey('target')-or$node.ContainsKey('context'))){$current=$node}
 }
 if(!$current){throw 'No review event in fixture request'}
 $cap=$current['current_capabilities']
 if($cap-is[System.Text.Json.Nodes.JsonObject]-and$cap.ContainsKey('$review_capability')){$current['current_capabilities']=$capabilities[$cap['$review_capability'].ToString()].DeepClone()}
 $states=$current['current_plugin_state'];$cap=$current['current_capabilities']
 if($states-is[System.Text.Json.Nodes.JsonObject]-and$cap-is[System.Text.Json.Nodes.JsonObject]){foreach($plugin in $cap['plugins']){if($plugin-is[System.Text.Json.Nodes.JsonObject]-and$plugin.ContainsKey('id')-and!$plugin.ContainsKey('current')){$id=$plugin['id'].ToString();if($states.ContainsKey($id)){$copy=$null;if($null-ne$states[$id]){$copy=$states[$id].DeepClone()};$plugin['current']=$copy}}}}
 if($current['event_context']){
  $typed=$current['event_context']-is[System.Text.Json.Nodes.JsonObject]
  $context=[System.Text.Json.Nodes.JsonNode]::Parse($current['event_context'].ToString());$rows=$context['authorized_context']
  if($rows-is[System.Text.Json.Nodes.JsonArray]){for($i=0;$i-lt$rows.Count;$i++){if($rows[$i]-is[System.Text.Json.Nodes.JsonObject]-and$rows[$i].ContainsKey('$review_evidence')){$rows[$i]=$evidence[$rows[$i]['$review_evidence'].ToString()].DeepClone()}}}
  $names=@();foreach($field in $context){$names+=,$field.Key}
  foreach($name in $names){$value=$context[$name];if($value-is[System.Text.Json.Nodes.JsonObject]-and$value.ContainsKey('$review_field')){$ref=$value['$review_field'].ToString();$copy=$null;if($null-ne$current[$ref]){$copy=$current[$ref].DeepClone()};$context[$name]=$copy}}
  if($typed){foreach($name in @('current_capabilities','authoritative_state','current_task','expression_preference','perception','open_questions','time_context','shared_understanding','conversation_delivery')){if(!$context.ContainsKey($name)-and$current.ContainsKey($name)){$copy=$null;if($null-ne$current[$name]){$copy=$current[$name].DeepClone()};$context[$name]=$copy}};if(!$context.ContainsKey('input')-and$current.ContainsKey('current_user_message')){$copy=$null;if($null-ne$current['current_user_message']){$copy=$current['current_user_message'].DeepClone()};$context['input']=$copy}}
  $current['event_context']=[System.Text.Json.Nodes.JsonNode]::Parse((ConvertTo-Json -InputObject ([string]$context.ToJsonString()) -Compress))
 }
 $current.ToJsonString()|ConvertFrom-Json -Depth 90
}
