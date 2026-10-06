/** Recorded producer labels; validation does not certify scientific provenance. */
export interface PlotfileFieldDeclaration {
 version:'candidate-field-1';unit:string|null;centering:'cell';
 basis:string|null;meaning:string|null;unitReason:string|null;
}
const object=(v:unknown):v is Record<string,unknown>=>!!v&&typeof v==='object'&&!Array.isArray(v);
export const declaredText=(v:unknown):v is string=>typeof v==='string'&&v.length>0&&v.length<=256&&!v.includes('\0')&&v!=='unknown';
export function validFieldDeclaration(v:unknown):v is PlotfileFieldDeclaration {
 return object(v)&&v.version==='candidate-field-1'&&v.centering==='cell'&&
  (v.unit===null||declaredText(v.unit))&&(v.basis===null||declaredText(v.basis))&&
  (v.meaning===null||declaredText(v.meaning))&&
  (v.unit===null?declaredText(v.unitReason):v.unitReason===null||declaredText(v.unitReason));
}
export function validMeasureLabels(unit:unknown,normalization:unknown,dimension?:number):boolean {
 if(unit===null)return normalization===undefined||normalization===null;
 return unit==='cm'&&normalization==='per_unit_transverse_area'&&(dimension===undefined||dimension===1)||
  unit==='cm^2'&&normalization==='per_unit_transverse_length'&&(dimension===undefined||dimension===2);
}
