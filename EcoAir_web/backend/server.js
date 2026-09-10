const express = require("express");
const cors = require("cors");

const app = express();

const PORT = 5000;

/*
=========================================================
ESP32 STATIC IP
=========================================================

ESP32:
10.16.90.50

Gateway:
10.16.90.203
*/

const ESP32_URL = "http://10.16.90.50";


app.use(cors());
app.use(express.json());


/* =========================================================
   ECRAFTONIC AIR-1
   ESP32 PROXY BACKEND
   ========================================================= */


/* =========================================================
   ESP32 REQUEST HELPER
   ========================================================= */

async function esp32Request(path, options = {}) {

    const response = await fetch(
        `${ESP32_URL}${path}`,
        {
            ...options,

            headers: {
                "Content-Type": "application/json",
                ...(options.headers || {})
            }
        }
    );


    const text = await response.text();

    let data;


    try {

        data = JSON.parse(text);

    } catch {

        data = {
            raw: text
        };

    }


    if (!response.ok) {

        throw new Error(
            data.error ||
            `ESP32 returned HTTP ${response.status}`
        );

    }


    return data;
}


/* =========================================================
   STATUS
   ========================================================= */

app.get("/api/status", async (req, res) => {

    try {

        const data =
            await esp32Request(
                "/api/status"
            );


        res.json(data);

    }

    catch (error) {

        console.error(
            "ESP32 STATUS ERROR:",
            error.message
        );


        res.status(503).json({

            connected: false,

            error:
                "ESP32 is not reachable",

            details:
                error.message

        });

    }

});


/* =========================================================
   POWER
   ========================================================= */

app.post("/api/power", async (req, res) => {

    try {

        const data =
            await esp32Request(
                "/api/power",
                {
                    method: "POST",

                    body: JSON.stringify({
                        power:
                            Boolean(req.body.state)
                    })
                }
            );


        res.json(data);

    }

    catch (error) {

        console.error(
            "ESP32 POWER ERROR:",
            error.message
        );


        res.status(503).json({

            connected: false,

            error:
                error.message

        });

    }

});


/* =========================================================
   FAN
   =========================================================

   React sends:

   0 = OFF
   1 = LOW
   2 = MEDIUM
   3 = HIGH

   Express converts to ESP32:

   0   = OFF
   25  = LOW
   50  = MEDIUM
   100 = HIGH

   ESP32 converts:

   0   -> 0°
   25  -> 95°
   50  -> 120°
   100 -> 140°
   ========================================================= */

app.post("/api/fan", async (req, res) => {

    try {

        const level =
            Number(req.body.speed);


        let espSpeed;


        switch (level) {

            case 0:

                espSpeed = 0;

                break;


            case 1:

                espSpeed = 25;

                break;


            case 2:

                espSpeed = 50;

                break;


            case 3:

                espSpeed = 100;

                break;


            default:

                return res.status(400).json({

                    error:
                        "Fan speed must be 0, 1, 2 or 3"

                });

        }


        console.log(
            `Website fan level: ${level} -> ESP32 speed: ${espSpeed}`
        );


        const data =
            await esp32Request(
                "/api/fan",
                {
                    method: "POST",

                    body: JSON.stringify({

                        speed:
                            espSpeed

                    })
                }
            );


        console.log(
            "ESP32 response:",
            data
        );


        res.json(data);

    }

    catch (error) {

        console.error(
            "ESP32 FAN ERROR:",
            error.message
        );


        res.status(503).json({

            connected: false,

            error:
                error.message

        });

    }

});


/* =========================================================
   MODE
   ========================================================= */

app.post("/api/mode", async (req, res) => {

    try {

        const data =
            await esp32Request(
                "/api/mode",
                {
                    method: "POST",

                    body: JSON.stringify({

                        mode:
                            req.body.mode

                    })
                }
            );


        res.json(data);

    }

    catch (error) {

        console.error(
            "ESP32 MODE ERROR:",
            error.message
        );


        res.status(503).json({

            connected: false,

            error:
                error.message

        });

    }

});


/* =========================================================
   SLEEP
   ========================================================= */

app.post("/api/sleep", async (req, res) => {

    try {

        const data =
            await esp32Request(
                "/api/sleep",
                {
                    method: "POST",

                    body: JSON.stringify({

                        sleep:
                            Boolean(
                                req.body.state
                            )

                    })
                }
            );


        res.json(data);

    }

    catch (error) {

        console.error(
            "ESP32 SLEEP ERROR:",
            error.message
        );


        res.status(503).json({

            connected: false,

            error:
                error.message

        });

    }

});


/* =========================================================
   TIMER
   ========================================================= */

app.post("/api/timer", async (req, res) => {

    try {

        const minutes =
            Number(
                req.body.minutes
            );


        const data =
            await esp32Request(
                "/api/timer",
                {
                    method: "POST",

                    body: JSON.stringify({

                        minutes:
                            minutes

                    })
                }
            );


        res.json(data);

    }

    catch (error) {

        console.error(
            "ESP32 TIMER ERROR:",
            error.message
        );


        res.status(503).json({

            connected: false,

            error:
                error.message

        });

    }

});


/* =========================================================
   HEALTH
   ========================================================= */

app.get("/api/health", async (req, res) => {

    try {

        const esp32 =
            await esp32Request(
                "/api/status"
            );


        res.json({

            status:
                "OK",

            service:
                "Ecraftonic Air-1",

            controller:
                "ESP32",

            esp32IP:
                "10.16.90.50",

            esp32:
                esp32,

            network:
                "Local Wi-Fi",

            cloud:
                false

        });

    }

    catch (error) {

        res.status(503).json({

            status:
                "ERROR",

            service:
                "Ecraftonic Air-1",

            controller:
                "ESP32",

            esp32IP:
                "10.16.90.50",

            error:
                error.message,

            network:
                "Local Wi-Fi",

            cloud:
                false

        });

    }

});


/* =========================================================
   START SERVER
   ========================================================= */

app.listen(
    PORT,
    () => {

        console.log("");

        console.log(
            "======================================"
        );

        console.log(
            " Ecraftonic Air-1"
        );

        console.log(
            " ESP32 PROXY BACKEND"
        );

        console.log(
            "======================================"
        );

        console.log(
            `Server: http://localhost:${PORT}`
        );

        console.log(
            `ESP32:  ${ESP32_URL}`
        );

        console.log(
            `API:    http://localhost:${PORT}/api/status`
        );

        console.log(
            "======================================"
        );

        console.log("");

    }
);