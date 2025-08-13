#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>
#include <SPIFFS.h>

// Configuration Access Point
const char* ap_ssid = "ESP32-Stepper";
const char* ap_password = "stepper123";

// Configuration pins stepper
#define PULSE_PIN 4
#define DIR_PIN 2

// Configuration stepper
AccelStepper stepper(AccelStepper::DRIVER, PULSE_PIN, DIR_PIN);
#define STEPS_PER_MM 800
#define MAX_SPEED 10000
#define ACCELERATION 5000

// Soft Limits
float SOFT_LIMIT_MIN = -100.0;
float SOFT_LIMIT_MAX = 100.0;
bool SOFT_LIMITS_ENABLED = true;

// Serveur web
WebServer server(80);

// Variables d'état
bool isRunning = false;
int direction = 1;
float speed_setting = 300.0;
float position = 0.0;
float targetPosition = 0.0;
bool limitError = false;

// Logging
unsigned long sessionStart = 0;
int logCounter = 0;

// Fonction de log dans fichier
void logToFile(String message) {
  File logFile = SPIFFS.open("/stepper.log", "a");
  if (logFile) {
    String timestamp = String(millis() - sessionStart);
    logFile.println("[" + timestamp + "ms] " + message);
    logFile.close();
    logCounter++;
    
    // Nettoyer le log si trop gros (>100 entrées)
    if (logCounter > 100) {
      SPIFFS.remove("/stepper.log");
      logCounter = 0;
      logToFile("Log reinitialise");
    }
  }
}

// Vérification soft limits
bool checkSoftLimits(float targetPos) {
  if (!SOFT_LIMITS_ENABLED) return true;
  
  if (targetPos < SOFT_LIMIT_MIN || targetPos > SOFT_LIMIT_MAX) {
    limitError = true;
    logToFile("SOFT LIMIT atteinte: " + String(targetPos) + "mm");
    return false;
  }
  
  limitError = false;
  return true;
}

void setup() {
  // Pas de Serial - économie batterie
  
  // Initialisation SPIFFS pour les logs
  SPIFFS.begin(true);
  
  sessionStart = millis();
  logToFile("=== DEMARRAGE ESP32 STEPPER ===");
  
  // Configuration stepper
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCELERATION);
  stepper.setCurrentPosition(0);
  logToFile("Stepper configure");
  
  // Création du point d'accès WiFi
  WiFi.mode(WIFI_AP);
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ap_ssid, ap_password);
  
  logToFile("WiFi AP cree: " + String(ap_ssid));
  logToFile("Soft Limits: " + String(SOFT_LIMIT_MIN) + " a " + String(SOFT_LIMIT_MAX) + "mm");
  
  setupRoutes();
  server.begin();
  logToFile("Interface web prete");
} 

