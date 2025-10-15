#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>
#include <SPIFFS.h>
#include <DNSServer.h>
#include <Preferences.h>

// ===== CONFIGURATION RÉSEAU =====
const char* ap_ssid = "ESP32-Stepper";
const char* ap_password = "stepper123";
const char* admin_password = "admin123";

// ===== PINS MOTEUR =====
#define PULSE_PIN 4
#define DIR_PIN 2

// ===== CONFIGURATION MOTEUR (sauvegardée) =====
float STEPS_PER_REVOLUTION = 200.0;  // Steps moteur (200 = 1.8°)
float MICROSTEPS = 1.0;               // Microstepping driver
float LEAD_SCREW_PITCH = 2.0;         // Pas de vis en mm
float STEPS_PER_MM = 100.0;           // Calculé automatiquement

// ===== CONFIGURATION VITESSES (sauvegardée) =====
float SPEED_MIN = 50.0;       // Vitesse minimale mm/min
float SPEED_MAX = 2000.0;     // Vitesse maximale mm/min
float SPEED_DEFAULT = 300.0;  // Vitesse par défaut
float SPEED_HOME = 600.0;     // Vitesse retour origine

// ===== LIMITES LOGICIELLES =====
float SOFT_LIMIT_MIN = -100.0;
float SOFT_LIMIT_MAX = 100.0;
bool SOFT_LIMITS_ENABLED = true;

// ===== OBJETS =====
AccelStepper stepper(AccelStepper::DRIVER, PULSE_PIN, DIR_PIN);
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// ===== VARIABLES GLOBALES =====
bool isRunning = false;
bool movingToTarget = false;
bool continuousMode = false;
bool adminUnlocked = false;
int moveDirection = 1;

float currentPosition = 0.0;
float targetPosition = 0.0;
float currentSpeed = 300.0;

unsigned long sessionStart = 0;

const byte DNS_PORT = 53;

// ===== FONCTIONS UTILITAIRES =====

void calculateStepsPerMm() {
  STEPS_PER_MM = (STEPS_PER_REVOLUTION * MICROSTEPS) / LEAD_SCREW_PITCH;
  Serial.println("Steps/mm calculé: " + String(STEPS_PER_MM));
}

void saveConfig() {
  preferences.begin("stepper", false);
  preferences.putFloat("steps_rev", STEPS_PER_REVOLUTION);
  preferences.putFloat("microsteps", MICROSTEPS);
  preferences.putFloat("pitch", LEAD_SCREW_PITCH);
  preferences.putFloat("speed_min", SPEED_MIN);
  preferences.putFloat("speed_max", SPEED_MAX);
  preferences.putFloat("speed_def", SPEED_DEFAULT);
  preferences.putFloat("speed_home", SPEED_HOME);
  preferences.end();
  Serial.println("✅ Configuration sauvegardée");
}

void loadConfig() {
  preferences.begin("stepper", true);
  STEPS_PER_REVOLUTION = preferences.getFloat("steps_rev", 200.0);
  MICROSTEPS = preferences.getFloat("microsteps", 1.0);
  LEAD_SCREW_PITCH = preferences.getFloat("pitch", 2.0);
  SPEED_MIN = preferences.getFloat("speed_min", 50.0);
  SPEED_MAX = preferences.getFloat("speed_max", 2000.0);
  SPEED_DEFAULT = preferences.getFloat("speed_def", 300.0);
  SPEED_HOME = preferences.getFloat("speed_home", 600.0);
  preferences.end();
  
  calculateStepsPerMm();
  currentSpeed = SPEED_DEFAULT;
  Serial.println("✅ Configuration chargée");
}

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

// ===== SETUP =====

void setup() {
  Serial.begin(115200);
  
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ Erreur SPIFFS");
  }
  
  sessionStart = millis();
  loadConfig();
  logToFile("=== DÉMARRAGE ESP32 ===");

  stepper.setMaxSpeed(10000);
  stepper.setAcceleration(5000);
  stepper.setCurrentPosition(0);
  currentPosition = 0.0;
  targetPosition = 0.0;

  Serial.println("=== STEPPER ESP32 DÉMARRÉ ===");
  Serial.println("Steps/mm: " + String(STEPS_PER_MM));

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);

  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  Serial.println("WiFi AP: " + String(ap_ssid));
  Serial.println("IP: " + WiFi.softAPIP().toString());

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  
  setupWebServer();
  server.begin();
  Serial.println("Interface: http://192.168.4.1");
}

// ===== WEB SERVER =====

