import type { CoreGroup } from '../../data/parSchema';
import { coreBlocks } from './panelPresentation';
export function BlockNavigator({selected,onSelect,summary}: {selected:CoreGroup;onSelect:(group:CoreGroup)=>void;summary:(group:CoreGroup)=>string}) {
 return <nav className="block-navigator" aria-label="Core parameter blocks">{coreBlocks.map(group=><button key={group} type="button" aria-pressed={selected===group} onClick={()=>onSelect(group)}><strong>{group}</strong><span>{summary(group)}</span></button>)}</nav>;
}
