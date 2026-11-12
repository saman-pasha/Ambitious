import { add } from "./lib.js";
console.log("main module", add(1, 1), import.meta.url.replace(/^http:\/\/[^/]*/, ""));
