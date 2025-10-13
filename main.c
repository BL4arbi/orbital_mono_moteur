#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>
#include <SPIFFS.h>
#include <DNSServer.h>

// ============================================
// CONFIGURATION OPTIMALE
// ============================================
const char* ap_ssid = "ESP32-Stepper";
const char* ap_password = "stepper123";

#define PULSE_PIN 4
#define DIR_PIN 2

// ⚠️ DRIVER CONFIGURÉ EN 1/4 STEP (200 microsteps/tour)
// DIP Switches: SW1=ON, SW2=OFF, SW3=OFF
#define STEPS_PER_MM 200    // Vis à billes 1mm pas, 1/4 microstepping

// Vitesses optimisées (mm/min)
#define MIN_SPEED 30        // Très lent
#define DEFAULT_SPEED 180   // Bon compromis
#define MAX_SPEED 360       // Rapide mais stable

AccelStepper stepper(AccelStepper::DRIVER, PULSE_PIN, DIR_PIN);
WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

// Variables globales
bool isRunning = false;
float currentPosition = 0.0;
float targetPosition = 0.0;
float currentSpeed = DEFAULT_SPEED;
bool movingToTarget = false;
bool continuousMode = false;
int moveDirection = 1;

// Soft limits
float SOFT_LIMIT_MIN = -200.0;
float SOFT_LIMIT_MAX = 200.0;
bool SOFT_LIMITS_ENABLED = true;
bool limitError = false;

unsigned long sessionStart = 0;

void logToFile(String message) {
  File logFile = SPIFFS.open("/stepper.log", "a");
  if (logFile) {
    String timestamp = String(millis() - sessionStart);
    logFile.println("[" + timestamp + "ms] " + message);
    logFile.close();
  }
}

bool checkLimits(float pos) {
  if (!SOFT_LIMITS_ENABLED) return true;
  return (pos >= SOFT_LIMIT_MIN && pos <= SOFT_LIMIT_MAX);
}

void stopMotor() {
  stepper.stop();
  isRunning = false;
  movingToTarget = false;
  continuousMode = false;
  currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
  targetPosition = currentPosition;
  Serial.println("MOTEUR ARRÊTÉ - Position: " + String(currentPosition, 3) + "mm");
}

void setup() {
  Serial.begin(115200);
  SPIFFS.begin(true);
  sessionStart = millis();
  
  // Configuration stepper OPTIMISÉE
  float maxSpeedSteps = (MAX_SPEED * STEPS_PER_MM) / 60.0;  // 1200 steps/sec
  
  stepper.setMaxSpeed(maxSpeedSteps * 1.5);     // 1800 steps/sec (marge sécurité)
  stepper.setAcceleration(maxSpeedSteps * 3);   // 3600 steps/sec² (accélération rapide)
  stepper.setCurrentPosition(0);
  
  currentPosition = 0.0;
  targetPosition = 0.0;
  currentSpeed = DEFAULT_SPEED;
  
  Serial.println("\n=== STEPPER ESP32 - CONFIG OPTIMALE ===");
  Serial.println("Microstepping: 1/4 step (200 steps/tour)");
  Serial.println("DIP Switches: SW1=ON, SW2=OFF, SW3=OFF");
  Serial.println("Steps/mm: " + String(STEPS_PER_MM));
  Serial.println("Résolution: 0.005mm/step");
  Serial.println("Vitesse défaut: " + String(DEFAULT_SPEED) + " mm/min");
  Serial.println("Vitesse max: " + String(MAX_SPEED) + " mm/min (" + String(maxSpeedSteps) + " steps/sec)");
  Serial.println("MaxSpeed moteur: " + String(maxSpeedSteps * 1.5) + " steps/sec");
  Serial.println("Accélération: " + String(maxSpeedSteps * 3) + " steps/sec²");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  
  Serial.println("\nWiFi AP: " + String(ap_ssid));
  Serial.println("IP: " + WiFi.softAPIP().toString());
  
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.println("Captive Portal activé");
  
  setupWebServer();
  server.begin();
  Serial.println("Interface: http://192.168.4.1");
  Serial.println("=== PRÊT ===\n");
}

