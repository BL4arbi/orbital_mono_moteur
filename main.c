#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>
#include <SPIFFS.h>
#include <DNSServer.h>  // Pour le captive portal

// Configuration
const char* ap_ssid = "ESP32-Stepper";
const char* ap_password = "stepper123";

#define PULSE_PIN 4
#define DIR_PIN 2
#define STEPS_PER_MM 800

AccelStepper stepper(AccelStepper::DRIVER, PULSE_PIN, DIR_PIN);
WebServer server(80);
DNSServer dnsServer;  // Serveur DNS pour captive portal

// Configuration DNS
const byte DNS_PORT = 53;

// Variables globales
bool isRunning = false;
float currentPosition = 0.0;
float targetPosition = 0.0;
float currentSpeed = 300.0;  // Vitesse par défaut en mm/min
bool movingToTarget = false;
bool continuousMode = false;
int moveDirection = 1;

// Soft limits
float SOFT_LIMIT_MIN = -100.0;
float SOFT_LIMIT_MAX = 100.0;
bool SOFT_LIMITS_ENABLED = true;
bool limitError = false;

unsigned long sessionStart = 0;
unsigned long lastRunTime = 0;  // Pour le mode continu

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
  
  // Configuration stepper - IMPORTANT: Augmenter les valeurs max
  stepper.setMaxSpeed(10000);  // Augmenté pour permettre des vitesses élevées
  stepper.setAcceleration(5000);  // Augmenté pour une meilleure réactivité
  stepper.setCurrentPosition(0);
  currentPosition = 0.0;
  targetPosition = 0.0;
  
  Serial.println("=== STEPPER ESP32 DÉMARRÉ ===");
  Serial.println("Steps par mm: " + String(STEPS_PER_MM));
  Serial.println("MaxSpeed: 10000 steps/sec");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  // Configuration IP fixe pour l'AP
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  
  Serial.println("WiFi AP: " + String(ap_ssid));
  Serial.println("IP: " + WiFi.softAPIP().toString());
  
  // Démarrer le serveur DNS pour captive portal
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.println("Captive Portal activé - Toute URL redirige vers l'interface");
  
  setupWebServer();
  server.begin();
  Serial.println("Interface: http://192.168.4.1");
}

