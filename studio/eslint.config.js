import js from '@eslint/js';
import globals from 'globals';
import tseslint from 'typescript-eslint';
import reactHooks from 'eslint-plugin-react-hooks';
import reactRefresh from 'eslint-plugin-react-refresh';

export default tseslint.config(
  { ignores: ['dist/**', 'node_modules/**', '.local/**'] },
  js.configs.recommended,
  {files:['desktop/**/*.{cjs,mjs}'],languageOptions:{globals:globals.node}},
  {files:['desktop/launcher.js'],languageOptions:{globals:globals.browser}},
  ...tseslint.configs.recommended,
  // Sandboxed Electron preload and main use native CommonJS.
  {files:['desktop/**/*.cjs'],rules:{'@typescript-eslint/no-require-imports':'off'}},
  {
    files: ['src/**/*.{ts,tsx}'],
    languageOptions: { globals: globals.browser },
    plugins: { 'react-hooks': reactHooks, 'react-refresh': reactRefresh },
    rules: {
      ...reactHooks.configs.recommended.rules,
      'react-refresh/only-export-components': ['warn', { allowConstantExport: true }],
    },
  },
);
