import { name } from "./lib.js";
console.log("ins.js evaluated", name, document.currentScript, import.meta.url.replace(/^http:\/\/[^/]*/, ""));
export const ins = "ins";