void setupWebServer() {
  // Captive Portal - Redirection pour toutes les URLs non trouvées
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  
  // Page principale (inchangée)
  server.on("/", []() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="mobile-web-app-capable" content="yes">
    <title>Stepper Controller</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }
        .status { background: #e8f4fd; padding: 15px; border-radius: 5px; margin-bottom: 20px; }
        .controls { background: #f8f9fa; padding: 15px; border-radius: 5px; margin-bottom: 20px; }
        input[type="number"] { padding: 8px; margin: 5px; border: 1px solid #ccc; border-radius: 4px; width: 100px; }
        button { padding: 10px 15px; margin: 5px; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; }
        .btn-primary { background: #007bff; color: white; }
        .btn-success { background: #28a745; color: white; }
        .btn-danger { background: #dc3545; color: white; }
        .btn-warning { background: #ffc107; color: black; }
        .running { color: #28a745; } .stopped { color: #dc3545; }
        .log { background: #2d3748; color: #e2e8f0; padding: 10px; border-radius: 5px; font-family: monospace; font-size: 12px; max-height: 200px; overflow-y: auto; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔧 Stepper ESP32 Controller</h1>
        
        <div class="status">
            <h3>État du moteur</h3>
            <p><strong>État:</strong> <span id="status" class="stopped">Arrêté</span></p>
            <p><strong>Position actuelle:</strong> <span id="position">0.000</span> mm</p>
            <p><strong>Position cible:</strong> <span id="target">0.000</span> mm</p>
            <p><strong>Vitesse:</strong> <span id="speed">0</span> mm/min</p>
            <p><strong>Steps:</strong> <span id="steps">0</span> | <strong>Distance restante:</strong> <span id="remaining">0</span> steps</p>
        </div>

        <div class="controls">
            <h3>Contrôles</h3>
            <p>
                <label>Vitesse (mm/min):</label>
                <input type="number" id="speedInput" value="300" min="50" max="2000" step="50">
                <button class="btn-primary" onclick="updateSpeedNow()">⚡ Appliquer Vitesse</button>
            </p>
            <p>
                <label>Distance (mm):</label>
                <input type="number" id="distanceInput" value="10" step="0.1">
            </p>
            <p>
                <button class="btn-primary" onclick="moveDistance()">📏 Déplacer Distance</button>
                <button class="btn-success" onclick="moveForward()">➡️ Avant Continu</button>
                <button class="btn-success" onclick="moveBackward()">⬅️ Arrière Continu</button>
            </p>
            <p>
                <button class="btn-danger" onclick="stopMotor()">⏹️ ARRÊT</button>
                <button class="btn-warning" onclick="homeMotor()">🏠 Origine</button>
                <button class="btn-warning" onclick="resetPosition()">🔄 Reset Position</button>
            </p>
        </div>

        <div class="controls">
            <h3>Limites</h3>
            <p>
                <label>Min:</label> <input type="number" id="limitMin" value="-100" step="1">
                <label>Max:</label> <input type="number" id="limitMax" value="100" step="1">
                <button class="btn-primary" onclick="setLimits()">Appliquer</button>
                <button class="btn-warning" onclick="toggleLimits()">ON/OFF</button>
            </p>
            <p><strong>Limites:</strong> <span id="limitsStatus">Activées</span></p>
        </div>

        <div>
            <h3>Logs</h3>
            <div id="logs" class="log">Chargement...</div>
            <button class="btn-primary" onclick="loadLogs()">🔄 Actualiser logs</button>
        </div>
    </div>

    <script>
        console.log('=== INTERFACE STEPPER CHARGÉE ===');

        async function apiCall(endpoint, data = null) {
            try {
                console.log(`🔄 API Call: ${endpoint}`, data);
                const config = { method: data ? 'POST' : 'GET' };
                if (data) {
                    config.headers = { 'Content-Type': 'application/json' };
                    config.body = JSON.stringify(data);
                }
                const response = await fetch('/api/' + endpoint, config);
                if (!response.ok) throw new Error(`HTTP ${response.status}`);
                const result = await response.json();
                console.log(`✅ API Response:`, result);
                return result;
            } catch (error) {
                console.error('❌ Erreur API:', error);
                alert('Erreur: ' + error.message);
                return null;
            }
        }

        async function moveDistance() {
            const distance = parseFloat(document.getElementById('distanceInput').value);
            const speed = parseFloat(document.getElementById('speedInput').value);
            
            if (isNaN(distance) || distance === 0) {
                alert('Distance invalide!');
                return;
            }
            
            console.log(`🎯 Mouvement DISTANCE: ${distance}mm à ${speed}mm/min`);
            await apiCall('move', { distance: distance, speed: speed });
        }

        async function moveForward() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            console.log(`➡️ Mouvement CONTINU avant à ${speed}mm/min`);
            await apiCall('move', { continuous: true, direction: 1, speed: speed });
        }

        async function moveBackward() {
            const speed = parseFloat(document.getElementById('speedInput').value);
            console.log(`⬅️ Mouvement CONTINU arrière à ${speed}mm/min`);
            await apiCall('move', { continuous: true, direction: -1, speed: speed });
        }

        async function stopMotor() {
            console.log('⏹️ ARRÊT DEMANDÉ');
            await apiCall('stop', { action: 'stop' });
        }

        async function homeMotor() {
            if (confirm('Retourner à la position 0mm ?')) {
                console.log('🏠 Retour origine');
                await apiCall('home', { action: 'home' });
            }
        }

        async function resetPosition() {
            if (confirm('Définir la position actuelle comme 0mm ?')) {
                console.log('🔄 Reset position');
                await apiCall('reset', { action: 'reset' });
            }
        }

        async function updateSpeedNow() {
            console.log('=== ⚡ BOUTON APPLIQUER VITESSE CLIQUÉ ===');
            
            const speedInput = document.getElementById('speedInput');
            if (!speedInput) {
                console.error('❌ Élément speedInput non trouvé!');
                alert('Erreur: champ vitesse non trouvé');
                return;
            }
            
            const speed = parseFloat(speedInput.value);
            console.log('📊 Valeur lue:', speed);
            
            if (isNaN(speed) || speed < 50 || speed > 2000) {
                console.error('❌ Vitesse invalide:', speed);
                alert('Vitesse invalide! (50-2000 mm/min)');
                return;
            }
            
            console.log(`🚀 Envoi changement vitesse: ${speed}mm/min`);
            
            const result = await apiCall('speed', { speed: speed });
            if (result && result.status === 'speed_updated') {
                console.log('✅ Vitesse mise à jour avec succès');
                alert(`✅ Vitesse mise à jour: ${speed} mm/min`);
            } else {
                console.error('❌ Réponse inattendue:', result);
            }
        }

        async function setLimits() {
            const min = parseFloat(document.getElementById('limitMin').value);
            const max = parseFloat(document.getElementById('limitMax').value);
            if (min >= max) {
                alert('Min doit être < Max!');
                return;
            }
            console.log(`📏 Nouvelles limites: ${min} à ${max}mm`);
            await apiCall('limits', { min: min, max: max });
        }

        async function toggleLimits() {
            console.log('🔄 Toggle limites');
            await apiCall('limits/toggle', { action: 'toggle' });
        }

        async function loadLogs() {
            try {
                console.log('📄 Chargement logs...');
                const response = await fetch('/api/logs');
                const text = await response.text();
                document.getElementById('logs').innerHTML = text.replace(/\n/g, '<br>');
                const logsDiv = document.getElementById('logs');
                logsDiv.scrollTop = logsDiv.scrollHeight;
            } catch (error) {
                document.getElementById('logs').innerHTML = 'Erreur logs: ' + error.message;
            }
        }

        async function updateStatus() {
            const status = await apiCall('status');
            if (status) {
                document.getElementById('status').textContent = status.running ? 'En mouvement' : 'Arrêté';
                document.getElementById('status').className = status.running ? 'running' : 'stopped';
                document.getElementById('position').textContent = status.position.toFixed(3);
                document.getElementById('target').textContent = status.target.toFixed(3);
                document.getElementById('speed').textContent = status.speed.toFixed(0);
                document.getElementById('steps').textContent = status.steps;
                document.getElementById('remaining').textContent = status.remaining;
                document.getElementById('limitsStatus').textContent = status.limitsEnabled ? 'Activées' : 'Désactivées';
                document.getElementById('limitsStatus').style.color = status.limitsEnabled ? '#28a745' : '#dc3545';
            }
        }

        // Démarrage automatique
        setInterval(updateStatus, 300);
        setTimeout(() => {
            console.log('🚀 Initialisation interface...');
            updateStatus();
            loadLogs();
        }, 500);

        console.log('✅ JavaScript stepper prêt');
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

  // API Move - CORRIGÉ
  server.on("/api/move", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    Serial.println("=== API MOVE ===");
    Serial.println("Body: " + body);

    float speed = 300;
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

    // Arrêt propre et mise à jour position
    if (isRunning) {
      stepper.stop();
      while(stepper.isRunning()) {
        stepper.run();
      }
    }
    
    isRunning = false;
    movingToTarget = false;
    continuousMode = false;
    
    // Mise à jour position actuelle
    currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
    
    // Sauvegarde de la vitesse
    currentSpeed = speed;
    
    // Conversion vitesse mm/min -> steps/sec
    float speedStepsPerSec = (speed * STEPS_PER_MM) / 60.0;
    
    Serial.println("Vitesse: " + String(speed) + "mm/min = " + String(speedStepsPerSec) + " steps/sec");
    Serial.println("Distance: " + String(distance) + "mm");
    Serial.println("Continu: " + String(continuous ? "OUI" : "NON"));

    if (continuous) {
      // MOUVEMENT CONTINU - Utilise setSpeed() pour vitesse constante
      Serial.println("→ Mode CONTINU");
      continuousMode = true;
      moveDirection = direction;
      
      // Pour le mode continu, on utilise setSpeed() qui définit une vitesse constante
      // IMPORTANT: setMaxSpeed doit être >= à la vitesse désirée
      stepper.setMaxSpeed(speedStepsPerSec * 2);  // Marge importante pour que setSpeed fonctionne
      stepper.setSpeed(direction > 0 ? speedStepsPerSec : -speedStepsPerSec);
      
      isRunning = true;
      
      Serial.println("SetSpeed: " + String(direction > 0 ? speedStepsPerSec : -speedStepsPerSec) + " steps/sec");
      
      logToFile("Continu " + String(direction > 0 ? "AVANT" : "ARRIERE") + " " + String(speed) + "mm/min");
      server.send(200, "application/json", "{\"status\":\"continuous\"}");
      
    } else {
      // MOUVEMENT DISTANCE - Utilise run() avec accélération
      Serial.println("→ Mode DISTANCE");
      float newTarget = currentPosition + distance;
      
      if (!checkLimits(newTarget)) {
        Serial.println("❌ Limite dépassée");
        server.send(400, "application/json", "{\"error\":\"limit_exceeded\"}");
        return;
      }

      // Pour le mode distance, on utilise setMaxSpeed et setAcceleration
      stepper.setMaxSpeed(speedStepsPerSec);
      stepper.setAcceleration(speedStepsPerSec * 2);  // Accélération = 2x la vitesse pour être rapide
      
      long steps = (long)(distance * STEPS_PER_MM);
      stepper.move(steps);
      
      targetPosition = newTarget;
      movingToTarget = true;
      isRunning = true;
      
      Serial.println("MaxSpeed: " + String(speedStepsPerSec) + " steps/sec");
      Serial.println("Acceleration: " + String(speedStepsPerSec * 2) + " steps/sec²");
      Serial.println("Steps: " + String(steps) + ", Cible: " + String(newTarget, 3) + "mm");
      
      logToFile("Distance " + String(distance) + "mm vers " + String(newTarget, 3) + "mm");
      server.send(200, "application/json", "{\"status\":\"moving\"}");
    }
  });

  // API Speed - CHANGEMENT VITESSE EN TEMPS RÉEL - CORRIGÉ
  server.on("/api/speed", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    Serial.println("=== API SPEED ===");
    Serial.println("Body: " + body);
    
    float newSpeed = 300;
    if (body.indexOf("\"speed\":") >= 0) {
      int start = body.indexOf("\"speed\":") + 8;
      int end = body.indexOf(",", start);
      if (end == -1) end = body.indexOf("}", start);
      newSpeed = body.substring(start, end).toFloat();
    }

    float newSpeedSteps = (newSpeed * STEPS_PER_MM) / 60.0;
    
    Serial.println("=== CHANGEMENT VITESSE ===");
    Serial.println("Ancienne: " + String(currentSpeed) + " mm/min");
    Serial.println("Nouvelle: " + String(newSpeed) + " mm/min = " + String(newSpeedSteps) + " steps/sec");

    currentSpeed = newSpeed;

    if (continuousMode && isRunning) {
      // MODE CONTINU - Changement immédiat avec setSpeed()
      Serial.println("→ Mode CONTINU: Mise à jour vitesse");
      
      // IMPORTANT: D'abord augmenter MaxSpeed si nécessaire
      stepper.setMaxSpeed(newSpeedSteps * 2);  // Toujours garder une marge
      
      // Puis appliquer la nouvelle vitesse
      float finalSpeed = moveDirection > 0 ? newSpeedSteps : -newSpeedSteps;
      stepper.setSpeed(finalSpeed);
      
      Serial.println("→ MaxSpeed: " + String(newSpeedSteps * 2) + " steps/sec");
      Serial.println("→ SetSpeed: " + String(finalSpeed) + " steps/sec");
      
    } else if (movingToTarget && isRunning) {
      // MODE DISTANCE - Changement de la vitesse maximale
      Serial.println("→ Mode DISTANCE: Mise à jour vitesse max");
      
      // Pour le mouvement avec accélération, on change MaxSpeed et Acceleration
      stepper.setMaxSpeed(newSpeedSteps);
      stepper.setAcceleration(newSpeedSteps * 2);
      
      Serial.println("→ MaxSpeed: " + String(newSpeedSteps) + " steps/sec");
      Serial.println("→ Acceleration: " + String(newSpeedSteps * 2) + " steps/sec²");
      
      // Note: Le changement prendra effet progressivement avec l'accélération
      
    } else {
      Serial.println("→ Moteur arrêté: vitesse sauvée pour prochain mouvement");
    }

    logToFile("Vitesse changée: " + String(newSpeed) + "mm/min");
    server.send(200, "application/json", "{\"status\":\"speed_updated\",\"speed\":" + String(newSpeed) + "}");
  });

  // API Stop
  server.on("/api/stop", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("=== API STOP ===");
    stopMotor();
    logToFile("ARRÊT utilisateur");
    server.send(200, "application/json", "{\"status\":\"stopped\"}");
  });

  // API Home - CORRIGÉ
  server.on("/api/home", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("=== API HOME ===");
    
    currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
    float distanceToHome = -currentPosition;
    
    if (abs(distanceToHome) < 0.001) {
      server.send(200, "application/json", "{\"status\":\"already_home\"}");
      return;
    }

    stopMotor();
    delay(10);

    // Vitesse fixe pour le homing
    float homeSpeed = (600.0 * STEPS_PER_MM) / 60.0;  // 600 mm/min
    stepper.setMaxSpeed(homeSpeed);
    stepper.setAcceleration(homeSpeed * 2);
    
    long steps = (long)(distanceToHome * STEPS_PER_MM);
    stepper.move(steps);
    
    targetPosition = 0.0;
    movingToTarget = true;
    isRunning = true;
    currentSpeed = 600;

    logToFile("Retour origine: " + String(distanceToHome, 3) + "mm");
    server.send(200, "application/json", "{\"status\":\"homing\"}");
  });

  // API Reset
  server.on("/api/reset", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    Serial.println("=== API RESET ===");
    stopMotor();
    stepper.setCurrentPosition(0);
    currentPosition = 0.0;
    targetPosition = 0.0;
    logToFile("Position reset à 0");
    server.send(200, "application/json", "{\"status\":\"reset\"}");
  });

  // API Limits (inchangé)
  server.on("/api/limits", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    Serial.println("=== API LIMITS ===");

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
      Serial.println("Nouvelles limites: " + String(newMin) + " à " + String(newMax));
      logToFile("Limites: " + String(newMin) + " à " + String(newMax) + "mm");
      server.send(200, "application/json", "{\"status\":\"limits_updated\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"invalid_limits\"}");
    }
  });

  // API Limits Toggle
  server.on("/api/limits/toggle", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    SOFT_LIMITS_ENABLED = !SOFT_LIMITS_ENABLED;
    limitError = false;
    Serial.println("Limites " + String(SOFT_LIMITS_ENABLED ? "ACTIVÉES" : "DÉSACTIVÉES"));
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
      server.send(200, "text/plain", logs);
    } else {
      server.send(200, "text/plain", "Pas de logs");
    }
  });

  // CORS Headers (conservés pour toutes les routes)
  // ... [Code CORS identique, non répété pour la lisibilité]
}