void setupWebServer() {
  // Captive Portal - Redirection
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  
  // Page principale
  server.on("/", []() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="mobile-web-app-capable" content="yes">
    <title>Stepper Controller Pro</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { 
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 10px;
        }
        .container { 
            max-width: 900px;
            margin: 0 auto;
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            overflow: hidden;
        }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 20px;
            text-align: center;
        }
        .header h1 { 
            font-size: 24px;
            margin-bottom: 5px;
            font-weight: 700;
        }
        .header .subtitle {
            font-size: 12px;
            opacity: 0.9;
            margin-top: 5px;
        }
        .content { padding: 15px; }
        
        .status-card {
            background: linear-gradient(135deg, #e8f4fd 0%, #d4e9ff 100%);
            padding: 15px;
            border-radius: 15px;
            margin-bottom: 15px;
            border-left: 5px solid #007bff;
        }
        .status-card h3 {
            color: #007bff;
            margin-bottom: 12px;
            font-size: 16px;
        }
        .status-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 10px;
        }
        .status-item {
            background: white;
            padding: 10px;
            border-radius: 10px;
            box-shadow: 0 2px 8px rgba(0,0,0,0.1);
        }
        .status-label {
            font-size: 11px;
            color: #666;
            margin-bottom: 3px;
            text-transform: uppercase;
            font-weight: 600;
        }
        .status-value {
            font-size: 18px;
            font-weight: bold;
            color: #333;
        }
        .status-value.running { color: #28a745; }
        .status-value.stopped { color: #dc3545; }
        
        .control-card {
            background: #f8f9fa;
            padding: 15px;
            border-radius: 15px;
            margin-bottom: 15px;
        }
        .control-card h3 {
            color: #333;
            margin-bottom: 12px;
            font-size: 16px;
            display: flex;
            align-items: center;
            gap: 6px;
        }
        
        .speed-presets {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 8px;
            margin-bottom: 12px;
        }
        .speed-presets button {
            padding: 12px 8px;
            font-size: 13px;
            border-radius: 10px;
            border: 2px solid transparent;
            cursor: pointer;
            font-weight: 600;
            transition: all 0.2s;
        }
        .speed-presets button:active {
            transform: scale(0.95);
        }
        .btn-slow { background: #ffc107; color: #000; }
        .btn-slow:hover { background: #ffb300; border-color: #ff8f00; }
        .btn-medium { background: #17a2b8; color: white; }
        .btn-medium:hover { background: #138496; border-color: #0c6b7a; }
        .btn-normal { background: #28a745; color: white; }
        .btn-normal:hover { background: #218838; border-color: #1e7e34; }
        .btn-fast { background: #dc3545; color: white; }
        .btn-fast:hover { background: #c82333; border-color: #bd2130; }
        
        .input-row {
            display: flex;
            gap: 8px;
            margin-bottom: 12px;
            align-items: stretch;
        }
        .input-row label {
            font-weight: 600;
            color: #333;
            font-size: 13px;
            display: flex;
            align-items: center;
            white-space: nowrap;
            min-width: 80px;
        }
        .input-wrapper {
            flex: 1;
            display: flex;
            gap: 8px;
        }
        input[type="number"] {
            flex: 1;
            padding: 10px;
            border: 2px solid #ddd;
            border-radius: 10px;
            font-size: 15px;
            font-weight: 600;
            text-align: center;
            min-width: 0;
        }
        input[type="number"]:focus {
            outline: none;
            border-color: #667eea;
        }
        
        /* CORRECTION MOBILE - Limites sur une seule ligne */
        .limits-row {
            display: flex;
            gap: 8px;
            margin-bottom: 12px;
            align-items: center;
        }
        .limits-row .input-group {
            display: flex;
            align-items: center;
            gap: 5px;
            margin: 0;
            flex: 1;
        }
        .limits-row .input-group label {
            font-size: 12px;
            font-weight: 600;
            color: #333;
            min-width: auto;
            white-space: nowrap;
        }
        .limits-row .input-group input {
            flex: 1;
            min-width: 60px;
            padding: 8px;
            font-size: 14px;
        }
        
        .button-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 8px;
            margin-top: 12px;
        }
        button {
            padding: 12px 10px;
            border: none;
            border-radius: 10px;
            cursor: pointer;
            font-weight: bold;
            font-size: 14px;
            transition: all 0.2s;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
        }
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 12px rgba(0,0,0,0.15);
        }
        button:active {
            transform: translateY(0);
        }
        .btn-primary { background: #007bff; color: white; }
        .btn-success { background: #28a745; color: white; }
        .btn-danger { background: #dc3545; color: white; }
        .btn-warning { background: #ffc107; color: black; }
        .btn-info { background: #17a2b8; color: white; }
        
        .log-card {
            background: #2d3748;
            padding: 12px;
            border-radius: 15px;
            margin-top: 15px;
        }
        .log-card h3 {
            color: #e2e8f0;
            margin-bottom: 8px;
            font-size: 14px;
        }
        .logs {
            background: #1a202c;
            color: #e2e8f0;
            padding: 12px;
            border-radius: 10px;
            font-family: 'Courier New', monospace;
            font-size: 11px;
            max-height: 150px;
            overflow-y: auto;
            line-height: 1.5;
        }
        
        .info-banner {
            background: linear-gradient(135deg, #fff3cd 0%, #ffe69c 100%);
            padding: 12px;
            border-radius: 10px;
            margin-bottom: 15px;
            border-left: 5px solid #ffc107;
            font-size: 12px;
            color: #856404;
        }
        .info-banner strong {
            color: #533f03;
        }
        
        @media (min-width: 600px) {
            .content { padding: 20px; }
            .speed-presets {
                grid-template-columns: repeat(4, 1fr);
            }
            .status-grid {
                grid-template-columns: repeat(4, 1fr);
            }
            .button-grid {
                grid-template-columns: repeat(3, 1fr);
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🔧 Stepper Controller Pro</h1>
            <div class="subtitle">ESP32 - Configuration optimale 1/4 step</div>
        </div>
        
        <div class="content">
            <div class="info-banner">
                <strong>⚙️ Config:</strong> 1/4 step | 200 steps/mm | 0.005mm/step
            </div>
            
            <div class="status-card">
                <h3>📊 État Système</h3>
                <div class="status-grid">
                    <div class="status-item">
                        <div class="status-label">État</div>
                        <div class="status-value stopped" id="status">Arrêté</div>
                    </div>
                    <div class="status-item">
                        <div class="status-label">Position</div>
                        <div class="status-value" id="position">0.0 mm</div>
                    </div>
                    <div class="status-item">
                        <div class="status-label">Cible</div>
                        <div class="status-value" id="target">0.0 mm</div>
                    </div>
                    <div class="status-item">
                        <div class="status-label">Vitesse</div>
                        <div class="status-value" id="speed">0</div>
                    </div>
                </div>
            </div>
            
            <div class="control-card">
                <h3>⚡ Vitesse</h3>
                <div class="speed-presets">
                    <button class="btn-slow" onclick="setSpeed(30)">🐌 30</button>
                    <button class="btn-medium" onclick="setSpeed(90)">🚶 90</button>
                    <button class="btn-normal" onclick="setSpeed(180)">🏃 180</button>
                    <button class="btn-fast" onclick="setSpeed(360)">⚡ 360</button>
                </div>
                <div class="input-row">
                    <label>Custom:</label>
                    <div class="input-wrapper">
                        <input type="number" id="speedInput" value="180" min="30" max="360" step="10">
                        <button class="btn-primary" onclick="updateSpeedNow()" style="flex-shrink: 0; padding: 10px 15px;">OK</button>
                    </div>
                </div>
            </div>
            
            <div class="control-card">
                <h3>🎯 Déplacements</h3>
                <div class="input-row">
                    <label>Distance:</label>
                    <div class="input-wrapper">
                        <input type="number" id="distanceInput" value="10" step="0.5">
                        <button class="btn-primary" onclick="moveDistance()" style="flex-shrink: 0; padding: 10px 15px;">GO</button>
                    </div>
                </div>
                <div class="button-grid">
                    <button class="btn-success" onclick="moveForward()">➡️ Avant</button>
                    <button class="btn-success" onclick="moveBackward()">⬅️ Arrière</button>
                    <button class="btn-danger" onclick="stopMotor()">⏹️ STOP</button>
                    <button class="btn-warning" onclick="homeMotor()">🏠 Home</button>
                    <button class="btn-info" onclick="resetPosition()">🔄 Reset</button>
                </div>
            </div>
            
            <div class="control-card">
                <h3>🛡️ Limites</h3>
                <div class="limits-row">
                    <div class="input-group">
                        <label>Min:</label>
                        <input type="number" id="limitMin" value="-200" step="10">
                    </div>
                    <div class="input-group">
                        <label>Max:</label>
                        <input type="number" id="limitMax" value="200" step="10">
                    </div>
                </div>
                <div class="button-grid">
                    <button class="btn-primary" onclick="setLimits()">Appliquer</button>
                    <button class="btn-warning" onclick="toggleLimits()">ON/OFF</button>
                </div>
                <div style="margin-top: 8px; font-weight: 600; font-size: 13px;">
                    Status: <span id="limitsStatus" style="color: #28a745;">Activées</span>
                </div>
            </div>
            
            <div class="log-card">
                <h3>📜 Logs</h3>
                <div id="logs" class="logs">Chargement...</div>
                <button class="btn-primary" onclick="loadLogs()" style="margin-top: 8px; width: 100%;">🔄 Actualiser</button>
            </div>
        </div>
    </div>
    
    <script>
        console.log('=== INTERFACE STEPPER PRO CHARGÉE ===');
        
        async function apiCall(endpoint, data = null) {
            try {
                const config = { method: data ? 'POST' : 'GET' };
                if (data) {
                    config.headers = { 'Content-Type': 'application/json' };
                    config.body = JSON.stringify(data);
                }
                const response = await fetch('/api/' + endpoint, config);
                if (!response.ok) throw new Error(`HTTP ${response.status}`);
                return await response.json();
            } catch (error) {
                console.error('❌ Erreur API:', error);
                alert('❌ Erreur: ' + error.message);
                return null;
            }
        }
        
        function setSpeed(speed) {
            document.getElementById('speedInput').value = speed;
            updateSpeedNow();
        }
        
        async function moveDistance() {
            const distance = parseFloat(document.getElementById('distanceInput').value);
            const speed = parseFloat(document.getElementById('speedInput').value);
            if (isNaN(distance) || distance === 0) {
                alert('⚠️ Distance invalide!');
                return;
            }
            console.log(`🎯 Déplacement: ${distance}mm à ${speed}mm/min`);
            await apiCall('move', { distance: distance, speed: speed });
        }
        
        async function moveForward() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            console.log(`➡️ Mouvement continu avant à ${speed}mm/min`);
            await apiCall('move', { continuous: true, direction: 1, speed: speed });
        }
        
        async function moveBackward() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            console.log(`⬅️ Mouvement continu arrière à ${speed}mm/min`);
            await apiCall('move', { continuous: true, direction: -1, speed: speed });
        }
        
        async function stopMotor() {
            console.log('⏹️ ARRÊT');
            await apiCall('stop', { action: 'stop' });
        }
        
        async function homeMotor() {
            console.log('🏠 Retour origine');
            await apiCall('home', { action: 'home' });
        }
        
        async function resetPosition() {
            console.log('🔄 Reset position');
            await apiCall('reset', { action: 'reset' });
        }
        
        async function updateSpeedNow() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            if (isNaN(speed) || speed < 30 || speed > 360) {
                alert('⚠️ Vitesse invalide! (30-360 mm/min)');
                return;
            }
            console.log(`⚡ Nouvelle vitesse: ${speed}mm/min`);
            const result = await apiCall('speed', { speed: speed });
            if (result && result.status === 'speed_updated') {
                console.log('✅ Vitesse appliquée');
            }
        }
        
        async function setLimits() {
            const min = parseFloat(document.getElementById('limitMin').value);
            const max = parseFloat(document.getElementById('limitMax').value);
            if (min >= max) {
                alert('⚠️ Min doit être < Max!');
                return;
            }
            console.log(`🛡️ Limites: ${min} à ${max}mm`);
            await apiCall('limits', { min: min, max: max });
        }
        
        async function toggleLimits() {
            console.log('🔄 Toggle limites');
            await apiCall('limits/toggle', { action: 'toggle' });
        }
        
        async function loadLogs() {
            try {
                const response = await fetch('/api/logs');
                const text = await response.text();
                const logsDiv = document.getElementById('logs');
                logsDiv.innerHTML = text.replace(/\n/g, '<br>') || 'Aucun log';
                logsDiv.scrollTop = logsDiv.scrollHeight;
            } catch (error) {
                document.getElementById('logs').innerHTML = '❌ Erreur logs';
            }
        }
        
        async function updateStatus() {
            const status = await apiCall('status');
            if (status) {
                const statusEl = document.getElementById('status');
                statusEl.textContent = status.running ? 'Actif' : 'Arrêté';
                statusEl.className = 'status-value ' + (status.running ? 'running' : 'stopped');
                
                document.getElementById('position').textContent = status.position.toFixed(1) + ' mm';
                document.getElementById('target').textContent = status.target.toFixed(1) + ' mm';
                document.getElementById('speed').textContent = status.speed.toFixed(0);
                
                const limitsEl = document.getElementById('limitsStatus');
                limitsEl.textContent = status.limitsEnabled ? 'Activées' : 'Désactivées';
                limitsEl.style.color = status.limitsEnabled ? '#28a745' : '#dc3545';
            }
        }
        
        setInterval(updateStatus, 300);
        
        setTimeout(() => {
            console.log('🚀 Initialisation...');
            updateStatus();
            loadLogs();
        }, 500);
        
        console.log('✅ Interface prête');
    </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
  });

  // API Status
  server.on("/api/status", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "{";
    json += "\"running\":" + String(isRunning ? "true" : "false") + ",";
    json += "\"position\":" + String(currentPosition, 3) + ",";
    json += "\"target\":" + String(targetPosition, 3) + ",";
    json += "\"speed\":" + String(currentSpeed) + ",";
    json += "\"steps\":" + String(stepper.currentPosition()) + ",";
    json += "\"remaining\":" + String(stepper.distanceToGo()) + ",";
    json += "\"limitsEnabled\":" + String(SOFT_LIMITS_ENABLED ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  });

  // API Move
  server.on("/api/move", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    
    Serial.println("\n=== API MOVE ===");
    Serial.println("Body: " + body);
    
    float speed = DEFAULT_SPEED;
    float distance = 0;
    int direction = 1;
    bool continuous = false;

    // Parse JSON
    if (body.indexOf("\"speed\":") >= 0) {
      int start = body.indexOf("\"speed\":") + 8;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      speed = body.substring(start, end).toFloat();
    }
    if (body.indexOf("\"distance\":") >= 0) {
      int start = body.indexOf("\"distance\":") + 11;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      distance = body.substring(start, end).toFloat();
    }
    if (body.indexOf("\"direction\":") >= 0) {
      int start = body.indexOf("\"direction\":") + 12;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      direction = body.substring(start, end).toInt();
    }
    if (body.indexOf("\"continuous\":true") >= 0) {
      continuous = true;
    }

    // Arrêt propre
    if (isRunning) {
      stepper.stop();
      while(stepper.isRunning()) {
        stepper.run();
      }
    }
    
    isRunning = false;
    movingToTarget = false;
    continuousMode = false;
    currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
    currentSpeed = speed;
    
    float speedStepsPerSec = (speed * STEPS_PER_MM) / 60.0;
    
    Serial.println("Vitesse: " + String(speed) + "mm/min = " + String(speedStepsPerSec) + " steps/sec");
    
    if (continuous) {
      Serial.println("→ Mode CONTINU " + String(direction > 0 ? "AVANT" : "ARRIERE"));
      continuousMode = true;
      moveDirection = direction;
      
      stepper.setMaxSpeed(speedStepsPerSec * 2);
      stepper.setSpeed(direction > 0 ? speedStepsPerSec : -speedStepsPerSec);
      
      isRunning = true;
      
      logToFile("Continu " + String(direction > 0 ? "AVANT" : "ARRIERE") + " " + String(speed) + "mm/min");
      server.send(200, "application/json", "{\"status\":\"continuous\"}");
      
    } else {
      Serial.println("→ Mode DISTANCE: " + String(distance) + "mm");
      float newTarget = currentPosition + distance;
      
      if (!checkLimits(newTarget)) {
        Serial.println("❌ Limite dépassée!");
        server.send(400, "application/json", "{\"error\":\"limit_exceeded\"}");
        return;
      }
      
      stepper.setMaxSpeed(speedStepsPerSec);
      stepper.setAcceleration(speedStepsPerSec * 3);
      
      long steps = (long)(distance * STEPS_PER_MM);
      stepper.move(steps);
      
      targetPosition = newTarget;
      movingToTarget = true;
      isRunning = true;
      
      Serial.println("Steps: " + String(steps) + ", Cible: " + String(newTarget, 3) + "mm");
      
      logToFile("Distance " + String(distance) + "mm vers " + String(newTarget, 3) + "mm à " + String(speed) + "mm/min");
      server.send(200, "application/json", "{\"status\":\"moving\"}");
    }
  });

  // API Speed
  server.on("/api/speed", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    
    Serial.println("\n=== API SPEED ===");
    Serial.println("Body: " + body);
    
    float newSpeed = DEFAULT_SPEED;
    if (body.indexOf("\"speed\":") >= 0) {
      int start = body.indexOf("\"speed\":") + 8;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newSpeed = body.substring(start, end).toFloat();
    }
    
    float newSpeedSteps = (newSpeed * STEPS_PER_MM) / 60.0;
    
    Serial.println("Ancienne vitesse: " + String(currentSpeed) + " mm/min");
    Serial.println("Nouvelle vitesse: " + String(newSpeed) + " mm/min = " + String(newSpeedSteps) + " steps/sec");
    
    currentSpeed = newSpeed;
    
    if (continuousMode && isRunning) {
      Serial.println("→ Mode CONTINU: Mise à jour vitesse immédiate");
      stepper.setMaxSpeed(newSpeedSteps * 2);
      stepper.setSpeed(moveDirection > 0 ? newSpeedSteps : -newSpeedSteps);
      Serial.println("→ SetSpeed: " + String(moveDirection > 0 ? newSpeedSteps : -newSpeedSteps) + " steps/sec");
      
    } else if (movingToTarget && isRunning) {
      Serial.println("→ Mode DISTANCE: Mise à jour vitesse max");
      stepper.setMaxSpeed(newSpeedSteps);
      stepper.setAcceleration(newSpeedSteps * 3);
      Serial.println("→ MaxSpeed: " + String(newSpeedSteps) + " steps/sec");
      Serial.println("→ Acceleration: " + String(newSpeedSteps * 3) + " steps/sec²");
      
    } else {
      Serial.println("→ Moteur arrêté: vitesse sauvée");
    }
    
    logToFile("Vitesse changée: " + String(newSpeed) + "mm/min");
    server.send(200, "application/json", "{\"status\":\"speed_updated\",\"speed\":" + String(newSpeed) + "}");
  });

  // API Stop
  server.on("/api/stop", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("\n=== API STOP ===");
    stopMotor();
    logToFile("ARRÊT utilisateur");
    server.send(200, "application/json", "{\"status\":\"stopped\"}");
  });

  // API Home
  server.on("/api/home", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("\n=== API HOME ===");
    
    currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
    float distanceToHome = -currentPosition;
    
    Serial.println("Position actuelle: " + String(currentPosition, 3) + "mm");
    Serial.println("Distance vers origine: " + String(distanceToHome, 3) + "mm");
    
    if (abs(distanceToHome) < 0.001) {
      Serial.println("Déjà à l'origine");
      server.send(200, "application/json", "{\"status\":\"already_home\"}");
      return;
    }
    
    stopMotor();
    delay(10);
    
    // Vitesse fixe pour le homing: 180 mm/min
    float homeSpeed = (180.0 * STEPS_PER_MM) / 60.0;  // 600 steps/sec
    stepper.setMaxSpeed(homeSpeed);
    stepper.setAcceleration(homeSpeed * 3);
    
    long steps = (long)(distanceToHome * STEPS_PER_MM);
    stepper.move(steps);
    
    targetPosition = 0.0;
    movingToTarget = true;
    isRunning = true;
    currentSpeed = 180;
    
    Serial.println("Démarrage retour origine...");
    
    logToFile("Retour origine: " + String(distanceToHome, 3) + "mm");
    server.send(200, "application/json", "{\"status\":\"homing\"}");
  });

  // API Reset
  server.on("/api/reset", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("\n=== API RESET ===");
    stopMotor();
    stepper.setCurrentPosition(0);
    currentPosition = 0.0;
    targetPosition = 0.0;
    Serial.println("Position reset à 0mm");
    logToFile("Position reset à 0mm");
    server.send(200, "application/json", "{\"status\":\"reset\"}");
  });

  // API Limits
  server.on("/api/limits", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    
    Serial.println("\n=== API LIMITS ===");
    
    float newMin = SOFT_LIMIT_MIN;
    float newMax = SOFT_LIMIT_MAX;
    
    if (body.indexOf("\"min\":") >= 0) {
      int start = body.indexOf("\"min\":") + 6;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newMin = body.substring(start, end).toFloat();
    }
    if (body.indexOf("\"max\":") >= 0) {
      int start = body.indexOf("\"max\":") + 6;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newMax = body.substring(start, end).toFloat();
    }
    
    if (newMin < newMax) {
      SOFT_LIMIT_MIN = newMin;
      SOFT_LIMIT_MAX = newMax;
      Serial.println("Nouvelles limites: " + String(newMin) + " à " + String(newMax) + "mm");
      logToFile("Limites: " + String(newMin) + " à " + String(newMax) + "mm");
      server.send(200, "application/json", "{\"status\":\"limits_updated\"}");
    } else {
      Serial.println("❌ Limites invalides!");
      server.send(400, "application/json", "{\"error\":\"invalid_limits\"}");
    }
  });

  // API Limits Toggle
  server.on("/api/limits/toggle", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    SOFT_LIMITS_ENABLED = !SOFT_LIMITS_ENABLED;
    limitError = false;
    Serial.println("\n=== LIMITES " + String(SOFT_LIMITS_ENABLED ? "ACTIVÉES" : "DÉSACTIVÉES") + " ===");
    logToFile("Limites " + String(SOFT_LIMITS_ENABLED ? "ON" : "OFF"));
    server.send(200, "application/json", "{\"status\":\"" + String(SOFT_LIMITS_ENABLED ? "enabled" : "disabled") + "\"}");
  });

  // API Logs
  server.on("/api/logs", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    File logFile = SPIFFS.open("/stepper.log", "r");
    if (logFile) {
      String logs = logFile.readString();
      logFile.close();
      
      // Garder seulement les 50 dernières lignes
      int lineCount = 0;
      int lastNewline = logs.length();
      for (int i = logs.length() - 1; i >= 0 && lineCount < 50; i--) {
        if (logs[i] == '\n') {
          lineCount++;
          if (lineCount == 50) {
            lastNewline = i + 1;
            break;
          }
        }
      }
      
      String recentLogs = logs.substring(lastNewline);
      server.send(200, "text/plain", recentLogs);
    } else {
      server.send(200, "text/plain", "Aucun log disponible");
    }
  });
  
  // CORS Preflight
  server.on("/api/move", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  
  server.on("/api/speed", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  
  server.on("/api/stop", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  
  server.on("/api/home", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  
  server.on("/api/reset", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  
  server.on("/api/limits", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  
  server.on("/api/limits/toggle", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  if (isRunning) {
    if (continuousMode) {
      // MOUVEMENT CONTINU - runSpeed() à chaque itération
      stepper.runSpeed();
      
      // Vérification limites toutes les 100ms
      static unsigned long lastCheck = 0;
      if (millis() - lastCheck > 100) {
        lastCheck = millis();
        currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
        
        // Vérification limites
        if (SOFT_LIMITS_ENABLED) {
          if ((moveDirection > 0 && currentPosition >= SOFT_LIMIT_MAX) ||
              (moveDirection < 0 && currentPosition <= SOFT_LIMIT_MIN)) {
            Serial.println("\n⚠️ LIMITE ATTEINTE - ARRÊT AUTO");
            Serial.println("Position: " + String(currentPosition, 3) + "mm");
            stopMotor();
            limitError = true;
            logToFile("LIMITE ATTEINTE: " + String(currentPosition, 3) + "mm");
          }
        }
      }
      
    } else if (movingToTarget) {
      // MOUVEMENT DISTANCE - run() à chaque itération
      stepper.run();
      
      // Vérification arrivée toutes les 50ms
      static unsigned long lastCheck = 0;
      if (millis() - lastCheck > 50) {
        lastCheck = millis();
        currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
        
        if (stepper.distanceToGo() == 0) {
          Serial.println("\n✅ DESTINATION ATTEINTE");
          Serial.println("Position finale: " + String(currentPosition, 3) + "mm");
          currentPosition = targetPosition;
          isRunning = false;
          movingToTarget = false;
          logToFile("Arrivé à: " + String(currentPosition, 3) + "mm");
        }
      }
    }
  }
  
  // PAS DE DELAY ICI - Critique pour AccelStepper
}