#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

/**
 * WebInterface.h
 * Self-contained HTML/CSS/JS dashboard for controlling EyeMech over HTTP.
 * Served offline by the ESP32 SoftAP, so NO external fonts/CDNs are used.
 * Routes used: /mode?set=, /mood?set=, /update?lr&ud&trim, /blink, /state
 *
 * Phase A: Digital Twin — live SVG eye mirror of the animatronic state.
 */

const char WEB_INTERFACE_HTML[] PROGMEM = R"raw(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>EyeMech Control</title>
    <style>
        :root {
            --bg:        #07070c;
            --ink:       #ececf4;
            --muted:     #7d7d92;
            --violet:    #bb86fc;
            --violet-dk: #6a32d4;
            --teal:      #03dac6;
            --teal-dk:   #018786;
            --line:      rgba(255,255,255,.08);
            --glass:     rgba(255,255,255,.045);
            --radius:    16px;
            --mono: ui-monospace, "SF Mono", "Cascadia Code", "JetBrains Mono", Menlo, Consolas, monospace;
            --sans: ui-rounded, "Segoe UI", system-ui, -apple-system, "Helvetica Neue", sans-serif;
        }

        * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }

        body {
            margin: 0; min-height: 100vh; padding: 28px 18px 20px;
            font-family: var(--sans); color: var(--ink);
            background:
                radial-gradient(120% 80% at 50% -10%, rgba(187,134,252,.16), transparent 55%),
                radial-gradient(90% 60% at 50% 110%, rgba(3,218,198,.12), transparent 55%),
                var(--bg);
            background-attachment: fixed;
            display: flex; flex-direction: column; align-items: center;
            user-select: none; -webkit-user-select: none;
            overflow-x: hidden;
        }
        body::before {
            content: ""; position: fixed; inset: 0; z-index: -1; pointer-events: none;
            background-image:
                linear-gradient(var(--line) 1px, transparent 1px),
                linear-gradient(90deg, var(--line) 1px, transparent 1px);
            background-size: 38px 38px;
            -webkit-mask-image: radial-gradient(120% 90% at 50% 30%, #000 35%, transparent 80%);
                    mask-image: radial-gradient(120% 90% at 50% 30%, #000 35%, transparent 80%);
            opacity: .5;
        }

        .shell { width: 100%; max-width: 400px; display: flex; flex-direction: column; gap: 18px; }

        header { text-align: center; opacity: 0; animation: rise .6s ease forwards; }
        .eyebrow {
            font: 600 10px/1 var(--mono); letter-spacing: .42em; text-transform: uppercase;
            color: var(--teal); margin: 0 0 8px; padding-left: .42em;
        }
        h1 {
            margin: 0; font-size: 1.7rem; font-weight: 800; letter-spacing: -.01em;
            background: linear-gradient(180deg, #fff, #c9b8ff);
            -webkit-background-clip: text; background-clip: text; -webkit-text-fill-color: transparent;
        }
        .status-pill {
            display: inline-flex; align-items: center; gap: 7px; margin-top: 10px;
            font: 600 10px/1 var(--mono); letter-spacing: .15em; text-transform: uppercase;
            color: var(--muted); padding: 5px 11px; border: 1px solid var(--line);
            border-radius: 999px; background: var(--glass);
        }
        .status-pill .dot {
            width: 7px; height: 7px; border-radius: 50%; background: var(--muted);
            box-shadow: 0 0 8px currentColor; transition: .3s;
        }
        .status-pill.live .dot { background: var(--teal); }

        .panel {
            background: var(--glass); border: 1px solid var(--line);
            border-radius: var(--radius); padding: 14px;
            box-shadow: inset 0 1px 0 rgba(255,255,255,.05), 0 12px 30px -18px #000;
        }
        .grid { display: grid; gap: 11px; }
        .grid-2 { grid-template-columns: 1fr 1fr; }
        .grid-3 { grid-template-columns: repeat(3, 1fr); }
        /* Six-column span grid: clean 3+2 fill for any 5-item row, no clipping */
        .six-grid { display: grid; grid-template-columns: repeat(6, 1fr); gap: 8px; }
        .six-grid .s2 { grid-column: span 2; }
        .six-grid .s3 { grid-column: span 3; }

        button {
            font-family: var(--sans); font-weight: 700; font-size: .82rem;
            letter-spacing: .06em; text-transform: uppercase; color: var(--ink);
            border: 1px solid var(--line); border-radius: 12px; padding: 14px 6px;
            background: linear-gradient(180deg, rgba(255,255,255,.06), rgba(255,255,255,.02));
            cursor: pointer; transition: transform .08s ease, box-shadow .2s, border-color .2s, background .2s;
        }
        button:active { transform: scale(.96); }

        .mode-btn { color: #d9ccff; }
        .mode-btn.active {
            color: #fff; border-color: transparent;
            background: linear-gradient(180deg, var(--violet), var(--violet-dk));
            box-shadow: 0 0 0 1px rgba(187,134,252,.4), 0 8px 26px -8px rgba(124,50,212,.85),
                        inset 0 1px 0 rgba(255,255,255,.35);
        }

        .action-btn { color: #bff7f0; }
        .action-btn:active {
            border-color: var(--teal);
            box-shadow: 0 0 18px -4px rgba(3,218,198,.6), inset 0 0 12px -6px var(--teal);
        }

        .stage { display: flex; justify-content: center; padding: 4px 0 2px; }
        #joystick-zone {
            --r: 250px; width: var(--r); height: var(--r); position: relative;
            border-radius: 50%; touch-action: none; cursor: grab;
            background:
                radial-gradient(circle, transparent 0 27%, rgba(3,218,198,.16) 27% 27.6%, transparent 28%),
                radial-gradient(circle, transparent 0 45%, rgba(187,134,252,.14) 45% 45.6%, transparent 46%),
                radial-gradient(circle, transparent 0 63%, rgba(3,218,198,.10) 63% 63.6%, transparent 64%),
                radial-gradient(circle at 50% 42%, rgba(187,134,252,.16), transparent 62%),
                #0c0c15;
            border: 1px solid rgba(187,134,252,.28);
            box-shadow: 0 0 50px -14px rgba(124,50,212,.7),
                        inset 0 0 60px -22px rgba(3,218,198,.55),
                        inset 0 0 0 1px rgba(255,255,255,.03);
        }
        #joystick-zone:active { cursor: grabbing; }
        #joystick-zone::before {
            content: ""; position: absolute; inset: 8%; border-radius: 50%;
            background:
                linear-gradient(transparent calc(50% - .5px), rgba(255,255,255,.10) 50%, transparent calc(50% + .5px)),
                linear-gradient(90deg, transparent calc(50% - .5px), rgba(255,255,255,.10) 50%, transparent calc(50% + .5px));
        }
        .tick {
            position: absolute; font: 600 9px/1 var(--mono); letter-spacing: .15em;
            color: var(--muted); transform: translate(-50%, -50%);
        }
        .tick.n { top: 7%;  left: 50%; }
        .tick.s { top: 93%; left: 50%; }
        .tick.e { top: 50%; left: 93%; }
        .tick.w { top: 50%; left: 7%; }

        #joystick-knob {
            width: 76px; height: 76px; border-radius: 50%; position: absolute;
            top: 50%; left: 50%; transform: translate(-50%,-50%); pointer-events: none;
            background: radial-gradient(circle at 38% 32%, #e9d8ff, var(--violet) 42%, var(--violet-dk) 100%);
            box-shadow: 0 0 26px -2px rgba(187,134,252,.9), 0 6px 16px rgba(0,0,0,.6),
                        inset 0 2px 4px rgba(255,255,255,.5), inset 0 -6px 12px rgba(60,0,120,.6);
            transition: left .28s cubic-bezier(.22,1,.36,1), top .28s cubic-bezier(.22,1,.36,1);
        }
        #joystick-zone.dragging #joystick-knob { transition: none; }

        .trim-head { display: flex; justify-content: space-between; align-items: baseline; margin-bottom: 10px; }
        .trim-head .lbl { font: 600 10px/1 var(--mono); letter-spacing: .2em; text-transform: uppercase; color: var(--violet); }
        .trim-head .val { font: 700 13px/1 var(--mono); color: var(--teal); }
        input[type=range] {
            -webkit-appearance: none; appearance: none; width: 100%; height: 8px; border-radius: 999px;
            background: linear-gradient(90deg, var(--teal) 0%, var(--teal) var(--fill,50%), rgba(255,255,255,.08) var(--fill,50%));
            outline: none;
        }
        input[type=range]::-webkit-slider-thumb {
            -webkit-appearance: none; width: 24px; height: 24px; border-radius: 50%;
            background: radial-gradient(circle at 38% 32%, #d7fff9, var(--teal) 55%, var(--teal-dk));
            box-shadow: 0 0 16px -2px rgba(3,218,198,.9), 0 2px 6px rgba(0,0,0,.5); cursor: grab;
        }
        input[type=range]::-moz-range-thumb {
            width: 24px; height: 24px; border: none; border-radius: 50%;
            background: radial-gradient(circle at 38% 32%, #d7fff9, var(--teal) 55%, var(--teal-dk));
            box-shadow: 0 0 16px -2px rgba(3,218,198,.9);
        }
        .scale { display: flex; justify-content: space-between; margin-top: 6px;
                 font: 500 9px/1 var(--mono); letter-spacing: .12em; color: var(--muted); }

        #blinkBtn {
            width: 100%; padding: 17px; font-size: .92rem; letter-spacing: .14em;
            color: #052c29; border: none; border-radius: 14px;
            background: linear-gradient(180deg, #2af0dd, var(--teal) 60%, var(--teal-dk));
            box-shadow: 0 0 26px -6px rgba(3,218,198,.7), inset 0 1px 0 rgba(255,255,255,.5);
            position: relative; overflow: hidden;
        }
        #blinkBtn.busy {
            color: #6f6f80; cursor: pointer;
            background: linear-gradient(180deg, #2a2a35, #1c1c25);
            box-shadow: inset 0 0 0 1px var(--line);
        }
        #blinkBtn.busy::after {
            content: ""; position: absolute; inset: 0;
            background: linear-gradient(90deg, transparent, rgba(3,218,198,.22), transparent);
            transform: translateX(-100%); animation: sweep .3s linear infinite;
        }
        .queued::before {
            content: "+1 QUEUED"; position: absolute; top: 5px; right: 10px;
            font: 700 8px/1 var(--mono); letter-spacing: .1em; color: var(--teal);
        }

        .reveal { opacity: 0; animation: rise .6s ease forwards; }
        .d1 { animation-delay: .06s; } .d2 { animation-delay: .12s; }
        .d3 { animation-delay: .18s; } .d4 { animation-delay: .24s; } .d5 { animation-delay: .3s; }
        .d6 { animation-delay: .36s; } .d7 { animation-delay: .42s; } .d8 { animation-delay: .48s; }
        .d9 { animation-delay: .54s; }
        @keyframes rise { from { opacity: 0; transform: translateY(14px); } to { opacity: 1; transform: none; } }
        @keyframes sweep { to { transform: translateX(100%); } }

        /* ── Choreography panel ────────────────────────────────────────────── */
        .choreo-transport { display: flex; gap: 8px; flex-wrap: wrap; }
        .choreo-transport button { flex: 1 1 auto; min-width: 0; padding: 14px 6px; font-size: .78rem; }

        .btn-play  { color: #b8ffee; }
        .btn-play:active  { border-color: var(--teal);   box-shadow: 0 0 18px -4px rgba(3,218,198,.6); }
        .btn-stop  { color: #ffc1c1; }
        .btn-stop:active  { border-color: #ff6b6b;       box-shadow: 0 0 18px -4px rgba(255,107,107,.6); }

        .choreo-canned { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
        .choreo-canned select,
        .choreo-save   select { flex: 1 1 0; min-width: 0; }

        .choreo-save { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
        .choreo-save input[type=text] {
            flex: 1 1 0; min-width: 0;
            background: rgba(255,255,255,.06); border: 1px solid var(--line);
            border-radius: 10px; color: var(--ink); font: 600 .82rem var(--mono);
            padding: 10px 12px; outline: none;
        }
        .choreo-save input[type=text]:focus { border-color: rgba(187,134,252,.5); }

        select {
            background: rgba(255,255,255,.06); border: 1px solid var(--line);
            border-radius: 10px; color: var(--ink); font: 600 .82rem var(--mono);
            padding: 10px 10px; outline: none; cursor: pointer;
            -webkit-appearance: none; appearance: none;
        }
        select option { background: #16162a; color: var(--ink); }

        .choreo-btn-sm {
            font-family: var(--sans); font-weight: 700; font-size: .75rem;
            letter-spacing: .06em; text-transform: uppercase; color: var(--ink);
            border: 1px solid var(--line); border-radius: 10px; padding: 10px 14px;
            background: linear-gradient(180deg, rgba(255,255,255,.06), rgba(255,255,255,.02));
            cursor: pointer; transition: transform .08s ease, box-shadow .2s, border-color .2s;
            white-space: nowrap;
        }
        .choreo-btn-sm:active { transform: scale(.96); }

        /* Keyframe list */
        .kf-list { display: flex; flex-direction: column; gap: 8px; }

        .kf-row {
            background: rgba(255,255,255,.03); border: 1px solid var(--line);
            border-radius: 12px; padding: 10px;
        }
        .kf-row-top {
            display: flex; align-items: center; gap: 6px; margin-bottom: 8px;
        }
        .kf-idx {
            font: 700 10px/1 var(--mono); letter-spacing: .12em; color: var(--violet);
            min-width: 22px; text-align: center;
        }
        .kf-row-top .kf-reorder { display: flex; flex-direction: column; gap: 2px; }
        .kf-arrow {
            background: none; border: 1px solid var(--line); border-radius: 6px;
            color: var(--muted); font-size: .68rem; padding: 3px 7px; cursor: pointer;
            font-family: var(--sans); font-weight: 700; line-height: 1;
            transition: border-color .15s, color .15s;
        }
        .kf-arrow:active { border-color: var(--teal); color: var(--teal); }
        .kf-del {
            margin-left: auto;
            background: none; border: 1px solid rgba(255,100,100,.2); border-radius: 8px;
            color: #ff8080; font: 700 .72rem var(--sans); padding: 6px 10px; cursor: pointer;
            letter-spacing: .05em; text-transform: uppercase; transition: border-color .15s;
        }
        .kf-del:active { border-color: #ff6060; }

        .kf-fields {
            display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px;
        }
        .kf-field { display: flex; flex-direction: column; gap: 3px; }
        .kf-field label {
            font: 600 8px/1 var(--mono); letter-spacing: .18em; text-transform: uppercase;
            color: var(--muted);
        }
        .kf-field input[type=number],
        .kf-field input[type=text],
        .kf-field select {
            width: 100%; background: rgba(255,255,255,.06); border: 1px solid var(--line);
            border-radius: 8px; color: var(--ink); font: 600 .82rem var(--mono);
            padding: 8px 8px; outline: none;
            -webkit-appearance: none; appearance: none;
        }
        .kf-field input:focus, .kf-field select:focus { border-color: rgba(187,134,252,.5); }

        /* no-spin on number inputs */
        .kf-field input[type=number]::-webkit-outer-spin-button,
        .kf-field input[type=number]::-webkit-inner-spin-button { -webkit-appearance: none; }
        .kf-field input[type=number] { -moz-appearance: textfield; }

        .kf-empty {
            text-align: center; font: 600 11px/1.6 var(--mono);
            color: var(--muted); padding: 18px 0; letter-spacing: .12em;
        }

        /* Toast */
        #choreo-toast {
            position: fixed; bottom: 24px; left: 50%; transform: translateX(-50%) translateY(80px);
            background: rgba(20,20,38,.92); border: 1px solid var(--line);
            border-radius: 999px; padding: 9px 20px;
            font: 600 11px/1 var(--mono); letter-spacing: .12em; color: var(--ink);
            pointer-events: none; z-index: 999;
            transition: transform .25s cubic-bezier(.22,1,.36,1), opacity .25s;
            opacity: 0;
        }
        #choreo-toast.show { transform: translateX(-50%) translateY(0); opacity: 1; }
        #choreo-toast.ok   { border-color: rgba(3,218,198,.5);  color: var(--teal); }
        #choreo-toast.err  { border-color: rgba(255,107,107,.5); color: #ff8888; }

        .choreo-section-lbl {
            font: 600 9px/1 var(--mono); letter-spacing: .28em; text-transform: uppercase;
            color: var(--muted); margin-bottom: 6px; display: block;
        }

        /* ── Digital Twin SVG eye ─────────────────────────────────────────── */
        .twin-panel {
            display: flex; flex-direction: column; align-items: center; gap: 14px;
            padding: 18px 14px 14px;
        }
        .twin-label {
            font: 600 10px/1 var(--mono); letter-spacing: .38em; text-transform: uppercase;
            color: var(--teal); align-self: flex-start; padding-left: 2px;
        }
        #twin-svg {
            display: block; overflow: visible;
            /* Responsive: fill panel width, keep 340:130 ratio, never overflow on phones */
            width: 100%; max-width: 100%; height: auto; aspect-ratio: 340 / 130;
            filter: drop-shadow(0 0 22px rgba(3,218,198,.20)) drop-shadow(0 0 8px rgba(187,134,252,.16));
        }

        /* Telemetry strip */
        .telem {
            width: 100%; display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 6px 10px;
        }
        .telem-cell {
            display: flex; flex-direction: column; gap: 2px;
        }
        .telem-cell .tk {
            font: 600 8px/1 var(--mono); letter-spacing: .22em; text-transform: uppercase;
            color: var(--muted);
        }
        .telem-cell .tv {
            font: 700 12px/1 var(--mono); color: var(--teal);
        }
        .telem-cell .tv.mood-v { color: var(--violet); }
        .telem-cell .tv.blink-v { color: #f0d080; }
    </style>
</head>
<body>
    <div class="shell">
        <header>
            <p class="eyebrow">Animatronic &middot; ESP32-S3</p>
            <h1>EyeMech Control</h1>
            <span class="status-pill" id="statusPill"><span class="dot"></span><span id="statusTxt">Sleep</span></span>
        </header>

        <!-- ── Phase A: Digital Twin ───────────────────────────────────────── -->
        <div class="panel twin-panel reveal d1">
            <span class="twin-label">Digital Twin</span>

            <!-- Dual eye: viewBox 340x130; width:100% (responsive) keeps it inside the panel on phones -->
            <svg id="twin-svg" width="100%" viewBox="-170 -65 340 130" preserveAspectRatio="xMidYMid meet">
                <defs>
                    <!-- Per-eye sclera clip paths -->
                    <clipPath id="sclera-clip-l"><ellipse cx="-92" cy="0" rx="76" ry="50"/></clipPath>
                    <clipPath id="sclera-clip-r"><ellipse cx="92"  cy="0" rx="76" ry="50"/></clipPath>

                    <radialGradient id="sclera-grad" cx="48%" cy="40%" r="72%">
                        <stop offset="0%"   stop-color="#eef6f6"/>
                        <stop offset="62%"  stop-color="#c5d4d6"/>
                        <stop offset="100%" stop-color="#8b9ea1"/>
                    </radialGradient>
                    <linearGradient id="sclera-rim" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="0%"   stop-color="#03dac6" stop-opacity=".85"/>
                        <stop offset="100%" stop-color="#bb86fc" stop-opacity=".65"/>
                    </linearGradient>
                    <radialGradient id="iris-grad" cx="40%" cy="34%" r="66%">
                        <stop offset="0%"   stop-color="#3bf0e0"/>
                        <stop offset="42%"  stop-color="#03dac6"/>
                        <stop offset="78%"  stop-color="#018786"/>
                        <stop offset="100%" stop-color="#004f4d"/>
                    </radialGradient>
                    <radialGradient id="pupil-grad" cx="42%" cy="38%" r="62%">
                        <stop offset="0%"   stop-color="#1a0a2e"/>
                        <stop offset="100%" stop-color="#050509"/>
                    </radialGradient>
                    <!-- Lid body: a dark shell, clearly darker than the bright sclera -->
                    <linearGradient id="lid-upper-grad" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="0%"   stop-color="#241b38"/>
                        <stop offset="100%" stop-color="#120c20"/>
                    </linearGradient>
                    <linearGradient id="lid-lower-grad" x1="0" y1="1" x2="0" y2="0">
                        <stop offset="0%"   stop-color="#241b38"/>
                        <stop offset="100%" stop-color="#120c20"/>
                    </linearGradient>
                </defs>

                <!-- LEFT EYE (centre -92, 0) -->
                <ellipse cx="-92" cy="0" rx="76" ry="50" fill="url(#sclera-grad)"/>
                <g clip-path="url(#sclera-clip-l)">
                    <g id="gaze-l">
                        <circle cx="0" cy="0" r="26" fill="url(#iris-grad)" stroke="#03dac6" stroke-width="1.5" stroke-opacity=".5"/>
                        <circle cx="0" cy="0" r="26" fill="none" stroke="#00211f" stroke-width="2" stroke-opacity=".7"/>
                        <circle cx="0" cy="0" r="11" fill="url(#pupil-grad)"/>
                        <circle cx="-7" cy="-7" r="3.5" fill="rgba(255,255,255,.6)"/>
                    </g>
                </g>
                <g clip-path="url(#sclera-clip-l)">
                    <path id="lid-upper-l"     fill="url(#lid-upper-grad)"/>
                    <path id="lid-lower-l"     fill="url(#lid-lower-grad)"/>
                    <path id="lid-upper-rim-l" fill="none" stroke="#bb86fc" stroke-width="2.5" stroke-opacity=".8" stroke-linecap="round"/>
                    <path id="lid-lower-rim-l" fill="none" stroke="#03dac6" stroke-width="2.5" stroke-opacity=".8" stroke-linecap="round"/>
                </g>
                <ellipse cx="-92" cy="0" rx="76" ry="50" fill="none" stroke="url(#sclera-rim)" stroke-width="1.5"/>

                <!-- RIGHT EYE (centre +92, 0) -->
                <ellipse cx="92" cy="0" rx="76" ry="50" fill="url(#sclera-grad)"/>
                <g clip-path="url(#sclera-clip-r)">
                    <g id="gaze-r">
                        <circle cx="0" cy="0" r="26" fill="url(#iris-grad)" stroke="#03dac6" stroke-width="1.5" stroke-opacity=".5"/>
                        <circle cx="0" cy="0" r="26" fill="none" stroke="#00211f" stroke-width="2" stroke-opacity=".7"/>
                        <circle cx="0" cy="0" r="11" fill="url(#pupil-grad)"/>
                        <circle cx="-7" cy="-7" r="3.5" fill="rgba(255,255,255,.6)"/>
                    </g>
                </g>
                <g clip-path="url(#sclera-clip-r)">
                    <path id="lid-upper-r"     fill="url(#lid-upper-grad)"/>
                    <path id="lid-lower-r"     fill="url(#lid-lower-grad)"/>
                    <path id="lid-upper-rim-r" fill="none" stroke="#bb86fc" stroke-width="2.5" stroke-opacity=".8" stroke-linecap="round"/>
                    <path id="lid-lower-rim-r" fill="none" stroke="#03dac6" stroke-width="2.5" stroke-opacity=".8" stroke-linecap="round"/>
                </g>
                <ellipse cx="92" cy="0" rx="76" ry="50" fill="none" stroke="url(#sclera-rim)" stroke-width="1.5"/>
            </svg>

            <!-- Telemetry strip — 4 columns, per-eye aperture -->
            <div class="telem" style="grid-template-columns: repeat(4, 1fr);">
                <div class="telem-cell">
                    <span class="tk">Mode</span>
                    <span class="tv" id="tv-mode">Sleep</span>
                </div>
                <div class="telem-cell">
                    <span class="tk">Mood</span>
                    <span class="tv mood-v" id="tv-mood">Calm</span>
                </div>
                <div class="telem-cell">
                    <span class="tk">L-Apt</span>
                    <span class="tv" id="tv-apt-l">100%</span>
                </div>
                <div class="telem-cell">
                    <span class="tk">R-Apt</span>
                    <span class="tv" id="tv-apt-r">100%</span>
                </div>
                <div class="telem-cell">
                    <span class="tk">Blink</span>
                    <span class="tv blink-v" id="tv-blink">Idle</span>
                </div>
                <div class="telem-cell">
                    <span class="tk">LR</span>
                    <span class="tv" id="tv-lr">90&deg;</span>
                </div>
                <div class="telem-cell">
                    <span class="tk">UD</span>
                    <span class="tv" id="tv-ud">90&deg;</span>
                </div>
                <div class="telem-cell"></div>
            </div>
        </div>
        <!-- ── End Digital Twin ───────────────────────────────────────────── -->

        <div class="panel reveal d2">
            <div style="display:grid; grid-template-columns:repeat(3,1fr); gap:8px; margin-bottom:8px;">
                <button class="mode-btn" data-view="sleep"       data-mode="sleep"       onclick="selectView('sleep')"      >Sleep</button>
                <button class="mode-btn" data-view="alive"       data-mode="alive"       onclick="selectView('alive')"      >Alive</button>
                <button class="mode-btn" data-view="manual"      data-mode="manual"      onclick="selectView('manual')"     >Manual</button>
            </div>
            <div style="display:grid; grid-template-columns:repeat(2,1fr); gap:8px;">
                <button class="mode-btn" data-view="calibration" data-mode="calibration" onclick="selectView('calibration')">Calibrate</button>
                <button class="mode-btn" data-view="choreo"      data-mode="perform"     onclick="selectView('choreo')"     >Choreography</button>
            </div>
        </div>

        <div class="stage reveal d3" data-panel="manual">
            <div id="joystick-zone">
                <span class="tick n">UP</span><span class="tick s">DN</span>
                <span class="tick e">R</span><span class="tick w">L</span>
                <div id="joystick-knob"></div>
            </div>
        </div>

        <div class="panel reveal d5" data-panel="alive">

            <span class="choreo-section-lbl" style="margin-bottom:10px;">Mood</span>
            <div class="six-grid" style="margin-bottom:16px;">
                <button class="mode-btn mood-btn s2 active" data-mood="calm"     onclick="setMood('calm')"    >Calm</button>
                <button class="mode-btn mood-btn s2"        data-mood="alert"    onclick="setMood('alert')"   >Alert</button>
                <button class="mode-btn mood-btn s2"        data-mood="curious"  onclick="setMood('curious')" >Curious</button>
                <button class="mode-btn mood-btn s3"        data-mood="sleepy"   onclick="setMood('sleepy')"  >Sleepy</button>
                <button class="mode-btn mood-btn s3"        data-mood="skittish" onclick="setMood('skittish')">Skittish</button>
            </div>

            <span class="choreo-section-lbl" style="margin-bottom:10px;">Expressions</span>
            <div class="six-grid">
                <button class="choreo-btn-sm s2" onclick="doExpress('wink_l')"   >Wink L</button>
                <button class="choreo-btn-sm s2" onclick="doExpress('wink_r')"   >Wink R</button>
                <button class="choreo-btn-sm s2" onclick="doExpress('squint')"   >Squint</button>
                <button class="choreo-btn-sm s3" onclick="doExpress('wide')"     >Wide</button>
                <button class="choreo-btn-sm s3" onclick="doExpress('skeptical')">Skeptical</button>
            </div>

        </div>

        <div class="reveal d6" data-panel="manual">
            <button id="blinkBtn" onclick="doBlink()">Blink</button>
        </div>
        <div class="reveal d6" data-panel="manual" style="margin-top:8px;display:flex;gap:8px;">
            <button class="choreo-btn-sm" id="teach-rec-btn"  onclick="teachStart()" style="background:#c0392b;">&#9679; Record</button>
            <button class="choreo-btn-sm" id="teach-stop-btn" onclick="teachStop()"  style="display:none;">&#9646;&#9646; Stop &amp; Edit</button>
        </div>

        <div class="panel reveal d2" data-panel="calibration" style="margin-top:8px;">
            <span class="choreo-section-lbl">Calibration Wizard</span>

            <div id="wiz-active">
                <div id="wiz-progress" style="font:600 .68rem var(--mono);color:var(--teal);letter-spacing:.1em;margin:8px 0 4px;">STEP 1 / 14</div>
                <div id="wiz-title" style="font:700 1rem var(--sans);margin-bottom:6px;">Gaze — Centre (horizontal)</div>
                <div id="wiz-instr" style="font-size:.8rem;color:var(--ink);line-height:1.45;margin-bottom:14px;">Jog until both pupils point dead-centre left-to-right (looking straight ahead).</div>

                <div style="display:flex;align-items:center;justify-content:center;gap:14px;margin-bottom:12px;">
                    <button class="choreo-btn-sm" style="min-width:54px;font-size:1.2rem;" onpointerdown="wizJogStart(-1)" onpointerup="wizJogStop()" onpointerleave="wizJogStop()" onpointercancel="wizJogStop()">&minus;</button>
                    <div style="text-align:center;min-width:84px;">
                        <div style="font:700 2rem var(--mono);color:var(--teal);line-height:1;" id="wiz-angle">90</div>
                        <div style="font-size:.6rem;color:var(--muted);letter-spacing:.1em;">DEGREES</div>
                    </div>
                    <button class="choreo-btn-sm" style="min-width:54px;font-size:1.2rem;" onpointerdown="wizJogStart(1)" onpointerup="wizJogStop()" onpointerleave="wizJogStop()" onpointercancel="wizJogStop()">+</button>
                </div>

                <div style="display:flex;align-items:center;justify-content:center;gap:8px;margin-bottom:16px;">
                    <button class="choreo-btn-sm" id="wiz-step-btn" onclick="wizToggleStep()">Step: 1&deg;</button>
                </div>

                <div style="display:flex;gap:8px;">
                    <button class="choreo-btn-sm s3" id="wiz-back" onclick="wizBack()" style="flex:1;">&larr; Back</button>
                    <button class="choreo-btn-sm s3" id="wiz-next" onclick="wizNext()" style="flex:2;background:var(--teal-dk);">Capture &amp; Next &rarr;</button>
                </div>
            </div>

            <div id="wiz-done" style="display:none;text-align:center;padding:12px 0;">
                <div style="font:700 1rem var(--sans);color:var(--teal);margin-bottom:6px;">Calibration complete</div>
                <div style="font-size:.78rem;color:var(--ink);line-height:1.45;margin-bottom:14px;">Safe limits set automatically to wrap your captured positions. Save to keep them across power-offs.</div>
                <div style="display:flex;gap:8px;">
                    <button class="choreo-btn-sm s3" onclick="wizStart()" style="flex:1;">Restart</button>
                    <button class="choreo-btn-sm s3" onclick="wizSave()" style="flex:2;background:var(--teal-dk);">Save to Device</button>
                </div>
            </div>

            <div style="display:flex;gap:8px;align-items:center;margin-top:14px;border-top:1px solid var(--line);padding-top:10px;">
                <button class="choreo-btn-sm" onclick="fetchHealth()">Health Check</button>
                <span id="health-out" style="font-size:.7rem;color:var(--teal);font-family:monospace;"></span>
            </div>
        </div>

        <!-- ── Phase B: Choreography Editor ──────────────────────────────── -->
        <div class="panel reveal d7" id="choreo-panel" data-panel="choreo">
            <span class="twin-label" style="display:block;margin-bottom:14px;">Choreography</span>

            <!-- Transport -->
            <span class="choreo-section-lbl">Transport</span>
            <div class="choreo-transport" style="margin-bottom:14px;">
                <button class="btn-play choreo-btn-sm" onclick="choreoPlay(false)">&#9654; Play</button>
                <button class="btn-play choreo-btn-sm" onclick="choreoPlay(true)">&#9654; Loop</button>
                <button class="btn-stop choreo-btn-sm" onclick="choreoStop()">&#9646;&#9646; Stop</button>
            </div>

            <!-- Keyframe list -->
            <span class="choreo-section-lbl">Keyframes <span id="kf-count-lbl" style="color:var(--violet);">0 / 24</span></span>
            <div class="kf-list" id="kf-list" style="margin-bottom:10px;">
                <div class="kf-empty" id="kf-empty">No keyframes yet &mdash; add one below</div>
            </div>
            <div style="display:flex;gap:8px;margin-bottom:18px;flex-wrap:wrap;">
                <button class="choreo-btn-sm" id="btn-add-kf" onclick="kfAdd()">+ Add Keyframe</button>
                <button class="choreo-btn-sm" onclick="kfCapture(null)">&#9673; Capture Pose</button>
            </div>

            <!-- Canned sequences -->
            <span class="choreo-section-lbl">Canned Sequences</span>
            <div class="choreo-canned" style="margin-bottom:18px;">
                <select id="canned-sel" style="flex:1 1 0;">
                    <option value="0">0 &mdash; Idle Curiosity</option>
                    <option value="1">1 &mdash; Sleepy Settle</option>
                    <option value="2">2 &mdash; Startled</option>
                    <option value="3">3 &mdash; Scan Room</option>
                    <option value="4">4 &mdash; Deep Thought</option>
                    <option value="5">5 &mdash; Double Take</option>
                    <option value="6">6 &mdash; Nervous Dart</option>
                    <option value="7">7 &mdash; Drowsy Nod</option>
                    <option value="8">8 &mdash; Slow Blink</option>
                    <option value="9">9 &mdash; Side Eye</option>
                </select>
                <button class="choreo-btn-sm" onclick="cannedLoad()">Load</button>
            </div>

            <!-- localStorage save / load -->
            <span class="choreo-section-lbl">Saved Sequences (browser)</span>
            <div class="choreo-save" style="margin-bottom:8px;">
                <input type="text" id="save-name" placeholder="sequence name" maxlength="40">
                <button class="choreo-btn-sm" onclick="storeSave()">Save</button>
            </div>
            <div class="choreo-save">
                <select id="store-sel" style="flex:1 1 0;"><option value="">-- none saved --</option></select>
                <button class="choreo-btn-sm" onclick="storeLoad()">Load</button>
                <button class="choreo-btn-sm" onclick="storeDelete()" style="color:#ff8080;">Del</button>
            </div>
        </div>

        <!-- toast (lives outside .shell so it can be fixed-position) -->
        <div id="choreo-toast"></div>
    </div>

    <script>
        // ── Existing controller state ────────────────────────────────────────
        var CENTER_LR = 2960, CENTER_UD = 2970;
        var ZONE_R = 125, KNOB_R = 38, MAX_R = ZONE_R - KNOB_R;
        var BLINK_MS = 300;
        var lr = CENTER_LR, ud = CENTER_UD;
        var sendTimer = null, inflight = false;

        // Seed the controller with centre values (does not change mode)
        fetch('/update?lr=' + CENTER_LR + '&ud=' + CENTER_UD)
            .catch(function (e) { console.error(e); });

        function sendUpdate() {
            if (inflight) return;
            inflight = true;
            fetch('/update?lr=' + lr + '&ud=' + ud)
                .then(function () { inflight = false; })
                .catch(function () { inflight = false; });
        }

        // ── view selection + device-mode display ───────────────────────────
        var modeBtns = document.querySelectorAll('.mode-btn:not(.mood-btn)');
        var statusPill = document.getElementById('statusPill');
        var statusTxt  = document.getElementById('statusTxt');
        var tvModeEl   = document.getElementById('tv-mode');

        var MODE_NAMES = ['Sleep', 'Alive', 'Manual', 'Calibration', 'Choreography'];

        var currentView = 'alive';

        // Send a real device mode to the firmware. No UI side-effects — selectView owns the UI.
        function setDeviceMode(m) {
            fetch('/mode?set=' + m).catch(function (e) { console.error(e); });
        }

        // Switch the visible view: highlight the matching mode button and show ONLY that view's
        // panels (everything tagged data-panel). The 4 real modes also drive the device mode;
        // 'choreo' is view-only — the device enters PERFORM on Play, not when the editor opens.
        function selectView(v, sendMode) {
            if (sendMode === undefined) sendMode = true;
            currentView = v;
            modeBtns.forEach(function (b) { b.classList.toggle('active', b.dataset.view === v); });
            document.querySelectorAll('[data-panel]').forEach(function (el) {
                el.style.display = (el.dataset.panel === v) ? '' : 'none';
            });
            if (v === 'calibration') wizStart();
            if (sendMode && v !== 'choreo') {
                var btn = document.querySelector('.mode-btn[data-view="' + v + '"]');
                if (btn && btn.dataset.mode) setDeviceMode(btn.dataset.mode);
            }
        }

        // Reflect the TRUE device mode from /state in the status pill + telemetry (display only;
        // never touches the view highlight, so the Choreography editor can stay open while the
        // device is Alive/Perform underneath).
        function applyDeviceMode(idx) {
            var name = MODE_NAMES[idx] || String(idx);
            statusTxt.textContent = name;
            statusPill.classList.toggle('live', idx !== 0);   // 0 = Sleep
            tvModeEl.textContent = name;
        }

        applyDeviceMode(0);            // firmware boot default = Sleep
        selectView('sleep', false);    // land on the Sleep view + highlight, matching the asleep boot state

        // ── mood selection ─────────────────────────────────────────────────
        var moodBtns = document.querySelectorAll('.mood-btn');
        var MOOD_NAMES = ['Calm', 'Alert', 'Curious', 'Sleepy', 'Skittish'];
        var MOOD_KEYS  = ['calm', 'alert', 'curious', 'sleepy', 'skittish'];
        function setMood(x) {
            fetch('/mood?set=' + x).catch(function (e) { console.error(e); });
            moodBtns.forEach(function (b) { b.classList.toggle('active', b.dataset.mood === x); });
        }

        // ── momentary blink: one-shot, greys while playing, queues one ─────
        var blinkBtn = document.getElementById('blinkBtn');
        var blinkBusy = false, blinkQueued = false;
        function doBlink() {
            fetch('/blink').catch(function (e) { console.error(e); });
            if (blinkBusy) { blinkQueued = true; blinkBtn.classList.add('queued'); return; }
            startBlinkVisual();
        }
        function doExpress(name) {
            fetch('/express?name=' + name).catch(function (e) { console.error(e); });
        }

        // ── F5: Teach mode — record live pose stream → choreography editor ─────
        var teaching = false, teachInterval = null, teachRows = [];

        function teachStart() {
            if (teaching) return;
            teaching = true;
            teachRows = [];
            document.getElementById('teach-rec-btn').style.display  = 'none';
            document.getElementById('teach-stop-btn').style.display = '';
            var startedAt = Date.now();
            teachInterval = setInterval(function () {
                // Auto-stop at the firmware keyframe cap (or if Record was left running),
                // so we never record more frames than /seq can actually play.
                if (teachRows.length >= KF_MAX || Date.now() - startedAt > KF_MAX * 150 + 500) {
                    teachStop();
                    return;
                }
                fetch('/state')
                    .then(function (r) { return r.json(); })
                    .then(function (s) {
                        teachRows.push({
                            lr: s.a[0], ud: s.a[1],
                            ap: s.ap,
                            blink: 0, easeMs: 150, holdMs: 0, ease: 2
                        });
                    })
                    .catch(function () {});
            }, 150);
        }

        function teachStop() {
            if (!teaching) return;
            teaching = false;
            clearInterval(teachInterval);
            teachInterval = null;
            document.getElementById('teach-rec-btn').style.display  = '';
            document.getElementById('teach-stop-btn').style.display = 'none';
            if (teachRows.length === 0) { showToast('Nothing recorded', 'err'); return; }
            if (teachRows.length > KF_MAX) {
                showToast('Recording exceeded ' + KF_MAX + ' frames — keeping first ' + KF_MAX, 'err');
                teachRows = teachRows.slice(0, KF_MAX);
            }
            kfRows = teachRows.slice();
            renderKfList();
            selectView('choreo', false);
            showToast('Loaded ' + kfRows.length + ' frames — edit & play', '');
        }

        function fetchHealth() {
            fetch('/health')
                .then(function (r) { return r.json(); })
                .then(function (h) {
                    document.getElementById('health-out').textContent =
                        'up ' + h.up + 's  heap ' + h.heap + 'B  fps ' + h.fps;
                })
                .catch(function () {
                    document.getElementById('health-out').textContent = 'error';
                });
        }

        // ── Calibration wizard ───────────────────────────────────────────────
        // Each step: ch (0=LR 1=UD 2=TL 3=BL 4=TR 5=BR), slot (enda|endb|center),
        // title, instr. Jog runs full 0-180 (edge=1); SAFE limits auto-set on finish.
        var WIZ = [
            { ch:2, slot:'enda',   title:'Top-Left eyelid — Closed',    instr:'Jog until the TOP-LEFT eyelid is fully shut — meeting the lower lid, no strain.' },
            { ch:2, slot:'endb',   title:'Top-Left eyelid — Open',      instr:'Jog until the TOP-LEFT eyelid is fully open (eye wide).' },
            { ch:3, slot:'enda',   title:'Bottom-Left eyelid — Closed', instr:'Jog until the BOTTOM-LEFT eyelid is fully shut — meeting the upper lid, no strain.' },
            { ch:3, slot:'endb',   title:'Bottom-Left eyelid — Open',   instr:'Jog until the BOTTOM-LEFT eyelid is fully open.' },
            { ch:4, slot:'enda',   title:'Top-Right eyelid — Closed',   instr:'Jog until the TOP-RIGHT eyelid is fully shut — meeting the lower lid, no strain.' },
            { ch:4, slot:'endb',   title:'Top-Right eyelid — Open',     instr:'Jog until the TOP-RIGHT eyelid is fully open (eye wide).' },
            { ch:5, slot:'enda',   title:'Bottom-Right eyelid — Closed',instr:'Jog until the BOTTOM-RIGHT eyelid is fully shut — meeting the upper lid, no strain.' },
            { ch:5, slot:'endb',   title:'Bottom-Right eyelid — Open',  instr:'Jog until the BOTTOM-RIGHT eyelid is fully open.' },
            { ch:0, slot:'center', title:'Gaze — Centre (horizontal)',   instr:'Jog until both pupils point dead-centre left-to-right (looking straight ahead).' },
            { ch:0, slot:'enda',   title:'Gaze — Look Left',             instr:'Jog until the eyes look fully LEFT, just before the servo strains.' },
            { ch:0, slot:'endb',   title:'Gaze — Look Right',            instr:'Jog until the eyes look fully RIGHT, just before the servo strains.' },
            { ch:1, slot:'center', title:'Gaze — Centre (vertical)',     instr:'Jog until the pupils sit level — looking straight ahead, not up or down.' },
            { ch:1, slot:'enda',   title:'Gaze — Look Down',             instr:'Jog until the eyes look fully DOWN, just before the servo strains.' },
            { ch:1, slot:'endb',   title:'Gaze — Look Up',               instr:'Jog until the eyes look fully UP, just before the servo strains.' }
        ];
        var wizIdx = 0, wizStep = 1, wizJogTimer = null, wizCal = null;

        function wizPark() {
            fetch('/cal').then(function (r) { return r.json(); })
                .then(function (d) {
                    wizCal = d.cal;
                    for (var ch = 0; ch < 6; ch++) {
                        if (ch === WIZ[wizIdx].ch) continue;
                        if (!wizCal || !wizCal[ch]) continue;
                        var neutral = (ch === 0 || ch === 1) ? wizCal[ch][4] : wizCal[ch][3];
                        fetch('/cal/go?ch=' + ch + '&a=' + neutral).catch(function () {});
                    }
                })
                .catch(function () {});
        }

        function wizShowAngle() {
            fetch('/cal').then(function (r) { return r.json(); })
                .then(function (d) { document.getElementById('wiz-angle').textContent = d.ang[WIZ[wizIdx].ch]; })
                .catch(function () {});
        }

        function wizRender() {
            var s = WIZ[wizIdx];
            document.getElementById('wiz-done').style.display = 'none';
            document.getElementById('wiz-active').style.display = '';
            document.getElementById('wiz-progress').textContent = 'STEP ' + (wizIdx + 1) + ' / ' + WIZ.length;
            document.getElementById('wiz-title').textContent = s.title;
            document.getElementById('wiz-instr').textContent = s.instr;
            document.getElementById('wiz-back').disabled = (wizIdx === 0);
            document.getElementById('wiz-next').innerHTML = (wizIdx === WIZ.length - 1) ? 'Capture &amp; Finish &rarr;' : 'Capture &amp; Next &rarr;';
            wizShowAngle();
            wizPark();
        }

        function wizStart() { wizIdx = 0; wizRender(); }

        function wizToggleStep() {
            wizStep = (wizStep === 1) ? 5 : 1;
            document.getElementById('wiz-step-btn').innerHTML = 'Step: ' + wizStep + '°';
        }

        function wizJogOnce(dir) {
            fetch('/cal/jog?ch=' + WIZ[wizIdx].ch + '&d=' + (dir * wizStep) + '&edge=1')
                .then(function (r) { if (!r.ok) throw new Error('cal mode?'); return r.text(); })
                .then(function (a) { document.getElementById('wiz-angle').textContent = a; })
                .catch(function () { showToast('Open the Calibrate view first', 'err'); });
        }
        function wizJogStart(dir) { wizJogOnce(dir); wizJogTimer = setInterval(function () { wizJogOnce(dir); }, 200); }
        function wizJogStop() { if (wizJogTimer) { clearInterval(wizJogTimer); wizJogTimer = null; } }

        function wizBack() { if (wizIdx > 0) { wizIdx--; wizRender(); } }

        function wizNext() {
            var s = WIZ[wizIdx];
            fetch('/cal/set?ch=' + s.ch + '&slot=' + s.slot, { method: 'POST' })
                .then(function (r) { if (!r.ok) throw new Error('capture failed'); })
                .then(function () {
                    if (wizIdx < WIZ.length - 1) { wizIdx++; wizRender(); }
                    else { wizFinish(); }
                })
                .catch(function (e) { showToast(e.message, 'err'); });
        }

        function wizFinish() {
            // Auto-set SAFE limits to wrap captured points, then show the save screen.
            fetch('/cal/autosafe', { method: 'POST' }).finally(function () {
                document.getElementById('wiz-active').style.display = 'none';
                document.getElementById('wiz-done').style.display = '';
            });
        }

        function wizSave() {
            fetch('/cal/save', { method: 'POST' })
                .then(function () { showToast('Calibration saved to device', ''); loadTwinCal(); })
                .catch(function (e) { showToast('Save error: ' + e.message, 'err'); });
        }

        function startBlinkVisual() {
            blinkBusy = true;
            blinkBtn.classList.add('busy');
            setTimeout(endBlinkVisual, BLINK_MS);
        }
        function endBlinkVisual() {
            blinkBusy = false;
            blinkBtn.classList.remove('busy', 'queued');
            if (blinkQueued) { blinkQueued = false; startBlinkVisual(); }
        }

        // ── virtual joystick ──────────────────────────────────────────────
        var zone = document.getElementById('joystick-zone');
        var knob = document.getElementById('joystick-knob');

        function moveTo(cx, cy) {
            var rect = zone.getBoundingClientRect();
            var dx = cx - (rect.left + ZONE_R);
            var dy = cy - (rect.top  + ZONE_R);
            var dist = Math.sqrt(dx * dx + dy * dy);
            if (dist > MAX_R) { dx = dx * MAX_R / dist; dy = dy * MAX_R / dist; }
            knob.style.left = (ZONE_R + dx) + 'px';
            knob.style.top  = (ZONE_R + dy) + 'px';
            knob.style.transform = 'translate(-50%,-50%)';
            lr = dx >= 0 ? Math.round(CENTER_LR + (dx / MAX_R) * (4095 - CENTER_LR))
                         : Math.round(CENTER_LR + (dx / MAX_R) * CENTER_LR);
            ud = dy <= 0 ? Math.round(CENTER_UD + (-dy / MAX_R) * (4095 - CENTER_UD))
                         : Math.round(CENTER_UD + (-dy / MAX_R) * CENTER_UD);
        }
        function joystickRelease() {
            if (sendTimer === null) return;
            clearInterval(sendTimer); sendTimer = null;
            zone.classList.remove('dragging');
            knob.style.left = '50%'; knob.style.top = '50%';
            lr = CENTER_LR; ud = CENTER_UD; sendUpdate();
        }
        zone.addEventListener('pointerdown', function (e) {
            e.preventDefault(); zone.setPointerCapture(e.pointerId);
            zone.classList.add('dragging');
            moveTo(e.clientX, e.clientY); sendUpdate();
            if (sendTimer === null) sendTimer = setInterval(sendUpdate, 50);
        });
        zone.addEventListener('pointermove', function (e) {
            if (sendTimer === null) return; moveTo(e.clientX, e.clientY);
        });
        zone.addEventListener('pointerup',     joystickRelease);
        zone.addEventListener('pointercancel', joystickRelease);

        // ================================================================
        // ── Phase A: Digital Twin ─────────────────────────────────────
        // ================================================================

        // SVG geometry constants — dual eye
        var EYE_L = -92, EYE_R = 92;       // eye centre x positions
        var SCL_RX = 76;                    // sclera half-width
        var SVG_TOP = -65, SVG_BOT = 65;   // SVG vertical extents
        var LX_FILL = -170, RX_FILL = 170; // wide lid fill bounds (clip does the rest)
        var MAXOFF_X = 40;
        var MAXOFF_Y = 24;

        var UP_OPEN_MID = -38, UP_CLOSE_MID = 5;
        var LO_OPEN_MID =  33, LO_CLOSE_MID = 5;
        function upperMidY(f) { return UP_CLOSE_MID + (UP_OPEN_MID - UP_CLOSE_MID) * f; }
        function lowerMidY(f) { return LO_CLOSE_MID + (LO_OPEN_MID - LO_CLOSE_MID) * f; }

        // Lid limits (firmware defaults): closed angle, open angle per servo channel
        // a[2]=TL, a[3]=BL, a[4]=TR, a[5]=BR
        var LID_CFG = {
            TL: { closed: 90, open: 10  },
            BL: { closed: 10, open: 90  },
            TR: { closed: 10, open: 90  },
            BR: { closed: 90, open: 10  }
        };
        function openFrac(cur, cfg) {
            var f = (cur - cfg.closed) / (cfg.open - cfg.closed);
            return f < 0 ? 0 : f > 1 ? 1 : f;
        }

        // Twin state: targets (updated by poller) and rendered (lerped toward targets each rAF)
        var tgt = { irisX: 0, irisY: 0, fTL: 1, fBL: 1, fTR: 1, fBR: 1 };
        var cur = { irisX: 0, irisY: 0, fTL: 1, fBL: 1, fTR: 1, fBR: 1 };

        // SVG element refs — dual eye
        var gazeL    = document.getElementById('gaze-l');
        var lidUL    = document.getElementById('lid-upper-l');
        var lidULRim = document.getElementById('lid-upper-rim-l');
        var lidLL    = document.getElementById('lid-lower-l');
        var lidLLRim = document.getElementById('lid-lower-rim-l');
        var gazeR    = document.getElementById('gaze-r');
        var lidUR    = document.getElementById('lid-upper-r');
        var lidURRim = document.getElementById('lid-upper-rim-r');
        var lidLR    = document.getElementById('lid-lower-r');
        var lidLRRim = document.getElementById('lid-lower-rim-r');

        // Telemetry element refs
        var tvMode  = document.getElementById('tv-mode');
        var tvMood  = document.getElementById('tv-mood');
        var tvBlink = document.getElementById('tv-blink');
        var tvLR    = document.getElementById('tv-lr');
        var tvUD    = document.getElementById('tv-ud');
        var tvApL   = document.getElementById('tv-apt-l');
        var tvApR   = document.getElementById('tv-apt-r');

        var BLINK_PHASE = ['Idle', 'Closing', 'Hold', 'Opening', 'Gap'];

        // Draw one eye centred at cx, with independent top/bottom lid fractions.
        // Lid fill paths cover the full SVG width; the per-eye clipPath confines them.
        function applyEye(cx, gazeGrp, liU, liURim, liL, liLRim, fTop, fBot) {
            var cantL = cx - SCL_RX;   // inner canthus x
            var cantR = cx + SCL_RX;   // outer canthus x
            gazeGrp.setAttribute('transform',
                'translate(' + (cx + cur.irisX).toFixed(1) + ',' + cur.irisY.toFixed(1) + ')');
            var upC = (2 * upperMidY(fTop)).toFixed(1);
            var loC = (2 * lowerMidY(fBot)).toFixed(1);
            liU.setAttribute('d',
                'M ' + LX_FILL + ' ' + SVG_TOP +
                ' L ' + RX_FILL + ' ' + SVG_TOP +
                ' L ' + RX_FILL + ' 0' +
                ' L ' + cantR   + ' 0' +
                ' Q ' + cx      + ' ' + upC + ' ' + cantL + ' 0' +
                ' L ' + LX_FILL + ' 0 Z');
            liL.setAttribute('d',
                'M ' + LX_FILL + ' ' + SVG_BOT +
                ' L ' + RX_FILL + ' ' + SVG_BOT +
                ' L ' + RX_FILL + ' 0' +
                ' L ' + cantR   + ' 0' +
                ' Q ' + cx      + ' ' + loC + ' ' + cantL + ' 0' +
                ' L ' + LX_FILL + ' 0 Z');
            liURim.setAttribute('d', 'M ' + cantL + ' 0 Q ' + cx + ' ' + upC + ' ' + cantR + ' 0');
            liLRim.setAttribute('d', 'M ' + cantL + ' 0 Q ' + cx + ' ' + loC + ' ' + cantR + ' 0');
        }

        // Apply lerped state to SVG DOM each frame — both eyes independently
        function applyTwin() {
            applyEye(EYE_L, gazeL, lidUL, lidULRim, lidLL, lidLLRim, cur.fTL, cur.fBL);
            applyEye(EYE_R, gazeR, lidUR, lidURRim, lidLR, lidLRRim, cur.fTR, cur.fBR);
        }

        // rAF loop: lerp cur toward tgt, then paint
        var LERP = 0.3;
        function rafLoop() {
            cur.irisX += (tgt.irisX - cur.irisX) * LERP;
            cur.irisY += (tgt.irisY - cur.irisY) * LERP;
            cur.fTL   += (tgt.fTL   - cur.fTL)   * LERP;
            cur.fBL   += (tgt.fBL   - cur.fBL)   * LERP;
            cur.fTR   += (tgt.fTR   - cur.fTR)   * LERP;
            cur.fBR   += (tgt.fBR   - cur.fBR)   * LERP;
            applyTwin();
            requestAnimationFrame(rafLoop);
        }
        requestAnimationFrame(rafLoop);

        // ── F8: WebSocket telemetry — replaces 20 Hz /state poll ─────────────
        var ws = null;

        function wsConnect() {
            ws = new WebSocket('ws://' + location.hostname + ':81/');
            ws.onopen    = function () { console.log('WS connected'); };
            ws.onclose   = function () {
                console.log('WS closed — retrying in 2 s');
                setTimeout(wsConnect, 2000);
            };
            ws.onerror   = function () { ws.close(); };
            ws.onmessage = function (ev) {
                try { applyState(JSON.parse(ev.data)); } catch (e) {}
            };
        }

        function applyState(s) {
            // Update targets for twin (same logic as former pollState)
            var lrA = s.a[0];  // LR servo angle
            var udA = s.a[1];  // UD servo angle
            if (TWIN_CAL) {
                tgt.irisX = calFrac(lrA, TWIN_CAL[0][2], TWIN_CAL[0][4], TWIN_CAL[0][3]) * MAXOFF_X;
                tgt.irisY = -calFrac(udA, TWIN_CAL[1][2], TWIN_CAL[1][4], TWIN_CAL[1][3]) * MAXOFF_Y;
            } else {
                tgt.irisX = (lrA - 90) / 50 * MAXOFF_X;
                tgt.irisY = -(udA - 90) / 50 * MAXOFF_Y;
            }

            if (TWIN_CAL) {
                tgt.fTL = openFrac(s.a[2], { closed: TWIN_CAL[2][2], open: TWIN_CAL[2][3] });
                tgt.fBL = openFrac(s.a[3], { closed: TWIN_CAL[3][2], open: TWIN_CAL[3][3] });
                tgt.fTR = openFrac(s.a[4], { closed: TWIN_CAL[4][2], open: TWIN_CAL[4][3] });
                tgt.fBR = openFrac(s.a[5], { closed: TWIN_CAL[5][2], open: TWIN_CAL[5][3] });
            } else {
                tgt.fTL = openFrac(s.a[2], LID_CFG.TL);
                tgt.fBL = openFrac(s.a[3], LID_CFG.BL);
                tgt.fTR = openFrac(s.a[4], LID_CFG.TR);
                tgt.fBR = openFrac(s.a[5], LID_CFG.BR);
            }

            // Update telemetry strip. While a choreography plays (ambient or manual
            // canned), show "Choreo #N" in the mood slot instead of the mood name.
            tvMood.textContent  = (s.perf >= 0) ? ('Choreo #' + s.perf)
                                                : (MOOD_NAMES[s.mood] || String(s.mood));
            tvBlink.textContent = BLINK_PHASE[s.bp]  || String(s.bp);
            tvLR.textContent    = lrA + '°';
            tvUD.textContent    = udA + '°';
            // Per-eye aperture (computed from already-resolved lid fractions)
            tvApL.textContent = Math.round((tgt.fTL + tgt.fBL) * 50) + '%';
            tvApR.textContent = Math.round((tgt.fTR + tgt.fBR) * 50) + '%';

            // Reflect the true device mode (pill + tv-mode). View highlight is owned by
            // selectView, so the Choreography editor stays open while the device runs.
            applyDeviceMode(s.m);

            // Sync mood-button highlight to device's actual mood (tracks autonomous drift R3)
            var moodKey = MOOD_KEYS[s.mood];
            if (moodKey) moodBtns.forEach(function (b) {
                b.classList.toggle('active', b.dataset.mood === moodKey);
            });

            // Blink visual state
            if (s.bp === 1 || s.bp === 2) startBlinkVisual();
            else endBlinkVisual();
        }

        // ── Calibration-aware twin rendering ────────────────────────────
        // TWIN_CAL mirrors the firmware cal array: cal[ch] = [safeMin,safeMax,endA,endB,center]
        // endA=closed (lids) / left/down (gaze); endB=open (lids) / right/up (gaze); center=gaze centre
        var TWIN_CAL = null;

        function calFrac(v, eA, c, eB) {
            var frac, denom;
            if (v >= c) {
                denom = eB - c;
                frac = denom !== 0 ? (v - c) / denom : 0;
            } else {
                denom = c - eA;
                frac = denom !== 0 ? (v - c) / denom : 0;
            }
            return frac < -1 ? -1 : frac > 1 ? 1 : frac;
        }

        function loadTwinCal() {
            fetch('/cal').then(function (r) { return r.json(); })
                .then(function (d) { TWIN_CAL = d.cal; })
                .catch(function () {});
        }

        wsConnect();
        loadTwinCal();

        // ================================================================
        // ── Phase B: Choreography Engine ──────────────────────────────
        // ================================================================

        var KF_MAX = 24;

        // Each row object: { lr, ud, ap, blink, easeMs, holdMs, ease }
        // lr/ud: number 40-140, or the string "HOLD"
        // blink: 0-5, ap: 0-100, easeMs/holdMs: >=0, ease: 0/1/2
        var kfRows = [];

        var BLINK_OPTS = ['None','Normal','Quick','Double','Slow','Flutter'];
        var EASE_OPTS  = ['Out','InOut','Linear'];

        // ── Canned sequence definitions (mirrors firmware spirit) ─────────
        // Format per frame: [lr, ud, ap, blink, easeMs, holdMs, ease]
        // "HOLD" represented as -1 here (converted to string "HOLD" in editor)
        // Editor mirror of the firmware's canned sequences — kept byte-for-byte in
        // sync with the CANNED_* arrays in EyeMech.ino (field order: lr,ud,ap,blink,
        // easeMs,holdMs,ease; -1 = HOLD). Must cover every index the dropdown offers
        // (0-9) or cannedLoad() can't mirror it into the editor.
        var CANNED_SEQ = [
            // 0: Idle Curiosity
            [[90,90,100,0,600,1200,1],[100,85,100,1,500,800,0],[80,90,100,0,700,600,1],[90,95,100,0,800,1000,1],[105,88,100,1,600,700,0],[90,90,100,0,900,1400,1],[78,92,100,0,500,500,0],[90,90,100,1,700,1500,1]],
            // 1: Sleepy Settle
            [[90,90,100,0,1200,800,1],[90,95,85,4,1500,1000,1],[88,100,60,0,2000,1200,1],[90,105,40,4,2500,1500,1],[90,110,20,0,2000,2000,1],[90,112,15,4,1500,3000,1]],
            // 2: Startled
            [[90,90,100,2,80,200,0],[120,75,100,0,120,300,0],[60,80,100,0,150,250,0],[90,85,100,0,180,400,0],[90,90,100,2,300,600,1],[90,90,100,0,500,800,1]],
            // 3: Scan Room
            [[55,90,100,0,800,600,2],[70,90,100,1,700,400,2],[90,90,100,0,700,500,2],[110,90,100,0,700,400,2],[125,90,100,1,800,600,2],[110,90,100,0,600,300,2],[90,90,100,0,700,500,2],[70,90,100,0,700,400,2],[55,90,100,1,800,700,2],[90,90,100,0,900,800,1]],
            // 4: Deep Thought
            [[90,90,100,0,600,400,1],[72,78,85,0,900,1500,1],[65,75,80,0,400,900,1],[73,78,80,0,350,700,0],[65,76,80,0,280,600,0],[90,90,100,1,900,700,1]],
            // 5: Double Take
            [[112,90,100,0,700,900,1],[90,90,95,1,180,150,0],[116,82,100,3,90,200,0],[108,87,100,0,400,700,1],[90,90,100,1,600,600,1]],
            // 6: Nervous Dart
            [[112,82,80,0,140,180,0],[72,96,78,0,120,200,0],[116,78,78,0,130,160,0],[74,88,78,5,110,200,0],[102,80,78,0,120,200,0],[90,90,100,1,600,700,1]],
            // 7: Drowsy Nod
            [[90,90,100,0,1000,600,1],[90,98,75,4,1600,1000,1],[88,108,45,0,2200,1200,1],[90,115,20,0,1800,1800,1],[90,78,100,3,90,400,0],[90,90,100,1,500,700,1]],
            // 8: Slow Blink
            [[-1,-1,100,0,400,600,1],[-1,-1,55,0,1000,400,1],[-1,-1,15,0,800,500,1],[-1,-1,5,0,500,800,1],[-1,-1,100,0,1400,700,1]],
            // 9: Side Eye
            [[90,90,100,0,500,400,1],[108,90,75,0,900,500,1],[118,92,60,0,1100,2200,1],[116,90,58,0,300,600,0],[90,90,100,1,800,700,1]]
        ];

        // ── Toast helper ──────────────────────────────────────────────────
        var toastEl = document.getElementById('choreo-toast');
        var toastTimer = null;
        function showToast(msg, type) {
            // type: 'ok' | 'err'
            clearTimeout(toastTimer);
            toastEl.textContent = msg;
            toastEl.className = 'show ' + (type || 'ok');
            toastTimer = setTimeout(function () { toastEl.className = ''; }, 2800);
        }

        // ── Render helpers ────────────────────────────────────────────────
        function kfCountLbl() {
            document.getElementById('kf-count-lbl').textContent = kfRows.length + ' / ' + KF_MAX;
            var addBtn = document.getElementById('btn-add-kf');
            if (addBtn) addBtn.disabled = kfRows.length >= KF_MAX;
        }

        function renderKfList() {
            var list = document.getElementById('kf-list');
            var empty = document.getElementById('kf-empty');
            // Remove only the rendered .kf-row nodes — NOT via innerHTML='' which would
            // also delete the #kf-empty placeholder (it lives inside #kf-list), so the
            // next render re-fetched it as null and threw "cannot read properties of null".
            var rows = list.querySelectorAll('.kf-row');
            for (var r = 0; r < rows.length; r++) list.removeChild(rows[r]);
            if (kfRows.length === 0) {
                empty.style.display = '';
                kfCountLbl();
                return;
            }
            empty.style.display = 'none';
            kfRows.forEach(function (row, i) {
                list.appendChild(buildKfEl(row, i));
            });
            kfCountLbl();
        }

        function buildKfEl(row, i) {
            var div = document.createElement('div');
            div.className = 'kf-row';
            div.dataset.idx = i;

            // ─ top bar: index, reorder arrows, delete
            var top = document.createElement('div');
            top.className = 'kf-row-top';

            var idx = document.createElement('span');
            idx.className = 'kf-idx';
            idx.textContent = '#' + (i + 1);
            top.appendChild(idx);

            var reorder = document.createElement('div');
            reorder.className = 'kf-reorder';
            var upA = document.createElement('button');
            upA.className = 'kf-arrow'; upA.textContent = '▲'; upA.title = 'Move up';
            upA.disabled = (i === 0);
            upA.onclick = function () { kfMoveUp(i); };
            var dnA = document.createElement('button');
            dnA.className = 'kf-arrow'; dnA.textContent = '▼'; dnA.title = 'Move down';
            dnA.disabled = (i === kfRows.length - 1);
            dnA.onclick = function () { kfMoveDown(i); };
            reorder.appendChild(upA); reorder.appendChild(dnA);
            top.appendChild(reorder);

            var del = document.createElement('button');
            del.className = 'kf-del'; del.textContent = 'Del';
            del.onclick = function () { kfDelete(i); };
            top.appendChild(del);
            div.appendChild(top);

            // ─ field grid
            var fields = document.createElement('div');
            fields.className = 'kf-fields';

            fields.appendChild(makeField('LR', 'lr', row, i,
                { type: 'text', placeholder: '40-140 / HOLD', pattern: '^(HOLD|[4-9][0-9]|1[0-3][0-9]|140)$' }));
            fields.appendChild(makeField('UD', 'ud', row, i,
                { type: 'text', placeholder: '40-140 / HOLD', pattern: '^(HOLD|[4-9][0-9]|1[0-3][0-9]|140)$' }));
            fields.appendChild(makeField('Aperture%', 'ap', row, i,
                { type: 'number', min: 0, max: 100 }));

            fields.appendChild(makeSelectField('Blink', 'blink', row, i, BLINK_OPTS));
            fields.appendChild(makeField('Ease ms', 'easeMs', row, i,
                { type: 'number', min: 0, max: 9999 }));
            fields.appendChild(makeField('Hold ms', 'holdMs', row, i,
                { type: 'number', min: 0, max: 9999 }));

            fields.appendChild(makeSelectField('Ease', 'ease', row, i, EASE_OPTS));

            div.appendChild(fields);
            return div;
        }

        function makeField(labelTxt, key, row, rowIdx, attrs) {
            var wrap = document.createElement('div');
            wrap.className = 'kf-field';
            var lbl = document.createElement('label');
            lbl.textContent = labelTxt;
            wrap.appendChild(lbl);
            var inp = document.createElement('input');
            inp.type = attrs.type || 'text';
            if (attrs.placeholder) inp.placeholder = attrs.placeholder;
            if (attrs.min !== undefined) inp.min = attrs.min;
            if (attrs.max !== undefined) inp.max = attrs.max;
            if (attrs.pattern) inp.pattern = attrs.pattern;
            var val = row[key];
            inp.value = (val === -1 || val === '-1') ? 'HOLD' : val;
            inp.addEventListener('change', function () { kfFieldChange(rowIdx, key, this.value); });
            wrap.appendChild(inp);
            return wrap;
        }

        function makeSelectField(labelTxt, key, row, rowIdx, opts) {
            var wrap = document.createElement('div');
            wrap.className = 'kf-field';
            var lbl = document.createElement('label');
            lbl.textContent = labelTxt;
            wrap.appendChild(lbl);
            var sel = document.createElement('select');
            opts.forEach(function (o, oi) {
                var opt = document.createElement('option');
                opt.value = oi; opt.textContent = o;
                if (oi === row[key]) opt.selected = true;
                sel.appendChild(opt);
            });
            sel.addEventListener('change', function () { kfFieldChange(rowIdx, key, parseInt(this.value)); });
            wrap.appendChild(sel);
            return wrap;
        }

        // ── Row data mutation ─────────────────────────────────────────────
        function defaultRow() {
            return { lr: 'HOLD', ud: 'HOLD', ap: 100, blink: 0, easeMs: 300, holdMs: 500, ease: 0 };
        }

        function kfFieldChange(i, key, rawVal) {
            var row = kfRows[i];
            if (!row) return;
            if (key === 'lr' || key === 'ud') {
                var s = String(rawVal).trim().toUpperCase();
                if (s === 'HOLD' || s === '') { row[key] = 'HOLD'; }
                else {
                    var n = parseInt(s);
                    row[key] = isNaN(n) ? 'HOLD' : Math.max(40, Math.min(140, n));
                }
            } else if (key === 'ap') {
                row[key] = Math.max(0, Math.min(100, parseInt(rawVal) || 0));
            } else if (key === 'easeMs' || key === 'holdMs') {
                row[key] = Math.max(0, parseInt(rawVal) || 0);
            } else {
                row[key] = parseInt(rawVal) || 0;
            }
        }

        function kfAdd() {
            if (kfRows.length >= KF_MAX) return;
            kfRows.push(defaultRow());
            renderKfList();
        }

        function kfDelete(i) {
            kfRows.splice(i, 1);
            renderKfList();
        }

        function kfMoveUp(i) {
            if (i === 0) return;
            var tmp = kfRows[i]; kfRows[i] = kfRows[i-1]; kfRows[i-1] = tmp;
            renderKfList();
        }

        function kfMoveDown(i) {
            if (i >= kfRows.length - 1) return;
            var tmp = kfRows[i]; kfRows[i] = kfRows[i+1]; kfRows[i+1] = tmp;
            renderKfList();
        }

        // Capture pose: read /state, fill a new row (or target row) with current pose
        function kfCapture(targetIdx) {
            fetch('/state')
                .then(function (r) { return r.json(); })
                .then(function (s) {
                    var lrVal = Math.round(s.a[0]);
                    var udVal = Math.round(s.a[1]);
                    var apVal = Math.round(s.ap);
                    if (targetIdx !== null && kfRows[targetIdx]) {
                        kfRows[targetIdx].lr = lrVal;
                        kfRows[targetIdx].ud = udVal;
                        kfRows[targetIdx].ap = apVal;
                        renderKfList();
                        showToast('Pose captured into #' + (targetIdx + 1), 'ok');
                    } else {
                        if (kfRows.length >= KF_MAX) { showToast('Max ' + KF_MAX + ' keyframes', 'err'); return; }
                        var row = defaultRow();
                        row.lr = lrVal; row.ud = udVal; row.ap = apVal;
                        kfRows.push(row);
                        renderKfList();
                        showToast('Pose captured as #' + kfRows.length, 'ok');
                    }
                })
                .catch(function () { showToast('Capture failed', 'err'); });
        }

        // ── Serializer ────────────────────────────────────────────────────
        function serializeSeq() {
            if (kfRows.length === 0) return null;
            return kfRows.map(function (row) {
                var lr  = (row.lr  === 'HOLD' || row.lr  === -1) ? -1 : Math.max(40, Math.min(140, parseInt(row.lr)  || 90));
                var ud  = (row.ud  === 'HOLD' || row.ud  === -1) ? -1 : Math.max(40, Math.min(140, parseInt(row.ud)  || 90));
                var ap  = Math.max(0, Math.min(100, parseInt(row.ap)     || 0));
                var bl  = Math.max(0, Math.min(5,   parseInt(row.blink)  || 0));
                var ems = Math.max(0, parseInt(row.easeMs) || 0);
                var hms = Math.max(0, parseInt(row.holdMs) || 0);
                var ea  = Math.max(0, Math.min(2,   parseInt(row.ease)   || 0));
                return [lr, ud, ap, bl, ems, hms, ea].join(',');
            }).join(';');
        }

        // ── Transport ─────────────────────────────────────────────────────
        function choreoPlay(loop) {
            if (kfRows.length === 0) { showToast('No keyframes to play', 'err'); return; }
            var body = serializeSeq();
            fetch('/seq', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: body })
                .then(function (r) {
                    if (!r.ok) throw new Error('seq ' + r.status);
                    return fetch('/play' + (loop ? '?loop=1' : ''));
                })
                .then(function (r) {
                    if (!r.ok) throw new Error('play ' + r.status);
                    showToast(loop ? 'Looping sequence' : 'Playing sequence', 'ok');
                })
                .catch(function (e) { showToast('Play error: ' + e.message, 'err'); });
        }

        function choreoStop() {
            fetch('/stop')
                .then(function (r) {
                    if (!r.ok) throw new Error('stop ' + r.status);
                    showToast('Stopped', 'ok');
                })
                .catch(function (e) { showToast('Stop error: ' + e.message, 'err'); });
        }

        // ── Canned sequences ──────────────────────────────────────────────
        function cannedLoad() {
            var n = parseInt(document.getElementById('canned-sel').value);
            fetch('/canned?i=' + n)
                .then(function (r) {
                    if (!r.ok) throw new Error('canned ' + r.status);
                    return r.text();
                })
                .then(function () {
                    // Mirror the canned frames into the editor so user can inspect/edit
                    var frames = CANNED_SEQ[n];
                    if (!frames) { showToast('Unknown canned index', 'err'); return; }
                    kfRows = frames.map(function (f) {
                        return {
                            lr:     f[0] === -1 ? 'HOLD' : f[0],
                            ud:     f[1] === -1 ? 'HOLD' : f[1],
                            ap:     f[2],
                            blink:  f[3],
                            easeMs: f[4],
                            holdMs: f[5],
                            ease:   f[6]
                        };
                    });
                    renderKfList();
                    showToast('Loaded canned #' + n + ' (' + kfRows.length + ' frames)', 'ok');
                })
                .catch(function (e) { showToast('Load error: ' + e.message, 'err'); });
        }

        // ── localStorage save / load / delete ────────────────────────────
        var LS_PREFIX = 'eyemech.choreo.';

        function storeListNames() {
            var names = [];
            for (var i = 0; i < localStorage.length; i++) {
                var k = localStorage.key(i);
                if (k && k.indexOf(LS_PREFIX) === 0) {
                    names.push(k.slice(LS_PREFIX.length));
                }
            }
            names.sort();
            return names;
        }

        function storeRefreshSel() {
            var sel = document.getElementById('store-sel');
            var prev = sel.value;
            sel.innerHTML = '';
            var names = storeListNames();
            if (names.length === 0) {
                var opt = document.createElement('option');
                opt.value = ''; opt.textContent = '-- none saved --';
                sel.appendChild(opt);
                return;
            }
            names.forEach(function (n) {
                var opt = document.createElement('option');
                opt.value = n; opt.textContent = n;
                if (n === prev) opt.selected = true;
                sel.appendChild(opt);
            });
        }

        function storeSave() {
            var name = document.getElementById('save-name').value.trim();
            if (!name) { showToast('Enter a name first', 'err'); return; }
            if (kfRows.length === 0) { showToast('Nothing to save', 'err'); return; }
            try {
                localStorage.setItem(LS_PREFIX + name, JSON.stringify(kfRows));
                storeRefreshSel();
                showToast('Saved "' + name + '"', 'ok');
            } catch (e) { showToast('Save failed (storage full?)', 'err'); }
        }

        function storeLoad() {
            var name = document.getElementById('store-sel').value;
            if (!name) { showToast('Select a saved sequence', 'err'); return; }
            var raw = localStorage.getItem(LS_PREFIX + name);
            if (!raw) { showToast('Not found: "' + name + '"', 'err'); return; }
            try {
                var loaded = JSON.parse(raw);
                kfRows = loaded;
                renderKfList();
                document.getElementById('save-name').value = name;
                showToast('Loaded "' + name + '"', 'ok');
            } catch (e) { showToast('Parse error', 'err'); }
        }

        function storeDelete() {
            var name = document.getElementById('store-sel').value;
            if (!name) { showToast('Select a sequence to delete', 'err'); return; }
            localStorage.removeItem(LS_PREFIX + name);
            storeRefreshSel();
            showToast('Deleted "' + name + '"', 'ok');
        }

        // Init saved sequences dropdown on load
        storeRefreshSel();
        // Render empty list
        renderKfList();
    </script>
</body>
</html>
)raw";

#endif
