import { defineConfig, type Plugin } from 'vite';
import { createApiMiddleware } from './server/api';

// Vite plugin that mounts the ahflc API middleware on the dev server.
// In production (vite build), the API is served by a separate static host
// or the playground is deployed alongside a lightweight ahflc wrapper.
function ahflApiPlugin(): Plugin {
    return {
        name: 'ahfl-api',
        configureServer(server) {
            server.middlewares.use('/api', createApiMiddleware());
        },
        configurePreviewServer(server) {
            server.middlewares.use('/api', createApiMiddleware());
        },
    };
}

export default defineConfig({
    plugins: [ahflApiPlugin()],
    server: {
        port: 5173,
    },
    build: {
        outDir: 'dist',
    },
});
