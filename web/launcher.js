/* SPDX-License-Identifier: MIT */
"use strict";

const element = id => document.getElementById(id);
const status = message => { element("status").textContent = message; };
const log = message => {
  const output = element("log");
  output.textContent = (output.textContent + message + "\n").slice(-24000);
};
let game;
let selected = [];
let started = false;

function syncSaves(populate = false) {
  return new Promise((resolve, reject) => game.FS.syncfs(populate, error => error ? reject(error) : resolve()));
}

// Only import the actual UW folder, even when a parent installation folder
// was selected. Normalize DOS names for the case-sensitive virtual filesystem.
function gameFiles(files) {
  const paths = files.map(file => ({file, path: file.webkitRelativePath.toUpperCase()}));
  const exe = paths.find(entry => entry.path.endsWith("/UW.EXE"));
  if (!exe) throw new Error("Choose a folder containing UW.EXE and the game directories.");
  const root = exe.path.slice(0, -6);
  const entries = paths.filter(entry => entry.path.startsWith(root)).map(entry => ({
    file: entry.file, path: entry.path.slice(root.length)
  })).filter(entry => entry.path === "UW.EXE" || /^(DATA|CRIT|CUTS|SOUND|SAVE[1-4])\//.test(entry.path));
  for (const folder of ["DATA", "CRIT", "CUTS", "SOUND"]) {
    if (!entries.some(entry => entry.path.startsWith(folder + "/"))) {
      throw new Error(`The selected UW folder has no ${folder} files.`);
    }
  }
  return entries;
}

element("files").addEventListener("change", event => {
  element("start").disabled = true;
  selected = [];
  try {
    selected = gameFiles(Array.from(event.target.files));
    status(`Ready: ${selected.length} game files. Press Play to begin.`);
    element("start").disabled = false;
  } catch (error) { status(error.message); }
});

element("start").addEventListener("click", async () => {
  if (started || !selected.length) return;
  element("start").disabled = element("files").disabled = element("restore").disabled = true;
  try {
    let count = 0;
    for (const {file, path} of selected) {
      const save = /^SAVE[1-4]\//.test(path);
      const destination = (save ? "/saves/" : "/game/") + path;
      game.FS.mkdirTree(destination.slice(0, destination.lastIndexOf("/")));
      // Browser saves take precedence over saves shipped in the chosen folder.
      if (!save || !game.FS.analyzePath(destination).exists) {
        game.FS.writeFile(destination, new Uint8Array(await file.arrayBuffer()));
      }
      status(`Loading game files (${++count}/${selected.length})…`);
    }
    await syncSaves();
    started = true;
    element("setup").hidden = true;
    element("fullscreen").disabled = false;
    element("canvas").focus();
    status("Running. Saves are stored in this browser.");
    game.callMain(["--dir", "/game", "--saves", "/saves", element("sound").checked ? "--sound" : "--nosound"]);
  } catch (error) {
    log(String(error));
    status(`Could not start: ${error.message || error}. Reload the page to try again.`);
  }
});

element("canvas").addEventListener("contextmenu", event => event.preventDefault());
element("canvas").addEventListener("click", () => element("canvas").focus());
element("fullscreen").addEventListener("click", () => {
  element("canvas").requestFullscreen().then(() => element("canvas").focus()).catch(error => status(error.message));
});

function collectSaves(directory = "/saves", result = {}) {
  for (const name of game.FS.readdir(directory)) {
    if (name === "." || name === "..") continue;
    const path = directory + "/" + name;
    if (game.FS.isDir(game.FS.stat(path).mode)) collectSaves(path, result);
    else result[path.slice(7)] = Array.from(game.FS.readFile(path));
  }
  return result;
}

element("backup").addEventListener("click", async () => {
  try {
    await syncSaves();
    const blob = new Blob([JSON.stringify({format: "openabyss-saves-v1", files: collectSaves()})], {type: "application/json"});
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "openabyss-saves.json";
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    status("Save backup downloaded.");
  } catch (error) { status(`Save backup failed: ${error.message || error}`); }
});

element("restore").addEventListener("change", async event => {
  const file = event.target.files[0];
  if (!file || started) return;
  element("start").disabled = element("files").disabled = element("restore").disabled = true;
  try {
    const backup = JSON.parse(await file.text());
    if (backup.format !== "openabyss-saves-v1" || !backup.files || typeof backup.files !== "object" || Array.isArray(backup.files)) {
      throw new Error("This is not an OpenAbyss save backup.");
    }
    const entries = Object.entries(backup.files);
    for (const [path, bytes] of entries) {
      if (!/^SAVE[1-4]\/[A-Z0-9_.-]+$/.test(path) || !Array.isArray(bytes) ||
          !bytes.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255)) {
        throw new Error("Invalid save file in backup.");
      }
    }
    for (const [path, bytes] of entries) {
      game.FS.mkdirTree("/saves/" + path.split("/")[0]);
      game.FS.writeFile("/saves/" + path, new Uint8Array(bytes));
    }
    await syncSaves();
    status("Save backup restored. Choose your game folder and press Play.");
  } catch (error) { status(`Restore failed: ${error.message || error}`); }
  event.target.value = "";
  element("files").disabled = element("restore").disabled = false;
  element("start").disabled = !selected.length;
});

async function initialize() {
  try {
    game = await createOpenAbyss({
      canvas: element("canvas"),
      noInitialRun: true,
      print: log,
      printErr: log,
      onAbort: reason => status(`Game stopped: ${reason}. See the game log.`),
      onExit: code => {
        status(code ? `Game exited with error ${code}. See the game log.` : "Game closed. Reload to play again.");
        syncSaves().catch(error => status(`Could not store saves: ${error}`));
      }
    });
    game.FS.mkdirTree("/game");
    game.FS.mkdirTree("/saves");
    game.FS.mount(game.IDBFS, {autoPersist: true}, "/saves");
    await syncSaves(true);
    element("files").disabled = element("backup").disabled = element("restore").disabled = false;
    status("Choose your Ultima Underworld game folder to begin.");
  } catch (error) {
    log(String(error));
    status(`Could not load the game or browser save storage: ${error.message || error}`);
  }
}
initialize();
