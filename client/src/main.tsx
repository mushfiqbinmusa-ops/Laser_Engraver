import React from 'react';
import ReactDOM from 'react-dom/client';
import { App } from './App';
import { MachineProvider } from './context/MachineContext';
import './tokens/theme.css';

const rootElement = document.getElementById('root');
if (!rootElement) {
  throw new Error('Failed to find root DOM element');
}

ReactDOM.createRoot(rootElement).render(
  <React.StrictMode>
    <MachineProvider>
      <App />
    </MachineProvider>
  </React.StrictMode>
);
