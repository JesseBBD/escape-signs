// Gen-2 Cloud Functions (Node 18)
const {initializeApp} = require("firebase-admin/app");
const {getDatabase} = require("firebase-admin/database");
const {onValueCreated} = require("firebase-functions/v2/database");
const {setGlobalOptions, logger} = require("firebase-functions/v2");

initializeApp();
setGlobalOptions({
  region: "europe-west1", // <-- set to your region
  memory: "256MiB",
  timeoutSeconds: 10,
});

const path = "escape-signs/rooms/{room}/vote-queue/{id}";
exports.onVote = onValueCreated(path, async (event) => {
  const room = event.params.room;
  const db = getDatabase();
  const v = event.data.val() || {};

  try {
    // Validate payload
    const hexRaw = (v.hex || "").toString().toUpperCase();
    const session = (v.session || "").toString();
    const code = (v.code || "").toString();
    if (!/^#?[0-9A-F]{6}$/.test(hexRaw) || !session || !code) {
      await event.data.ref.remove();
      return;
    }
    const normHex = "#" + hexRaw.replace("#", "");

    // Check room code
    const codeSnap = await db.ref(`escape-signs/rooms/${room}/code`).get();
    if (!codeSnap.exists() || codeSnap.val() !== code) {
      await event.data.ref.remove();
      return;
    }

    // Enforce 5s per session
    const sessRef = db.ref(`escape-signs/rooms/${room}/sessions/${session}`);
    const sessSnap = await sessRef.get();
    const sess = sessSnap.exists() ? sessSnap.val() : {};
    const now = Date.now();
    if (sess.lastTs && (now - sess.lastTs) < 5000) {
      await event.data.ref.remove();
      return;
    }

    // Transactionally update tally (NOTE: no second argument!)
    const tallyRef = db.ref(`escape-signs/rooms/${room}/tally`);
    await tallyRef.transaction((t) => {
      if (!t) t = {counts: {}, total: 0, leader: "#000000", ts: 0};
      t.counts = t.counts || {};
      const key = normHex.slice(1); // remove '#'
      t.counts[key] = (t.counts[key] || 0) + 1;
      t.total = (t.total || 0) + 1;

      // recompute leader
      let best = t.leader || "#000000";
      let bestC = -1;
      for (const k in t.counts) {
        if (k) {
          const c = t.counts[k];
          if (c > bestC) {
            bestC = c; best = "#" + k;
          }
        }
      }
      if (best !== t.leader) {
        t.leader = best;
        t.ts = now; // change marker
      }
      return t;
    });

    // Record cooldown & remove queue item
    await sessRef.set({lastTs: now});
    await event.data.ref.remove();
  } catch (e) {
    logger.error("onVote failed:", e);
    // Optional: keep the queue item to allow automatic retry.
    // For now we remove it to avoid stuck items:
    await event.data.ref.remove().catch(()=>{});
  }
});
