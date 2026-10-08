/// <reference types="vite/client" />

declare module '*.ahfl?raw' {
    const content: string;
    export default content;
}
