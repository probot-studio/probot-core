#pragma once
#ifdef ESP32
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif

const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta http-equiv="Content-Security-Policy" content="default-src * 'unsafe-inline' 'unsafe-eval';">
  <title>Probot Driver Station</title>
  <style>
    :root{
      --navy:#00204d;
      --ice:#e5e4e2;
      --start:#28a745;
      --stop:#d93025;
      --sky:#0aa6d9;
      font-family:"Inter","Segoe UI",sans-serif;
    }
    *{margin:0;padding:0;box-sizing:border-box;}
    body{
      min-height:100vh;
      background:var(--ice);
      color:var(--navy);
      display:flex;
      flex-direction:column;
    }
    .app-header{
      position:sticky;
      top:0;
      z-index:100;
      padding:16px 28px;
      display:flex;
      align-items:center;
      gap:16px;
      justify-content:space-between;
      background:var(--navy);
      color:var(--ice);
      box-shadow:0 8px 18px rgba(0,32,77,0.24);
      border-bottom:1px solid rgba(229,228,226,0.12);
    }
    .app-header .header-left{
      display:flex;
      flex-direction:column;
      gap:4px;
    }
    .app-header .header-left h1{
      font-size:1.5rem;
      letter-spacing:0.08em;
    }
    .app-header .header-left .header-subtitle{
      font-size:0.82rem;
      color:rgba(229,228,226,0.8);
      text-transform:uppercase;
      letter-spacing:0.08em;
    }
    .app-header nav{
      display:flex;
      gap:16px;
      flex:1;
      justify-content:center;
    }
    .nav-link{
      color:var(--ice);
      text-decoration:none;
      letter-spacing:0.1em;
      font-size:0.85rem;
      text-transform:uppercase;
      opacity:0.6;
      transition:opacity 120ms ease;
      cursor:pointer;
      padding-bottom:4px;
      border-bottom:2px solid transparent;
    }
    .nav-link:hover{opacity:0.85;}
    .nav-link.active{
      opacity:1;
      border-bottom-color:var(--ice);
    }
    .app-header .header-status{
      display:flex;
      flex-direction:row;
      align-items:center;
      gap:6px;
      text-transform:uppercase;
      letter-spacing:0.16em;
    }
    .app-header .header-status .status-label{
      font-size:0.6rem;
      letter-spacing:0.2em;
      opacity:0.7;
    }
    .app-header .header-status .status-value{
      font-size:0.9rem;
      letter-spacing:0.14em;
    }
    .app-header .header-status .status-detail{
      font-size:0.7rem;
      letter-spacing:0.08em;
      opacity:0.7;
      text-transform:none;
    }

    @media(max-width:900px){
      .app-header{
        flex-direction:column;
        align-items:flex-start;
        gap:12px;
        padding:18px 24px;
      }
      .app-header .header-left{width:100%;}
      .app-header .header-left h1{display:none;}
      .app-header .header-left .header-subtitle{display:none;}
      .app-header nav{
        order:3;
        width:100%;
        flex-wrap:wrap;
        justify-content:space-between;
        gap:12px;
      }
      .nav-link{font-size:0.98rem;}
      .app-header .header-status{display:none;}
    }

    main{
      flex:1;
      background:linear-gradient(180deg,var(--ice) 0%,rgba(229,228,226,0.85) 70%,rgba(229,228,226,0.7) 100%);
      box-shadow:0 -24px 48px rgba(0,32,77,0.12);
      padding:36px 48px 48px;
    }
    .page{display:none;}
    .page.active{
      display:grid;
      grid-template-columns:minmax(0,1fr) minmax(0,1fr);
      gap:36px;
      min-height:calc(100vh - 80px);
    }
    .column{
      display:flex;
      flex-direction:column;
      gap:32px;
      min-width:0;
    }
    .stack-card{
      background:var(--ice);
      border-radius:24px;
      padding:28px;
      border:1px solid rgba(0,32,77,0.12);
      box-shadow:0 24px 36px rgba(0,32,77,0.08);
      display:flex;
      flex-direction:column;
      gap:20px;
    }
    .stack-card h2{
      font-size:1.1rem;
      letter-spacing:0.12em;
      text-transform:uppercase;
      color:rgba(0,32,77,0.85);
    }
    .control-row{
      display:grid;
      grid-template-columns:repeat(auto-fit,minmax(160px,1fr));
      gap:16px;
    }
    .control{
      background:rgba(229,228,226,0.9);
      border-radius:16px;
      padding:16px;
      border:1px solid rgba(0,32,77,0.08);
      display:flex;
      flex-direction:column;
      gap:10px;
    }
    .control label{
      font-size:0.96rem;
      letter-spacing:0.08em;
      color:rgba(0,32,77,0.65);
    }
    .control input[type="number"]{
      padding:10px;
      border-radius:12px;
      border:1px solid rgba(0,32,77,0.14);
      background:var(--ice);
      text-align:center;
      font-size:1rem;
      color:var(--navy);
    }
    .auto-progress{
      margin-top:16px;
      background:linear-gradient(180deg,rgba(0,32,77,0.08),rgba(0,32,77,0.03));
      border-radius:16px;
      padding:16px;
      border:1px solid rgba(0,32,77,0.12);
      display:flex;
      flex-direction:column;
      gap:12px;
      box-shadow:0 16px 28px rgba(0,32,77,0.08);
    }
    .auto-progress-header{
      display:flex;
      justify-content:space-between;
      align-items:center;
      font-size:0.95rem;
      color:rgba(0,32,77,0.75);
      letter-spacing:0.08em;
      text-transform:uppercase;
    }
    .auto-progress-header strong{
      font-size:1.25rem;
      letter-spacing:0.08em;
      color:rgba(0,32,77,0.8);
    }
    .auto-progress-bar{
      position:relative;
      height:12px;
      border-radius:999px;
      background:rgba(0,32,77,0.1);
      overflow:hidden;
    }
    .auto-progress-fill{
      position:absolute;
      left:0;top:0;bottom:0;
      width:0%;
      background:linear-gradient(90deg,var(--navy),rgba(0,32,77,0.6));
      border-radius:inherit;
      transition:width 120ms linear;
    }

    /* Full joystick */
    .joy-grid{
      display:grid;
      grid-template-columns:160px 1fr;
      gap:20px;
      align-items:center;
    }
    .joy-indicator{
      display:flex;
      flex-direction:column;
      align-items:center;
      gap:8px;
      color:rgba(0,32,77,0.7);
      letter-spacing:0.08em;
      font-size:0.92rem;
    }
    .joy-axes{
      position:relative;
      width:120px;
      height:120px;
      border-radius:50%;
      background:radial-gradient(circle,var(--ice) 0%,rgba(229,228,226,0.85) 65%,rgba(229,228,226,0.7) 100%);
      box-shadow:inset 0 4px 12px rgba(0,32,77,0.12);
    }
    .joy-cross{
      position:absolute;
      left:50%;top:50%;
      width:4px;height:100%;
      background:rgba(0,32,77,0.2);
      transform:translate(-50%,-50%);
    }
    .joy-cross::before{
      content:"";position:absolute;
      left:50%;top:50%;
      width:100%;height:4px;
      background:rgba(0,32,77,0.2);
      transform:translate(-50%,-50%);
    }
    .joy-dot{
      position:absolute;
      width:14px;height:14px;
      border-radius:50%;
      background:var(--navy);
      box-shadow:0 0 10px rgba(0,32,77,0.4);
      transform:translate(-50%,-50%);
      left:50%;top:50%;
    }
    .joy-buttons{
      display:grid;
      grid-template-columns:repeat(auto-fit,minmax(48px,1fr));
      gap:12px;
    }
    .joy-button{
      position:relative;
      width:48px;height:48px;
      border-radius:50%;
      background:radial-gradient(circle at 30% 30%,rgba(229,228,226,0.95),rgba(0,32,77,0.08));
      border:1px solid rgba(0,32,77,0.18);
      color:rgba(0,32,77,0.75);
      font-size:0.75rem;
      display:flex;align-items:center;justify-content:center;
      letter-spacing:0.08em;
      text-transform:uppercase;
      box-shadow:0 8px 16px rgba(0,32,77,0.16);
      transition:transform 120ms ease, box-shadow 120ms ease, background 160ms ease;
    }
    .joy-button::after{
      content:attr(data-label);
      position:absolute;inset:0;
      display:flex;align-items:center;justify-content:center;
    }
    .joy-button.active{
      background:radial-gradient(circle at 30% 30%,rgba(229,228,226,0.95),rgba(0,32,77,0.2));
      border-color:rgba(0,32,77,0.7);
      color:var(--navy);
      box-shadow:0 10px 20px rgba(0,32,77,0.35),0 0 12px rgba(0,32,77,0.3);
      transform:translateY(-2px);
    }
    .joy-status{
      margin-top:18px;
      display:flex;flex-direction:column;gap:4px;
      color:rgba(0,32,77,0.75);
      letter-spacing:0.06em;
    }
    .joy-status strong{
      font-size:1.05rem;
      text-transform:uppercase;
    }

    /* Mini joystick on dashboard */
    .mini-joy{
      display:flex;
      align-items:center;
      gap:20px;
    }
    .mini-joy-axes{
      position:relative;
      width:80px;height:80px;
      border-radius:50%;
      background:radial-gradient(circle,var(--ice) 0%,rgba(229,228,226,0.85) 65%,rgba(229,228,226,0.7) 100%);
      box-shadow:inset 0 3px 8px rgba(0,32,77,0.12);
      flex-shrink:0;
    }
    .mini-joy-axes .joy-cross{width:3px;}
    .mini-joy-axes .joy-cross::before{height:3px;}
    .mini-joy-axes .joy-dot{width:10px;height:10px;}
    .mini-joy-info{
      display:flex;flex-direction:column;gap:4px;
    }
    .mini-joy-info strong{
      font-size:1rem;
      text-transform:uppercase;
      letter-spacing:0.06em;
    }
    .mini-joy-info .hint a{
      color:var(--sky);
      text-decoration:none;
    }
    .mini-joy-info .hint a:hover{text-decoration:underline;}

    .switch{
      display:flex;
      align-items:center;
      justify-content:space-between;
      background:rgba(229,228,226,0.85);
      border-radius:16px;
      padding:10px 14px;
    }
    .switch span{
      font-size:0.92rem;
      letter-spacing:0.1em;
      color:rgba(0,32,77,0.7);
    }
    .switch input{
      width:46px;height:24px;
      appearance:none;
      background:rgba(0,32,77,0.18);
      border-radius:999px;
      position:relative;
      cursor:pointer;
      transition:background 160ms ease;
    }
    .switch input::after{
      content:"";
      width:20px;height:20px;
      border-radius:50%;
      position:absolute;top:2px;left:3px;
      background:var(--ice);
      box-shadow:0 6px 12px rgba(0,32,77,0.24);
      transition:transform 160ms ease;
    }
    .switch input:checked{background:var(--sky);}
    .switch input:checked::after{transform:translateX(20px);}
    button{
      padding:16px;
      border-radius:18px;
      border:none;
      background:var(--navy);
      color:var(--ice);
      font-size:1.14rem;
      font-weight:600;
      letter-spacing:0.12em;
      text-transform:uppercase;
      cursor:pointer;
      box-shadow:0 18px 26px rgba(0,32,77,0.3);
      transition:transform 140ms ease, box-shadow 140ms ease, background 140ms ease;
    }
    button:hover{transform:translateY(-2px);}
    button:active{transform:translateY(1px);box-shadow:0 12px 18px rgba(0,32,77,0.24);}

    .telemetry{
      display:flex;flex-direction:column;gap:16px;
    }
    select, .telemetry pre{
      width:100%;
      padding:14px;
      border-radius:16px;
      border:1px solid rgba(0,32,77,0.12);
      background:var(--ice);
      font-size:1.05rem;
      color:var(--navy);
      letter-spacing:0.04em;
    }
    .telemetry pre{
      height:140px;
      overflow:auto;
      background:rgba(229,228,226,0.92);
      font-family:"SFMono-Regular","Roboto Mono",monospace;
      line-height:1.5;
      white-space:pre-wrap;
      word-break:break-word;
    }
    #telemetryOutput{
      flex:1;
      min-height:300px;
      overflow-y:auto;
      background:rgba(0,32,77,0.05);
      padding:12px;
      border-radius:12px;
      font-size:0.9rem;
      font-family:"SFMono-Regular","Roboto Mono",monospace;
      line-height:1.5;
      white-space:pre-wrap;
      word-break:break-word;
    }
    .hint{
      font-size:0.88rem;
      color:rgba(0,32,77,0.6);
      letter-spacing:0.06em;
    }

    /* Connection bar */
    .conn-bar{
      display:flex;align-items:center;gap:8px;
      font-size:0.75rem;letter-spacing:0.08em;
      color:rgba(229,228,226,0.85);
    }
    .conn-dot{
      width:8px;height:8px;border-radius:50%;
      background:#28a745;
      box-shadow:0 0 6px rgba(40,167,69,0.5);
      transition:background 300ms ease, box-shadow 300ms ease;
    }
    .conn-dot.warn{background:#ffc107;box-shadow:0 0 6px rgba(255,193,7,0.5);}
    .conn-dot.bad{background:#d93025;box-shadow:0 0 6px rgba(217,48,37,0.5);}
    .conn-signal{
      display:flex;align-items:flex-end;gap:2px;height:14px;
    }
    .conn-signal .bar{
      width:3px;
      background:rgba(229,228,226,0.25);
      border-radius:1px;
      transition:background 300ms ease;
    }
    .conn-signal .bar.active{background:rgba(229,228,226,0.9);}
    .conn-signal .bar:nth-child(1){height:4px;}
    .conn-signal .bar:nth-child(2){height:7px;}
    .conn-signal .bar:nth-child(3){height:10px;}
    .conn-signal .bar:nth-child(4){height:14px;}
    .conn-ping, .conn-heap{
      font-variant-numeric:tabular-nums;
      min-width:36px;text-align:right;
    }

    /* Disconnect overlay */
    .disconnect-overlay{
      display:none;
      position:fixed;inset:0;z-index:9999;
      color:#fff;
      justify-content:center;align-items:center;
      flex-direction:column;gap:16px;
      font-size:2rem;font-weight:700;
      letter-spacing:0.2em;text-transform:uppercase;
      background:rgba(217,48,37,0.94);
    }
    .disconnect-overlay.show{display:flex;}
    .disconnect-overlay .sub{
      font-size:0.9rem;font-weight:400;
      letter-spacing:0.1em;opacity:0.85;
    }

    /* Emergency stop */
    .estop-btn{
      width:100%;margin-top:14px;
      padding:18px;border:none;border-radius:14px;cursor:pointer;
      background:var(--stop);color:#fff;
      font-size:1.4rem;font-weight:800;letter-spacing:0.12em;
      text-transform:uppercase;
    }
    .estop-btn:active{filter:brightness(0.85);}
    .estop-overlay{
      display:none;
      position:fixed;inset:0;z-index:10000;
      color:#fff;
      justify-content:center;align-items:center;
      flex-direction:column;gap:18px;
      font-size:2.2rem;font-weight:800;
      letter-spacing:0.2em;text-transform:uppercase;
      background:rgba(140,12,12,0.97);
    }
    .estop-overlay.show{display:flex;}
    .estop-overlay .sub{
      font-size:0.95rem;font-weight:400;
      letter-spacing:0.08em;opacity:0.9;text-transform:none;
    }
    .estop-overlay button{
      margin-top:10px;padding:16px 32px;border:none;border-radius:12px;
      cursor:pointer;background:#fff;color:#8c0c0c;
      font-size:1.1rem;font-weight:800;letter-spacing:0.06em;
    }

    /* Debug grid for Logs page */
    .debug-grid{
      display:grid;
      grid-template-columns:1fr 1fr;
      gap:12px;
    }
    .debug-item{
      background:rgba(229,228,226,0.9);
      border-radius:16px;
      padding:16px;
      border:1px solid rgba(0,32,77,0.08);
      display:flex;flex-direction:column;gap:6px;
    }
    .debug-label{
      font-size:0.78rem;
      text-transform:uppercase;
      letter-spacing:0.12em;
      color:rgba(0,32,77,0.5);
    }
    .debug-value{
      font-size:1.1rem;
      font-weight:600;
      color:var(--navy);
      font-variant-numeric:tabular-nums;
      word-break:break-all;
    }

    /* Responsive */
    @media(max-width:992px){
      .app-header{padding:16px 28px;gap:18px;}
      .app-header .header-left h1{font-size:1.7rem;}
      .app-header .header-left .header-subtitle{font-size:0.9rem;letter-spacing:0.08em;}
      .app-header nav{gap:16px;}
      .nav-link{font-size:0.9rem;letter-spacing:0.1em;}
      .app-header .header-status{align-items:flex-start;gap:6px;}
      .app-header .header-status .status-label{font-size:0.62rem;letter-spacing:0.18em;}
      .app-header .header-status .status-value{font-size:0.98rem;}
      .app-header .header-status .status-detail{font-size:0.74rem;}
    }
    @media(max-width:900px){
      main{padding:24px 24px 32px;}
      .page.active{
        grid-template-columns:1fr;
        gap:24px;
      }
      .column{gap:24px;}
      .app-header{
        flex-direction:column;align-items:flex-start;
        gap:12px;padding:18px 24px;
      }
      .app-header .header-left h1{font-size:1.8rem;}
      .app-header .header-left .header-subtitle{font-size:0.88rem;}
      .app-header nav{
        order:3;width:100%;
        flex-wrap:wrap;gap:12px;
        justify-content:space-between;
      }
      .nav-link{font-size:0.98rem;letter-spacing:0.09em;}
      .app-header .header-status{display:none;}
      .stack-card h2{font-size:1.2rem;}
    }
    @media(orientation:portrait){
      main{padding:36px 36px 48px;}
      .page.active{
        grid-template-columns:1fr;
        gap:36px;
      }
      .column{gap:36px;}
      .app-header{
        flex-direction:column;align-items:flex-start;
        gap:16px;padding:18px 24px;
        justify-content:flex-start;
      }
      .app-header nav{
        order:3;width:100%;
        flex-wrap:wrap;justify-content:space-between;gap:18px;
      }
      .nav-link{font-size:1.15rem;letter-spacing:0.08em;}
      .app-header .header-left{gap:6px;width:100%;}
      .app-header .header-left h1{display:none;}
      .app-header .header-left .header-subtitle{display:none;}
      .app-header .header-status{display:none;}
      body{font-size:1.9rem;line-height:1.5;}
      .stack-card h2{font-size:1.35rem;letter-spacing:0.12em;}
      .control-row{grid-template-columns:1fr;gap:20px;}
      button{font-size:2.4rem;padding:24px;}
      #robotButton{padding:109px 42px;font-size:4.7rem;}
      .control label{font-size:1.8rem;}
      .control input[type="number"]{font-size:2rem;padding:16px;}
      .switch{flex-direction:row;align-items:center;gap:18px;padding:14px 18px;}
      .switch span{font-size:1.6rem;}
      .switch input{width:88px;height:46px;}
      .switch input::after{width:38px;height:38px;top:4px;left:6px;}
      .switch input:checked::after{transform:translateX(42px);}
      select, .telemetry pre{font-size:1.9rem;padding:22px;}
      .telemetry pre{line-height:1.6;}
      .auto-progress-header{font-size:1.8rem;letter-spacing:0.05em;}
      .auto-progress-header strong{font-size:2.6rem;}
      .hint{font-size:1.6rem;}
      .joy-grid{grid-template-columns:1fr;gap:24px;}
      .joy-indicator{font-size:1.5rem;}
      .joy-axes{width:220px;height:220px;margin:0 auto;}
      .joy-buttons{grid-template-columns:repeat(auto-fit,minmax(84px,1fr));gap:16px;}
      .joy-button{width:84px;height:84px;font-size:1.4rem;margin:0 auto;}
      .joy-status strong{font-size:1.8rem;}
      .conn-bar{font-size:1.3rem;gap:12px;}
      .conn-dot{width:14px;height:14px;}
      .conn-signal{height:22px;gap:3px;}
      .conn-signal .bar{width:5px;}
      .conn-signal .bar:nth-child(1){height:6px;}
      .conn-signal .bar:nth-child(2){height:11px;}
      .conn-signal .bar:nth-child(3){height:16px;}
      .conn-signal .bar:nth-child(4){height:22px;}
      .disconnect-overlay{font-size:3.5rem;}
      .disconnect-overlay .sub{font-size:1.5rem;}
      .mini-joy-axes{width:120px;height:120px;}
      .mini-joy-info strong{font-size:1.6rem;}
      .debug-grid{grid-template-columns:1fr;}
      .debug-label{font-size:1.3rem;}
      .debug-value{font-size:1.7rem;}
    }
    @media(max-width:1024px) and (orientation:landscape){
      .app-header{
        flex-direction:row;align-items:center;justify-content:center;
        padding:6px 18px;gap:12px;
      }
      .app-header .header-left,
      .app-header .header-status{display:none;}
      .app-header nav{width:auto;flex:0 1 auto;gap:12px;flex-wrap:wrap;justify-content:center;}
      .nav-link{font-size:0.78rem;letter-spacing:0.14em;}
      main{padding:16px 20px 22px;}
      .page.active{
        grid-template-columns:minmax(0,1fr) minmax(0,1fr);
        gap:20px;
      }
      .column{gap:20px;}
    }
  </style>
</head>
<body>
  <header class="app-header" id="appHeader">
    <div class="header-left">
      <h1>Probot Driver Station</h1>
      <span class="header-subtitle">by NFR Products</span>
    </div>
    <nav>
      <a class="nav-link active" data-page="dashboard" onclick="showPage('dashboard')">Dashboard</a>
      <a class="nav-link" data-page="joystick" onclick="showPage('joystick')">Joystick</a>
      <a class="nav-link" data-page="logs" onclick="showPage('logs')">Logs</a>
    </nav>
    <div class="conn-bar" id="connBar">
      <div class="conn-dot" id="connDot"></div>
      <div class="conn-signal" id="connSignal">
        <div class="bar"></div>
        <div class="bar"></div>
        <div class="bar"></div>
        <div class="bar"></div>
      </div>
      <span class="conn-ping" id="connPing">--</span>
      <span class="conn-heap" id="connHeap">--</span>
    </div>
    <div class="header-status">
      <span class="status-label">Status</span>
      <span class="status-value" id="headerStatusValue">Standby</span>
      <span class="status-detail" id="headerStatusDetail">Awaiting command</span>
    </div>
  </header>

  <div class="disconnect-overlay" id="disconnectOverlay">
    <span>DISCONNECTED</span>
    <span class="sub">Trying to reconnect...</span>
  </div>

  <div class="estop-overlay" id="estopOverlay">
    <span>EMERGENCY STOPPED</span>
    <span class="sub">Robot disabled — reboot required to clear</span>
    <button id="rebootButton">Reboot Robot</button>
  </div>

<main>
  <!-- ===== DASHBOARD PAGE ===== -->
  <div class="page active" id="page-dashboard">
    <div class="column">
      <section class="stack-card">
        <h2>Match Control</h2>
        <div class="control-row">
          <button id="robotButton">Init</button>
          <div class="control">
            <label>Autonomous Duration</label>
            <input type="number" id="autoPeriod" value="30" min="1" max="120">
            <span class="hint">Seconds</span>
          </div>
          <div class="switch">
            <span>Autonomous</span>
            <input type="checkbox" id="enableAutonomous" checked>
          </div>
          <div class="control" id="autoModeControl" style="display:none;">
            <label for="autoModeSelect">Autonomous Routine</label>
            <select id="autoModeSelect" onchange="selectAutoMode()">
              <option value="">Select a routine</option>
            </select>
            <span class="hint" id="autoModeStatus">Loading autonomous routines…</span>
          </div>
        </div>
        <button id="estopButton" class="estop-btn">EMERGENCY STOP</button>
        <div class="auto-progress">
          <div class="auto-progress-header">
            <span>Autonomous Countdown</span>
            <strong id="autoCountdown">00.0 s</strong>
          </div>
          <div class="auto-progress-bar">
            <div class="auto-progress-fill" id="autoProgress"></div>
          </div>
        </div>
      </section>
      <section class="stack-card">
        <h2>Joystick</h2>
        <div class="mini-joy">
          <div class="mini-joy-axes">
            <div class="joy-dot" id="miniJoyDot"></div>
            <div class="joy-cross"></div>
          </div>
          <div class="mini-joy-info">
            <strong id="miniJoyStatus">Not Connected</strong>
            <p class="hint"><a href="#" onclick="showPage('joystick');return false;">Open Joystick page</a></p>
          </div>
        </div>
      </section>
    </div>
    <div class="column">
      <section class="stack-card" id="telemetry-panel" style="flex:1;display:flex;flex-direction:column;">
        <h2>Telemetry</h2>
        <pre id="telemetryOutput"></pre>
        <button onclick="clearTelemetry()" style="margin-top:12px;padding:10px 20px;font-size:0.9rem;">Clear</button>
        <button id="autoScrollToggle" onclick="toggleAutoScroll()" style="margin-top:8px;padding:10px 20px;font-size:0.9rem;">Auto-scroll: ON</button>
      </section>
    </div>
  </div>

  <!-- ===== JOYSTICK PAGE ===== -->
  <div class="page" id="page-joystick">
    <div class="column">
      <section class="stack-card">
        <h2>Joystick Check</h2>
        <div class="joy-grid">
          <div class="joy-indicator">
            <div class="joy-axes">
              <div class="joy-dot" id="joyDot"></div>
              <div class="joy-cross"></div>
            </div>
            <span>Axes</span>
          </div>
          <div class="joy-buttons" id="joyButtons"></div>
        </div>
        <div class="joy-status" id="joyStatus">
          <strong id="joystickStatusTxt">Not Connected</strong>
          <p class="hint" id="gamepadHint" style="display:none;">Press any controller button to activate.</p>
        </div>
      </section>
    </div>
    <div class="column">
      <section class="stack-card telemetry">
        <h2>System Logs</h2>
        <select id="joystickSelect" onchange="changeSelectedGamepad()">
          <option value="-1">No Gamepad</option>
        </select>
        <pre id="joystickStatus">No gamepad selected.</pre>
        <pre id="axisData">No axis data...</pre>
        <pre id="buttonData">No button data...</pre>
      </section>
    </div>
  </div>

  <!-- ===== LOGS PAGE ===== -->
  <div class="page" id="page-logs">
    <div class="column">
      <section class="stack-card">
        <h2>WiFi Configuration</h2>
        <div class="debug-grid">
          <div class="debug-item">
            <span class="debug-label">SSID</span>
            <span class="debug-value" id="dbgSsid">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Channel</span>
            <span class="debug-value" id="dbgCh">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">IP Address</span>
            <span class="debug-value" id="dbgIp">--</span>
          </div>
        </div>
        <div class="control" style="margin-top:12px;">
          <label>Kanal Değiştir (CSA — bağlantı korunur)</label>
          <div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap;">
            <select id="chSelect" style="flex:1;min-width:0;font-size:1rem;padding:10px;">
              <option value="0">Varsayılana dön (açılışta)</option>
              <option value="1">1</option><option value="5">5</option>
              <option value="9">9</option><option value="13">13</option>
              <option value="2">2</option><option value="3">3</option>
              <option value="4">4</option><option value="6">6</option>
              <option value="7">7</option><option value="8">8</option>
              <option value="10">10</option><option value="11">11</option>
              <option value="12">12</option>
            </select>
            <button onclick="applyChannel()" style="padding:10px 18px;font-size:0.9rem;">Uygula</button>
          </div>
          <span class="hint" id="chStatus">1/5/9/13 önerilir</span>
        </div>
      </section>
      <section class="stack-card">
        <h2>Network Status</h2>
        <div class="debug-grid">
          <div class="debug-item">
            <span class="debug-label">RSSI</span>
            <span class="debug-value" id="dbgRssi">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Ping</span>
            <span class="debug-value" id="dbgPing">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">WebSocket</span>
            <span class="debug-value" id="dbgWs">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Deadline Miss</span>
            <span class="debug-value" id="dbgDm">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Joystick Age</span>
            <span class="debug-value" id="dbgJoyAge">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Clients</span>
            <span class="debug-value" id="dbgSta">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Last Disconnect</span>
            <span class="debug-value" id="dbgDisc">--</span>
          </div>
        </div>
      </section>
    </div>
    <div class="column">
      <section class="stack-card">
        <h2>ESP32 System</h2>
        <div class="debug-grid">
          <div class="debug-item">
            <span class="debug-label">Chip</span>
            <span class="debug-value" id="dbgChip">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">CPU</span>
            <span class="debug-value" id="dbgCpu">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">SDK</span>
            <span class="debug-value" id="dbgSdk">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Uptime</span>
            <span class="debug-value" id="dbgUptime">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Heap (Free / Total)</span>
            <span class="debug-value" id="dbgHeap">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">PSRAM</span>
            <span class="debug-value" id="dbgPsram">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Flash (Sketch / Total)</span>
            <span class="debug-value" id="dbgFlash">--</span>
          </div>
          <div class="debug-item">
            <span class="debug-label">Free Sketch Space</span>
            <span class="debug-value" id="dbgFreeSketch">--</span>
          </div>
        </div>
      </section>
    </div>
  </div>
</main>

  <script>
    /* ===== PAGE NAVIGATION ===== */
    function showPage(name){
      document.querySelectorAll('.page').forEach(function(p){p.classList.remove('active');});
      document.querySelectorAll('.nav-link').forEach(function(a){a.classList.remove('active');});
      var page=document.getElementById('page-'+name);
      if(page) page.classList.add('active');
      var nav=document.querySelector('.nav-link[data-page="'+name+'"]');
      if(nav) nav.classList.add('active');
    }

    /* ===== STATE ===== */
    var controlState="idle";
    var autoModeEnabled=false;
    var autoScroll=true;
    var selectedGamepadIndex=-1;
    var gamepads={};
    var gamepadDetected=false;
    var autoModes=[];
    var autoModesSupported=false;
    var autoCatalogError=false;
    var autoSelectionBusy=false;
    var confirmedAutoId="";

    var headerStatusValue=document.getElementById('headerStatusValue');
    var headerStatusDetail=document.getElementById('headerStatusDetail');

    function setPhaseDisplay(mode){
      var map={
        init:{title:"Init", detail:"Systems primed"},
        auto:{title:"Autonomous", detail:"Running script"},
        teleop:{title:"Teleop", detail:"Drivers in control"},
        stopped:{title:"Stopped", detail:"Motors safe"},
        standby:{title:"Standby", detail:"Awaiting command"}
      };
      var next=map[mode]||map.standby;
      if(headerStatusValue) headerStatusValue.textContent=next.title;
      if(headerStatusDetail){
        if(mode==='auto') headerStatusDetail.textContent='Auto running - '+autoRemaining.toFixed(1)+' s';
        else headerStatusDetail.textContent=next.detail;
      }
      document.body.dataset.state=mode;
    }

    var autoTimer=null;
    var autoRemaining=0;
    var DEFAULT_BUTTON_COUNT=12;
    var BUTTON_LABELS=["A","B","X","Y","LB","RB","LT","RT","Back","Start","L3","R3","P13","P14","P15","P16","P17","P18","P19","P20"];

    function ensureJoyButtons(count){
      var container=document.getElementById('joyButtons');
      if(container.childElementCount===count) return;
      container.innerHTML="";
      for(var i=0;i<count;i++){
        var cell=document.createElement('div');
        cell.className='joy-button';
        var label=BUTTON_LABELS[i]||('B'+(i+1));
        cell.dataset.label=label;
        container.appendChild(cell);
      }
    }

    function updateJoyVisuals(gp){
      var dot=document.getElementById('joyDot');
      var container=document.getElementById('joyButtons');
      if(gp){
        var x=gp.axes&&gp.axes.length>0?gp.axes[0]:0;
        var y=gp.axes&&gp.axes.length>1?gp.axes[1]:0;
        dot.style.left=(50+x*45)+'%';
        dot.style.top=(50+y*45)+'%';
        ensureJoyButtons(gp.buttons.length);
        for(var i=0;i<gp.buttons.length;i++){
          var node=container.children[i];
          if(node){
            if(gp.buttons[i].pressed) node.classList.add('active');
            else node.classList.remove('active');
          }
        }
      }else{
        ensureJoyButtons(DEFAULT_BUTTON_COUNT);
        dot.style.left='50%';
        dot.style.top='50%';
        for(var j=0;j<container.children.length;j++) container.children[j].classList.remove('active');
      }
    }

    function updateMiniJoy(gp){
      var dot=document.getElementById('miniJoyDot');
      if(!dot) return;
      if(gp&&gp.axes){
        var x=gp.axes.length>0?gp.axes[0]:0;
        var y=gp.axes.length>1?gp.axes[1]:0;
        dot.style.left=(50+x*40)+'%';
        dot.style.top=(50+y*40)+'%';
      }else{
        dot.style.left='50%';
        dot.style.top='50%';
      }
    }

    function updateAutoDisplay(){
      var display=document.getElementById('autoCountdown');
      var fill=document.getElementById('autoProgress');
      var duration=parseFloat(document.getElementById('autoPeriod').value)||0;
      display.textContent=autoRemaining.toFixed(1)+' s';
      var pct=duration>0?Math.max(0,Math.min(100,(autoRemaining/duration)*100)):0;
      fill.style.width=pct+'%';
      if(autoModeEnabled) setPhaseDisplay('auto');
    }

    function setAutoModeStatus(message, isError){
      var status=document.getElementById('autoModeStatus');
      if(!status) return;
      status.textContent=message;
      status.style.color=isError?'#b42318':'';
    }

    function renderAutoModes(data){
      var control=document.getElementById('autoModeControl');
      var select=document.getElementById('autoModeSelect');
      if(!control||!select) return;
      autoModes=Array.isArray(data.modes)?data.modes:[];
      autoModesSupported=data.supported===true&&autoModes.length>0;
      autoCatalogError=data.registrationError===true;
      control.style.display=(autoModesSupported||autoCatalogError)?'flex':'none';
      select.innerHTML='';
      var placeholder=document.createElement('option');
      placeholder.value='';
      placeholder.textContent='Select a routine';
      select.appendChild(placeholder);
      autoModes.forEach(function(mode){
        var option=document.createElement('option');
        option.value=mode.id;
        option.textContent=mode.label;
        select.appendChild(option);
      });
      confirmedAutoId=(typeof data.selected==='string')?data.selected:'';
      select.value=confirmedAutoId;
      if(autoCatalogError) setAutoModeStatus('AutoChooser setup error. Check routine IDs and defaults.',true);
      else if(confirmedAutoId) setAutoModeStatus('Selected: '+(autoModes.find(function(m){return m.id===confirmedAutoId;})||{}).label,false);
      else setAutoModeStatus('Select a routine before starting autonomous.',false);
      updateAutoModeLock();
    }

    function loadAutoModes(){
      fetch('/getAutoModes',{cache:'no-store'}).then(function(r){
        if(!r.ok) throw new Error('AutoChooser unavailable');
        return r.json();
      }).then(function(data){renderAutoModes(data);}).catch(function(){
        autoModesSupported=false;
        var control=document.getElementById('autoModeControl');
        if(control) control.style.display='none';
      });
    }

    function updateAutoModeLock(){
      var select=document.getElementById('autoModeSelect');
      if(!select) return;
      select.disabled=autoCatalogError||autoSelectionBusy||controlState==='running';
    }

    function selectAutoMode(){
      var select=document.getElementById('autoModeSelect');
      if(!select||autoSelectionBusy) return;
      var id=select.value;
      if(!id){
        select.value=confirmedAutoId;
        setAutoModeStatus(confirmedAutoId?'A registered routine must remain selected.':'Select a routine before starting autonomous.',!confirmedAutoId);
        return;
      }
      autoSelectionBusy=true;
      lastCmdMs=performance.now();
      updateAutoModeLock();
      setAutoModeStatus('Applying selection…',false);
      fetch('/robotControl?cmd=selectAuto&autoId='+encodeURIComponent(id)).then(function(r){
        if(!r.ok) throw new Error('Selection rejected ('+r.status+')');
        return r.json();
      }).then(function(data){
        confirmedAutoId=data.selectedAutoId||'';
        select.value=confirmedAutoId;
        var mode=autoModes.find(function(m){return m.id===confirmedAutoId;});
        setAutoModeStatus(mode?'Selected: '+mode.label:'No routine selected',!mode);
      }).catch(function(err){
        select.value=confirmedAutoId;
        setAutoModeStatus(err.message||'Could not select routine.',true);
      }).finally(function(){
        autoSelectionBusy=false;
        updateAutoModeLock();
      });
    }

    function startAutoTimer(duration){
      autoModeEnabled=true;
      autoRemaining=duration;
      updateAutoDisplay();
      clearInterval(autoTimer);
      autoTimer=setInterval(function(){
        autoRemaining=Math.max(0,autoRemaining-0.1);
        updateAutoDisplay();
        if(autoRemaining<=0){
          clearInterval(autoTimer);
          autoTimer=null;
          autoModeEnabled=false;
          setPhaseDisplay('teleop');
        }
      },100);
    }

    function stopAutoTimer(){
      clearInterval(autoTimer);
      autoTimer=null;
      autoRemaining=0;
      autoModeEnabled=false;
      updateAutoDisplay();
    }

    /* ===== STATE RENDER ===== */
    /* Fed by 'S' WS frames normally; by the HTTP fallback when WS is down.
       After a button press we ignore incoming state briefly: an 'S' frame
       generated BEFORE the command was applied would revert the optimistic
       UI and a second click would then send the wrong command. */
    var lastCmdMs=-10000;
    function applyState(data){
        if(!data) return;
        var ignoreRecentCommandState=performance.now()-lastCmdMs<600;
      if(data.autoChooserError===true){
        autoCatalogError=true;
        setAutoModeStatus('AutoChooser setup error. Check routine IDs and defaults.',true);
      }
        if(!ignoreRecentCommandState&&data.autoChooser===true&&typeof data.selectedAutoId==='string'&&!autoSelectionBusy){
          confirmedAutoId=data.selectedAutoId;
          var autoSelect=document.getElementById('autoModeSelect');
          if(autoSelect&&autoModesSupported) autoSelect.value=confirmedAutoId;
        }
        var estopOv=document.getElementById('estopOverlay');
        if(estopOv) estopOv.classList.toggle('show',data.estop===true);
        if(ignoreRecentCommandState) return;
        var btn=document.getElementById('robotButton');
        if(!btn) return;

        var autoPeriodEl=document.getElementById('autoPeriod');
        var autoEnableEl=document.getElementById('enableAutonomous');

        var isAutonomous=(data.phase===2);
        var isTeleop=(data.phase===3);
        var isRunning=(isAutonomous||isTeleop);
        if(autoPeriodEl&&typeof data.autoPeriodSeconds==='number'&&isRunning){
          autoPeriodEl.value=data.autoPeriodSeconds;
        }
        if(autoEnableEl){
          if(typeof data.autonomousEnabled==='boolean'&&(isAutonomous||isTeleop)){
            autoEnableEl.checked=data.autonomousEnabled;
          }
          autoEnableEl.disabled=isTeleop;
        }

        var remainingMs=(typeof data.autoRemainingMs==='number')?data.autoRemainingMs:null;
        var remainingSec=remainingMs!==null?Math.max(0,remainingMs)/1000:(parseFloat(autoPeriodEl?autoPeriodEl.value:0)||0);

        if(data.phase===1){
          controlState="armed";
          btn.textContent="Start";
          btn.style.background="var(--start)";
          btn.style.color="var(--ice)";
          stopAutoTimer();
          if(autoEnableEl&&autoEnableEl.checked){
            autoRemaining=parseFloat(autoPeriodEl?autoPeriodEl.value:0)||0;
          }else{autoRemaining=0;}
          updateAutoDisplay();
          setPhaseDisplay('init');
        }else if(data.phase===2){
          controlState="running";
          btn.textContent="Stop";
          btn.style.background="var(--stop)";
          btn.style.color="var(--ice)";
          if(data.autonomousEnabled===false){
            stopAutoTimer();
            setPhaseDisplay('teleop');
          }else{
            stopAutoTimer();
            if(remainingSec>0) startAutoTimer(remainingSec);
            else{autoModeEnabled=true;autoRemaining=0;updateAutoDisplay();setPhaseDisplay('auto');}
          }
        }else if(data.phase===3){
          controlState="running";
          btn.textContent="Stop";
          btn.style.background="var(--stop)";
          btn.style.color="var(--ice)";
          stopAutoTimer();
          setPhaseDisplay('teleop');
        }else{
          controlState="idle";
          btn.textContent="Init";
          btn.style.background="var(--navy)";
          btn.style.color="var(--ice)";
          stopAutoTimer();
          setPhaseDisplay('stopped');
        }
        updateAutoModeLock();
    }

    var fetchStateBusy=false;
    function fetchState(){
      if(fetchStateBusy) return;
      fetchStateBusy=true;
      var ac=new AbortController();
      var tid=setTimeout(function(){ac.abort();},3000);
      fetch('/getState',{signal:ac.signal}).then(function(r){
        clearTimeout(tid);
        if(!r.ok) return;
        return r.json();
      }).then(function(data){
        fetchStateBusy=false;
        if(data){lastDataMs=performance.now();applyState(data);}
      }).catch(function(){
        fetchStateBusy=false;
      });
    }

    /* ===== ROBOT BUTTON ===== */
    function handleRobotButton(){
      var cmd="";
      var enableAuto=document.getElementById('enableAutonomous').checked;
      var autoLen=Math.max(0,parseFloat(document.getElementById('autoPeriod').value)||0);

      switch(controlState){
        case "idle": cmd="init"; break;
        case "armed": cmd="start"; break;
        default: cmd="stop"; break;
      }

      if(cmd==='start'&&enableAuto&&(autoCatalogError||(autoModesSupported&&!confirmedAutoId))){
        if(!autoCatalogError) setAutoModeStatus('Select a routine before starting autonomous.',true);
        return;
      }
      if(cmd==='start'&&autoSelectionBusy){
        setAutoModeStatus('Wait for the autonomous selection to be confirmed.',true);
        return;
      }

      lastCmdMs=performance.now();
      var url='/robotControl?cmd='+cmd+'&auto='+(enableAuto?1:0)+'&autoLen='+autoLen;
      if(cmd==='start'&&enableAuto&&confirmedAutoId) url+='&autoId='+encodeURIComponent(confirmedAutoId);
      var ac=new AbortController();
      var tid=setTimeout(function(){ac.abort();},3000);
      fetch(url,{signal:ac.signal}).then(function(r){
        clearTimeout(tid);
        if(!r.ok){
          return r.text().then(function(message){throw new Error(message||('Robot command failed: '+r.status));});
        }
      }).catch(function(err){
        console.error("robotControl fetch error:",err);
        if(cmd==='start'&&enableAuto&&autoModesSupported) setAutoModeStatus(err.message||'Start was rejected.',true);
        setTimeout(fetchState,650);
      });

      var btn=document.getElementById('robotButton');
      if(controlState==="idle"){
        controlState="armed";
        btn.textContent="Start";
        btn.style.background="var(--start)";
        btn.style.color="var(--ice)";
        stopAutoTimer();
        autoRemaining=autoLen;
        updateAutoDisplay();
        setPhaseDisplay('init');
      }else if(controlState==="armed"){
        controlState="running";
        btn.textContent="Stop";
        btn.style.background="var(--stop)";
        btn.style.color="var(--ice)";
        if(enableAuto&&autoLen>0){startAutoTimer(autoLen);setPhaseDisplay('auto');}
        else{stopAutoTimer();setPhaseDisplay('teleop');}
      }else{
        controlState="idle";
        btn.textContent="Init";
        btn.style.background="var(--navy)";
        btn.style.color="var(--ice)";
        stopAutoTimer();
        setPhaseDisplay('stopped');
      }
      updateAutoModeLock();
    }
    document.getElementById('robotButton').addEventListener('click',handleRobotButton);

    /* ===== EMERGENCY STOP / REBOOT ===== */
    function sendSimpleCmd(cmd){
      var ac=new AbortController();
      var tid=setTimeout(function(){ac.abort();},3000);
      return fetch('/robotControl?cmd='+cmd,{signal:ac.signal}).then(function(r){
        clearTimeout(tid);return r;
      }).catch(function(err){console.error(cmd+' fetch error:',err);});
    }
    document.getElementById('estopButton').addEventListener('click',function(){
      lastCmdMs=performance.now();
      sendSimpleCmd('estop');
      var ov=document.getElementById('estopOverlay');
      if(ov) ov.classList.add('show');
    });
    document.getElementById('rebootButton').addEventListener('click',function(){
      this.textContent='Rebooting...';this.disabled=true;
      sendSimpleCmd('reboot');
    });

    /* ===== GAMEPAD ===== */
    function updateGamepads(){
      var gpList=navigator.getGamepads?navigator.getGamepads():[];
      gamepads={};
      for(var i=0;i<gpList.length;i++){
        var gp=gpList[i];
        if(gp) gamepads[gp.index]=gp;
      }
    }
    /* Runs from gamepadLoop at ~60Hz — only touch the DOM when the set
       of gamepads actually changed, and always restore the selection
       afterwards (removing the selected option silently resets a
       <select> to its first entry). */
    var lastGamepadSig=null;
    function rebuildGamepadSelect(){
      var selectEl=document.getElementById('joystickSelect');
      var keys=Object.keys(gamepads);
      var sig=keys.map(function(k){return k+':'+gamepads[k].id;}).join('|');
      if(sig===lastGamepadSig) return;
      lastGamepadSig=sig;
      while(selectEl.options.length>1) selectEl.remove(1);
      keys.forEach(function(idx){
        var gp=gamepads[idx];
        var option=document.createElement('option');
        option.value=idx;
        option.text=gp.id+' (idx '+gp.index+')';
        selectEl.add(option);
      });
      if(selectedGamepadIndex>=0&&!gamepads[selectedGamepadIndex]){
        selectedGamepadIndex=-1;
      }
      if(keys.length>0&&selectedGamepadIndex<0){
        selectedGamepadIndex=parseInt(keys[0],10);
      }
      selectEl.value=String(selectedGamepadIndex);
    }
    function changeSelectedGamepad(){
      var val=document.getElementById('joystickSelect').value;
      selectedGamepadIndex=parseInt(val,10);
      if(isNaN(selectedGamepadIndex)) selectedGamepadIndex=-1;
    }
    var gamepadSending=false;
    var lastGamepadSend=0;
    var GAMEPAD_SEND_INTERVAL=20;

    /* ===== WEBSOCKET =====
       Single always-on socket. Client sends 'J' joystick frames (50Hz)
       or a 'P' idle ping (2s, keeps the owner slot alive). Robot pushes
       'S' state+health JSON (>=1Hz, doubles as heartbeat) and 'T'
       telemetry text — no HTTP polling while the socket is up. */
    var wsJoystick=null;
    var wsConnected=false;
    var wsReconnectTimer=null;
    var wsLastActivity=0;
    var lastDataMs=performance.now();
    var textDecoder=new TextDecoder();

    function killWs(){
      if(wsReconnectTimer){clearTimeout(wsReconnectTimer);wsReconnectTimer=null;}
      if(wsJoystick){wsJoystick.onopen=null;wsJoystick.onclose=null;wsJoystick.onerror=null;wsJoystick.onmessage=null;try{wsJoystick.close();}catch(e){}}
      wsJoystick=null;wsConnected=false;
    }
    function handleWsMessage(ev){
      wsLastActivity=performance.now();
      if(!(ev.data instanceof ArrayBuffer)) return;
      var v=new Uint8Array(ev.data);
      if(v.length<1) return;
      if(v[0]===0x53){ /* 'S' state+health */
        var data;
        try{data=JSON.parse(textDecoder.decode(v.subarray(1)));}catch(e){return;}
        lastDataMs=performance.now();
        applyState(data);
        applyHealth(data);
      }else if(v[0]===0x54){ /* 'T' telemetry */
        lastDataMs=performance.now();
        renderTelemetry(textDecoder.decode(v.subarray(1)));
      }
    }
    function connectWebSocket(){
      killWs();
      try{
        var ws=new WebSocket('ws://'+location.host+'/joystick');
        ws.binaryType='arraybuffer';
        ws.onopen=function(){wsConnected=true;wsLastActivity=performance.now();console.log('[WS] Connected');};
        ws.onclose=function(){wsConnected=false;wsJoystick=null;scheduleReconnect();};
        ws.onerror=function(){wsConnected=false;};
        ws.onmessage=handleWsMessage;
        wsJoystick=ws;
      }catch(e){scheduleReconnect();}
    }
    function scheduleReconnect(){
      if(wsReconnectTimer) return;
      wsReconnectTimer=setTimeout(function(){wsReconnectTimer=null;connectWebSocket();},2000);
    }
    /* Robot pushes an 'S' frame at least every second; ~5s without any
       message means the link is dead even if the socket looks open. Own
       sends do NOT count as activity: ws.send() into a dead TCP socket
       succeeds silently. The overlay appears when no data has arrived
       from any source (WS or HTTP fallback) for 6s. */
    function linkSupervisor(){
      if(wsJoystick){
        if(wsJoystick.readyState>1){
          /* killWs (not a bare null-out): detaches handlers so a late
             onclose from the dying socket can't clobber the next one. */
          killWs();scheduleReconnect();
        }else if(wsConnected&&performance.now()-wsLastActivity>5000){
          console.log('[WS] Stale, reconnecting');
          killWs();scheduleReconnect();
        }
      }
      var overlay=document.getElementById('disconnectOverlay');
      if(overlay){
        if(performance.now()-lastDataMs>6000) overlay.classList.add('show');
        else overlay.classList.remove('show');
      }
    }
    setInterval(linkSupervisor,1000);

    /* Idle keepalive: with no gamepad active nothing else flows
       client->robot, and the robot would release the owner slot. */
    setInterval(function(){
      if(wsConnected&&wsJoystick&&wsJoystick.readyState===1&&
         performance.now()-lastGamepadSend>2000){
        try{wsJoystick.send(new Uint8Array([0x50]));}catch(e){}
      }
    },2000);

    function packJoystickBinary(gp){
      /* Protocol caps at 20/20 — the robot rejects larger frames, so a
         HOTAS/button-box with more inputs must be clamped, not sent
         whole (it would silently disconnect the controls). */
      var nA=Math.min(gp.axes.length,20);
      var nB=Math.min(gp.buttons.length,20);
      var btnBytes=Math.ceil(nB/8);
      var buf=new ArrayBuffer(4+nA*2+btnBytes);
      var view=new DataView(buf);
      view.setUint8(0,0x4A);
      view.setUint8(1,nA);
      view.setUint8(2,nB);
      view.setUint8(3,0);
      for(var i=0;i<nA;i++){
        var v=Math.max(-1,Math.min(1,gp.axes[i]));
        view.setInt16(4+i*2,Math.round(v*32767),false);
      }
      var btnOff=4+nA*2;
      for(var i=0;i<nB;i++){
        if(gp.buttons[i].pressed){
          view.setUint8(btnOff+Math.floor(i/8),view.getUint8(btnOff+Math.floor(i/8))|(1<<(i%8)));
        }
      }
      return buf;
    }

    function sendGamepadData(gp){
      var now=performance.now();
      if(gamepadSending||(now-lastGamepadSend)<GAMEPAD_SEND_INTERVAL) return;
      gamepadSending=true;
      lastGamepadSend=now;
      if(wsConnected&&wsJoystick&&wsJoystick.readyState===1){
        try{
          wsJoystick.send(packJoystickBinary(gp));
          gamepadSending=false;
          return;
        }catch(e){killWs();scheduleReconnect();}
      }
      var ac=new AbortController();
      var tid=setTimeout(function(){ac.abort();},2000);
      var data={axes:Array.from(gp.axes),buttons:Array.from(gp.buttons).map(function(b){return b.pressed;})};
      fetch("/updateController",{
        method:"POST",
        headers:{"Content-Type":"application/json"},
        body:JSON.stringify(data),
        signal:ac.signal
      }).then(function(){clearTimeout(tid);}).catch(function(){}).then(function(){
        gamepadSending=false;
      });
    }

    function displayJoystickData(gp){
      document.getElementById('joystickStatus').textContent=gp.id+'\nAxes:'+gp.axes.length+' Buttons:'+gp.buttons.length;
      document.getElementById('axisData').textContent=Array.from(gp.axes).map(function(v,i){return 'Axis '+i+': '+v.toFixed(2);}).join("\n");
      document.getElementById('buttonData').textContent=Array.from(gp.buttons).map(function(b,i){return (b.pressed?"\u25CF":"\u25CB")+' '+i;}).join("  ");
      document.getElementById('joystickStatusTxt').textContent="Connected";
      updateJoyVisuals(gp);
    }

    function gamepadLoop(){
      updateGamepads();
      rebuildGamepadSelect();
      var txt=document.getElementById('joystickStatusTxt');
      var hint=document.getElementById('gamepadHint');
      var miniStatus=document.getElementById('miniJoyStatus');
      var gp=gamepads[selectedGamepadIndex];
      if(gp){
        if(txt) txt.textContent="Connected";
        if(hint) hint.style.display="none";
        if(miniStatus) miniStatus.textContent="Connected";
        gamepadDetected=true;
        displayJoystickData(gp);
        updateMiniJoy(gp);
        sendGamepadData(gp);
      }else{
        if(txt) txt.textContent="Not Connected";
        if(hint&&!gamepadDetected) hint.style.display="block";
        if(miniStatus) miniStatus.textContent="Not Connected";
        document.getElementById('joystickStatus').textContent="No gamepad selected.";
        document.getElementById('axisData').textContent="No axis data...";
        document.getElementById('buttonData').textContent="No button data...";
        updateJoyVisuals(null);
        updateMiniJoy(null);
      }
      requestAnimationFrame(gamepadLoop);
    }

    window.addEventListener('gamepadconnected',function(){
      updateGamepads();
      rebuildGamepadSelect();
    });
    window.addEventListener('gamepaddisconnected',function(e){
      delete gamepads[e.gamepad.index];
    });

    /* ===== TELEMETRY ===== */
    /* Pushed by the robot over WS ('T' frames) whenever the buffer
       changes — no polling. */
    function renderTelemetry(text){
      var el=document.getElementById('telemetryOutput');
      if(el&&text){
        el.textContent=text;
        if(autoScroll) el.scrollTop=el.scrollHeight;
      }
    }
    function clearTelemetry(){
      var el=document.getElementById('telemetryOutput');
      if(el) el.textContent='';
    }
    function updateAutoScrollButton(){
      var btn=document.getElementById('autoScrollToggle');
      if(!btn) return;
      btn.textContent=autoScroll?'Auto-scroll: ON':'Auto-scroll: OFF';
    }
    function toggleAutoScroll(){
      autoScroll=!autoScroll;
      updateAutoScrollButton();
      if(autoScroll){
        var el=document.getElementById('telemetryOutput');
        if(el) el.scrollTop=el.scrollHeight;
      }
    }
    /* HTTP fallback: only while the WS is down. One state+health pair
       per second keeps the UI alive; joystick falls back to HTTP POST
       in sendGamepadData. */
    setInterval(function(){
      if(!wsConnected){fetchState();fetchHealth();}
    },1000);
    /* RTT sample while WS is up: a single /health every 10s feeds the
       ping display; everything else arrives over WS. */
    setInterval(function(){
      if(wsConnected) fetchHealth();
    },10000);

    /* ===== CONNECTION HEALTH ===== */
    var healthFailCount=0;
    var lastPingMs=0;
    var lastRssi=-100;
    var lastHeap=0;
    var lastUpMs=0;
    var lastDm=false;
    var lastJoyAge=-1;
    var lastSta=0;
    var lastDisc=0;

    /* Fed by 'S' WS frames normally; by fetchHealth over HTTP otherwise. */
    function applyHealth(data){
      healthFailCount=0;
      lastRssi=(typeof data.rssi==='number')?data.rssi:-100;
      lastHeap=(typeof data.heap==='number')?data.heap:0;
      lastUpMs=(typeof data.up==='number')?data.up:0;
      lastDm=!!data.dm;
      if(typeof data.joyAgeMs==='number') lastJoyAge=data.joyAgeMs;
      if(typeof data.sta==='number') lastSta=data.sta;
      if(typeof data.disc==='number') lastDisc=data.disc;
      updateConnUI(true);
      updateDebugPanel();
    }

    var fetchHealthBusy=false;
    function fetchHealth(){
      if(fetchHealthBusy) return;
      fetchHealthBusy=true;
      var start=performance.now();
      var ac=new AbortController();
      var tid=setTimeout(function(){ac.abort();},3000);
      fetch('/health',{signal:ac.signal}).then(function(r){
        clearTimeout(tid);
        if(!r.ok) throw new Error('health');
        return r.json();
      }).then(function(data){
        fetchHealthBusy=false;
        lastPingMs=Math.round(performance.now()-start);
        lastDataMs=performance.now();
        healthFailCount=0;
        applyHealth(data);
      }).catch(function(){
        fetchHealthBusy=false;
        healthFailCount++;
        updateConnUI(false);
        updateDebugPanel();
      });
    }

    function updateConnUI(ok){
      var dot=document.getElementById('connDot');
      var ping=document.getElementById('connPing');
      var heap=document.getElementById('connHeap');
      var signal=document.getElementById('connSignal');
      if(!dot||!ping||!signal) return;

      if(!ok){
        dot.className='conn-dot bad';
        ping.textContent='--';
        if(heap) heap.textContent='--';
        signal.querySelectorAll('.bar').forEach(function(b){b.classList.remove('active');});
        return;
      }

      ping.textContent=lastPingMs+'ms';
      if(heap){
        if(lastHeap>0&&infoTotalHeap>0) heap.textContent=Math.round(lastHeap/1024)+'/'+Math.round(infoTotalHeap/1024)+'KB';
        else if(lastHeap>0) heap.textContent=Math.round(lastHeap/1024)+'KB';
        else heap.textContent='--';
      }

      var bars=0;
      if(lastRssi>-50) bars=4;
      else if(lastRssi>-60) bars=3;
      else if(lastRssi>-70) bars=2;
      else if(lastRssi>-80) bars=1;

      var barEls=signal.querySelectorAll('.bar');
      barEls.forEach(function(b,i){
        if(i<bars) b.classList.add('active');
        else b.classList.remove('active');
      });

      if(bars>=3) dot.className='conn-dot';
      else if(bars>=2) dot.className='conn-dot warn';
      else dot.className='conn-dot bad';
    }

    /* ===== DEBUG PANEL (Logs page) ===== */
    var infoTotalHeap=0;
    var infoTotalFlash=0;
    var infoSketchSize=0;

    function fmtKB(b){return b>0?Math.round(b/1024)+' KB':'--';}

    function updateDebugPanel(){
      var el=function(id){return document.getElementById(id);};
      if(el('dbgRssi')) el('dbgRssi').textContent=lastRssi+' dBm';
      if(el('dbgPing')) el('dbgPing').textContent=healthFailCount>0?'--':lastPingMs+' ms';
      if(el('dbgHeap')){
        if(lastHeap>0&&infoTotalHeap>0) el('dbgHeap').textContent=fmtKB(lastHeap)+' / '+fmtKB(infoTotalHeap);
        else if(lastHeap>0) el('dbgHeap').textContent=fmtKB(lastHeap);
        else el('dbgHeap').textContent='--';
      }
      if(el('dbgUptime')&&lastUpMs>0){
        var totalSec=Math.floor(lastUpMs/1000);
        var h=Math.floor(totalSec/3600);
        var m=Math.floor((totalSec%3600)/60);
        var s=totalSec%60;
        el('dbgUptime').textContent=h+'h '+m+'m '+s+'s';
      }
      if(el('dbgWs')) el('dbgWs').textContent=wsConnected?'Connected':'Disconnected';
      if(el('dbgDm')) el('dbgDm').textContent=lastDm?'YES':'No';
      if(el('dbgJoyAge')) el('dbgJoyAge').textContent=(lastJoyAge>=0)?(lastJoyAge+' ms'):'--';
      if(el('dbgSta')) el('dbgSta').textContent=String(lastSta);
      if(el('dbgDisc')) el('dbgDisc').textContent=lastDisc?('reason '+lastDisc):'--';
    }

    function fetchInfo(){
      fetch('/info').then(function(r){
        if(!r.ok) return;
        return r.json();
      }).then(function(data){
        if(!data) return;
        var el=function(id){return document.getElementById(id);};
        if(el('dbgSsid')) el('dbgSsid').textContent=data.ssid||'--';
        if(el('dbgCh')) el('dbgCh').textContent=data.ch?(data.ch+(data.chSource?' ('+data.chSource+')':'')):'--';
        var chSel=document.getElementById('chSelect');
        if(chSel&&typeof data.ch==='number') chSel.value=String(data.ch);
        if(el('dbgIp')) el('dbgIp').textContent=data.ip||'--';
        if(el('dbgChip')) el('dbgChip').textContent=data.chip||'--';
        if(el('dbgCpu')) el('dbgCpu').textContent=data.cpuMhz?data.cpuMhz+' MHz':'--';
        if(el('dbgSdk')) el('dbgSdk').textContent=data.sdk||'--';
        infoTotalHeap=data.totalHeap||0;
        infoTotalFlash=data.totalFlash||0;
        infoSketchSize=data.sketchSize||0;
        if(el('dbgFlash')&&infoTotalFlash>0){
          el('dbgFlash').textContent=fmtKB(infoSketchSize)+' / '+fmtKB(infoTotalFlash);
        }
        if(el('dbgFreeSketch')) el('dbgFreeSketch').textContent=fmtKB(data.freeSketch||0);
        if(el('dbgPsram')){
          var ps=data.psram||0;
          el('dbgPsram').textContent=ps>0?fmtKB(ps):'None';
        }
      }).catch(function(){});
    }

    /* ===== CHANNEL SWITCH (Logs page) ===== */
    function applyChannel(){
      var sel=document.getElementById('chSelect');
      var status=document.getElementById('chStatus');
      if(!sel) return;
      var ch=parseInt(sel.value,10);
      if(isNaN(ch)) return;
      if(status) status.textContent='...';
      fetch('/setChannel?ch='+ch).then(function(r){
        if(!r.ok) throw new Error('setChannel '+r.status);
        return r.json();
      }).then(function(d){
        if(!status) return;
        if(d.live) status.textContent='Kanal '+d.ch+' (canlı geçiş)';
        else if(d.ch===0) status.textContent='Varsayılan — yeniden başlatınca';
        else status.textContent='Kanal '+d.ch+' — kayıtlı';
        fetchInfo();
      }).catch(function(){
        if(status) status.textContent='Hata — tekrar deneyin';
      });
    }

    /* ===== AUTO PERIOD INPUT ===== */
    document.getElementById('autoPeriod').addEventListener('input',function(e){
      var duration=parseFloat(e.target.value)||0;
      if(autoTimer) autoRemaining=Math.min(autoRemaining,duration);
      else autoRemaining=duration;
      updateAutoDisplay();
    });

    document.getElementById('enableAutonomous').addEventListener('change',function(e){
      if(!e.target.checked){
        if(controlState==="running"){
          fetch('/robotControl?cmd=cancelAuto').then(function(r){
            if(!r.ok) throw new Error("Cancel auto failed");
            stopAutoTimer();
            setPhaseDisplay('teleop');
          }).catch(function(err){console.error(err);});
        }else{
          stopAutoTimer();
        }
      }
    });

    /* ===== INIT ===== */
    window.addEventListener('load',function(){
      ensureJoyButtons(DEFAULT_BUTTON_COUNT);
      updateJoyVisuals(null);
      updateMiniJoy(null);
      updateAutoScrollButton();
      autoRemaining=parseFloat(document.getElementById('autoPeriod').value)||0;
      updateAutoDisplay();
      setPhaseDisplay('standby');
      requestAnimationFrame(gamepadLoop);
      connectWebSocket();
      loadAutoModes();
      fetchState();
      fetchHealth();
      fetchInfo();
    });
</script>
</body>
</html>

)=====";
