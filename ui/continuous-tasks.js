'use strict';
(() => {
  const labels={ready:'待继续',running:'正在做',waiting_input:'等你补充',paused:'暂时放下',blocked:'暂时卡住',completed:'已完成',abandoned:'已放下'};
  let signature='',pending=false,latest=null,operations=null;
  const node=(tag,text,className)=>{const value=document.createElement(tag);if(text!==undefined)value.textContent=String(text);if(className)value.className=className;return value;};
  function render(){
    if(!latest)return;
    let root=document.getElementById('continuous-tasks');
    if(!root){root=node('article',undefined,'card wide');root.id='continuous-tasks';root.dataset.sectionKey='continuous-tasks';document.getElementById('page-overview')?.append(root);}
    const data=latest.continuous_tasks;if(!data){root.hidden=true;return;}root.hidden=false;
    const key=JSON.stringify([data,latest.paused,pending]);if(key===signature)return;signature=key;
    const content=root.querySelector(':scope > .fold-content')||root;
    content.replaceChildren(node('span','持续目标','label'),node('h3','说完之后，事情仍可以继续。'),node('p','闲聊直接回应；接受的目标会在后台分段推进。只保存简短进度，不展示内部记忆或模型思考。','helper'));
    if(!data.enabled){content.append(node('p','持续任务当前已关闭。','helper'));return;}
    const tasks=data.tasks||[];if(!tasks.length){content.append(node('p','目前没有接下的持续目标。','helper'));return;}
    for(const task of tasks.slice(0,8)){
      const row=node('div',undefined,'continuous-task-row');row.style.cssText='border-top:1px solid var(--line,#dde6da);padding:16px 0';
      const head=node('div',undefined,'card-head');head.append(node('h4',task.goal),node('span',labels[task.status]||task.status,'pill'));row.append(head);
      if(task.summary)row.append(node('p',task.summary));
      if(task.deferred_reason==='circadian_sleep')row.append(node('p','正在休息，醒来后再接续；这不代表任务失败或已经完成。','helper'));
      row.append(node('p',`完成条件：${task.completion_criterion}`,'helper'));
      if(task.status==='blocked')row.append(node('p','本项已停止自动尝试。可在对话中讨论下一步；权限、工作区或预算条件未满足时不能直接继续。','helper'));
      if(!['completed','abandoned'].includes(task.status)){
        const controls=node('div',undefined,'dialog-actions');
        for(const [action,label] of [['ready','running'].includes(task.status)?['pause','暂放']:['resume','继续'],['cancel','取消目标']]){
          const button=node('button',label,'quiet');button.type='button';button.dataset.taskId=task.id;button.dataset.taskRevision=task.revision;button.dataset.taskAction=action;button.disabled=pending||(action==='resume'&&latest.paused);
          button.addEventListener('click',async()=>{
            pending=true;render();
            try{await operations.request('/api/tasks/control',{id:task.id,expected_revision:task.revision,action});const data=await operations.request('/api/tasks');latest={...latest,continuous_tasks:data};operations.notify(action==='cancel'?'已放下这项目标。':action==='pause'?'已暂放，实际进度保留。':'已安排继续。');}
            catch(error){operations.notify(error.message);try{latest={...latest,continuous_tasks:await operations.request('/api/tasks')};}catch{}}
            finally{pending=false;signature='';render();}
          });controls.append(button);
        }row.append(controls);
      }content.append(row);
    }
  }
  window.updateContinuousTasks=(value,ops)=>{latest=value;operations=ops;render();};
})();
