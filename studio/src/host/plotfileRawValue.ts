/** Display the stored sign of zero without changing the numeric value. */
export function formatPlotfileRawValue(value:number|string):string {
 return Object.is(value,-0)?'-0':String(value);
}