void setupRoutes() {
  // Interface principale
  server.on("/", []() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>🎛️ Stepper ESP32 Controller</title>
    <link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/all.min.css" rel="stylesheet">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            background: linear-gradient(135deg, #f0f9ff 0%, #e0e7ff 100%);
            min-height: 100vh; padding: 20px; color: #1f2937;
        }
        .container { max-width: 1000px; margin: 0 auto; }
        .header {
            background: white; border-radius: 16px; padding: 24px; margin-bottom: 24px;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1);
            display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap;
        }
        .title { display: flex; align-items: center; gap: 12px; }
        .title h1 { font-size: 1.875rem; font-weight: bold; color: #1f2937; }
        .connection-status { display: flex; align-items: center; gap: 8px; color: #059669; font-weight: 600; }
        .info-section { background: #f9fafb; border-radius: 8px; padding: 16px; margin-top: 16px; font-size: 0.875rem; color: #6b7280; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 24px; margin-bottom: 24px; }
        @media (max-width: 768px) { .grid { grid-template-columns: 1fr; } .header { flex-direction: column; text-align: center; gap: 16px; } }
        .card { background: white; border-radius: 16px; padding: 24px; box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1); }
        .card-title { display: flex; align-items: center; gap: 8px; font-size: 1.25rem; font-weight: bold; margin-bottom: 20px; color: #1f2937; }
        .status-indicator { width: 12px; height: 12px; border-radius: 50%; margin-left: 8px; }
        .running { background: #10b981; animation: pulse 2s infinite; }
        .stopped { background: #ef4444; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }
        .status-row { display: flex; justify-content: space-between; align-items: center; padding: 12px 16px; background: #f9fafb; border-radius: 8px; margin-bottom: 8px; }
        .status-label { font-weight: 500; color: #6b7280; }
        .status-value { font-weight: bold; font-family: 'SF Mono', Consolas, monospace; }
        .running-text { color: #059669; } .stopped-text { color: #dc2626; } .position-text { color: #2563eb; }
        .form-group { margin-bottom: 16px; }
        .form-label { display: block; font-size: 0.875rem; font-weight: 600; color: #374151; margin-bottom: 4px; }
        .form-input { width: 100%; padding: 8px 12px; border: 1px solid #d1d5db; border-radius: 6px; font-size: 0.875rem; transition: border-color 0.2s; }
        .form-input:focus { outline: none; border-color: #3b82f6; box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.1); }
        .form-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
        .btn {
            padding: 12px 20px; border: none; border-radius: 8px; font-weight: 600; cursor: pointer;
            transition: all 0.2s; display: flex; align-items: center; justify-content: center;
            gap: 8px; font-size: 0.875rem; width: 100%; margin-bottom: 8px;
        }
        .btn:hover { transform: translateY(-1px); box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15); }
        .btn:active { transform: translateY(0); }
        .btn-primary { background: #8b5cf6; color: white; } .btn-primary:hover { background: #7c3aed; }
        .btn-secondary { background: #6366f1; color: white; } .btn-secondary:hover { background: #5b21b6; }
        .btn-danger { background: #ef4444; color: white; } .btn-danger:hover { background: #dc2626; }
        .btn-warning { background: #f59e0b; color: white; } .btn-warning:hover { background: #d97706; }
        .btn-success { background: #10b981; color: white; } .btn-success:hover { background: #059669; }
        .btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
        .section-title { font-weight: bold; color: #374151; margin: 20px 0 12px 0; display: flex; align-items: center; gap: 8px; }
        .limits-card { background: #fef3c7; border: 1px solid #f59e0b; border-radius: 8px; padding: 16px; margin: 16px 0; }
        .limits-card.disabled { background: #f3f4f6; border-color: #9ca3af; }
        .limits-card.error { background: #fee2e2; border-color: #ef4444; }
        .limits-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
        .limits-status { font-weight: bold; display: flex; align-items: center; gap: 6px; }
        .limits-enabled { color: #d97706; } .limits-disabled { color: #6b7280; } .limits-error { color: #dc2626; }
        .footer { background: white; border-radius: 16px; padding: 16px; text-align: center; color: #6b7280; font-size: 0.875rem; }
        .ping-btn { background: #3b82f6; color: white; border: none; padding: 8px 16px; border-radius: 6px; font-size: 0.875rem; cursor: pointer; transition: background-color 0.2s; }
        .ping-btn:hover { background: #2563eb; } .ping-btn.testing { background: #fbbf24; } .ping-btn.success { background: #10b981; } .ping-btn.error { background: #ef4444; }
        .log-section { background: #1f2937; color: #f3f4f6; border-radius: 8px; padding: 16px; margin: 16px 0; font-family: 'SF Mono', Consolas, monospace; font-size: 0.75rem; max-height: 200px; overflow-y: auto; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="title">
                <i class="fas fa-microchip" style="color: #8b5cf6;"></i>
                <h1>Stepper ESP32 Controller</h1>
            </div>
            <div>
                <div class="connection-status">
                    <i class="fas fa-wifi"></i>
                    <span>Connecté</span>
                </div>
                <button class="ping-btn" onclick="testPing()">
                    <i class="fas fa-bolt"></i> Tester Ping
                </button>
            </div>
            <div class="info-section">
                <strong>IP ESP32:</strong> 192.168.4.1 (Access Point) |
                <strong>Config:</strong> PULSE=Pin4, DIR=Pin2, 800steps/mm |
                <strong>Mode:</strong> Optimisé Batterie
            </div>
        </div>
)rawliteral";
    
    server.send(200, "text/html", html);
  }); 
  // Continuation de la route principale "/"
  server.on("/", []() {
    String html2 = R"rawliteral(
        <div class="grid">
            <div class="card">
                <h2 class="card-title">
                    <i class="fas fa-tachometer-alt"></i>
                    Statut Moteur
                    <div id="statusIndicator" class="status-indicator stopped"></div>
                </h2>

                <div class="status-row">
                    <span class="status-label">État:</span>
                    <span id="motorState" class="status-value stopped-text">⏹️ Arrêté</span>
                </div>
                <div class="status-row">
                    <span class="status-label">Position:</span>
                    <span id="motorPosition" class="status-value position-text">0.00 mm</span>
                </div>
                <div class="status-row">
                    <span class="status-label">Vitesse:</span>
                    <span id="motorSpeed" class="status-value">0 mm/min</span>
                </div>
                <div class="status-row">
                    <span class="status-label">Direction:</span>
                    <span id="motorDirection" class="status-value">➡️ Avant</span>
                </div>
                <div class="status-row">
                    <span class="status-label">Cible:</span>
                    <span id="motorTarget" class="status-value">0.00 mm</span>
                </div>

                <div id="limitsCard" class="limits-card">
                    <div class="limits-header">
                        <span class="status-label">
                            <i class="fas fa-shield-alt"></i> Soft Limits
                        </span>
                        <span id="limitsStatus" class="limits-status limits-enabled">🛡️ Activé</span>
                    </div>
                    <div style="font-size: 0.875rem; color: #6b7280;">
                        Zone: <span id="limitMin">-100.0</span> à <span id="limitMax">100.0</span> mm
                    </div>
                    <div id="limitError" style="display: none; margin-top: 8px; color: #dc2626; font-weight: bold;">
                        <i class="fas fa-exclamation-triangle"></i> LIMITE DÉPASSÉE !
                    </div>
                </div>

                <div class="section-title">
                    <i class="fas fa-file-alt"></i>
                    Logs Système
                    <button class="ping-btn" onclick="loadLogs()" style="margin-left: auto; font-size: 0.75rem; padding: 4px 8px;">
                        <i class="fas fa-sync"></i> Actualiser
                    </button>
                </div>
                <div id="logSection" class="log-section">Chargement des logs...</div>
            </div>

            <div class="card">
                <h2 class="card-title">
                    <i class="fas fa-gamepad"></i>
                    Contrôles
                </h2>

                <div class="section-title">
                    <i class="fas fa-cog"></i>
                    Paramètres
                </div>
                <div class="form-grid">
                    <div class="form-group">
                        <label class="form-label">Vitesse (mm/min)</label>
                        <input type="number" id="speedInput" class="form-input" value="300" min="50" max="1000">
                    </div>
                    <div class="form-group">
                        <label class="form-label">Distance (mm)</label>
                        <input type="number" id="distanceInput" class="form-input" value="10" step="0.1">
                    </div>
                </div>

                <div class="section-title">
                    <i class="fas fa-arrows-alt"></i>
                    Mouvements
                </div>
                <button class="btn btn-primary" onclick="moveDistance()">
                    <i class="fas fa-play"></i>
                    Déplacer <span id="distanceDisplay">10</span>mm
                </button>
                <div class="btn-grid">
                    <button class="btn btn-secondary" onclick="moveForward()">
                        <i class="fas fa-arrow-right"></i>
                        Avant Continu
                    </button>
                    <button class="btn btn-secondary" onclick="moveBackward()">
                        <i class="fas fa-arrow-left"></i>
                        Arrière Continu
                    </button>
                </div>
                <div class="btn-grid">
                    <button class="btn btn-danger" onclick="stopMotor()">
                        <i class="fas fa-stop"></i>
                        ARRÊT
                    </button>
                    <button class="btn btn-warning" onclick="homeMotor()">
                        <i class="fas fa-home"></i>
                        Reset Position
                    </button>
                </div>

                <div class="section-title">
                    <i class="fas fa-shield-alt"></i>
                    Gestion Soft Limits
                </div>
                <div class="btn-grid">
                    <button class="btn btn-success" onclick="enableLimits()">
                        <i class="fas fa-shield-alt"></i>
                        Activer Limites
                    </button>
                    <button class="btn btn-warning" onclick="disableLimits()">
                        <i class="fas fa-shield-alt"></i>
                        Désactiver Limites
                    </button>
                </div>
                <div class="form-grid">
                    <div class="form-group">
                        <label class="form-label">Limite Min (mm)</label>
                        <input type="number" id="limitMinInput" class="form-input" value="-100" step="0.1">
                    </div>
                    <div class="form-group">
                        <label class="form-label">Limite Max (mm)</label>
                        <input type="number" id="limitMaxInput" class="form-input" value="100" step="0.1">
                    </div>
                </div>
                <button class="btn btn-primary" onclick="setLimits()">
                    <i class="fas fa-save"></i>
                    Appliquer Nouvelles Limites
                </button>
            </div>
        </div>

        <div class="footer">
            <p>🔧 Contrôleur stepper ESP32 - Mode Batterie Optimisé</p>
            <p>📶 WiFi "ESP32-Stepper" | 🔋 Logs fichier /stepper.log</p>
        </div>
    </div>

    <script>
        document.getElementById('distanceInput').addEventListener('input', function() {
            document.getElementById('distanceDisplay').textContent = this.value;
        });

        async function apiCall(endpoint, method = 'GET', data = null) {
            try {
                const config = { method };
                if (data) {
                    config.headers = { 'Content-Type': 'application/json' };
                    config.body = JSON.stringify(data);
                }
                const response = await fetch('/api/' + endpoint, config);
                return await response.json();
            } catch (error) {
                console.error('Erreur API:', error);
                return null;
            }
        }

        async function testPing() {
            const btn = document.querySelector('.ping-btn');
            btn.className = 'ping-btn testing';
            btn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Test...';
            const result = await apiCall('ping');
            if (result) {
                btn.className = 'ping-btn success';
                btn.innerHTML = '<i class="fas fa-check"></i> OK';
            } else {
                btn.className = 'ping-btn error';
                btn.innerHTML = '<i class="fas fa-times"></i> Erreur';
            }
            setTimeout(() => {
                btn.className = 'ping-btn';
                btn.innerHTML = '<i class="fas fa-bolt"></i> Tester Ping';
            }, 2000);
        }

        async function loadLogs() {
            try {
                const response = await fetch('/api/logs');
                const text = await response.text();
                document.getElementById('logSection').innerHTML = text || 'Aucun log disponible';
            } catch (error) {
                document.getElementById('logSection').innerHTML = 'Erreur chargement logs';
            }
        }

        async function stopMotor() { await apiCall('stop', 'POST'); }
        async function homeMotor() { await apiCall('home', 'POST'); }

        async function moveDistance() {
            const distance = parseFloat(document.getElementById('distanceInput').value);
            const speed = parseFloat(document.getElementById('speedInput').value);
            await apiCall('move', 'POST', { distance: distance, speed: speed, direction: distance >= 0 ? 1 : 0 });
        }

        async function moveForward() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            await apiCall('move', 'POST', { continuous: true, speed: speed, direction: 1 });
        }

        async function moveBackward() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            await apiCall('move', 'POST', { continuous: true, speed: speed, direction: 0 });
        }

        async function enableLimits() { await apiCall('limits', 'POST', { enabled: true }); }
        async function disableLimits() { await apiCall('limits', 'POST', { enabled: false }); }

        async function setLimits() {
            const min = parseFloat(document.getElementById('limitMinInput').value);
            const max = parseFloat(document.getElementById('limitMaxInput').value);
            if (min >= max) {
                alert('La limite minimale doit être inférieure à la limite maximale !');
                return;
            }
            await apiCall('limits', 'POST', { min: min, max: max });
        }

        async function updateStatus() {
            const status = await apiCall('status');
            if (status) {
                document.getElementById('motorState').textContent = status.isRunning ? '🏃 En mouvement' : '⏹️ Arrêté';
                document.getElementById('motorState').className = 'status-value ' + (status.isRunning ? 'running-text' : 'stopped-text');
                document.getElementById('statusIndicator').className = 'status-indicator ' + (status.isRunning ? 'running' : 'stopped');
                document.getElementById('motorPosition').textContent = status.position.toFixed(2) + ' mm';
                document.getElementById('motorSpeed').textContent = status.speed.toFixed(0) + ' mm/min';
                document.getElementById('motorDirection').textContent = status.direction === 1 ? '➡️ Avant' : '⬅️ Arrière';
                document.getElementById('motorTarget').textContent = status.targetPosition.toFixed(2) + ' mm';

                const limitsCard = document.getElementById('limitsCard');
                const limitsStatus = document.getElementById('limitsStatus');
                const limitError = document.getElementById('limitError');
                
                document.getElementById('limitMin').textContent = status.softLimitMin.toFixed(1);
                document.getElementById('limitMax').textContent = status.softLimitMax.toFixed(1);
                
                if (status.limitError) {
                    limitsCard.className = 'limits-card error';
                    limitError.style.display = 'block';
                } else if (status.softLimitEnabled) {
                    limitsCard.className = 'limits-card';
                    limitError.style.display = 'none';
                } else {
                    limitsCard.className = 'limits-card disabled';
                    limitError.style.display = 'none';
                }
                
                limitsStatus.textContent = status.softLimitEnabled ? '🛡️ Activé' : '⚠️ Désactivé';
                limitsStatus.className = 'limits-status ' + (status.softLimitEnabled ? 'limits-enabled' : 'limits-disabled');
                
                document.getElementById('limitMinInput').value = status.softLimitMin;
                document.getElementById('limitMaxInput').value = status.softLimitMax;
            }
        }

        setInterval(updateStatus, 1000);
        updateStatus();
        setTimeout(loadLogs, 2000);
    </script>
</body>
</html>
)rawliteral";
    
    server.send(200, "text/html", html2);
  }); 
// API Ping
  server.on("/api/ping", HTTP_GET, []() {
    logToFile("Ping recu depuis " + server.client().remoteIP().toString());
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"status\":\"connected\",\"timestamp\":" + String(millis()) + "}");
  });

  // API Logs
  server.on("/api/logs", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    File logFile = SPIFFS.open("/stepper.log", "r");
    if (logFile) {
      String logs = logFile.readString();
      logFile.close();
      logs.replace("\n", "<br>");
      server.send(200, "text/plain", logs);
    } else {
      server.send(200, "text/plain", "Aucun log disponible");
    }
  });

  // API Status
  server.on("/api/status", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "{";
    json += "\"isRunning\":" + String(isRunning ? "true" : "false") + ",";
    json += "\"direction\":" + String(direction) + ",";
    json += "\"speed\":" + String(speed_setting) + ",";
    json += "\"position\":" + String(position) + ",";
    json += "\"targetPosition\":" + String(targetPosition) + ",";
    json += "\"softLimitEnabled\":" + String(SOFT_LIMITS_ENABLED ? "true" : "false") + ",";
    json += "\"softLimitMin\":" + String(SOFT_LIMIT_MIN) + ",";
    json += "\"softLimitMax\":" + String(SOFT_LIMIT_MAX) + ",";
    json += "\"limitError\":" + String(limitError ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  });

  // API Move
  server.on("/api/move", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    
    float speed_val = 300.0;
    int dir_val = 1;
    float distance_val = 0.0;
    bool continuous = false;
    
    // Parse speed
    if (body.indexOf("\"speed\":") >= 0) {
      int start = body.indexOf("\"speed\":") + 8;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      speed_val = body.substring(start, end).toFloat();
    }
    
    // Parse direction
    if (body.indexOf("\"direction\":") >= 0) {
      int start = body.indexOf("\"direction\":") + 12;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      dir_val = body.substring(start, end).toInt();
    }
    
    // Parse distance
    if (body.indexOf("\"distance\":") >= 0) {
      int start = body.indexOf("\"distance\":") + 11;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      distance_val = body.substring(start, end).toFloat();
    }
    
    // Parse continuous
    if (body.indexOf("\"continuous\":true") >= 0) {
      continuous = true;
    }
    
stepper.setPinsInverted(dir_val == 0);
    direction = dir_val;
    speed_setting = speed_val;
    
    float speedStepsPerSec = (speed_val / 60.0) * STEPS_PER_MM;
    stepper.setMaxSpeed(speedStepsPerSec);
    
    if (continuous) {
      if (SOFT_LIMITS_ENABLED) {
        if ((dir_val == 1 && position >= SOFT_LIMIT_MAX) || 
            (dir_val == 0 && position <= SOFT_LIMIT_MIN)) {
          limitError = true;
          logToFile("Mouvement continu bloque par limite");
          server.send(400, "application/json", "{\"error\":\"soft_limit_reached\"}");
          return;
        }
      }
      
      stepper.setSpeed(dir_val == 0 ? -speedStepsPerSec : speedStepsPerSec);
      isRunning = true;
      limitError = false;
      logToFile("Mouvement continu: " + String(dir_val == 1 ? "AVANT" : "ARRIERE") + " " + String(speed_val) + "mm/min");
      server.send(200, "application/json", "{\"status\":\"continuous_move\"}");
      
    } else if (distance_val != 0) {
      float newPos = position + distance_val;
      
      if (checkSoftLimits(newPos)) {
        float steps = abs(distance_val) * STEPS_PER_MM;
        stepper.move(steps);
        isRunning = true;
        targetPosition = newPos;
        logToFile("Mouvement distance: " + String(distance_val) + "mm vers " + String(newPos) + "mm");
        server.send(200, "application/json", "{\"status\":\"distance_move\"}");
      } else {
        server.send(400, "application/json", "{\"error\":\"soft_limit_violation\"}");
      }
    } else {
      server.send(400, "application/json", "{\"error\":\"missing_params\"}");
    }
  });

  // API Stop
  server.on("/api/stop", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    stepper.stop();
    isRunning = false;
    limitError = false;
    logToFile("ARRET moteur");
    server.send(200, "application/json", "{\"status\":\"stopped\"}");
  });

  // API Home (Reset position)
  server.on("/api/home", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    stepper.stop();
    stepper.setCurrentPosition(0);
    position = 0.0;
    targetPosition = 0.0;
    isRunning = false;
    limitError = false;
    logToFile("Position reset a 0");
    server.send(200, "application/json", "{\"status\":\"homed\"}");
  });

  // API Limits configuration
  server.on("/api/limits", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    
    // Parse enabled
    if (body.indexOf("\"enabled\":true") >= 0) {
      SOFT_LIMITS_ENABLED = true;
      logToFile("Soft limits ACTIVES");
      server.send(200, "application/json", "{\"status\":\"limits_enabled\"}");
      return;
    }
    
    if (body.indexOf("\"enabled\":false") >= 0) {
      SOFT_LIMITS_ENABLED = false;
      limitError = false;
      logToFile("Soft limits DESACTIVES");
      server.send(200, "application/json", "{\"status\":\"limits_disabled\"}");
      return;
    }
    
    // Parse new limits
    float new_min = SOFT_LIMIT_MIN;
    float new_max = SOFT_LIMIT_MAX;
    
    if (body.indexOf("\"min\":") >= 0) {
      int start = body.indexOf("\"min\":") + 6;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      new_min = body.substring(start, end).toFloat();
    }
    
    if (body.indexOf("\"max\":") >= 0) {
      int start = body.indexOf("\"max\":") + 6;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      new_max = body.substring(start, end).toFloat();
    }
    
    if (new_min < new_max) {
      SOFT_LIMIT_MIN = new_min;
      SOFT_LIMIT_MAX = new_max;
      logToFile("Nouvelles limites: " + String(new_min) + " a " + String(new_max) + "mm");
      server.send(200, "application/json", "{\"status\":\"limits_updated\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"invalid_limits\"}");
    }
  });

// Gestion CORS pour OPTIONS
  server.on("/api/ping", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200);
  });

  server.on("/api/status", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200);
  });

  server.on("/api/move", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200);
  });

  server.on("/api/stop", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200);
  });

  server.on("/api/home", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200);
  });

  server.on("/api/limits", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200);
  });

  server.on("/api/logs", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200);
  });
}

