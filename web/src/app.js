(() => {
  const $ = (id) => document.getElementById(id);
  let ws = null;
  let state = null;
  let selected = new Set();
  let reconnectLeft = 3;
  let reconnectTimer = null;
  let springIntro = false;
  let dragOn = false;
  let dragAdd = true;
  let hallQuery = "";
  let hallPage = "list";

  const ranks = { 3: "3", 4: "4", 5: "5", 6: "6", 7: "7", 8: "8", 9: "9", 10: "10", 11: "J", 12: "Q", 13: "K", 14: "A", 15: "2" };
  const suits = ["♠", "♥", "♣", "♦"];
  const phases = { hall: "大厅", lobby: "准备", dealing: "发牌中", call: "叫地主", rob: "抢地主", double: "加倍", play: "出牌", settle: "结算", set_over: "大局结束" };

  function savedUser() { return sessionStorage.getItem("ddz_user") || ""; }
  function savedPass() { return sessionStorage.getItem("ddz_pass") || ""; }

  function isRed(id) {
    if (id === 53) return true;
    if (id === 52) return false;
    return id % 4 === 1 || id % 4 === 3;
  }

  function cardFace(id) {
    if (id === 52) return { rank: "小", suit: "王", pip: "小王", red: false, joker: true, ten: false };
    if (id === 53) return { rank: "大", suit: "王", pip: "大王", red: true, joker: true, ten: false };
    const r = Math.floor(id / 4) + 3;
    const suit = suits[id % 4];
    return { rank: ranks[r], suit, pip: suit, red: isRed(id), joker: false, ten: r === 10 };
  }

  function makeCard(id, opts) {
    const small = !!(opts && opts.small);
    const selectable = !!(opts && opts.selectable);
    const z = (opts && opts.z) || 1;
    const f = cardFace(id);
    const el = document.createElement("div");
    el.className = "card"
      + (f.red ? " red" : "")
      + (small ? " small" : "")
      + (f.joker ? " joker" : "")
      + (f.ten ? " ten" : "")
      + (selectable && selected.has(id) ? " sel" : "");
    el.style.zIndex = String(z);
    el.dataset.id = String(id);
    el.innerHTML = `<span class="corner"><span class="rk">${f.rank}</span><span class="st">${f.suit}</span></span><span class="pip">${f.pip}</span>`;
    return el;
  }

  function fillCards(el, ids, opts) {
    el.innerHTML = "";
    (ids || []).forEach((id, i) => el.appendChild(makeCard(id, Object.assign({ z: i + 1 }, opts))));
  }

  function toast(msg) {
    const el = $("toast");
    el.textContent = msg;
    el.hidden = false;
    setTimeout(() => { el.hidden = true; }, 2200);
  }

  function send(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(obj));
    }
  }

  function showLogin() {
    state = null;
    selected.clear();
    $("view-game").hidden = true;
    $("view-hall").hidden = true;
    $("view-create").hidden = true;
    $("view-login").hidden = false;
    $("settle").hidden = true;
    $("set-board").hidden = true;
    $("spring-flash").hidden = true;
    hallQuery = "";
    hallPage = "list";
    const q = $("room-search");
    if (q) q.value = "";
  }

  function doLogin() {
    const u = $("username").value.trim();
    const p = $("password").value;
    sessionStorage.setItem("ddz_user", u);
    sessionStorage.setItem("ddz_pass", p);
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      connect();
      return;
    }
    send({ op: "login", username: u, password: p });
  }

  function connect() {
    fetch("config.json")
      .then((r) => r.json())
      .catch(() => ({ ws_path: "ws" }))
      .then((cfg) => {
        const rel = cfg.ws_path || "ws";
        const url = new URL(rel, location.href);
        url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
        ws = new WebSocket(url);
        ws.onopen = () => {
          if (reconnectTimer) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
          }
          reconnectLeft = 3;
          const u = savedUser();
          const p = savedPass();
          if (u && p) send({ op: "login", username: u, password: p });
        };
        ws.onmessage = (ev) => {
          const msg = JSON.parse(ev.data);
          if (msg.op === "ping") {
            send({ op: "pong" });
            return;
          }
          if (msg.op === "leave_ok") {
            sessionStorage.removeItem("ddz_user");
            sessionStorage.removeItem("ddz_pass");
            showLogin();
            toast("已退出登录");
            return;
          }
          if (msg.op === "error") {
            toast(msg.msg || "错误");
            return;
          }
          if (msg.op === "abort") {
            toast(msg.reason || "连接已断开");
            selected.clear();
            springIntro = false;
          }
          if (msg.op === "deal") {
            selected.clear();
            springIntro = false;
          }
          state = msg;
          render();
        };
        ws.onclose = () => {
          if (reconnectLeft <= 0) {
            toast("连接已断开");
            return;
          }
          reconnectLeft -= 1;
          reconnectTimer = setTimeout(connect, 10000);
        };
      });
  }

  function addBtn(act, label, fn, ghost) {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = label;
    if (ghost) b.className = "ghost";
    b.onclick = fn;
    act.appendChild(b);
  }

  function matchLabel(st) {
    const total = st.match_total || 1;
    const done = st.match_done || 0;
    const playing = st.phase === "call" || st.phase === "rob" || st.phase === "double" || st.phase === "play" || st.phase === "dealing";
    const finished = playing ? Math.max(0, done - 1) : done;
    return `${finished}/${total}`;
  }

  function showAfterSpring(needSpring, modalEl, stillValid) {
    const flash = $("spring-flash");
    if (needSpring && !springIntro) {
      springIntro = true;
      modalEl.hidden = true;
      $("spring-text").textContent = state.spring === "spring" ? "春天 ×2" : "反春 ×2";
      flash.hidden = false;
      setTimeout(() => {
        flash.hidden = true;
        if (stillValid()) modalEl.hidden = false;
      }, 1600);
    } else if (needSpring && flash && !flash.hidden) {
      modalEl.hidden = true;
    } else {
      modalEl.hidden = false;
    }
  }

  function nameWeight(s) {
    let n = 0;
    for (const ch of s) {
      n += ch.codePointAt(0) <= 0x7F ? 1 : 2;
    }
    return n;
  }

  function roomMatches(r, q) {
    if (!q) return true;
    const needle = q.toLowerCase();
    const name = String(r.name || "").toLowerCase();
    const id = String(r.id || "").toLowerCase();
    return name.indexOf(needle) >= 0 || id.indexOf(needle) >= 0;
  }

  function fillRoomList() {
    const list = $("room-list");
    list.innerHTML = "";
    const rooms = (state.rooms || []).filter((r) => roomMatches(r, hallQuery));
    if (!(state.rooms || []).length) {
      list.innerHTML = '<p class="hint">暂无房间</p>';
      return;
    }
    if (!rooms.length) {
      list.innerHTML = '<p class="hint">没有符合条件的房间</p>';
      return;
    }
    rooms.forEach((r) => {
      const n = r.n || 0;
      const full = n >= 3;
      const status = full ? "已满" : (r.busy ? "游戏中" : "等待中");
      const row = document.createElement("div");
      row.className = "room-card" + (full ? " full" : "");
      row.innerHTML = `<div class="room-card-left"><div class="room-card-name">${r.name || ""}</div><div class="room-card-id">${r.id}</div></div><div class="room-card-right"><div class="room-card-matches">${r.match_total}局</div><div class="room-card-status">${status}</div></div>`;
      row.onclick = () => {
        if (full) {
          toast("房间已满");
          return;
        }
        send({ op: "enter_room", room_id: String(r.id) });
      };
      list.appendChild(row);
    });
  }

  function showHallList() {
    hallPage = "list";
    $("view-login").hidden = true;
    $("view-game").hidden = true;
    $("view-create").hidden = true;
    $("view-hall").hidden = false;
    fillRoomList();
  }

  function showCreatePage() {
    hallPage = "create";
    $("view-login").hidden = true;
    $("view-game").hidden = true;
    $("view-hall").hidden = true;
    $("view-create").hidden = false;
  }

  function renderHall() {
    $("settle").hidden = true;
    $("set-board").hidden = true;
    $("spring-flash").hidden = true;
    springIntro = false;
    $("hall-user").textContent = state.username || savedUser();
    fillRoomList();
    if (hallPage === "create") {
      showCreatePage();
      return;
    }
    showHallList();
  }

  function renderBoard() {
    const board = $("set-board");
    const items = [];
    (state.seats || []).forEach((s, i) => {
      if (!s.occupied) return;
      items.push({ name: s.username, score: s.set_score || 0, i });
    });
    items.sort((a, b) => b.score - a.score);
    $("board-list").innerHTML = items.map((x) => {
      const sc = x.score > 0 ? `+${x.score}` : String(x.score);
      return `<div class="board-row"><span>${x.name}</span><span>${sc}</span></div>`;
    }).join("");
    showAfterSpring(
      state.spring === "spring" || state.spring === "anti",
      board,
      () => state && state.phase === "set_over" && state.can_close_board
    );
  }

  function render() {
    if (!state || state.op === "error") {
      return;
    }
    if (state.phase === "hall" || state.op === "hall") {
      renderHall();
      return;
    }
    if (state.you < 0) {
      return;
    }
    $("view-login").hidden = true;
    $("view-hall").hidden = true;
    $("view-create").hidden = true;
    $("view-game").hidden = false;
    hallPage = "list";

    const you = state.you;
    const seats = state.seats || [];
    const me = seats[you] || {};
    $("meta").textContent = `${state.room_name || ""} ${state.room_id || ""} · ${me.username || ""} · 座位${you + 1} · ${phases[state.phase] || state.phase} · ${matchLabel(state)}`;
    $("mult").textContent = `倍数 ×${state.public_mult || 1}`;

    $("seats").innerHTML = "";
    seats.forEach((s, i) => {
      const div = document.createElement("div");
      div.className = "seat" + (i === you ? " me" : "") + (state.actor === i ? " turn" : "");
      if (!s.occupied) {
        div.innerHTML = "<b>空位</b>";
        $("seats").appendChild(div);
        return;
      }
      const role = s.role === "landlord" ? "地主" : s.role === "farmer" ? "农民" : "未定";
      const score = s.set_score == null ? 0 : s.set_score;
      const inHand = state.phase === "call" || state.phase === "rob" || state.phase === "double" || state.phase === "play";
      const remain = s.remain == null ? 0 : s.remain;
      div.innerHTML = `<b>${s.username}</b><br/>${role}<br/>得分 ${score}`
        + (inHand ? `<br/>剩 ${remain} 张` : "")
        + (s.ready && (state.phase === "lobby" || state.phase === "set_over") ? "<br/>已准备" : "")
        + (s.online === false ? "<br/>断线重连中" : "")
        + (s.doubled ? "<br/>加倍" : "");
      $("seats").appendChild(div);
    });

    const bottomEl = $("bottom-cards");
    if (state.bottom && state.bottom.length) {
      $("bottom-label").textContent = "底牌";
      fillCards(bottomEl, state.bottom, { small: true });
    } else {
      $("bottom-label").textContent = "底牌：未翻开";
      bottomEl.innerHTML = "";
    }

    const lastEl = $("last-cards");
    const lp = state.last_play;
    if (lp) {
      $("last-label").textContent = `上家牌（座位${lp.seat + 1}）`;
      fillCards(lastEl, lp.cards, { small: true });
    } else if (state.phase === "play") {
      $("last-label").textContent = "当前无人出牌，由引牌者先出";
      lastEl.innerHTML = "";
    } else {
      $("last-label").textContent = "";
      lastEl.innerHTML = "";
    }

    const handIds = state.hand || [];
    const inHand = new Set(handIds);
    Array.from(selected).forEach((id) => {
      if (!inHand.has(id)) selected.delete(id);
    });
    fillCards($("hand"), handIds, { selectable: true });

    const act = $("actions");
    act.innerHTML = "";
    if (state.can_ready) addBtn(act, "准备", () => send({ op: "ready" }));
    if (state.can_unready) addBtn(act, "取消准备", () => send({ op: "unready" }), true);
    if (state.phase === "dealing") {
      const p = document.createElement("p");
      p.className = "hint";
      p.textContent = "正在评估发牌…";
      act.appendChild(p);
    }
    if (state.can_call) {
      addBtn(act, "叫地主", () => send({ op: "call", yes: true }));
      addBtn(act, "不叫", () => send({ op: "call", yes: false }), true);
    }
    if (state.can_rob) {
      addBtn(act, "抢地主", () => send({ op: "rob", yes: true }));
      addBtn(act, "不抢", () => send({ op: "rob", yes: false }), true);
    }
    if (state.can_double) {
      addBtn(act, "加倍", () => send({ op: "double", yes: true }));
      addBtn(act, "不加倍", () => send({ op: "double", yes: false }), true);
    }
    if (state.can_play) {
      addBtn(act, "出牌", () => {
        send({ op: "play", cards: Array.from(selected) });
      });
    }
    if (state.can_pass) addBtn(act, "不出", () => send({ op: "pass" }), true);

    const settle = $("settle");
    const board = $("set-board");
    const flash = $("spring-flash");
    const showSmallSettle = state.phase === "lobby" && state.match_done > 0 && state.can_ready;
    if (showSmallSettle) {
      board.hidden = true;
      const sc = state.score || 0;
      $("settle-title").textContent = sc > 0 ? `得分 +${sc}` : `得分 ${sc}`;
      $("settle-detail").hidden = true;
      $("settle-detail").textContent = "";
      $("btn-settle-ready").disabled = !state.can_ready;
      showAfterSpring(
        state.spring === "spring" || state.spring === "anti",
        settle,
        () => state && state.phase === "lobby" && state.match_done > 0 && state.can_ready
      );
    } else if (state.phase === "set_over" && state.can_close_board) {
      settle.hidden = true;
      renderBoard();
    } else {
      settle.hidden = true;
      board.hidden = true;
      if (state.phase !== "set_over") {
        flash.hidden = true;
        if (state.phase !== "lobby" || !state.match_done) {
          springIntro = false;
        }
      }
    }
  }

  function peekPx() {
    const n = parseFloat(getComputedStyle($("hand")).getPropertyValue("--peek"));
    return n > 0 ? n : 22;
  }

  function handCardAt(clientX) {
    const cards = $("hand").querySelectorAll(".card");
    if (!cards.length) {
      return null;
    }
    let i = Math.floor((clientX - cards[0].getBoundingClientRect().left) / peekPx());
    if (i < 0) i = 0;
    if (i >= cards.length) i = cards.length - 1;
    return cards[i];
  }

  function paintSel(el, on) {
    const id = Number(el.dataset.id);
    if (on) selected.add(id);
    else selected.delete(id);
    el.classList.toggle("sel", on);
  }

  const hand = $("hand");
  hand.onpointerdown = (e) => {
    if (e.button !== 0) return;
    const el = handCardAt(e.clientX);
    if (!el) return;
    e.preventDefault();
    dragOn = true;
    dragAdd = !selected.has(Number(el.dataset.id));
    paintSel(el, dragAdd);
    hand.setPointerCapture(e.pointerId);
  };
  hand.onpointermove = (e) => {
    if (!dragOn) return;
    const el = handCardAt(e.clientX);
    if (el) paintSel(el, dragAdd);
  };
  const dragEnd = () => { dragOn = false; };
  hand.onpointerup = dragEnd;
  hand.onpointercancel = dragEnd;

  $("form-login").onsubmit = (e) => {
    e.preventDefault();
    doLogin();
  };
  $("btn-register").onclick = () => toast("该功能暂未上线");
  $("btn-settle-ready").onclick = () => send({ op: "ready" });
  $("btn-leave").onclick = $("btn-leave-settle").onclick = $("btn-leave-board").onclick = () => send({ op: "leave_room" });
  $("btn-logout").onclick = () => send({ op: "logout" });
  $("btn-create-room").onclick = () => {
    if (state && (state.phase === "hall" || state.op === "hall")) {
      showCreatePage();
    }
  };
  $("btn-create-back").onclick = () => {
    if (state && (state.phase === "hall" || state.op === "hall")) {
      showHallList();
    }
  };
  $("btn-close-board").onclick = () => send({ op: "close_board" });
  $("form-create").onsubmit = (e) => {
    e.preventDefault();
    const name = $("room-name").value.trim();
    if (!name) {
      toast("房间名不合法");
      return;
    }
    if (nameWeight(name) > 60) {
      toast("房间名不能超过60字符（一个汉字算2字符）");
      return;
    }
    const matchCount = Number($("match-count").value);
    if (!Number.isInteger(matchCount) || matchCount < 1 || matchCount > 99) {
      toast("局数不合法");
      return;
    }
    send({
      op: "create_room",
      name,
      match_count: matchCount
    });
  };
  $("form-search").onsubmit = (e) => {
    e.preventDefault();
    hallQuery = $("room-search").value.trim();
    if (state && (state.phase === "hall" || state.op === "hall") && hallPage === "list") {
      fillRoomList();
    }
  };

  connect();
})();