void loop() {
  dnsServer.processNextRequest();  // Traiter les requêtes DNS pour captive portal
  server.handleClient();

  if (isRunning) {
    if (continuousMode) {
      // MOUVEMENT CONTINU - IMPORTANT: Appeler runSpeed() à chaque iteration!
      stepper.runSpeed();  // Doit être appelé sans délai pour maintenir la vitesse
      
      // Mise à jour position moins fréquente pour ne pas surcharger
      static unsigned long lastPositionUpdate = 0;
      if (millis() - lastPositionUpdate > 100) {
        lastPositionUpdate = millis();
        currentPosition = (float)stepper.currentPosition() / STEPS_PER_MM;
        
        // Vérification limites
        if (SOFT_LIMITS_ENABLED) {
          if ((moveDirection > 0 && currentPosition >= SOFT_LIMIT_MAX) ||
              (moveDirection < 0 && currentPosition <= SOFT_LIMIT_MIN)) {
            Serial.println("LIMITE ATTEINTE - ARRÊT AUTO");
            stopMotor();
            limitError = true;
          }
        }
      }
      
    } else if (movingToTarget) {
      // MOUVEMENT DISTANCE - run() doit être appelé à chaque itération
      stepper.run();
      
      // Mise à jour position moins fréquente
      static unsigned long lastDistanceUpdate = 0;
      if (millis() - lastDistanceUpdate > 50) {
        lastDistanceUpdate = millis();
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
  
  // TRÈS IMPORTANT: Ne pas mettre de delay ici car cela perturbe AccelStepper
  // La bibliothèque a besoin d'être appelée le plus souvent possible
}
