import { defineConfig } from 'vitest/config';
import { sveltekit } from '@sveltejs/kit/vite';

export default defineConfig({
	plugins: [sveltekit()],
	// Without the browser condition Svelte resolves to its server build and
	// every component render throws lifecycle_function_unavailable.
	resolve: { conditions: ['browser'] },
	test: {
		environment: 'jsdom',
		globals: true,
		include: ['src/**/*.{test,spec}.{js,ts}'],
	},
});
