/*
 * ESP32 code for ZE29A-C2H5OH Ethanol Sensor via UART
 * 
 * Based on the official documentation from Zhengzhou Winsen Electronics
 * 
 * Connections:
 * Sensor Pin 1 (Vin) -> ESP32 5V
 * Sensor Pin 2 (GND) -> ESP32 GND
 * Sensor Pin 3 (TXD) -> ESP32 RX pin (GPIO16)
 * Sensor Pin 4 (RXD) -> ESP32 TX pin (GPIO17)
 */
#include <Arduino.h>
#include <HardwareSerial.h>

HardwareSerial SensorSerial(1); // UART1: RX=16, TX=17

// Status codes from the sensor documentation
#define STATUS_IDLE 0x31
#define STATUS_PREHEATING 0x32
#define STATUS_WAITING_FOR_BLOW 0x33
#define STATUS_BLOWING 0x34
#define STATUS_BLOW_INTERRUPTED 0x35
#define STATUS_CALCULATING 0x36
#define STATUS_READ_RESULT 0x37

// Alarm status codes
#define ALARM_NONE 0x00     // No alcohol (<20mg/100ml)
#define ALARM_DRINKING 0x01 // Drinking (20-80mg/100ml)
#define ALARM_DRUNK 0x02    // Drunk (>=80mg/100ml)

unsigned long lastStatusCheck = 0;
byte currentStatus = STATUS_IDLE;
bool resultAvailable = false;

// Function prototypes
void imprimirRespuesta(byte* response, int len);
void verificarEstado();

void esperarEstado(byte estadoDeseado, int timeoutMs) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    verificarEstado();
    if (currentStatus == estadoDeseado) return;
    delay(500);
  }
  Serial.println("Timeout esperando estado deseado.");
}

void enviarComando(byte* cmd, int len) {
  // Vaciar el buffer de recepción antes de enviar
  while (SensorSerial.available()) {
    SensorSerial.read();
  }
  
  // Enviar comando con flush para garantizar transmisión
  SensorSerial.write(cmd, len);
  SensorSerial.flush();
  delay(500); // Aumentar delay para dar más tiempo al sensor para responder
}

byte calcularChecksum(byte* data, int len) {
  int sum = 0;
  for (int i = 0; i < len; i++) {
    sum += data[i];
  }
  return (byte)((-sum) + 1);
}

bool leerRespuesta(byte* buffer, int expectedLen) {
  unsigned long startTime = millis();
  int bytesRead = 0;
  bool startByteFound = false;
  
  // Timeout aumentado a 3 segundos
  while (millis() - startTime < 3000) {
    if (SensorSerial.available()) {
      byte currentByte = SensorSerial.read();
      
      // Esperamos el byte de inicio 0xFF
      if (!startByteFound) {
        if (currentByte == 0xFF) {
          buffer[0] = currentByte;
          bytesRead = 1;
          startByteFound = true;
        }
        continue;
      }
      
      // Si ya encontramos el byte de inicio, seguimos llenando el buffer
      buffer[bytesRead++] = currentByte;
      
      // Si tenemos todos los bytes esperados, terminamos
      if (bytesRead >= expectedLen) {
        Serial.print("Bytes leídos: ");
        Serial.println(bytesRead);
        imprimirRespuesta(buffer, expectedLen);
        return true;
      }
    }
    delay(10);  // Pequeña pausa para no saturar el CPU
  }
  
  Serial.println("Timeout esperando respuesta completa");
  if (bytesRead > 0) {
    Serial.print("Bytes parciales recibidos: ");
    Serial.println(bytesRead);
    imprimirRespuesta(buffer, bytesRead);
  }
  return false;
}

