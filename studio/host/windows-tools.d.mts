/** Shared validation at Electron Main and private Host bootstrap boundaries. */
export function compilerEnvironment(value:unknown):Partial<Record<'INCLUDE'|'LIB'|'LIBPATH',string>>;
export function windowsExecutable(value:unknown):string;
export function windowsSearchPath(value:unknown):string;
export interface NativeWindowsTools {
 cmake:string;supervisor:string;ninja:string;objdump:string;publisher:string;rc:string;path:string;terminal?:string;
 compilerEnvironment?:Partial<Record<'INCLUDE'|'LIB'|'LIBPATH',string>>;
 runtimeLibraries?:string[];
}
export type WindowsProcessTools = Pick<NativeWindowsTools,'supervisor'|'path'>;
export type WindowsHostTools = Pick<NativeWindowsTools,'cmake'|'supervisor'|'path'> & Partial<Omit<NativeWindowsTools,'cmake'|'supervisor'|'path'>>;
export function parseWindowsTools(text:string):NativeWindowsTools;
export function loadWindowsTools(text:string):Promise<NativeWindowsTools>;
export function loadWindowsToolsFromEnvironment(environment?:NodeJS.ProcessEnv):Promise<NativeWindowsTools>;
export function windowsHostEnvironment(tools:WindowsProcessTools & Partial<NativeWindowsTools>,environment?:NodeJS.ProcessEnv):NodeJS.ProcessEnv;
