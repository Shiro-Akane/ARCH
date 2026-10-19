/**
 * Mount the browser application and shared stylesheet into the root element.
 * Application providers own state and Host negotiation; this entry performs no
 * filesystem access, scientific initialization or backend selection.
 */
import React from 'react';
import ReactDOM from 'react-dom/client';
import App from './App';
import './styles.css';
import './shell-review.css';
import './components/ParameterPanel/parameter-review.css';
import './components/workflow-review.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode><App /></React.StrictMode>,
);
