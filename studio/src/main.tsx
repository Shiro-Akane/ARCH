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
