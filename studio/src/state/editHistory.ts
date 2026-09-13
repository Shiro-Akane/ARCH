export class EditHistory<T> {
 past:T[]=[]; future:T[]=[]; private transaction:{start:T;latest:T}|null=null;
 same(a:T,b:T){return JSON.stringify(a)===JSON.stringify(b);}
 begin(value:T){if(!this.transaction)this.transaction={start:value,latest:value};}
 record(before:T,after:T){if(this.same(before,after))return;if(this.transaction){this.transaction.latest=after;}else{this.past.push(before);this.past=this.past.slice(-100);this.future=[];}}
 end(){const tx=this.transaction;this.transaction=null;if(tx)this.record(tx.start,tx.latest);}
 reset(){this.past=[];this.future=[];this.transaction=null;}
 undo(value:T):T{this.end();const next=this.past.pop();if(next===undefined)return value;this.future.push(value);return next;}
 redo(value:T):T{this.end();const next=this.future.pop();if(next===undefined)return value;this.past.push(value);return next;}
}
export function historyShortcut(key:string,ctrl:boolean,meta:boolean,shift:boolean):'undo'|'redo'|null {
 if(!ctrl&&!meta)return null;return key.toLowerCase()==='z' ? shift?'redo':'undo' : key.toLowerCase()==='y' ? 'redo' : null;
}
