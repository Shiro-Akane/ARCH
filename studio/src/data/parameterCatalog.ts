import type {StandardParameter,ConfigurationSchema,CoordinateSystem,InspectionParameter} from '../host/configurationContracts.ts';
export const parameterGroups=['Grid','EOS','Network','Gravity','Diffusion','Runtime'] as const;
export function catalog(schema:StandardParameter[],values:Record<string,string>){
 return schema.filter(p=>!p.aliasOf).map(parameter=>{
  const aliases=schema.filter(p=>p.aliasOf===parameter.key).map(p=>p.key);
  const sourceKey=Object.hasOwn(values,parameter.key)?parameter.key:aliases.find(k=>Object.hasOwn(values,k))??parameter.key;
  return {parameter,aliases,sourceKey,explicit:Object.hasOwn(values,sourceKey),value:values[sourceKey]??String(parameter.defaultValue)};
 });
}
export function catalogCoordinates(schema:ConfigurationSchema,values:Record<string,string>):CoordinateSystem|undefined {
 const rows=catalog(schema.parameters,values);const value=(key:string)=>rows.find(r=>r.parameter.key===key)?.value??'';
 const blocks=[1,2,3].map(i=>value(`nblockx${i}`));
 if(blocks.some(v=>!/^[+-]?\d+$/.test(v)||!Number.isSafeInteger(Number(v))||Number(v)<0||Number(v)>2147483647)||Number(blocks[0])<1||Number(blocks[2])>0&&Number(blocks[1])===0)return;
 const dimension=Number(blocks[2])>0?3:Number(blocks[1])>0?2:1;
 return schema.coordinateSystems?.find(c=>c.geometry===value('geometry').toLowerCase()&&c.dimension===dimension);
}
export function parameterUnit(p:StandardParameter,parsed?:InspectionParameter,coordinates?:CoordinateSystem){
 const u=parsed?.units??p.units;
 const unit=u.unit??(typeof u.axis==='string'?coordinates?.axes.find(a=>a.key===u.axis)?.unit:undefined);
 return `${unit??'Unit not provided'} · ${u.status??'unknown'}${u.reason?' · '+u.reason:''}`;
}
export function catalogOptions(p:StandardParameter):string[]|undefined {
 const choices=p.options?.choices;
 return Array.isArray(choices)?choices.flatMap(c=>typeof c==='object'&&c!==null&&'value'in c&&typeof c.value==='string'?[c.value]:[]):undefined;
}