void setupWebServer() {
  
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });

  server.on("/", []() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Stepper Controller</title>
    <style>
        * { box-sizing: border-box; }
        body { 
            font-family: Arial, sans-serif; 
            margin: 0; 
            padding: 20px; 
            background: #f0f0f0; 
        }
        .container { 
            max-width: 900px; 
            margin: 0 auto; 
            background: white; 
            padding: 20px; 
            border-radius: 10px; 
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1 { 
            color: #333; 
            margin-top: 0; 
        }
        .tabs { 
            display: flex; 
            border-bottom: 2px solid #dee2e6; 
            margin-bottom: 20px; 
        }
        .tab { 
            padding: 12px 24px; 
            cursor: pointer; 
            background: #f8f9fa; 
            border: none; 
            border-bottom: 3px solid transparent; 
            font-weight: bold; 
            font-size: 15px;
        }
        .tab.active { 
            border-bottom-color: #007bff; 
            color: #007bff; 
            background: white; 
        }
        .tab-content { 
            display: none; 
        }
        .tab-content.active { 
            display: block; 
        }
        .panel { 
            background: #f8f9fa; 
            padding: 20px; 
            border-radius: 8px; 
            margin-bottom: 20px; 
        }
        .panel h3 { 
            margin-top: 0; 
            color: #495057;
        }
        .status-panel { 
            background: #e8f4fd; 
        }
        .admin-panel { 
            background: #fff3cd; 
            border: 2px solid #ffc107; 
        }
        .admin-locked { 
            background: #f8d7da; 
            padding: 30px; 
            text-align: center; 
            border: 2px solid #dc3545; 
        }
        input[type="number"], input[type="password"] { 
            padding: 10px; 
            margin: 5px; 
            border: 1px solid #ccc; 
            border-radius: 5px; 
            font-size: 14px;
        }
        input[type="number"] { 
            width: 120px; 
        }
        input[type="password"] { 
            width: 200px; 
            font-size: 16px;
        }
        button { 
            padding: 10px 20px; 
            margin: 5px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer; 
            font-weight: bold; 
            font-size: 14px;
        }
        .btn-primary { background: #007bff; color: white; }
        .btn-success { background: #28a745; color: white; }
        .btn-danger { background: #dc3545; color: white; }
        .btn-warning { background: #ffc107; color: black; }
        .btn-primary:hover { background: #0056b3; }
        .btn-success:hover { background: #1e7e34; }
        .btn-danger:hover { background: #bd2130; }
        .btn-warning:hover { background: #e0a800; }
        .status-value { 
            font-weight: bold; 
            font-size: 18px;
        }
        .running { color: #28a745; }
        .stopped { color: #dc3545; }
        .info-box { 
            background: #d1ecf1; 
            border: 1px solid #bee5eb; 
            padding: 15px; 
            border-radius: 5px; 
            margin: 15px 0; 
        }
        .formula { 
            font-family: monospace; 
            background: #f8f9fa; 
            padding: 5px 10px; 
            border-radius: 3px; 
            display: inline-block; 
        }
        .log { 
            background: #2d3748; 
            color: #e2e8f0; 
            padding: 15px; 
            border-radius: 5px; 
            font-family: monospace; 
            font-size: 12px; 
            max-height: 300px; 
            overflow-y: auto; 
        }
        .grid { 
            display: grid; 
            grid-template-columns: 1fr 1fr; 
            gap: 15px; 
        }
        @media (max-width: 768px) {
            .grid { 
                grid-template-columns: 1fr; 
            }
        }
        label { 
            display: block; 
            margin-bottom: 5px; 
            font-weight: 600; 
            color: #495057;
        }
        small { 
            color: #6c757d; 
            font-size: 12px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔧 Stepper ESP32 Controller</h1>
        
        <div class="tabs">
            <button class="tab active" onclick="switchTab('control')">🎮 Contrôle</button>
            <button class="tab" onclick="switchTab('admin')">⚙️ Admin</button>
            <button class="tab" onclick="switchTab('logs')">📄 Logs</button>
        </div>

        <!-- ONGLET CONTROLE -->
        <div id="tab-control" class="tab-content active">
            <div class="panel status-panel">
                <h3>📊 État du moteur</h3>
                <div class="grid">
                    <div>
                        <strong>État:</strong> <span id="status" class="status-value stopped">Arrêté</span><br>
                        <strong>Position:</strong> <span id="position" class="status-value">0.000</span> mm<br>
                        <strong>Cible:</strong> <span id="target" class="status-value">0.000</span> mm
                    </div>
                    <div>
                        <strong>Vitesse:</strong> <span id="speed" class="status-value">0</span> mm/min<br>
                        <strong>Steps/mm:</strong> <span id="stepsPerMm" class="status-value">100.00</span><br>
                        <strong>Limites:</strong> <span id="limitsStatus">Activées</span>
                    </div>
                </div>
            </div>

            <div class="panel">
                <h3>🎮 Contrôles</h3>
                <div>
                    <label>Vitesse (mm/min):</label>
                    <input type="number" id="speedInput" value="300" step="50">
                    <button class="btn-primary" onclick="updateSpeedNow()">⚡ Appliquer</button>
                </div>
                <div style="margin-top: 15px;">
                    <label>Distance (mm):</label>
                    <input type="number" id="distanceInput" value="10" step="0.1">
                    <button class="btn-primary" onclick="moveDistance()">📏 Déplacer</button>
                </div>
                <div style="margin-top: 15px;">
                    <button class="btn-success" onclick="moveForward()">➡️ Avant Continu</button>
                    <button class="btn-success" onclick="moveBackward()">⬅️ Arrière Continu</button>
                </div>
                <div style="margin-top: 15px;">
                    <button class="btn-danger" onclick="stopMotor()">⏹️ ARRÊT</button>
                    <button class="btn-warning" onclick="homeMotor()">🏠 Origine</button>
                    <button class="btn-warning" onclick="resetPosition()">🔄 Reset</button>
                </div>
            </div>

            <div class="panel">
                <h3>📏 Limites de sécurité</h3>
                <div>
                    <label>Min (mm):</label>
                    <input type="number" id="limitMin" value="-100" step="1">
                    <label>Max (mm):</label>
                    <input type="number" id="limitMax" value="100" step="1">
                </div>
                <div style="margin-top: 10px;">
                    <button class="btn-primary" onclick="setLimits()">✅ Appliquer Limites</button>
                    <button class="btn-warning" onclick="toggleLimits()">🔄 ON/OFF</button>
                </div>
            </div>
        </div>

        <!-- ONGLET ADMIN -->
        <div id="tab-admin" class="tab-content">
            <!-- Écran de verrouillage -->
            <div id="admin-lock" class="panel admin-locked">
                <h3>🔒 Accès Administrateur</h3>
                <p>Entrez le mot de passe pour accéder aux réglages avancés</p>
                <input type="password" id="adminPassword" placeholder="Mot de passe">
                <br>
                <button class="btn-primary" onclick="unlockAdmin()">🔓 Déverrouiller</button>
                <p style="margin-top: 20px; color: #6c757d;">
                    <small>Par défaut: admin123</small>
                </p>
            </div>

            <!-- Contenu admin -->
            <div id="admin-content" style="display: none;">
                
                <!-- CALIBRATION MOTEUR -->
                <div class="panel admin-panel">
                    <h3>🔧 Calibration Moteur</h3>
                    <div>
                        <label>Steps par révolution moteur:</label>
                        <input type="number" id="stepsPerRev" value="200" min="1" step="1">
                        <small>(200 = 1.8°, 400 = 0.9°)</small>
                    </div>
                    <div style="margin-top: 10px;">
                        <label>Microstepping du driver:</label>
                        <input type="number" id="microsteps" value="1" min="1" step="1">
                        <small>(1, 2, 4, 8, 16, 32...)</small>
                    </div>
                    <div style="margin-top: 10px;">
                        <label>Pas de vis (mm/tour):</label>
                        <input type="number" id="leadScrewPitch" value="2" min="0.1" step="0.1">
                        <small>(Distance en 1 tour)</small>
                    </div>
                    <div class="info-box">
                        <strong>Calcul:</strong> 
                        <span class="formula">Steps/mm = (Steps/rev × Microsteps) ÷ Pas de vis</span>
                        <br><br>
                        <strong>Résultat:</strong> 
                        <span id="calculatedSteps" style="font-size: 20px; color: #007bff;">100.00</span> steps/mm
                    </div>
                </div>

                <!-- CONFIGURATION VITESSES -->
                <div class="panel admin-panel">
                    <h3>⚡ Configuration des Vitesses</h3>
                    <div class="grid">
                        <div>
                            <label>Vitesse MINIMALE:</label>
                            <input type="number" id="speedMin" value="50" min="1" step="10">
                            <small>mm/min - Limite basse</small>
                        </div>
                        <div>
                            <label>Vitesse MAXIMALE:</label>
                            <input type="number" id="speedMax" value="2000" min="100" step="100">
                            <small>mm/min - Limite haute</small>
                        </div>
                        <div>
                            <label>Vitesse PAR DÉFAUT:</label>
                            <input type="number" id="speedDefault" value="300" min="1" step="50">
                            <small>mm/min - Au démarrage</small>
                        </div>
                        <div>
                            <label>Vitesse RETOUR ORIGINE:</label>
                            <input type="number" id="speedHome" value="600" min="1" step="50">
                            <small>mm/min - Bouton 🏠</small>
                        </div>
                    </div>
                    <div class="info-box" style="background: #fff3cd; border-color: #ffc107;">
                        <strong>💡 Vitesses typiques:</strong><br>
                        • <strong>Précision:</strong> 50-200 mm/min<br>
                        • <strong>Normal:</strong> 300-600 mm/min<br>
                        • <strong>Rapide:</strong> 800-1500 mm/min<br>
                        • <strong>Maximum:</strong> 2000-5000 mm/min
                    </div>
                </div>

                <!-- BOUTONS ADMIN -->
                <div style="text-align: center; margin-top: 20px;">
                    <button class="btn-success" style="font-size: 16px; padding: 15px 30px;" onclick="applyCalibration()">
                        ✅ APPLIQUER ET SAUVEGARDER
                    </button>
                    <button class="btn-warning" onclick="loadCalibration()">🔄 Recharger</button>
                    <button class="btn-danger" onclick="lockAdmin()">🔒 Verrouiller</button>
                </div>

                <!-- EXEMPLES -->
                <div class="info-box" style="background: #d4edda; border-color: #c3e6cb; margin-top: 20px;">
                    <strong>💡 Exemples de configurations:</strong><br><br>
                    <strong>Config 1 - Précision haute:</strong><br>
                    • Nema 17 + 1/16 microstep + vis M8 (1.25mm) = 2560 steps/mm<br>
                    • Vitesses: Min=20, Max=500, Défaut=100, Origine=200<br><br>
                    <strong>Config 2 - Polyvalent:</strong><br>
                    • Nema 17 + 1/8 microstep + vis trapèze (2mm) = 800 steps/mm<br>
                    • Vitesses: Min=50, Max=2000, Défaut=300, Origine=600<br><br>
                    <strong>Config 3 - Rapide:</strong><br>
                    • Nema 17 + pas entier + courroie GT2 (2mm) = 100 steps/mm<br>
                    • Vitesses: Min=100, Max=5000, Défaut=1000, Origine=2000
                </div>
            </div>
        </div>

        <!-- ONGLET LOGS -->
        <div id="tab-logs" class="tab-content">
            <div class="panel">
                <h3>📄 Logs système</h3>
                <div class="info-box">
                    <strong>ℹ️ Les logs sont permanents</strong><br>
                    Stockés dans la mémoire Flash - Conservés après redémarrage
                </div>
                <div id="logs" class="log">Chargement...</div>
                <div style="margin-top: 10px;">
                    <button class="btn-primary" onclick="loadLogs()">🔄 Actualiser</button>
                    <button class="btn-danger" onclick="clearLogs()">🗑️ Effacer</button>
                </div>
            </div>
        </div>
    </div>

    <script>
        let currentTab = 'control';
        let adminUnlocked = false;

        // ===== GESTION DES ONGLETS =====
        function switchTab(tabName) {
            document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(el => el.classList.remove('active'));
            document.getElementById('tab-' + tabName).classList.add('active');
            event.target.classList.add('active');
            currentTab = tabName;

            if (tabName === 'logs') loadLogs();
            if (tabName === 'admin' && adminUnlocked) loadCalibration();
        }

        // ===== ADMIN =====
        function unlockAdmin() {
            const password = document.getElementById('adminPassword').value;
            fetch('/api/admin/unlock', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ password: password })
            })
            .then(res => res.json())
            .then(data => {
                if (data.status === 'unlocked') {
                    adminUnlocked = true;
                    document.getElementById('admin-lock').style.display = 'none';
                    document.getElementById('admin-content').style.display = 'block';
                    alert('✅ Accès déverrouillé');
                    loadCalibration();
                } else {
                    alert('❌ Mot de passe incorrect');
                    document.getElementById('adminPassword').value = '';
                }
            });
        }

        function lockAdmin() {
            adminUnlocked = false;
            document.getElementById('admin-lock').style.display = 'block';
            document.getElementById('admin-content').style.display = 'none';
            document.getElementById('adminPassword').value = '';
            fetch('/api/admin/lock', { method: 'POST' });
        }

        // Touche Entrée pour déverrouiller
        document.addEventListener('DOMContentLoaded', () => {
            const pwdInput = document.getElementById('adminPassword');
            if (pwdInput) {
                pwdInput.addEventListener('keypress', (e) => {
                    if (e.key === 'Enter') unlockAdmin();
                });
            }
            ['stepsPerRev', 'microsteps', 'leadScrewPitch'].forEach(id => {
                const el = document.getElementById(id);
                if (el) el.addEventListener('input', updateCalculatedSteps);
            });
        });

        // ===== CALCUL STEPS/MM =====
        function updateCalculatedSteps() {
            const stepsRev = parseFloat(document.getElementById('stepsPerRev').value);
            const microsteps = parseFloat(document.getElementById('microsteps').value);
            const pitch = parseFloat(document.getElementById('leadScrewPitch').value);
            
            if (!isNaN(stepsRev) && !isNaN(microsteps) && !isNaN(pitch) && pitch > 0) {
                const result = (stepsRev * microsteps) / pitch;
                document.getElementById('calculatedSteps').textContent = result.toFixed(2);
            }
        }

        // ===== CALIBRATION =====
        async function applyCalibration() {
            const stepsRev = parseFloat(document.getElementById('stepsPerRev').value);
            const microsteps = parseFloat(document.getElementById('microsteps').value);
            const pitch = parseFloat(document.getElementById('leadScrewPitch').value);
            const speedMin = parseFloat(document.getElementById('speedMin').value);
            const speedMax = parseFloat(document.getElementById('speedMax').value);
            const speedDefault = parseFloat(document.getElementById('speedDefault').value);
            const speedHome = parseFloat(document.getElementById('speedHome').value);

            if (isNaN(stepsRev) || isNaN(microsteps) || isNaN(pitch) || 
                isNaN(speedMin) || isNaN(speedMax) || isNaN(speedDefault) || isNaN(speedHome)) {
                alert('❌ Valeurs invalides');
                return;
            }

            if (speedMin >= speedMax) {
                alert('❌ Vitesse min doit être < max');
                return;
            }

            if (speedDefault < speedMin || speedDefault > speedMax) {
                alert('❌ Vitesse par défaut doit être entre min et max');
                return;
            }

            const stepsPerMm = ((stepsRev * microsteps) / pitch).toFixed(2);

            if (!confirm('⚠️ Appliquer cette configuration?\n\n' +
                `Steps/mm: ${stepsPerMm}\n` +
                `Vitesses: ${speedMin} - ${speedMax} mm/min\n` +
                `Défaut: ${speedDefault} mm/min\n\n` +
                'Le moteur sera arrêté et remis à 0.')) {
                return;
            }

            const result = await apiCall('calibration', {
                stepsPerRev: stepsRev,
                microsteps: microsteps,
                pitch: pitch,
                speedMin: speedMin,
                speedMax: speedMax,
                speedDefault: speedDefault,
                speedHome: speedHome
            });

            if (result && result.status === 'calibration_updated') {
                alert(`✅ Configuration sauvegardée!\n\nSteps/mm: ${result.stepsPerMm}`);
                document.getElementById('speedInput').value = speedDefault;
                document.getElementById('speedInput').min = speedMin;
                document.getElementById('speedInput').max = speedMax;
                updateStatus();
            }
        }

        async function loadCalibration() {
            const result = await apiCall('calibration');
            if (result) {
                document.getElementById('stepsPerRev').value = result.stepsPerRev;
                document.getElementById('microsteps').value = result.microsteps;
                document.getElementById('leadScrewPitch').value = result.pitch;
                document.getElementById('speedMin').value = result.speedMin;
                document.getElementById('speedMax').value = result.speedMax;
                document.getElementById('speedDefault').value = result.speedDefault;
                document.getElementById('speedHome').value = result.speedHome;
                
                document.getElementById('speedInput').min = result.speedMin;
                document.getElementById('speedInput').max = result.speedMax;
                document.getElementById('speedInput').value = result.speedDefault;
                
                updateCalculatedSteps();
            }
        }

        // ===== CONTRÔLES MOTEUR =====
        async function moveDistance() {
            const distance = parseFloat(document.getElementById('distanceInput').value);
            const speed = parseFloat(document.getElementById('speedInput').value);
            
            if (isNaN(distance) || distance === 0) {
                alert('Distance invalide');
                return;
            }
            
            await apiCall('move', { distance: distance, speed: speed });
        }

        async function moveForward() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            await apiCall('move', { continuous: true, direction: 1, speed: speed });
        }

        async function moveBackward() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            await apiCall('move', { continuous: true, direction: -1, speed: speed });
        }

        async function stopMotor() {
            await apiCall('stop', { action: 'stop' });
        }

        async function homeMotor() {
            if (confirm('Retourner à la position 0mm ?')) {
                await apiCall('home', { action: 'home' });
            }
        }

        async function resetPosition() {
            if (confirm('Définir la position actuelle comme 0mm ?')) {
                await apiCall('reset', { action: 'reset' });
            }
        }

        async function updateSpeedNow() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            const status = await apiCall('status');
            const speedMin = status ? status.speedMin : 50;
            const speedMax = status ? status.speedMax : 2000;
            
            if (isNaN(speed) || speed < speedMin || speed > speedMax) {
                alert(`Vitesse invalide! (${speedMin}-${speedMax} mm/min)`);
                return;
            }
            
            const result = await apiCall('speed', { speed: speed });
            if (result && result.status === 'speed_updated') {
                alert(`✅ Vitesse: ${speed} mm/min`);
            }
        }

        // ===== LIMITES =====
        async function setLimits() {
            const min = parseFloat(document.getElementById('limitMin').value);
            const max = parseFloat(document.getElementById('limitMax').value);
            
            if (min >= max) {
                alert('Min doit être < Max');
                return;
            }
            
            await apiCall('limits', { min: min, max: max });
        }

        async function toggleLimits() {
            await apiCall('limits/toggle', { action: 'toggle' });
        }

        // ===== LOGS =====
        async function loadLogs() {
            try {
                const response = await fetch('/api/logs');
                const text = await response.text();
                const logsDiv = document.getElementById('logs');
                
                if (text && text.length > 0) {
                    logsDiv.innerHTML = text.replace(/\n/g, '<br>');
                    logsDiv.scrollTop = logsDiv.scrollHeight;
                } else {
                    logsDiv.innerHTML = '<em>Aucun log</em>';
                }
            } catch (error) {
                document.getElementById('logs').innerHTML = 'Erreur: ' + error.message;
            }
        }

        async function clearLogs() {
            if (!confirm('⚠️ Effacer tous les logs?\nIrréversible!')) {
                return;
            }
            
            try {
                const response = await fetch('/api/logs/clear', { method: 'POST' });
                const result = await response.json();
                
                if (result.status === 'cleared') {
                    alert('✅ Logs effacés');
                    loadLogs();
                }
            } catch (error) {
                alert('Erreur: ' + error.message);
            }
        }

        // ===== STATUT =====
        async function updateStatus() {
            const status = await apiCall('status');
            if (status) {
                document.getElementById('status').textContent = status.running ? 'En mouvement' : 'Arrêté';
                document.getElementById('status').className = 'status-value ' + (status.running ? 'running' : 'stopped');
                document.getElementById('position').textContent = status.position.toFixed(3);
                document.getElementById('target').textContent = status.target.toFixed(3);
                document.getElementById('speed').textContent = status.speed.toFixed(0);
                document.getElementById('stepsPerMm').textContent = status.stepsPerMm.toFixed(2);
                document.getElementById('limitsStatus').textContent = status.limitsEnabled ? 'Activées' : 'Désactivées';
                document.getElementById('limitsStatus').style.color = status.limitsEnabled ? '#28a745' : '#dc3545';
                
                const speedInput = document.getElementById('speedInput');
                if (speedInput && status.speedMin && status.speedMax) {
                    speedInput.min = status.speedMin;
                    speedInput.max = status.speedMax;
                }
            }
        }

        // ===== API =====
        async function apiCall(endpoint, data = null) {
            try {
                const config = { method: data ? 'POST' : 'GET' };
                if (data) {
                    config.headers = { 'Content-Type': 'application/json' };
                    config.body = JSON.stringify(data);
                }
                const response = await fetch('/api/' + endpoint, config);
                if (!response.ok) throw new Error(`HTTP ${response.status}`);
                const result = await response.json();
                return result;
            } catch (error) {
                console.error('Erreur API:', error);
                alert('Erreur: ' + error.message);
                return null;
            }
        }

        // ===== INITIALISATION =====
        setInterval(updateStatus, 300);
        setTimeout(() => {
            updateStatus();
            updateCalculatedSteps();
            
            apiCall('calibration').then(result => {
                if (result) {
                    const speedInput = document.getElementById('speedInput');
                    if (speedInput) {
                        speedInput.min = result.speedMin;
                        speedInput.max = result.speedMax;
                        speedInput.value = result.speedDefault;
                    }
                }
            });
        }, 500);
    </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
  });

  // ===== API STATUS =====
  server.on("/api/status", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "{";
    json += "\"running\":" + String(isRunning ? "true" : "false") + ",";
    json += "\"position\":" + String(currentPosition, 3) + ",";
    json += "\"target\":" + String(targetPosition, 3) + ",";
    json += "\"speed\":" + String(currentSpeed) + ",";
    json += "\"steps\":" + String(stepper.currentPosition()) + ",";
    json += "\"remaining\":" + String(stepper.distanceToGo()) + ",";
    json += "\"stepsPerMm\":" + String(STEPS_PER_MM, 2) + ",";
    json += "\"speedMin\":" + String(SPEED_MIN) + ",";
    json += "\"speedMax\":" + String(SPEED_MAX) + ",";
    json += "\"speedDefault\":" + String(SPEED_DEFAULT) + ",";
    json += "\"limitsEnabled\":" + String(SOFT_LIMITS_ENABLED ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  });

  // ===== API ADMIN UNLOCK =====
  server.on("/api/admin/unlock", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");

    String password = "";
    if (body.indexOf("\"password\":\"") >= 0) {
      int start = body.indexOf("\"password\":\"") + 12;
      int end = body.indexOf("\"", start);
      password = body.substring(start, end);
    }

    if (password == admin_password) {
      adminUnlocked = true;
      Serial.println("✅ Admin déverrouillé");
      logToFile("Admin déverrouillé");
      server.send(200, "application/json", "{\"status\":\"unlocked\"}");
    } else {
      Serial.println("❌ Mot de passe incorrect");
      logToFile("Tentative accès admin échouée");
      server.send(403, "application/json", "{\"status\":\"wrong_password\"}");
    }
  });

  // ===== API ADMIN LOCK =====
  server.on("/api/admin/lock", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    adminUnlocked = false;
    Serial.println("🔒 Admin verrouillé");
    logToFile("Admin verrouillé");
    server.send(200, "application/json", "{\"status\":\"locked\"}");
  });

  // ===== API CALIBRATION GET =====
  server.on("/api/calibration", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "{";
    json += "\"stepsPerRev\":" + String(STEPS_PER_REVOLUTION, 1) + ",";
    json += "\"microsteps\":" + String(MICROSTEPS, 1) + ",";
    json += "\"pitch\":" + String(LEAD_SCREW_PITCH, 2) + ",";
    json += "\"stepsPerMm\":" + String(STEPS_PER_MM, 2) + ",";
    json += "\"speedMin\":" + String(SPEED_MIN, 0) + ",";
    json += "\"speedMax\":" + String(SPEED_MAX, 0) + ",";
    json += "\"speedDefault\":" + String(SPEED_DEFAULT, 0) + ",";
    json += "\"speedHome\":" + String(SPEED_HOME, 0);
    json += "}";
    server.send(200, "application/json", json);
  });

  // ===== API CALIBRATION POST =====
  server.on("/api/calibration", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    if (!adminUnlocked) {
      Serial.println("❌ Accès refusé");
      server.send(403, "application/json", "{\"error\":\"admin_locked\"}");
      return;
    }
    
    String body = server.arg("plain");
    stopMotor();

    float newStepsRev = STEPS_PER_REVOLUTION;
    float newMicrosteps = MICROSTEPS;
    float newPitch = LEAD_SCREW_PITCH;
    float newSpeedMin = SPEED_MIN;
    float newSpeedMax = SPEED_MAX;
    float newSpeedDefault = SPEED_DEFAULT;
    float newSpeedHome = SPEED_HOME;

    // Parse JSON
    if (body.indexOf("\"stepsPerRev\":") >= 0) {
      int start = body.indexOf("\"stepsPerRev\":") + 14;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newStepsRev = body.substring(start, end).toFloat();
    }

    if (body.indexOf("\"microsteps\":") >= 0) {
      int start = body.indexOf("\"microsteps\":") + 13;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newMicrosteps = body.substring(start, end).toFloat();
    }

    if (body.indexOf("\"pitch\":") >= 0) {
      int start = body.indexOf("\"pitch\":") + 8;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newPitch = body.substring(start, end).toFloat();
    }

    if (body.indexOf("\"speedMin\":") >= 0) {
      int start = body.indexOf("\"speedMin\":") + 11;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newSpeedMin = body.substring(start, end).toFloat();
    }

    if (body.indexOf("\"speedMax\":") >= 0) {
      int start = body.indexOf("\"speedMax\":") + 11;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newSpeedMax = body.substring(start, end).toFloat();
    }

    if (body.indexOf("\"speedDefault\":") >= 0) {
      int start = body.indexOf("\"speedDefault\":") + 15;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newSpeedDefault = body.substring(start, end).toFloat();
    }

    if (body.indexOf("\"speedHome\":") >= 0) {
      int start = body.indexOf("\"speedHome\":") + 12;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newSpeedHome = body.substring(start, end).toFloat();
    }

    // Validation
    if (newStepsRev <= 0 || newMicrosteps <= 0 || newPitch <= 0) {
      server.send(400, "application/json", "{\"error\":\"invalid_calibration\"}");
      return;
    }

    if (newSpeedMin <= 0 || newSpeedMax <= 0 || newSpeedMin >= newSpeedMax) {
      server.send(400, "application/json", "{\"error\":\"invalid_speed\"}");
      return;
    }

    // Appliquer
    STEPS_PER_REVOLUTION = newStepsRev;
    MICROSTEPS = newMicrosteps;
    LEAD_SCREW_PITCH = newPitch;
    SPEED_MIN = newSpeedMin;
    SPEED_MAX = newSpeedMax;
    SPEED_DEFAULT = newSpeedDefault;
    SPEED_HOME = newSpeedHome;
    
    calculateStepsPerMm();
    saveConfig();

    stepper.setCurrentPosition(0);
    currentPosition = 0.0;
    targetPosition = 0.0;
    currentSpeed = SPEED_DEFAULT;

    Serial.println("✅ Configuration appliquée");
    logToFile("Config: " + String(STEPS_PER_MM, 2) + " steps/mm");

    String json = "{";
    json += "\"status\":\"calibration_updated\",";
    json += "\"stepsPerMm\":" + String(STEPS_PER_MM, 2) + ",";
    json += "\"speedDefault\":" + String(SPEED_DEFAULT, 0);
    json += "}";
    
    server.send(200, "application/json", json);
  });

  // ===== API MOVE =====
  server.on("/api/move", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");

    float speed = SPEED_DEFAULT;
    float distance = 0;
    int direction = 1;
    bool continuous = false;

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

    if (isRunning) {
      stepper.stop();
      while (stepper.isRunning()) {
        stepper.run();
      }
    }

    isRunning = false;
    movingToTarget = false;
    continuousMode = false;
    currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
    currentSpeed = speed;

    float speedStepsPerSec = (speed * STEPS_PER_MM) / 60.0;

    if (continuous) {
      continuousMode = true;
      moveDirection = direction;
      stepper.setMaxSpeed(speedStepsPerSec * 2);
      stepper.setSpeed(direction > 0 ? speedStepsPerSec : -speedStepsPerSec);
      isRunning = true;
      logToFile("Continu " + String(direction > 0 ? "avant" : "arrière"));
      server.send(200, "application/json", "{\"status\":\"continuous\"}");
    } else {
      float newTarget = currentPosition + distance;
      if (!checkLimits(newTarget)) {
        server.send(400, "application/json", "{\"error\":\"limit_exceeded\"}");
        return;
      }

      stepper.setMaxSpeed(speedStepsPerSec);
      stepper.setAcceleration(speedStepsPerSec * 2);
      long steps = (long)(distance * STEPS_PER_MM);
      stepper.move(steps);
      targetPosition = newTarget;
      movingToTarget = true;
      isRunning = true;
      logToFile("Distance " + String(distance) + "mm");
      server.send(200, "application/json", "{\"status\":\"moving\"}");
    }
  });

  // ===== API SPEED =====
  server.on("/api/speed", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");

    float newSpeed = SPEED_DEFAULT;
    if (body.indexOf("\"speed\":") >= 0) {
      int start = body.indexOf("\"speed\":") + 8;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newSpeed = body.substring(start, end).toFloat();
    }

    float newSpeedSteps = (newSpeed * STEPS_PER_MM) / 60.0;
    currentSpeed = newSpeed;

    if (continuousMode && isRunning) {
      stepper.setMaxSpeed(newSpeedSteps * 2);
      float finalSpeed = moveDirection > 0 ? newSpeedSteps : -newSpeedSteps;
      stepper.setSpeed(finalSpeed);
    } else if (movingToTarget && isRunning) {
      stepper.setMaxSpeed(newSpeedSteps);
      stepper.setAcceleration(newSpeedSteps * 2);
    }

    logToFile("Vitesse: " + String(newSpeed) + "mm/min");
    server.send(200, "application/json", "{\"status\":\"speed_updated\",\"speed\":" + String(newSpeed) + "}");
  });

  // ===== API STOP =====
  server.on("/api/stop", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    stopMotor();
    logToFile("ARRÊT");
    server.send(200, "application/json", "{\"status\":\"stopped\"}");
  });

  // ===== API HOME =====
  server.on("/api/home", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");

    currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
    float distanceToHome = -currentPosition;

    if (abs(distanceToHome) < 0.001) {
      server.send(200, "application/json", "{\"status\":\"already_home\"}");
      return;
    }

    stopMotor();
    delay(10);

    float homeSpeed = (SPEED_HOME * STEPS_PER_MM) / 60.0;
    stepper.setMaxSpeed(homeSpeed);
    stepper.setAcceleration(homeSpeed * 2);

    long steps = (long)(distanceToHome * STEPS_PER_MM);
    stepper.move(steps);

    targetPosition = 0.0;
    movingToTarget = true;
    isRunning = true;
    currentSpeed = SPEED_HOME;

    logToFile("Retour origine");
    server.send(200, "application/json", "{\"status\":\"homing\"}");
  });

  // ===== API RESET =====
  server.on("/api/reset", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    stopMotor();
    stepper.setCurrentPosition(0);
    currentPosition = 0.0;
    targetPosition = 0.0;
    logToFile("Position reset");
    server.send(200, "application/json", "{\"status\":\"reset\"}");
  });

  // ===== API LIMITS =====
  server.on("/api/limits", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");

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
      logToFile("Limites: " + String(newMin) + " à " + String(newMax));
      server.send(200, "application/json", "{\"status\":\"limits_updated\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"invalid_limits\"}");
    }
  });

  // ===== API LIMITS TOGGLE =====
  server.on("/api/limits/toggle", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    SOFT_LIMITS_ENABLED = !SOFT_LIMITS_ENABLED;
    logToFile("Limites " + String(SOFT_LIMITS_ENABLED ? "ON" : "OFF"));
    server.send(200, "application/json", "{\"status\":\"" + String(SOFT_LIMITS_ENABLED ? "enabled" : "disabled") + "\"}");
  });

  // ===== API LOGS =====
  server.on("/api/logs", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    File logFile = SPIFFS.open("/stepper.log", "r");
    if (logFile) {
      String logs = logFile.readString();
      logFile.close();
      server.send(200, "text/plain", logs.length() > 0 ? logs : "Aucun log");
    } else {
      server.send(200, "text/plain", "Fichier introuvable");
    }
  });

  // ===== API LOGS CLEAR =====
  server.on("/api/logs/clear", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (SPIFFS.exists("/stepper.log")) {
      SPIFFS.remove("/stepper.log");
    }
    logToFile("=== LOGS EFFACÉS ===");
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
  });
}

// ===== LOOP =====

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (isRunning) {
    if (continuousMode) {
      stepper.runSpeed();

      static unsigned long lastCheck = 0;
      if (millis() - lastCheck > 100) {
        lastCheck = millis();
        currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;

        if (SOFT_LIMITS_ENABLED) {
          if ((moveDirection > 0 && currentPosition >= SOFT_LIMIT_MAX) || 
              (moveDirection < 0 && currentPosition <= SOFT_LIMIT_MIN)) {
            Serial.println("LIMITE ATTEINTE");
            stopMotor();
          }
        }
      }

    } else if (movingToTarget) {
      stepper.run();

      static unsigned long lastCheck = 0;
      if (millis() - lastCheck > 50) {
        lastCheck = millis();
        currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;

        if (stepper.distanceToGo() == 0) {
          Serial.println("DESTINATION ATTEINTE");
          currentPosition = targetPosition;
          isRunning = false;
          movingToTarget = false;
          logToFile("Arrivé à: " + String(currentPosition, 3) + "mm");
        }
      }
    }
  }
}