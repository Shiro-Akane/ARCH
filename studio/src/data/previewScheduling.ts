import {effectiveEntries,parsePar} from './ParDocument.ts';
import type {CoreParameter} from '../host/previewContracts.ts';
import {standardValueError} from './standardValidation.ts';

/** Only types actually observed by this matching Core build/model may gate automatic work. */
export function previewInputIssue(text:string,parameters:CoreParameter[]|undefined):string|undefined {
 const entries=new Map(effectiveEntries(parsePar(text)).map(entry=>[entry.key,entry.value]));
 for(const parameter of parameters??[]){
  const raw=entries.get(parameter.key);
  if(raw===undefined||!['float','int'].includes(parameter.type??''))continue;
  const error=standardValueError({type:parameter.type as 'float'|'int',constraints:{}},raw);
  if(error)return parameter.key+': '+error;
 }
 return undefined;
}