void imprimirRespuesta(byte* response, int len) {
  Serial.print("Respuesta: ");
  for (int i = 0; i < len; i++) {
    Serial.print("0x");
    if (response[i] < 0x10) Serial.print("0");
    Serial.print(response[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void cambiarEstado(byte nuevoEstado) {
  Serial.print("Intentando cambiar estado a 0x");
  Serial.println(nuevoEstado, HEX);

  // Construir el comando (según el manual)
  byte cmd[9] = {0xFF, 0x01, 0x87, nuevoEstado, 0x00, 0x00, 0x00, 0x00, 0x00};
  
  // Calcular checksum según el método documentado exactamente:
  // "Check value algorithm: (negative (data 1 + data 2 + ... + data 7)) + 1"
  int sum = 0;
  for (int i = 1; i < 8; i++) {  // Empezamos desde el byte 1 (dirección)
    sum += cmd[i];
  }
  cmd[8] = (byte)(~(sum & 0xFF) + 1);  // Checksum según la fórmula del manual
  //cmd[8] = (byte)((-sum) + 1);

  // Mostrar el comando para depuración
  Serial.print("Comando enviado: ");
  for (int i = 0; i < 9; i++) {
    Serial.print("0x");
    if (cmd[i] < 0x10) Serial.print("0");
    Serial.print(cmd[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Vaciar ambos buffers antes de enviar
  while (SensorSerial.available()) {
    SensorSerial.read();
  }
  
  SensorSerial.write(cmd, 9);
  
  SensorSerial.flush();  // Asegurar que todos los bytes se envíen
  delay(800);  // Dar tiempo suficiente para que el sensor procese

  // Leer la respuesta
  byte response[9];
  if (leerRespuesta(response, 9)) {
    if (response[0] == 0xFF && response[1] == 0x87) {
      if (response[2] == 0x01) {
        Serial.print("Cambio de estado exitoso a 0x");
        Serial.println(nuevoEstado, HEX);
      } else {
        Serial.print("Cambio de estado rechazado: 0x");
        Serial.println(response[2], HEX);
      }
    } else {
      Serial.println("Respuesta incorrecta al cambiar estado");
    }
  } else {
    Serial.println("Sin respuesta al cambiar estado");
    // Verificar datos parciales (esto se mantiene igual)
  }
}

void verificarEstado() {
  byte cmdEstado[] = {0xFF, 0x01, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7A};
  byte response[9];
  
  enviarComando(cmdEstado, 9);
  if (leerRespuesta(response, 9)) {
    if (response[0] == 0xFF && response[1] == 0x85) {
      currentStatus = response[2];
      
      // Print human-readable status
      Serial.print("Estado: ");
      switch (currentStatus) {
        case STATUS_IDLE:
          Serial.println("Inactivo (esperando instrucciones)");
          break;
        case STATUS_PREHEATING:
          Serial.println("Precalentamiento");
          break;
        case STATUS_WAITING_FOR_BLOW:
          Serial.println("Esperando soplido");
          break;
        case STATUS_BLOWING:
          Serial.println("Soplando");
          break;
        case STATUS_BLOW_INTERRUPTED:
          Serial.println("Soplido interrumpido");
          break;
        case STATUS_CALCULATING:
          Serial.println("Calculando resultado");
          break;
        case STATUS_READ_RESULT:
          Serial.println("Resultado listo para lectura");
          resultAvailable = true;
          break;
        default:
          Serial.print("Desconocido: 0x");
          Serial.println(currentStatus, HEX);
      }
    }
  } else {
    Serial.println("Error al leer el estado");
  }
}

void leerResultado() {
  byte cmdLeerResultado[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
  byte response[9];
  
  enviarComando(cmdLeerResultado, 9);
  if (leerRespuesta(response, 9)) {
    if (response[0] == 0xFF && response[1] == 0x86) {
      // Calculate alcohol content
      int alcoholContent = (response[2] << 8) | response[3];
      float alcoholMg100ml = alcoholContent;
      byte alarmStatus = response[7];
      
      Serial.print("Contenido de alcohol: ");
      Serial.print(alcoholMg100ml);
      Serial.println(" mg/100ml");
      
      Serial.print("Estado de alarma: ");
      switch (alarmStatus) {
        case ALARM_NONE:
          Serial.println("Sin alcohol (<20mg/100ml)");
          break;
        case ALARM_DRINKING:
          Serial.println("Bebido (20-80mg/100ml)");
          break;
        case ALARM_DRUNK:
          Serial.println("Ebrio (>=80mg/100ml)");
          break;
        default:
          Serial.print("Desconocido: 0x");
          Serial.println(alarmStatus, HEX);
      }
    } else {
      Serial.println("Respuesta inválida al leer resultado");
    }
  } else {
    Serial.println("Error al leer el resultado");
  }
}

void iniciarPrueba() {
  Serial.println("\n------------------------------");
  Serial.println("Iniciando prueba de alcohol");
  Serial.println("------------------------------");
  
  // Verificar estado actual antes de cambiar
  byte cmdEstado[] = {0xFF, 0x01, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7A};
  enviarComando(cmdEstado, 9);
  
  byte response[9];
  if (leerRespuesta(response, 9)) {
    if (response[0] == 0xFF && response[1] == 0x85) {
      currentStatus = response[2];
      Serial.print("Estado actual antes de iniciar: 0x");
      Serial.println(currentStatus, HEX);
    }
  }
  
  // Solo podemos cambiar a precalentamiento desde estado inactivo o desde lectura de resultado
  if (currentStatus == STATUS_IDLE || currentStatus == STATUS_READ_RESULT) {
    // Cambiar a estado de preheat (0x32)
    esperarEstado(STATUS_IDLE, 10000);

    cambiarEstado(STATUS_PREHEATING);
    Serial.println("Iniciando precalentamiento del sensor (10 segundos)...");
    // El sensor cambiará automáticamente a STATUS_WAITING_FOR_BLOW después del precalentamiento
  } else {
    Serial.println("No se puede iniciar prueba desde el estado actual.");
    Serial.println("El sensor debe estar en estado IDLE (0x31) o READ_RESULT (0x37).");
  }
}

void consultarUmbrales() {
  byte cmdUmbral[] = {0xFF, 0x01, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F};
  enviarComando(cmdUmbral, 9);
  byte response[9];
  if (leerRespuesta(response, 9)) {
    if (response[0] == 0xFF && response[1] == 0x90) {
      Serial.print("Umbral de bebido: ");
      Serial.print(response[2]);
      Serial.println(" mg/100ml");
      Serial.print("Umbral de ebriedad: ");
      Serial.print(response[3]);
      Serial.println(" mg/100ml");
    }
  }
}

void probarComunicacion() {
  Serial.println("Probando comunicación...");
  // Test de comando simple - consultar estado
  byte cmdTest[] = {0xFF, 0x01, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7A};
  Serial.println("Enviando comando de estado:");
  for (int i = 0; i < 9; i++) {
    Serial.print("0x");
    if (cmdTest[i] < 0x10) Serial.print("0");
    Serial.print(cmdTest[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  
  enviarComando(cmdTest, 9);
  delay(100);
  Serial.print("Bytes disponibles después del comando: ");
  Serial.println(SensorSerial.available());
}

void resetComunicacion() {
  Serial.println("Reseteando comunicación...");
  SensorSerial.end();
  delay(1000);
  SensorSerial.begin(9600, SERIAL_8N1, 16, 17);
  delay(1000);
  
  // Limpiar buffer
  while (SensorSerial.available()) {
    SensorSerial.read();
  }
}

// Función para leer el tiempo de soplado configurado (comando 0x88)
void leerTiempoSoplado() {
  Serial.println("Leyendo tiempo de soplado configurado...");
  
  // Construir el comando 0x88 (Read blow time) exactamente como indica la documentación
  byte cmd[9] = {0xFF, 0x01, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  
  // Calcular checksum según la documentación exactamente:
  // "Check value algorithm: (negative (data 1 + data 2 + ... + data 7)) + 1"
  int sum = 0;
  for (int i = 1; i < 8; i++) {
    sum += cmd[i];
  }
  cmd[8] = (byte)((~sum) + 1);  // Negación bit a bit + 1
  
  // Mostrar comando para depuración
  Serial.print("Comando enviado: ");
  for (int i = 0; i < 9; i++) {
    Serial.print("0x");
    if (cmd[i] < 0x10) Serial.print("0");
    Serial.print(cmd[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  
  // Vaciar buffer de recepción antes de enviar
  while (SensorSerial.available()) {
    SensorSerial.read();
  }
  
  // Enviar comando
  SensorSerial.write(cmd, 9);
  SensorSerial.flush();
  delay(800);
  
  // Leer respuesta
  byte response[9] = {0};
  if (leerRespuesta(response, 9)) {
    if (response[0] == 0xFF && response[1] == 0x88) {
      byte tiempoSoplado = response[2];
      Serial.print("Tiempo de soplado actual: ");
      Serial.print(tiempoSoplado);
      Serial.println(" segundos");
    } else {
      Serial.println("Respuesta incorrecta al leer tiempo de soplado");
      imprimirRespuesta(response, 9);
    }
  } else {
    Serial.println("Sin respuesta al leer tiempo de soplado");
  }
}

// Función para configurar el tiempo de soplado (comando 0x89)
void configurarTiempoSoplado(byte nuevoTiempo) {
  if (nuevoTiempo < 1 || nuevoTiempo > 10) {
    Serial.println("Error: Tiempo fuera de rango (1-10s)");
    return;
  }
  
  Serial.print("Configurando tiempo de soplado a ");
  Serial.print(nuevoTiempo);
  Serial.println(" segundos...");
  
  // Construir el comando exactamente según la documentación
  byte cmd[9] = {0xFF, 0x01, 0x89, nuevoTiempo, 0x00, 0x00, 0x00, 0x00, 0x00};
  
  // Calcular checksum según la documentación exactamente:
  // "Check value algorithm: (negative (data 1 + data 2 + ... + data 7)) + 1"
  int sum = 0;
  for (int i = 1; i < 8; i++) {
    sum += cmd[i];
  }
  cmd[8] = (byte)((~sum) + 1);  // Negación bit a bit + 1
  
  // Mostrar comando para depuración
  Serial.print("Enviando: ");
  for (int i = 0; i < 9; i++) {
    Serial.print("0x");
    if (cmd[i] < 0x10) Serial.print("0");
    Serial.print(cmd[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  
  // Vaciar buffer de recepción antes de enviar
  while (SensorSerial.available()) {
    SensorSerial.read();
  }
  
  // Enviar comando
  SensorSerial.write(cmd, 9);
  SensorSerial.flush();
  delay(800);
  
  // Leer respuesta
  byte response[9] = {0};
  if (leerRespuesta(response, 9)) {
    if (response[0] == 0xFF && response[1] == 0x89) {
      if (response[2] == 0x01) {
        Serial.println("¡Configuración de tiempo de soplado exitosa!");
      } else {
        Serial.println("Configuración de tiempo de soplado rechazada.");
      }
    } else {
      Serial.println("Respuesta incorrecta al configurar tiempo de soplado");
      imprimirRespuesta(response, 9);
    }
  } else {
    Serial.println("Sin respuesta al configurar tiempo de soplado");
  }
}

void setup() {
  Serial.begin(115200);
  
  // Iniciar puerto serial para el sensor con buffer más grande
  SensorSerial.begin(9600, SERIAL_8N1, 16, 17);
  SensorSerial.setRxBufferSize(256); // Aumentar buffer de recepción
  
  delay(5000); // Dar más tiempo para que todo se estabilice
  
  Serial.println("\n\nSensor de Alcohol ZE29A-C2H5OH");
  Serial.println("--------------------------------");
  Serial.println("Comandos disponibles:");
  Serial.println(" i - Iniciar nueva prueba");
  Serial.println(" s - Verificar estado");
  Serial.println(" r - Leer resultado");
  Serial.println(" q - Consultar umbrales");
  Serial.println(" t - Probar comunicación");
  Serial.println(" b - Leer tiempo de soplado");
  Serial.println(" c - Configurar tiempo de soplado");
  Serial.println(" z - Reset comunicación");
  delay(1000);
  
  // Verificar comunicación básica antes de iniciar
  verificarEstado();
}

void loop() {
  // Procesar comandos desde la consola serial
  if (Serial.available()) {
    char cmd = Serial.read();
    switch (cmd) {
      case 'i': // Iniciar prueba
        iniciarPrueba();
        break;
      case 'r': // Leer resultado
        if (currentStatus == STATUS_READ_RESULT) {
          leerResultado();
        } else {
          Serial.println("No hay resultado disponible para leer");
        }
        break;
      case 's': // Consultar estado
        verificarEstado();
        break;
      case 'q': // Consultar umbral de alcohol
        consultarUmbrales();
        break;
      case 't': // Probar comunicación básica
        probarComunicacion();
        break;
        case 'b': // Leer tiempo de soplado
        leerTiempoSoplado();
        break;
      case 'c': // Configurar tiempo de soplado
        Serial.println("Introduzca el nuevo tiempo de soplado (1-10 segundos):");
        // Esperar entrada del usuario
        while (!Serial.available()) {
          delay(100);
        }
        // Leer nuevo tiempo de soplado
        if (Serial.available()) {
          String input = Serial.readStringUntil('\n');
          byte nuevoTiempo = input.toInt();
          configurarTiempoSoplado(nuevoTiempo);
        }
        break;
      case 'z': // Reset comunicación
        resetComunicacion();
        break;
    }
    
    // Limpiar buffer serial
    while (Serial.available()) {
      Serial.read();
    }
  }
  
  // Verificar estado periódicamente con menos frecuencia 
  // para no saturar la comunicación
  if (millis() - lastStatusCheck >= 3000) {
    lastStatusCheck = millis();
    
    // Si hay resultado disponible para leer
    if (currentStatus == STATUS_READ_RESULT && resultAvailable) {
      leerResultado();
      resultAvailable = false;
    }
  }
}