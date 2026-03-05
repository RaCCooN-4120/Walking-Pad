// ================= FIREBASE CONFIG =================
const firebaseConfig = {
  apiKey: "AIzaSyDwsj3SQGid1M2qlrESoCO24yyLV6lBD-A",
  databaseURL: "https://walkingpad-psu-mte-default-rtdb.asia-southeast1.firebasedatabase.app/"
};

firebase.initializeApp(firebaseConfig);
const db = firebase.database();

// ================= SPRITE CONFIG =================
const FRAME_COUNT = 6;
const FRAME_INTERVAL = 120;
const BASE_PATH = "assets/animations/walk/";

let left = 0;
let right = 0;
let speed = 0;
let distance = 0;

let direction = "north";
let frame = 0;
let lastFrameTime = 0;

const character = document.getElementById("character");
const leftVal = document.getElementById("leftVal");
const rightVal = document.getElementById("rightVal");
const speedVal = document.getElementById("speedVal");
const distanceVal = document.getElementById("distanceVal");

// ================== FIREBASE LISTENER ==================

db.ref("/walkingpad").on("value", (snapshot) => {

  const data = snapshot.val();
  if (!data) return;

  const turn = data.turn || "STOP";
  const omega = data.omega || 0;

  speed = data.speed || 0;
  distance = data.distance || 0;

  if (turn === "LEFT") {
    left = omega;
    right = 0;
  }
  else if (turn === "RIGHT") {
    right = omega;
    left = 0;
  }
  else {
    left = 0;
    right = 0;
  }
});

// ================= ANIMATION =================

function update(timestamp) {

  const walking = (left > 0 || right > 0 || speed > 0);

  if (left > 0) direction = "west";
  else if (right > 0) direction = "east";
  else if (speed > 0) direction = "north";
  else direction = "north";

  if (walking) {
    if (timestamp - lastFrameTime > FRAME_INTERVAL) {
      frame = (frame + 1) % FRAME_COUNT;
      lastFrameTime = timestamp;
    }
  } else {
    frame = 0; // idle frame 0
  }

  character.src =
    `${BASE_PATH}${direction}/frame_${String(frame).padStart(3, "0")}.png`;

  leftVal.textContent = left.toFixed(2);
  rightVal.textContent = right.toFixed(2);
  speedVal.textContent = speed.toFixed(2);
  distanceVal.textContent = distance.toFixed(2);

  requestAnimationFrame(update);
}

requestAnimationFrame(update);
