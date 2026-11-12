p("the inserted external script", "runs: " + log.join(",") + " " + (document.currentScript && document.currentScript.src.split("/").pop()));
log.push("external");
