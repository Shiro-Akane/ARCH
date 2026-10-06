/** Node 24 Host transport: JSON numbers retain FP64 signed zero at both wire boundaries. */
export function stringifyPlotfile(value:unknown):string {
 const rawJSON=(JSON as typeof JSON&{rawJSON(text:string):unknown}).rawJSON;
 return JSON.stringify(value,(_key,item)=>
  typeof item==='number'&&Object.is(item,-0)?rawJSON('-0'):item);
}
