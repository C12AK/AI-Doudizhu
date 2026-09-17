// 与 server/src/cards.cpp 中的牌型判定对齐，供网页判断「是否合法一手」以及滑动收串。
var DdzCards = (() => {
  const RANK_3 = 3;
  const RANK_A = 14;
  const RANK_2 = 15;
  const RANK_X = 16;
  const RANK_D = 17;

  // 取点数。c：牌编号。返回 3～15 为 3～2，16 小王，17 大王。
  function rankOf(c) {
    if (c === 52) return RANK_X;
    if (c === 53) return RANK_D;
    return Math.floor(c / 4) + 3;
  }

  // 按点数统计张数。cards：牌。返回下标为点数、值为张数的表。
  function countsOf(cards) {
    const cnt = new Array(18).fill(0);
    for (const c of cards) {
      const r = rankOf(c);
      if (r >= 3 && r <= 17) cnt[r]++;
    }
    return cnt;
  }

  // 是否含大小王。cnt：张数表。含王为 true。
  function containsJoker(cnt) {
    return cnt[RANK_X] !== 0 || cnt[RANK_D] !== 0;
  }

  // 3～2 是否有某点至少 4 张。cnt：张数表。有则为 true。
  function containsFour(cnt) {
    for (let r = RANK_3; r <= RANK_2; r++) {
      if (cnt[r] >= 4) return true;
    }
    return false;
  }

  // 点数是否从 3 到 A、无重复、且相邻差 1。2 和王不能出现。
  // ranks：待检查的点数。满足则为 true。
  function consecutive3ToA(ranks) {
    if (!ranks.length) return false;
    for (const r of ranks) {
      if (r < RANK_3 || r > RANK_A) return false;
    }
    const s = ranks.slice().sort((a, b) => a - b);
    for (let i = 1; i < s.length; i++) {
      if (s[i] === s[i - 1]) return false;
      if (s[i] !== s[i - 1] + 1) return false;
    }
    return true;
  }

  function maxOf(xs) {
    let m = xs[0];
    for (let i = 1; i < xs.length; i++) {
      if (xs[i] > m) m = xs[i];
    }
    return m;
  }

  // 是否为单张。cards：要判定的牌。对得上返回点数，对不上返回空。
  function matchSolo(cards) {
    if (cards.length !== 1) return null;
    return rankOf(cards[0]);
  }

  // 是否为火箭（大王小王）。cards：要判定的牌。对得上返回大王的点数，对不上返回空。
  function matchRocket(cards) {
    if (cards.length !== 2) return null;
    const a = rankOf(cards[0]);
    const b = rankOf(cards[1]);
    if ((a === RANK_X && b === RANK_D) || (a === RANK_D && b === RANK_X)) return RANK_D;
    return null;
  }

  // 是否为一对（不含火箭）。cards：要判定的牌。对得上返回点数，对不上返回空。
  function matchPair(cards) {
    if (cards.length !== 2) return null;
    if (matchRocket(cards) != null) return null;
    const r = rankOf(cards[0]);
    if (r !== rankOf(cards[1])) return null;
    if (r >= RANK_X) return null;
    return r;
  }

  // 是否为三张不带牌。cards：要判定的牌。对得上返回点数，对不上返回空。
  function matchTrio(cards) {
    if (cards.length !== 3) return null;
    const cnt = countsOf(cards);
    for (let r = RANK_3; r <= RANK_2; r++) {
      if (cnt[r] === 3) return r;
    }
    return null;
  }

  // 是否为四张炸弹。cards：要判定的牌。对得上返回点数，对不上返回空。
  function matchBomb(cards) {
    if (cards.length !== 4) return null;
    const cnt = countsOf(cards);
    for (let r = RANK_3; r <= RANK_2; r++) {
      if (cnt[r] === 4) return r;
    }
    return null;
  }

  // 是否为三带一。cards：要判定的牌。对得上返回主体点数，对不上返回空。
  function matchTrioSolo(cards) {
    if (cards.length !== 4) return null;
    if (matchBomb(cards) != null) return null;
    const cnt = countsOf(cards);
    let body = 0;
    let others = 0;
    for (let r = RANK_3; r <= RANK_2; r++) {
      const n = cnt[r];
      if (n === 0) continue;
      if (n === 3 && body === 0) body = r;
      else others += n;
    }
    if (containsJoker(cnt)) return null;
    if (body !== 0 && others === 1) return body;
    return null;
  }

  // 是否为三带一对。cards：要判定的牌。对得上返回主体点数，对不上返回空。
  function matchTrioPair(cards) {
    if (cards.length !== 5) return null;
    const cnt = countsOf(cards);
    let body = 0;
    let pair = 0;
    for (let r = RANK_3; r <= RANK_2; r++) {
      const n = cnt[r];
      if (n === 3 && body === 0) body = r;
      else if (n === 2 && pair === 0) pair = r;
      else if (n !== 0) return null;
    }
    if (cnt[RANK_X] || cnt[RANK_D]) return null;
    if (body !== 0 && pair !== 0 && body !== pair) return body;
    return null;
  }

  // 是否为顺子（至少 5 张，不能含 2 和王）。cards：要判定的牌。对得上返回最大点数，对不上返回空。
  function matchStraight(cards) {
    const k = cards.length;
    if (k < 5) return null;
    const cnt = countsOf(cards);
    const ranks = [];
    for (let r = RANK_3; r <= RANK_D; r++) {
      const n = cnt[r];
      if (n === 0) continue;
      if (n !== 1) return null;
      ranks.push(r);
    }
    if (ranks.length !== k) return null;
    if (!consecutive3ToA(ranks)) return null;
    return maxOf(ranks);
  }

  // 是否为连对（至少 3 对）。cards：要判定的牌。对得上返回最大点数，对不上返回空。
  function matchPairStraight(cards) {
    const k = cards.length;
    if (k < 6 || k % 2 !== 0) return null;
    const n = k / 2;
    if (n < 3) return null;
    const cnt = countsOf(cards);
    const ranks = [];
    for (let r = RANK_3; r <= RANK_D; r++) {
      const c = cnt[r];
      if (c === 0) continue;
      if (c !== 2) return null;
      ranks.push(r);
    }
    if (ranks.length !== n) return null;
    if (!consecutive3ToA(ranks)) return null;
    return maxOf(ranks);
  }

  // 是否为飞机不带翅膀（至少两连三张）。cards：要判定的牌。对得上返回主体最大点数，对不上返回空。
  function matchPlane(cards) {
    const k = cards.length;
    if (k < 6 || k % 3 !== 0) return null;
    const n = k / 3;
    if (n < 2) return null;
    const cnt = countsOf(cards);
    const ranks = [];
    for (let r = RANK_3; r <= RANK_D; r++) {
      const c = cnt[r];
      if (c === 0) continue;
      if (c !== 3) return null;
      ranks.push(r);
    }
    if (ranks.length !== n) return null;
    if (!consecutive3ToA(ranks)) return null;
    return maxOf(ranks);
  }

  // 是否为四带二单张。cards：要判定的牌。对得上返回主体点数，对不上返回空。
  function matchFourSolo(cards) {
    if (cards.length !== 6) return null;
    const cnt = countsOf(cards);
    let body = 0;
    for (let r = RANK_3; r <= RANK_2; r++) {
      if (cnt[r] === 4) {
        if (body !== 0) return null;
        body = r;
      }
    }
    if (body === 0 || containsJoker(cnt)) return null;
    let rest = 0;
    for (let r = RANK_3; r <= RANK_2; r++) {
      if (r === body) continue;
      rest += cnt[r];
    }
    if (rest !== 2) return null;
    return body;
  }

  // 是否为四带两对。cards：要判定的牌。对得上返回主体点数，对不上返回空。
  function matchFourPair(cards) {
    if (cards.length !== 8) return null;
    const cnt = countsOf(cards);
    let body = 0;
    let pairs = 0;
    for (let r = RANK_3; r <= RANK_2; r++) {
      const c = cnt[r];
      if (c === 4) {
        if (body !== 0) return null;
        body = r;
      } else if (c === 2) {
        pairs++;
      } else if (c !== 0) {
        return null;
      }
    }
    if (cnt[RANK_X] || cnt[RANK_D]) return null;
    if (body !== 0 && pairs === 2) return body;
    return null;
  }

  // 是否为飞机带单张。cards：要判定的牌。对得上返回主体最大点数，对不上返回空。
  function matchPlaneSolo(cards) {
    const k = cards.length;
    if (k < 8 || k % 4 !== 0) return null;
    const n = k / 4;
    if (n < 2) return null;
    const cnt = countsOf(cards);
    if (containsJoker(cnt) || containsFour(cnt)) return null;
    let best = 0;
    let found = false;
    for (let start = RANK_3; start + n - 1 <= RANK_A; start++) {
      let ok = true;
      for (let i = 0; i < n; i++) {
        if (cnt[start + i] !== 3) {
          ok = false;
          break;
        }
      }
      if (!ok) continue;
      const rem = cnt.slice();
      let left = k;
      for (let i = 0; i < n; i++) {
        rem[start + i] -= 3;
        left -= 3;
      }
      if (left !== n) continue;
      let wingsOk = true;
      let wingCards = 0;
      const hi = start + n - 1;
      if ((start > RANK_3 && rem[start - 1] >= 3) || (hi < RANK_A && rem[hi + 1] >= 3)) {
        continue;
      }
      for (let r = RANK_3; r <= RANK_2; r++) {
        const c = rem[r];
        if (c === 0) continue;
        if (r >= start && r <= hi) {
          wingsOk = false;
          break;
        }
        wingCards += c;
      }
      if (!wingsOk || wingCards !== n) continue;
      found = true;
      best = Math.max(best, hi);
    }
    if (!found) return null;
    return best;
  }

  // 是否为飞机带对子。cards：要判定的牌。对得上返回主体最大点数，对不上返回空。
  function matchPlanePair(cards) {
    const k = cards.length;
    if (k < 10 || k % 5 !== 0) return null;
    const n = k / 5;
    if (n < 2) return null;
    const cnt = countsOf(cards);
    if (containsJoker(cnt) || containsFour(cnt)) return null;
    let best = 0;
    let found = false;
    for (let start = RANK_3; start + n - 1 <= RANK_A; start++) {
      let ok = true;
      for (let i = 0; i < n; i++) {
        if (cnt[start + i] !== 3) {
          ok = false;
          break;
        }
      }
      if (!ok) continue;
      let pairN = 0;
      let wingsOk = true;
      for (let r = RANK_3; r <= RANK_D; r++) {
        if (r >= start && r <= start + n - 1) continue;
        const c = cnt[r];
        if (c === 0) continue;
        if (c !== 2 || r >= RANK_X) {
          wingsOk = false;
          break;
        }
        pairN++;
      }
      if (!wingsOk || pairN !== n) continue;
      found = true;
      best = Math.max(best, start + n - 1);
    }
    if (!found) return null;
    return best;
  }

  const matchers = [
    matchRocket,
    matchBomb,
    matchSolo,
    matchPair,
    matchTrio,
    matchTrioSolo,
    matchTrioPair,
    matchStraight,
    matchPairStraight,
    matchPlane,
    matchPlaneSolo,
    matchPlanePair,
    matchFourSolo,
    matchFourPair
  ];

  // 列出这组牌所有合法牌型。cards：牌。有任一种则返回 true。
  function isLegalPlay(cards) {
    for (const fn of matchers) {
      if (fn(cards) != null) return true;
    }
    return false;
  }

  // 从 cards 里按出现顺序，在 [lo, hi] 每门最多留 n 张。
  function keepNPerRank(cards, lo, hi, n) {
    const taken = new Array(18).fill(0);
    const out = [];
    for (const c of cards) {
      const r = rankOf(c);
      if (r < lo || r > hi) continue;
      if (taken[r] < n) {
        out.push(c);
        taken[r]++;
      }
    }
    return out;
  }

  // 滑动选中后：已是合法一手则原样返回；否则钉死最小、最大牌名，能收成双串则每门留 2 张，
  // 否则能收成单串则每门留 1 张；再否则原样返回。
  // cards：本次划过的牌编号。返回应保留的编号（输入的子列，顺序不变）。
  function keepSwipeChain(cards) {
    if (!cards.length || isLegalPlay(cards)) return cards.slice();

    let lo = 99;
    let hi = 0;
    const cnt = countsOf(cards);
    for (let r = RANK_3; r <= RANK_D; r++) {
      if (cnt[r] === 0) continue;
      if (r < lo) lo = r;
      if (r > hi) hi = r;
    }
    if (lo > hi || lo < RANK_3 || hi > RANK_A) return cards.slice();

    const span = hi - lo + 1;
    let pairOk = span >= 3;
    let soloOk = span >= 5;
    for (let r = lo; r <= hi; r++) {
      if (cnt[r] < 1) {
        pairOk = false;
        soloOk = false;
        break;
      }
      if (cnt[r] < 2) pairOk = false;
    }
    if (pairOk) return keepNPerRank(cards, lo, hi, 2);
    if (soloOk) return keepNPerRank(cards, lo, hi, 1);
    return cards.slice();
  }

  return { rankOf, isLegalPlay, keepSwipeChain };
})();
