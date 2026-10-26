(() => {
  const joinScreen = document.getElementById('join');
  const playScreen = document.getElementById('play');
  const slotsEl = document.getElementById('slots');
  const joinMsg = document.getElementById('joinMsg');
  const youAre = document.getElementById('youAre');
  const dot = document.getElementById('connDot');

  const SLOT_LABELS = Object.fromEntries(Array.from({length: 8}, (_, i) => [i + 1, `Player ${i + 1}`]));
  const BUTTONS = ['red', 'blue', 'orange', 'green', 'yellow'];

  let ws = null;
  let mySlot = null;
  let wantSlot = null;   // slot we're trying to (re)join, survives reconnects
  let players = 8;
  let slotState = {};
  let backoff = 500;

  function wsUrl() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${proto}//${location.host}/ws`;
  }

  function renderSlots() {
    slotsEl.innerHTML = '';
    for (let n = 1; n <= players; n++) {
      const taken = !!slotState[String(n)];
      const b = document.createElement('button');
      b.className = `slot p${n}`;
      b.disabled = taken;
      b.innerHTML = `${SLOT_LABELS[n]}${taken ? '<span class="taken">taken</span>' : ''}`;
      b.addEventListener('click', () => joinSlot(n));
      slotsEl.appendChild(b);
    }
  }

  function joinSlot(n) {
    wantSlot = n;
    joinMsg.textContent = '';
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ type: 'join', slot: n }));
    }
  }

  function showPlay() {
    joinScreen.classList.add('hidden');
    playScreen.classList.remove('hidden');
    youAre.textContent = SLOT_LABELS[mySlot];
  }

  function showJoin() {
    playScreen.classList.add('hidden');
    joinScreen.classList.remove('hidden');
    mySlot = null;
    renderSlots();
  }

  function connect() {
    ws = new WebSocket(wsUrl());

    ws.onopen = () => {
      backoff = 500;
      dot.className = 'dot ok';
      if (wantSlot) {
        ws.send(JSON.stringify({ type: 'join', slot: wantSlot }));
      }
    };

    ws.onclose = () => {
      dot.className = 'dot bad';
      clearButtons();
      showJoin();
      setTimeout(connect, backoff);
      backoff = Math.min(backoff * 1.6, 5000);
    };

    ws.onerror = () => ws.close();

    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.type === 'status') {
        slotState = msg.slots;
        if (typeof msg.players === 'number') players = msg.players;
        if (mySlot === null) renderSlots();
      } else if (msg.type === 'joined') {
        mySlot = msg.slot;
        showPlay();
      } else if (msg.type === 'lights') {
        document.getElementById('red').classList.toggle('lit', !!msg.slots?.[String(mySlot)]);
      } else if (msg.type === 'join_rejected') {
        wantSlot = null;
        joinMsg.textContent = 'That slot was just taken -- pick another.';
        showJoin();
      }
    };
  }

  function send(type, button) {
    if (mySlot !== null && ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ type, button }));
    }
  }

  function clearButtons() {
    document.querySelectorAll('.pressed').forEach(el => el.classList.remove('pressed'));
    document.getElementById('red').classList.remove('lit');
  }

  function releaseAll() {
    BUTTONS.forEach(name => send('up', name));
    clearButtons();
  }
  window.addEventListener('blur', releaseAll);
  window.addEventListener('pagehide', releaseAll);

  function bindButton(el) {
    const name = el.dataset.btn;
    const press = (ev) => {
      ev.preventDefault();
      el.classList.add('pressed');
      if (navigator.vibrate) navigator.vibrate(15);
      send('down', name);
    };
    const release = (ev) => {
      ev.preventDefault();
      el.classList.remove('pressed');
      send('up', name);
    };
    el.addEventListener('pointerdown', press);
    el.addEventListener('pointerup', release);
    el.addEventListener('pointercancel', release);
    el.addEventListener('pointerleave', release);
    el.addEventListener('contextmenu', (ev) => ev.preventDefault());
  }

  BUTTONS.forEach((name) => bindButton(document.querySelector(`[data-btn="${name}"]`)));

  document.addEventListener('visibilitychange', () => {
    // Release everything if the tab is backgrounded mid-press.
    if (document.hidden) releaseAll();
  });

  renderSlots();
  connect();
})();