void loop() {
  // Gestion du serveur web
  server.handleClient();
  
  // Mise à jour du stepper
  if (isRunning) {
    // Mode mouvement continu
    if (stepper.speed() != 0) {
      stepper.runSpeed();
      
      // Calcul de la position courante
      float currentSteps = stepper.currentPosition();
      position = currentSteps / STEPS_PER_MM;
      
      // Vérification des soft limits en mouvement continu
      if (SOFT_LIMITS_ENABLED) {
        if (position <= SOFT_LIMIT_MIN && direction == 0) {
          stepper.stop();
          isRunning = false;
          limitError = true;
          logToFile("ARRET: Limite MIN atteinte (" + String(position) + "mm)");
        } else if (position >= SOFT_LIMIT_MAX && direction == 1) {
          stepper.stop();
          isRunning = false;
          limitError = true;
          logToFile("ARRET: Limite MAX atteinte (" + String(position) + "mm)");
        }
      }
    } 
    // Mode mouvement vers position cible
    else if (stepper.distanceToGo() != 0) {
      stepper.run();
      
      // Calcul de la position courante
      float currentSteps = stepper.currentPosition();
      position = currentSteps / STEPS_PER_MM;
      
      // Vérification si le mouvement est terminé
      if (stepper.distanceToGo() == 0) {
        isRunning = false;
        position = targetPosition;
        logToFile("Mouvement termine - Position: " + String(position) + "mm");
      }
    } else {
      // Plus de mouvement en cours
      isRunning = false;
    }
  }
  
  // Petite pause pour économiser la batterie
  delay(1);
}
