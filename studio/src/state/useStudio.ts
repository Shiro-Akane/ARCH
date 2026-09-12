import { useEffect, useReducer } from 'react';
import { initialState, studioReducer } from './studioState';
import { generateMockPreview } from '../data/MockPreviewProvider';
export function useStudio() {
  const [state, dispatch] = useReducer(studioReducer, undefined, initialState);
  useEffect(() => {
    if (state.preview !== 'generating' || state.request === null) return;
    const controller = new AbortController();
    const revision = state.request;
    generateMockPreview(state.working, controller.signal).then(
      data => dispatch({ type: 'preview/success', revision, data }),
      error => { if (!controller.signal.aborted) dispatch({ type: 'preview/failure', revision, message: error instanceof Error ? error.message : 'Preview failed.' }); },
    );
    return () => controller.abort();
  }, [state.preview, state.request, state.working]);
  return { state, dispatch };
}
