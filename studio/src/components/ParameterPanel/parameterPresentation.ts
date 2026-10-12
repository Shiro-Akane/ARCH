import {matchesParameter} from './panelPresentation.ts';
import type {CoordinateSystem,InspectionParameter,StandardParameter} from '../../host/configurationContracts.ts';

/** Presentation only; membership and coordinate labels remain Core-owned. */
export function orderedParameterGroups(groups:readonly string[],hasCaseParameters=false):string[]{
 const preferred=['Runtime','Grid','EOS','Network','Gravity','Diffusion','Case'];
 const present=new Set(groups);
 if(hasCaseParameters)present.add('Case');
 return [...preferred.filter(group=>present.has(group)),...present].filter((group,index,all)=>all.indexOf(group)===index);
}
export function parameterGroupLabel(group:string){return group==='Case'?'Case parameters':group;}

/** Keep only an authoritative physical unit in the compact field row. */
export function physicalUnit(unit:unknown,status:unknown=undefined):string|undefined {
 if(typeof unit!=='string'||!unit.trim())return;
 if(['unknown','not-applicable','dimensionless'].includes(String(status)))return;
 if(['1','dimensionless','unknown','not applicable','not-applicable'].includes(unit.trim().toLowerCase()))return;
 return unit;
}
export function compactParameterUnit(parameter:StandardParameter,parsed?:InspectionParameter,coordinates?:CoordinateSystem):string|undefined{
 const units=parsed?.units??parameter.units;
 const unit=units.unit??(typeof units.axis==='string'?coordinates?.axes.find(axis=>axis.key===units.axis)?.unit:undefined);
 return physicalUnit(unit,units.status);
}
export function authoritativeAxisLabel(axis:CoordinateSystem['axes'][number]|undefined){
 return axis?.displayName??'Coordinate unavailable';
}
export function caseParameterMatches(key:string,value:string,description:string|null|undefined,query:string){
 return matchesParameter(key,value,query)||!!query.trim()&&!!description?.toLowerCase().includes(query.trim().toLowerCase());
}
