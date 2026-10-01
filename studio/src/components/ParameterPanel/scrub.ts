export function scrubValue(raw:string,dx:number,fine:boolean,integer:boolean,range?:readonly [number,number]):string {
 if(!raw.trim()||!Number.isFinite(Number(raw))||Math.abs(dx)<4)return raw;
 const start=Number(raw); const sensitivity=(Math.abs(start)||1)*0.01*(fine?0.1:1);
 let value=start+dx*sensitivity;
 if(integer)value=Math.round(value);
 if(range)value=Math.max(range[0],Math.min(range[1],value));
 return Number.isFinite(value)?String(Number(value.toPrecision(15))):raw;
}
