import { useEffect, useState } from "react";
import "./App.css";

const API = "http://localhost:5000";

function App() {
  const [status, setStatus] = useState({
    connected: false,
    power: false,
    fan: "OFF",
    fanLevel: 0,
    mode: "MANUAL",
    dust: 0,
    airQuality: "LOW",
    timer: 0,
    autoMeasuring: false,
    servoAngle: 0,
  });

  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  // --------------------------------------------------
  // GET ESP32 STATUS
  // --------------------------------------------------

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
      setStatus((prev) => ({
        ...prev,
        connected: false,
      }));

      setError("Unable to connect to EcoAir");
    }
  };

  // --------------------------------------------------
  // INITIAL STATUS + AUTO REFRESH
  // --------------------------------------------------

  useEffect(() => {
    getStatus();

    const interval = setInterval(() => {
      getStatus();
    }, 2000);

    return () => clearInterval(interval);
  }, []);

  // --------------------------------------------------
  // POWER
  // --------------------------------------------------

  const changePower = async () => {
    try {
      setLoading(true);

      const response = await fetch(`${API}/api/power`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          state: !status.power,
        }),
      });

      if (!response.ok) {
        throw new Error("Power command failed");
      }

      await getStatus();
    } catch (err) {
      setError("Power command failed");
    } finally {
      setLoading(false);
    }
  };

  // --------------------------------------------------
  // FAN SPEED
  // --------------------------------------------------

  const changeFan = async (level) => {
    try {
      setLoading(true);

      const response = await fetch(`${API}/api/fan`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          speed: level,
        }),
      });

      if (!response.ok) {
        throw new Error("Fan command failed");
      }

      await getStatus();
    } catch (err) {
      setError("Fan command failed");
    } finally {
      setLoading(false);
    }
  };

  // --------------------------------------------------
  // MODE
  // --------------------------------------------------

  const changeMode = async (mode) => {
    try {
      setLoading(true);

      const response = await fetch(`${API}/api/mode`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          mode,
        }),
      });

      if (!response.ok) {
        throw new Error("Mode command failed");
      }

      await getStatus();
    } catch (err) {
      setError("Mode command failed");
    } finally {
      setLoading(false);
    }
  };

  // --------------------------------------------------
  // TIMER
  // --------------------------------------------------

  const changeTimer = async (minutes) => {
    try {
      setLoading(true);

      const response = await fetch(`${API}/api/timer`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          minutes,
        }),
      });

      if (!response.ok) {
        throw new Error("Timer command failed");
      }

      await getStatus();
    } catch (err) {
      setError("Timer command failed");
    } finally {
      setLoading(false);
    }
  };

  // --------------------------------------------------
  // AIR QUALITY CLASS
  // --------------------------------------------------

  const airQualityClass =
    status.airQuality === "HIGH"
      ? "high"
      : status.airQuality === "MEDIUM"
      ? "medium"
      : "low";

  return (
    <div className="app">

      {/* --------------------------------------------
          HEADER
      --------------------------------------------- */}

      <header className="header">

        <div className="brand">
          <div className="brand-icon">
            EA
          </div>

          <div>
            <h1 className="EcoAir">EcoAir</h1>
            <p>Smart Air Purifier</p>
          </div>
        </div>

        <div
          className={`connection ${
            status.connected ? "online" : "offline"
          }`}
        >
          <span className="connection-dot"></span>

          {status.connected ? "ESP32 Online" : "Offline"}
        </div>

      </header>


      {/* --------------------------------------------
          ERROR
      --------------------------------------------- */}

      {error && (
        <div className="error-box">
          {error}
        </div>
      )}


      {/* --------------------------------------------
          MAIN DASHBOARD
      --------------------------------------------- */}

      <main className="dashboard">


        {/* ------------------------------------------
            AIR QUALITY CARD
        ------------------------------------------- */}

        <section className="air-card">

          <div className="air-card-top">

            <div>
              <p className="section-label">
                AIR QUALITY
              </p>

              <h2 className={airQualityClass}>
                {status.airQuality || "LOW"}
              </h2>
            </div>


            <div className={`air-circle ${airQualityClass}`}>
              <span>
                {Math.round(Number(status.dust || 0))}
              </span>

              <small>
                µg/m³
              </small>
            </div>

          </div>


          <div className="air-details">

            <div className="detail">
              <span>Dust Density</span>

              <strong>
                {Number(status.dust || 0).toFixed(1)}
                {" "}
                µg/m³
              </strong>
            </div>


            <div className="detail">
              <span>Fan Speed</span>

              <strong>
                {status.fan || "OFF"}
              </strong>
            </div>


            <div className="detail">
              <span>Servo Position</span>

              <strong>
                {status.servoAngle ?? 0}°
              </strong>
            </div>

          </div>

        </section>


        {/* ------------------------------------------
            POWER
        ------------------------------------------- */}

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
                status.power ? "active" : ""
              }`}
            >
              {status.power ? "ON" : "OFF"}
            </div>

          </div>


          <button
            className={`power-button ${
              status.power ? "active" : ""
            }`}
            onClick={changePower}
            disabled={loading || !status.connected}
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


        {/* ------------------------------------------
            FAN SPEED
        ------------------------------------------- */}

        <section className="card">

          <div className="card-header">

            <div>
              <p className="section-label">
                FAN SPEED
              </p>

              <h3>
                {status.fan || "OFF"}
              </h3>
            </div>

            <div className="servo-display">
              {status.servoAngle ?? 0}°
            </div>

          </div>


          <div className="button-grid">

            <button
              className={`control-button ${
                status.fanLevel === 0
                  ? "selected"
                  : ""
              }`}
              onClick={() => changeFan(0)}
              disabled={loading || !status.connected}
            >
              <span>OFF</span>
              <small>0°</small>
            </button>


            <button
              className={`control-button ${
                status.fanLevel === 1
                  ? "selected"
                  : ""
              }`}
              onClick={() => changeFan(1)}
              disabled={loading || !status.connected}
            >
              <span>LOW</span>
              <small>95°</small>
            </button>


            <button
              className={`control-button ${
                status.fanLevel === 2
                  ? "selected"
                  : ""
              }`}
              onClick={() => changeFan(2)}
              disabled={loading || !status.connected}
            >
              <span>MEDIUM</span>
              <small>120°</small>
            </button>


            <button
              className={`control-button ${
                status.fanLevel === 3
                  ? "selected"
                  : ""
              }`}
              onClick={() => changeFan(3)}
              disabled={loading || !status.connected}
            >
              <span>HIGH</span>
              <small>140°</small>
            </button>

          </div>

        </section>


        {/* ------------------------------------------
            MODE
        ------------------------------------------- */}

        <section className="card">

          <div className="card-header">

            <div>
              <p className="section-label">
                OPERATING MODE
              </p>

              <h3>
                {status.mode || "MANUAL"}
              </h3>
            </div>

          </div>


          <div className="mode-buttons">

            <button
              className={`mode-button ${
                status.mode === "MANUAL"
                  ? "selected"
                  : ""
              }`}
              onClick={() => changeMode("MANUAL")}
              disabled={loading || !status.connected}
            >
              <strong>
                MANUAL
              </strong>

              <span>
                Control fan yourself
              </span>
            </button>


            <button
              className={`mode-button ${
                status.mode === "AUTO"
                  ? "selected"
                  : ""
              }`}
              onClick={() => changeMode("AUTO")}
              disabled={loading || !status.connected}
            >
              <strong>
                AUTO
              </strong>

              <span>
                Automatic dust control
              </span>
            </button>

          </div>


          {status.autoMeasuring && (
            <div className="auto-message">
              <span className="pulse"></span>

              Measuring air quality...
            </div>
          )}

        </section>


        {/* ------------------------------------------
            TIMER
        ------------------------------------------- */}

        <section className="card">

          <div className="card-header">

            <div>
              <p className="section-label">
                TIMER
              </p>

              <h3>
                {status.timer
                  ? `${status.timer} min`
                  : "OFF"}
              </h3>
            </div>

          </div>


          <div className="timer-buttons">

            <button
              className={`timer-button ${
                status.timer === 0
                  ? "selected"
                  : ""
              }`}
              onClick={() => changeTimer(0)}
              disabled={loading || !status.connected}
            >
              OFF
            </button>


            <button
              className={`timer-button ${
                status.timer === 30
                  ? "selected"
                  : ""
              }`}
              onClick={() => changeTimer(30)}
              disabled={loading || !status.connected}
            >
              30 MIN
            </button>


            <button
              className={`timer-button ${
                status.timer === 60
                  ? "selected"
                  : ""
              }`}
              onClick={() => changeTimer(60)}
              disabled={loading || !status.connected}
            >
              60 MIN
            </button>

          </div>

          {status.timer > 0 && (
            <p className="timer-info">
              Purifier timer is active
            </p>
          )}

        </section>


        {/* ------------------------------------------
            SYSTEM INFO
        ------------------------------------------- */}

        <section className="system-card">

          <div className="system-item">
            <span>Controller</span>
            <strong>ESP32</strong>
          </div>

          <div className="system-item">
            <span>Network</span>
            <strong>Local Wi-Fi</strong>
          </div>

          <div className="system-item">
            <span>Sensor</span>
            <strong>GP2Y1010AU0F</strong>
          </div>

          <div className="system-item">
            <span>Servo</span>
            <strong>0° / 95° / 120° / 140°</strong>
          </div>

        </section>

      </main>


      {/* --------------------------------------------
          FOOTER
      --------------------------------------------- */}

      <footer>
        <span>EcoAir</span>
        <span>Smart Air Purification System</span>
      </footer>

    </div>
  );
}

export default App;