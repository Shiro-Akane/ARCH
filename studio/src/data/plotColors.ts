/**
 * Convert a normalized display fraction to a chosen color map.
 * The three Hot segments fill RGB channels in order; Viridis comes from h5web.
 * Color clamping is visual and never clips the scientific field array.
 */
import {INTERPOLATORS} from '@h5web/lib';
/** Hot: black -> red -> yellow -> white. Library Viridis remains unchanged. */
export function plotColor(map:'Viridis'|'Hot',fraction:number){const t=Math.max(0,Math.min(1,fraction));return map==='Viridis'?INTERPOLATORS.Viridis(t):`rgb(${Math.round(255*Math.min(1,3*t))},${Math.round(255*Math.max(0,Math.min(1,3*t-1)))},${Math.round(255*Math.max(0,3*t-2))})`;}
