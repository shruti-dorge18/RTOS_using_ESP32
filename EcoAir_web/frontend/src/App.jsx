import { useEffect, useState } from "react";
import "./App.css";

const API = "http://localhost:5000";

function App() {
  const [status, setStatus] = useState({
    connected: false,
    power: false,
    fan: "OFF",
    fanLevel: 0,
    mode: "manual",
    dust: 0,
    airQuality: "LOW",
    timer: 0,
    autoMeasuring: false,
    servoAngle: 0,
  });

  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  // ============================================================
  // GET ESP32 STATUS
  // ============================================================

  const getStatus = async () => {
    try {
      const response = await fetch(`${API}/api/status`);

      if (!response.ok) {
        throw new Error("ESP32 is not reachable");
      }

      const data = await response.json();

      setStatus((prev) => ({
        ...prev,
        ...data,
        connected: true,
      }));

      setError("");
    } catch (err) {
      console.error("STATUS ERROR:", err);

      setStatus((prev) => ({
        ...prev,
        connected: false,
      }));

      setError("Unable to connect to EcoAir");
    }
  };

  // ============================================================
  // INITIAL STATUS + AUTO REFRESH
  // ============================================================

  useEffect(() => {
    getStatus();

    const interval = setInterval(() => {
      getStatus();
    }, 2000);

    return () => clearInterval(interval);
  }, []);

  // ============================================================
  // POWER
  // ============================================================

  const changePower = async () => {
    try {
      setLoading(true);
      setError("");

      const response = await fetch(`${API}/api/power`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          state: !status.power,
        }),
      });

      const data = await response.json();

      if (!response.ok) {
        throw new Error(
          data.error || "Power command failed"
        );
      }

      await getStatus();
    } catch (err) {
      console.error("POWER ERROR:", err);

      setError(
        err.message || "Power command failed"
      );
    } finally {
      setLoading(false);
    }
  };

  // ============================================================
  // FAN SPEED
  // ============================================================

  const changeFan = async (level) => {
    try {
      setLoading(true);
      setError("");

      /*
        Website fan levels:

        0 = OFF
        1 = LOW
        2 = MEDIUM
        3 = HIGH

        Express converts these to:

        0   -> OFF
        25  -> LOW
        50  -> MEDIUM
        100 -> HIGH
      */

      const response = await fetch(`${API}/api/fan`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          speed: level,
        }),
      });

      const data = await response.json();

      if (!response.ok) {
        throw new Error(
          data.error || "Fan command failed"
        );
      }

      await getStatus();
    } catch (err) {
      console.error("FAN ERROR:", err);

      setError(
        err.message || "Fan command failed"
      );
    } finally {
      setLoading(false);
    }
  };

  // ============================================================
  // MODE
  // ============================================================

  const changeMode = async (mode) => {
    try {
      setLoading(true);
      setError("");

      /*
        IMPORTANT:

        ESP32 expects lowercase values:

        "manual"
        "auto"

        Therefore we always convert the value to lowercase.
      */

      const espMode = mode.toLowerCase();

      console.log(
        `Changing EcoAir mode to: ${espMode}`
      );

      const response = await fetch(`${API}/api/mode`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          mode: espMode,
        }),
      });

      const data = await response.json();

      console.log(
        "ESP32 mode response:",
        data
      );

      if (!response.ok) {
        throw new Error(
          data.error || "Mode command failed"
        );
      }

      await getStatus();
    } catch (err) {
      console.error("MODE ERROR:", err);

      setError(
        err.message || "Mode command failed"
      );
    } finally {
      setLoading(false);
    }
  };

  // ============================================================
  // TIMER
  // ============================================================

  const changeTimer = async (minutes) => {
    try {
      setLoading(true);
      setError("");

      const response = await fetch(`${API}/api/timer`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          minutes: minutes,
        }),
      });

      const data = await response.json();

      if (!response.ok) {
        throw new Error(
          data.error || "Timer command failed"
        );
      }

      await getStatus();
    } catch (err) {
      console.error("TIMER ERROR:", err);

      setError(
        err.message || "Timer command failed"
      );
    } finally {
      setLoading(false);
    }
  };

  // ============================================================
  // AIR QUALITY CLASS
  // ============================================================

  const airQuality =
    String(
      status.airQuality || "LOW"
    ).toUpperCase();

  const airQualityClass =
    airQuality === "HIGH"
      ? "high"
      : airQuality === "MEDIUM"
      ? "medium"
      : "low";

  // ============================================================
  // MODE NORMALIZATION
  // ============================================================

  const currentMode =
    String(
      status.mode || "manual"
    ).toLowerCase();

  // ============================================================
  // FAN LEVEL NORMALIZATION
  // ============================================================

  const currentFanLevel =
    Number(
      status.fanLevel ?? 0
    );

  // ============================================================
  // TIMER NORMALIZATION
  // ============================================================

  const currentTimer =
    Number(
      status.timer ?? 0
    );

  // ============================================================
  // RENDER
  // ============================================================

  return (
    <div className="app">

      {/* ======================================================
          HEADER
      ======================================================= */}

      <header className="header">

        <div className="brand">

          <div className="brand-icon">
            EA
          </div>

          <div>
            <h1>
              EcoAir
            </h1>

            <p>
              Smart Air Purifier
            </p>
          </div>

        </div>


        <div
          className={`connection ${
            status.connected
              ? "online"
              : "offline"
          }`}
        >

          <span className="connection-dot"></span>

          {status.connected
            ? "ESP32 Online"
            : "Offline"}

        </div>

      </header>


      {/* ======================================================
          ERROR MESSAGE
      ======================================================= */}

      {error && (
        <div className="error-box">
          {error}
        </div>
      )}


      {/* ======================================================
          DASHBOARD
      ======================================================= */}

      <main className="dashboard">


        {/* ====================================================
            AIR QUALITY
        ===================================================== */}

        <section className="air-card">

          <div className="air-card-top">

            <div>

              <p className="section-label">
                AIR QUALITY
              </p>

              <h2
                className={
                  airQualityClass
                }
              >
                {airQuality}
              </h2>

            </div>


            <div
              className={`air-circle ${
                airQualityClass
              }`}
            >

              <span>
                {Math.round(
                  Number(
                    status.dust || 0
                  )
                )}
              </span>

              <small>
                µg/m³
              </small>

            </div>

          </div>


          <div className="air-details">


            <div className="detail">

              <span>
                Dust Density
              </span>

              <strong>
                {Number(
                  status.dust || 0
                ).toFixed(1)}{" "}
                µg/m³
              </strong>

            </div>


            <div className="detail">

              <span>
                Fan Speed
              </span>

              <strong>
                {String(
                  status.fan || "OFF"
                ).toUpperCase()}
              </strong>

            </div>


            <div className="detail">

              <span>
                Servo Position
              </span>

              <strong>
                {status.servoAngle ?? 0}°
              </strong>

            </div>

          </div>

        </section>


        {/* ====================================================
            POWER
        ===================================================== */}

        <section className="card">

          <div className="card-header">

            <div>

              <p className="section-label">
                POWER
              </p>

              <h3>
                Purifier
              </h3>

            </div>


            <div
              className={`power-status ${
                status.power
                  ? "active"
                  : ""
              }`}
            >
              {status.power
                ? "ON"
                : "OFF"}
            </div>

          </div>


          <button
            className={`power-button ${
              status.power
                ? "active"
                : ""
            }`}
            onClick={changePower}
            disabled={
              loading ||
              !status.connected
            }
          >

            <span className="power-symbol">
              ⏻
            </span>

            <span>
              {status.power
                ? "Turn Off"
                : "Turn On"}
            </span>

          </button>

        </section>


        {/* ====================================================
            FAN SPEED
        ===================================================== */}

        <section className="card">

          <div className="card-header">

            <div>

              <p className="section-label">
                FAN SPEED
              </p>

              <h3>
                {String(
                  status.fan || "OFF"
                ).toUpperCase()}
              </h3>

            </div>


            <div className="servo-display">
              {status.servoAngle ?? 0}°
            </div>

          </div>


          <div className="button-grid">


            {/* OFF */}

            <button
              className={`control-button ${
                currentFanLevel === 0
                  ? "selected"
                  : ""
              }`}
              onClick={() =>
                changeFan(0)
              }
              disabled={
                loading ||
                !status.connected
              }
            >

              <span>
                OFF
              </span>

              <small>
                0°
              </small>

            </button>


            {/* LOW */}

            <button
              className={`control-button ${
                currentFanLevel === 1
                  ? "selected"
                  : ""
              }`}
              onClick={() =>
                changeFan(1)
              }
              disabled={
                loading ||
                !status.connected
              }
            >

              <span>
                LOW
              </span>

              <small>
                95°
              </small>

            </button>


            {/* MEDIUM */}

            <button
              className={`control-button ${
                currentFanLevel === 2
                  ? "selected"
                  : ""
              }`}
              onClick={() =>
                changeFan(2)
              }
              disabled={
                loading ||
                !status.connected
              }
            >

              <span>
                MEDIUM
              </span>

              <small>
                120°
              </small>

            </button>


            {/* HIGH */}

            <button
              className={`control-button ${
                currentFanLevel === 3
                  ? "selected"
                  : ""
              }`}
              onClick={() =>
                changeFan(3)
              }
              disabled={
                loading ||
                !status.connected
              }
            >

              <span>
                HIGH
              </span>

              <small>
                140°
              </small>

            </button>

          </div>

        </section>


        {/* ====================================================
            OPERATING MODE
        ===================================================== */}

        <section className="card">

          <div className="card-header">

            <div>

              <p className="section-label">
                OPERATING MODE
              </p>

              <h3>
                {currentMode.toUpperCase()}
              </h3>

            </div>

          </div>


          <div className="mode-buttons">


            {/* MANUAL */}

            <button
              className={`mode-button ${
                currentMode === "manual"
                  ? "selected"
                  : ""
              }`}
              onClick={() =>
                changeMode("manual")
              }
              disabled={
                loading ||
                !status.connected
              }
            >

              <strong>
                MANUAL
              </strong>

              <span>
                Control fan yourself
              </span>

            </button>


            {/* AUTO */}

            <button
              className={`mode-button ${
                currentMode === "auto"
                  ? "selected"
                  : ""
              }`}
              onClick={() =>
                changeMode("auto")
              }
              disabled={
                loading ||
                !status.connected
              }
            >

              <strong>
                AUTO
              </strong>

              <span>
                Automatic dust control
              </span>

            </button>

          </div>


          {/* AUTO MEASUREMENT */}

          {status.autoMeasuring && (
            <div className="auto-message">

              <span className="pulse"></span>

              Measuring air quality...

            </div>
          )}

        </section>


        {/* ====================================================
            TIMER
        ===================================================== */}

        <section className="card">

          <div className="card-header">

            <div>

              <p className="section-label">
                TIMER
              </p>

              <h3>
                {currentTimer > 0
                  ? `${currentTimer} min`
                  : "OFF"}
              </h3>

            </div>

          </div>


          <div className="timer-buttons">


            {/* TIMER OFF */}

            <button
              className={`timer-button ${
                currentTimer === 0
                  ? "selected"
                  : ""
              }`}
              onClick={() =>
                changeTimer(0)
              }
              disabled={
                loading ||
                !status.connected
              }
            >
              OFF
            </button>


            {/* 30 MIN */}

            <button
              className={`timer-button ${
                currentTimer === 30
                  ? "selected"
                  : ""
              }`}
              onClick={() =>
                changeTimer(30)
              }
              disabled={
                loading ||
                !status.connected
              }
            >
              30 MIN
            </button>


            {/* 60 MIN */}

            <button
              className={`timer-button ${
                currentTimer === 60
                  ? "selected"
                  : ""
              }`}
              onClick={() =>
                changeTimer(60)
              }
              disabled={
                loading ||
                !status.connected
              }
            >
              60 MIN
            </button>

          </div>


          {currentTimer > 0 && (
            <p className="timer-info">
              Purifier timer is active
            </p>
          )}

        </section>


        {/* ====================================================
            SYSTEM INFORMATION
        ===================================================== */}

        <section className="system-card">


          <div className="system-item">

            <span>
              Controller
            </span>

            <strong>
              ESP32
            </strong>

          </div>


          <div className="system-item">

            <span>
              Network
            </span>

            <strong>
              Local Wi-Fi
            </strong>

          </div>


          <div className="system-item">

            <span>
              Dust Sensor
            </span>

            <strong>
              GP2Y1010AU0F
            </strong>

          </div>


          <div className="system-item">

            <span>
              Servo
            </span>

            <strong>
              0° / 95° / 120° / 140°
            </strong>

          </div>

        </section>

      </main>


      {/* ======================================================
          FOOTER
      ======================================================= */}

      <footer>

        <span>
          EcoAir
        </span>

        <span>
          Smart Air Purification System
        </span>

      </footer>

    </div>
  );
}

export default App;