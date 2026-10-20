/**
 * Track inspection generations across model, text and build changes.
 * Only the latest ticket can publish a response, including when text returns to
 * an earlier value; validation still checks the full Core request scope.
 */
import type {ConfigurationBuildScope,ConfigurationRequest,InspectionResponse} from '../host/configurationContracts.ts';
import {validateInspectionResponse} from '../host/configurationValidation.ts';
/** Invalidated on every model/text/build transition, including transitions back to earlier text. */
export class InspectionRequests {
 private generation=0;
 begin(){return ++this.generation;}
 invalidate(){this.generation++;}
 accept(ticket:number,value:unknown,request:ConfigurationRequest,scope:ConfigurationBuildScope):InspectionResponse|null {
  if(ticket!==this.generation)return null;
  return validateInspectionResponse(value,request,scope);
 }
}
