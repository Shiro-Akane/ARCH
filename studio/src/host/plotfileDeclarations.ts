/** Recorded producer labels; validation does not certify scientific provenance. */
export interface PlotfileFieldDeclaration {
 version:'candidate-field-1'|'arch-field-1';unit:string|null;centering:'cell';
 basis:string|null;meaning:string|null;unitReason:string|null;
}
const object=(v:unknown):v is Record<string,unknown>=>!!v&&typeof v==='object'&&!Array.isArray(v);
export const declaredText=(v:unknown):v is string=>typeof v==='string'&&v.length>0&&v.length<=256&&!v.includes('\0')&&v!=='unknown';
export function validFieldDeclaration(v:unknown):v is PlotfileFieldDeclaration {
 return object(v)&&['candidate-field-1','arch-field-1'].includes(String(v.version))&&v.centering==='cell'&&
  (v.unit===null||declaredText(v.unit))&&(v.basis===null||declaredText(v.basis))&&
  (v.meaning===null||declaredText(v.meaning))&&
  (v.unit===null?declaredText(v.unitReason):v.unitReason===null||declaredText(v.unitReason));
}
export function validMeasureLabels(unit:unknown,normalization:unknown,dimension?:number):boolean {
 if(unit===null)return normalization===undefined||normalization===null;
 return unit==='cm'&&normalization==='per_unit_transverse_area'&&(dimension===undefined||dimension===1)||
  unit==='cm^2'&&normalization==='per_unit_transverse_length'&&(dimension===undefined||dimension===2)||
  unit==='cm^3'&&normalization==='full_volume'&&(dimension===undefined||dimension===3)||
  unit==='cm^3'&&normalization==='full_rotation'&&(dimension===undefined||dimension===2)||
  unit==='cm^3'&&normalization==='per_unit_solid_angle'&&(dimension===undefined||dimension===1)||
  unit==='cm^2'&&normalization==='per_unit_azimuth_and_axial_length'&&(dimension===undefined||dimension===1);
}
